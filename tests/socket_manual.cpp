#include <utility>
#include "socket.hpp"

#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>

int main()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        std::cerr << "Could not create socket\n";
        return 1;
    }

    {
        Socket connection{fd};
        Socket moved{std::move(connection)};
        std::cout << "Original: " << connection.get() << '\n';
        std::cout << "Moved: " << moved.get() << '\n';
        std::cout << "Descriptor: " << connection.get() << '\n';
        std::cout << "Inside scope: "
                  << ::fcntl(fd, F_GETFD) << '\n';
    } // connection is destroyed and closes fd here

    std::cout << "After scope: "
              << ::fcntl(fd, F_GETFD) << '\n';
}