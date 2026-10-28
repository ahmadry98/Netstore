#include "client_session.hpp"
#include "command_executor.hpp"
#include "command_parser.hpp"

#include <cerrno>
#include <iostream>
#include <string>
#include <sys/socket.h>

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