#include "socket.hpp"

#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <utility>

int main()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        std::cerr << "Could not create socket\n";
        return 1;
    }

    {
        Socket client{fd};

        std::thread worker{
            [connection = std::move(client)]() {
                std::cout << "Worker owns descriptor: " << connection.get() << '\n';
            }
        };

        worker.join();

        std::cout << "Original after moving: " << client.get() << '\n';
    }

    std::cout << "After scope: "
              << ::fcntl(fd, F_GETFD) << '\n';
}
