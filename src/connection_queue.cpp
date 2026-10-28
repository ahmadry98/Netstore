#include "connection_queue.hpp"

#include <utility>

ConnectionQueue::ConnectionQueue(std::size_t capacity)
    : capacity_(capacity)
{
}

void ConnectionQueue::stop()
{
    {
        std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
    }
    available_.notify_all();
}

bool ConnectionQueue::push(Socket connection)
{
    {
        std::lock_guard<std::mutex> lock{mutex_};

        if (stopping_ || pending_.size() >= capacity_) {
            return false;
        }
        pending_.push(std::move(connection));
    }

    available_.notify_one();
    return true;
}

std::optional<Socket> ConnectionQueue::pop()
{
    std::unique_lock<std::mutex> lock{mutex_};

    available_.wait(lock, [this]() {
        return stopping_ || !pending_.empty();
    });

    if (pending_.empty()) {
        return std::nullopt;
    }
    Socket connection = std::move(pending_.front());
    pending_.pop();
    return connection;
}
