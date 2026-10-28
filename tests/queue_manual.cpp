#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

int main()
{
    std::queue<int> pending;
    std::mutex queue_mutex;
    std::condition_variable queue_condition;
    bool stopping = false; // Protected by queue_mutex, like pending.

    std::jthread worker{[&]() {
        while (true) {
            std::unique_lock<std::mutex> lock{queue_mutex};

            queue_condition.wait(lock, [&]() {
                return stopping || !pending.empty();
            });

            // Finish queued work before stopping.
            if (stopping && pending.empty()) {
                break;
            }

            const int value = pending.front();
            pending.pop();

            lock.unlock();
            std::cout << value << '\n';
        }
    }};

    {
        std::lock_guard<std::mutex> lock{queue_mutex};
        pending.push(10);
        pending.push(20);
        stopping = true; // No more items will be added.
    }
    queue_condition.notify_one();

    worker.join(); // Wait until both items have been processed.
}
