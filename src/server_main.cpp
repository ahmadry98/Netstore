#include "socket.hpp"
#include "command_parser.hpp"
#include "key_value_store.hpp"
#include "command_executor.hpp"
#include <cstdio>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <sys/socket.h>
#include <string>
#include <netinet/in.h>

int main()
{
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    std::cerr << "Could not configure SIGPIPE handling\n";
    return 1;
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        std::cerr << "Could not create socket\n";
        return 1;
    }

    Socket socket{fd};
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

    int client_fd;

    do {
        client_fd = ::accept(socket.get(), nullptr, nullptr);
    } while (client_fd == -1 && errno == EINTR);
    if (client_fd == -1) {
        std::cerr << "Could not accept client connection\n";
        return 1;
    }
    Socket client{client_fd};
    std::cout << "Client connected!\n";
    char buffer[1024]{};
    std::string pending;
    KeyValueStore store;
    constexpr std::size_t max_line_size = 4096;
    while (true) {
        

        const auto bytes_received = ::recv(
            client.get(),
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
                const auto result = execute_command(*command, store);
                response = result.response + '\n';
                should_exit = result.should_exit;
            }
            std::size_t total_sent = 0;

            while (total_sent < response.size()) {
                const auto bytes_sent = ::send(
                    client.get(),
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
    
        std::cout << "Descriptor: " << socket.get() << '\n';    
        std::cout << "Received: ";
        std::cout.write(buffer, bytes_received);
        std::cout << '\n';
    }
}
