#include "connection_queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <latch>
#include <sys/socket.h>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

namespace {

std::array<Socket, 2> connected_sockets()
{
    int descriptors[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    return {Socket{descriptors[0]}, Socket{descriptors[1]}};
}

void check_closed(int descriptor)
{
    errno = 0;
    const int result = ::fcntl(descriptor, F_GETFD);
    const int error = errno;
    CHECK(result == -1);
    CHECK(error == EBADF);
}

} // namespace

TEST_CASE("Connection queue transfers socket ownership in FIFO order", "[queue]")
{
    auto first = connected_sockets();
    auto second = connected_sockets();
    const int first_fd = first[0].get();
    const int second_fd = second[0].get();
    ConnectionQueue queue;

    REQUIRE(queue.push(std::move(first[0])));
    REQUIRE(queue.push(std::move(second[0])));
    CHECK(first[0].get() == -1);
    CHECK(second[0].get() == -1);

    auto first_out = queue.pop();
    auto second_out = queue.pop();
    REQUIRE(first_out.has_value());
    REQUIRE(second_out.has_value());
    CHECK(first_out->get() == first_fd);
    CHECK(second_out->get() == second_fd);
    CHECK(::fcntl(first_fd, F_GETFD) != -1);
    CHECK(::fcntl(second_fd, F_GETFD) != -1);

    // The popped owners, not the queue, are responsible for final cleanup.
    first_out.reset();
    second_out.reset();
    check_closed(first_fd);
    check_closed(second_fd);
    CHECK(::fcntl(first[1].get(), F_GETFD) != -1);
    CHECK(::fcntl(second[1].get(), F_GETFD) != -1);
}

TEST_CASE("Destroying a queue closes connections still queued", "[queue]")
{
    auto pair = connected_sockets();
    const int descriptor = pair[0].get();
    {
        ConnectionQueue queue;
        REQUIRE(queue.push(std::move(pair[0])));
        CHECK(::fcntl(descriptor, F_GETFD) != -1);
    }
    check_closed(descriptor);
    CHECK(::fcntl(pair[1].get(), F_GETFD) != -1);
}

TEST_CASE("Stopping an empty queue is repeatable and pop returns empty", "[queue]")
{
    ConnectionQueue queue;
    queue.stop();
    queue.stop();
    CHECK_FALSE(queue.pop().has_value());
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("A stopped queue drains existing connections before returning empty", "[queue]")
{
    auto pair = connected_sockets();
    const int descriptor = pair[0].get();
    ConnectionQueue queue;
    REQUIRE(queue.push(std::move(pair[0])));
    queue.stop();

    auto connection = queue.pop();
    REQUIRE(connection.has_value());
    CHECK(connection->get() == descriptor);
    CHECK(::fcntl(descriptor, F_GETFD) != -1);
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("A stopped queue rejects and closes a new connection", "[queue]")
{
    auto pair = connected_sockets();
    const int descriptor = pair[0].get();
    ConnectionQueue queue;
    queue.stop();

    CHECK_FALSE(queue.push(std::move(pair[0])));
    CHECK(pair[0].get() == -1);
    check_closed(descriptor);
    CHECK_FALSE(queue.pop().has_value());
    CHECK(::fcntl(pair[1].get(), F_GETFD) != -1);
}

TEST_CASE("An empty queue waits and push wakes a consumer", "[queue]")
{
    auto pair = connected_sockets();
    const int descriptor = pair[0].get();
    ConnectionQueue queue;
    std::promise<std::optional<Socket>> result;
    auto received = result.get_future();
    std::latch started{1};
    std::jthread consumer{[&]() {
        started.count_down();
        result.set_value(queue.pop());
    }};

    started.wait();
    const auto before_push = received.wait_for(50ms);
    const bool accepted = queue.push(std::move(pair[0]));
    const auto after_push = received.wait_for(2s);
    queue.stop(); // Also unblock the consumer if an assertion below fails.
    consumer.join();

    CHECK(before_push == std::future_status::timeout);
    CHECK(accepted);
    REQUIRE(after_push == std::future_status::ready);
    auto connection = received.get();
    REQUIRE(connection.has_value());
    CHECK(connection->get() == descriptor);
}

TEST_CASE("A full queue closes rejected sockets and accepts after a pop", "[queue]")
{
    auto first = connected_sockets();
    auto second = connected_sockets();
    auto rejected = connected_sockets();
    auto replacement = connected_sockets();
    const int first_fd = first[0].get();
    const int second_fd = second[0].get();
    const int rejected_fd = rejected[0].get();
    const int replacement_fd = replacement[0].get();
    ConnectionQueue queue{2};

    REQUIRE(queue.push(std::move(first[0])));
    REQUIRE(queue.push(std::move(second[0])));
    CHECK_FALSE(queue.push(std::move(rejected[0])));
    CHECK(rejected[0].get() == -1);
    check_closed(rejected_fd);
    CHECK(::fcntl(rejected[1].get(), F_GETFD) != -1);

    auto popped_first = queue.pop();
    REQUIRE(popped_first.has_value());
    CHECK(popped_first->get() == first_fd);

    // A popped connection no longer counts toward queue capacity even while
    // its new owner keeps it open.
    REQUIRE(queue.push(std::move(replacement[0])));
    auto popped_second = queue.pop();
    auto popped_replacement = queue.pop();
    REQUIRE(popped_second.has_value());
    REQUIRE(popped_replacement.has_value());
    CHECK(popped_second->get() == second_fd);
    CHECK(popped_replacement->get() == replacement_fd);
    CHECK(::fcntl(popped_first->get(), F_GETFD) != -1);
    queue.stop();
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("A zero-capacity queue rejects and closes every connection", "[queue]")
{
    auto pair = connected_sockets();
    const int descriptor = pair[0].get();
    ConnectionQueue queue{0};

    CHECK_FALSE(queue.push(std::move(pair[0])));
    CHECK(pair[0].get() == -1);
    check_closed(descriptor);
    CHECK(::fcntl(pair[1].get(), F_GETFD) != -1);
    queue.stop();
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("Stopping wakes all consumers waiting on an empty queue", "[queue]")
{
    ConnectionQueue queue;
    std::array<std::promise<bool>, 3> results;
    std::array<std::future<bool>, 3> received;
    std::latch started{3};
    std::array<std::jthread, 3> consumers;

    for (std::size_t i = 0; i < consumers.size(); ++i) {
        received[i] = results[i].get_future();
        consumers[i] = std::jthread{[&, i]() {
            started.count_down();
            results[i].set_value(!queue.pop().has_value());
        }};
    }

    started.wait();
    for (auto& result : received) {
        CHECK(result.wait_for(50ms) == std::future_status::timeout);
    }
    queue.stop();
    for (auto& result : received) {
        REQUIRE(result.wait_for(2s) == std::future_status::ready);
        CHECK(result.get());
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }
}
