#pragma once

#include "connection_queue.hpp"
#include "socket.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class KeyValueStore;

class ThreadPool
{
public:
    ThreadPool(std::size_t worker_count,
               std::size_t queue_capacity,
               KeyValueStore& store);

    ~ThreadPool();

    bool submit(Socket connection);
    void stop();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    void worker_loop(std::size_t index);

    KeyValueStore& store_;
    std::mutex store_mutex_;
    ConnectionQueue queue_;
    std::mutex active_mutex_;
    std::vector<std::shared_ptr<Socket>> active_;
    bool stopping_ = false;
    // Serializes stop() callers, including joining and queue draining.
    std::mutex stop_mutex_;
    std::vector<std::jthread> workers_;
};
