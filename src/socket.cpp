#include "socket.hpp"

#include <unistd.h>

Socket::Socket(int fd) noexcept
    : fd_{fd}
{
}

Socket::~Socket()
{
    if (fd_ >= 0) {
        ::close(fd_);
    }
}
Socket::Socket(Socket&& other) noexcept
    : fd_{other.fd_}
{
    other.fd_ = -1;
}
int Socket::get() const noexcept
{
    return fd_;
}