#include "command_executor.hpp"
#include "command_parser.hpp"
#include "key_value_store.hpp"
#include "socket.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>

struct Worker
{
    std::shared_ptr<std::atomic<bool>> finished;
    std::shared_ptr<Socket> connection;
    std::jthread thread;
};
int handle_client(int client_fd, KeyValueStore& store,
                  std::mutex& store_mutex);
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> shutdown_requested{false};

void request_shutdown(int)
{
    shutdown_requested.store(true);
}

int main()
{
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        std::cerr << "Could not configure SIGPIPE handling\n";
        return 1;
    }
    if (std::signal(SIGINT, request_shutdown) == SIG_ERR) {
        std::cerr << "Could not configure SIGINT handling\n";
        return 1;
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        std::cerr << "Could not create socket\n";
        return 1;
    }

    Socket socket{fd};
    // Readiness can change between poll() and accept(). Never block there.
    const int flags = ::fcntl(socket.get(), F_GETFL);
    if (flags == -1 || ::fcntl(socket.get(), F_SETFL, flags | O_NONBLOCK) == -1) {
        std::perror("fcntl(listener)");
        return 1;
    }
    const int reuse_address = 1;
    if (::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR,
                     &reuse_address, sizeof(reuse_address)) == -1) {
        std::perror("setsockopt(SO_REUSEADDR)");
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int result = ::bind(
        socket.get(),
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );
    if (result == -1) {
        std::perror("bind");
        return 1;
    }
    const int listen_result = ::listen(socket.get(), 10);
    if (listen_result == -1) {
        std::cerr << "Could not listen on socket\n";
        return 1;
    }
    std::cout << "Waiting for a client on 127.0.0.1:8080..."
          << std::endl;
    KeyValueStore store;
    std::mutex store_mutex;
    std::vector<Worker> workers;
    pollfd listener{};
    listener.fd = socket.get();
    listener.events = POLLIN;
    int exit_code = 0;
    while (!shutdown_requested.load()) {
        const int ready = ::poll(&listener, 1, 200);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("poll");
            exit_code = 1;
            break;
        }

        if (ready == 0) {
            continue;
        }

        if (shutdown_requested.load()) {
            break;
        }
        if (listener.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::cerr << "Listening socket error\n";
            exit_code = 1;
            break;
        }

        if (!(listener.revents & POLLIN)) {
            continue;
        }
        int client_fd;

        do {
            client_fd = ::accept(socket.get(), nullptr, nullptr);
        } while (client_fd == -1 && errno == EINTR && !shutdown_requested.load());

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (!shutdown_requested.load()) {
                std::perror("accept");
                exit_code = 1;
            }
            break;
        }

        Socket client{client_fd};
        // Client sessions use blocking I/O. Some platforms inherit flags.
        const int client_flags = ::fcntl(client.get(), F_GETFL);
        if (client_flags == -1 ||
            ::fcntl(client.get(), F_SETFL, client_flags & ~O_NONBLOCK) == -1) {
            std::perror("fcntl(client)");
            continue;
        }
        std::erase_if(workers, [](const Worker& worker) {
            return worker.finished->load();
        });
        std::cout << "Client connected!\n";
        try {
            // Allocate vector storage before starting a thread: an allocation
            // failure afterward could destroy and join a blocked temporary.
            if (workers.size() == workers.capacity()) {
                workers.reserve(workers.empty() ? 1 : workers.size() * 2);
            }
            auto finished = std::make_shared<std::atomic<bool>>(false);
            auto connection = std::make_shared<Socket>(std::move(client));

            workers.push_back(Worker{
                finished,
                connection,
                std::jthread{
                    [connection, &store, &store_mutex, finished]() {
                        try {
                            handle_client(connection->get(), store, store_mutex);
                        } catch (const std::exception& error) {
                            std::cerr << "Client session failed: " << error.what() << '\n';
                        } catch (...) {
                            std::cerr << "Client session failed\n";
                        }
                        ::shutdown(connection->get(), SHUT_WR);
                        finished->store(true);
                    }
                }
            });
        } catch (const std::exception& error) {
            std::cerr << "Could not start worker: " << error.what() << '\n';
            exit_code = 1;
            break;
        }
    }
    for (const Worker& worker : workers) {
        ::shutdown(worker.connection->get(), SHUT_RDWR);
    }

    workers.clear();

    return exit_code;
}

int handle_client(int client_fd, KeyValueStore& store,
                  std::mutex& store_mutex)
{
    char buffer[1024]{};
    std::string pending;
    constexpr std::size_t max_line_size = 4096;
    while (true) {
        const auto bytes_received = ::recv(
            client_fd,
            buffer,
            sizeof(buffer),
            0
        );
        if (bytes_received == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Could not receive data from client\n";
            return 1;
        }
        if (bytes_received == 0) {
            std::cout << "Client disconnected\n";
            return 0;
        }
        pending.append(buffer, static_cast<std::size_t>(bytes_received));
        while (true) {
            const auto newline = pending.find('\n');

            if (newline == std::string::npos) {
                if (pending.size() > max_line_size) {
                    std::cerr << "Command too long\n";
                    return 1;
                }
                break;
            }

            if (newline > max_line_size) {
                std::cerr << "Command too long\n";
                return 1;
            }
            const std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            const auto command = parse_command(line);

            std::string response;
            bool should_exit = false;

            if (!command.has_value()) {
                response = "Invalid command\n";
            } else {
                std::lock_guard<std::mutex> lock{store_mutex};
                const auto result = execute_command(*command, store);
                response = result.response + '\n';
                should_exit = result.should_exit;
            }
            std::size_t total_sent = 0;

            while (total_sent < response.size()) {
                const auto bytes_sent = ::send(
                    client_fd,
                    response.data() + total_sent,
                    response.size() - total_sent,
                    0
                );
                if (bytes_sent == -1 && errno == EINTR) {
                    continue;
                }
                if (bytes_sent <= 0) {
                    std::cerr << "Could not send response\n";
                    return 1;
                }

                total_sent += static_cast<std::size_t>(bytes_sent);
            }
            std::cout << response;
            if (should_exit) {
                return 0;
            }
            std::cout << "Command line: " << line << '\n';
        }

        std::cout << "Descriptor: " << client_fd << '\n';
        std::cout << "Received: ";
        std::cout.write(buffer, bytes_received);
        std::cout << '\n';
    }
}
