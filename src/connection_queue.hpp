#pragma once

#include "socket.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

class ConnectionQueue
{
public:
    explicit ConnectionQueue(std::size_t capacity = 64);
    bool push(Socket connection);
    std::optional<Socket> pop();
    void stop();

private:
    const std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::queue<Socket> pending_;
    bool stopping_ = false;
};
