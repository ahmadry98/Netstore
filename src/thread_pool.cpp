#include "thread_pool.hpp"
#include "client_session.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <utility>

ThreadPool::ThreadPool(std::size_t worker_count, std::size_t queue_capacity,
                       KeyValueStore& store)
    : store_{store}, queue_{queue_capacity}, active_(worker_count)
{
    if (worker_count == 0) {
        throw std::invalid_argument("ThreadPool requires at least one worker");
    }
    workers_.reserve(worker_count);
    try {
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this, i]() { worker_loop(i); });
        }
    } catch (...) {
        // A partially constructed object has no destructor call of its own.
        stop();
        throw;
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

bool ThreadPool::submit(Socket connection)
{
    std::lock_guard<std::mutex> lock{active_mutex_};
    if (stopping_) {
        return false;
    }
    return queue_.push(std::move(connection));
}

void ThreadPool::worker_loop(std::size_t index)
{
    while (auto next = queue_.pop()) {
        try {
            auto connection = std::make_shared<Socket>(std::move(*next));
            {
                std::lock_guard<std::mutex> lock{active_mutex_};
                // Synchronize registration with stop(): a popped connection
                // must either be registered for shutdown or never be served.
                if (stopping_) {
                    break;
                }
                active_[index] = connection;
            }

            try {
                handle_client(connection->get(), store_, store_mutex_);
            } catch (const std::exception& error) {
                std::cerr << "Client session failed: " << error.what() << '\n';
            } catch (...) {
                std::cerr << "Client session failed\n";
            }
            // SHUT_WR also works after a client half-close on macOS.
            ::shutdown(connection->get(), SHUT_WR);
            {
                std::lock_guard<std::mutex> lock{active_mutex_};
                active_[index].reset();
            }
            // The last shared owner now closes this session's descriptor.
        } catch (const std::exception& error) {
            std::cerr << "Could not prepare client session: " << error.what() << '\n';
        }
    }
}

void ThreadPool::stop()
{
    std::lock_guard<std::mutex> stop_lock{stop_mutex_};
    {
        std::lock_guard<std::mutex> lock{active_mutex_};
        stopping_ = true;
        queue_.stop();
        for (const auto& connection : active_) {
            if (connection) {
                ::shutdown(connection->get(), SHUT_RDWR);
            }
        }
    }

    // Workers need active_mutex_ to finish. Never hold it while joining.
    workers_.clear();
    // Queue stop normally permits draining. Pool shutdown instead discards
    // pending clients so idle queued connections cannot delay server exit.
    while (queue_.pop()) {
    }
}
