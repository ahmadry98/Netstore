#pragma once

class Socket
{
public:
    explicit Socket(int fd) noexcept;
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    int get() const noexcept;

private:
    int fd_;
};