#include "key_value_store.hpp"
#include "socket.hpp"
#include "thread_pool.hpp"

#include <atomic>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> shutdown_requested{false};

void request_shutdown(int)
{
    shutdown_requested.store(true);
}

std::size_t parse_count(const char* argument)
{
    const std::string_view text{argument};
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("Worker count and queue capacity must be nonnegative integers");
    }
    return value;
}

int run_server(int argc, char** argv)
{
    if (argc > 3) {
        throw std::invalid_argument("Usage: netstore_server [worker_count] [queue_capacity]");
    }
    const std::size_t worker_count = argc > 1 ? parse_count(argv[1]) : 4;
    const std::size_t queue_capacity = argc > 2 ? parse_count(argv[2]) : 64;
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
    KeyValueStore store;
    ThreadPool pool{worker_count, queue_capacity, store};
    std::cout << "Workers: " << worker_count << ", queue capacity: " << queue_capacity << '\n';
    std::cout << "Waiting for a client on 127.0.0.1:8080..." << std::endl;
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
        std::cout << "Client connected!\n";
        if (!pool.submit(std::move(client))) {
            std::cerr << "Connection rejected: queue full or pool stopped\n";
        }
    }
    pool.stop();
    return exit_code;
}

int main(int argc, char** argv)
{
    try {
        return run_server(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Server failed: " << error.what() << '\n';
        return 1;
    }
}
