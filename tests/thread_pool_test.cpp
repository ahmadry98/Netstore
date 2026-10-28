#include "thread_pool.hpp"
#include "key_value_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>

namespace {

std::array<Socket, 2> socket_pair()
{
    int descriptors[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    return {Socket{descriptors[0]}, Socket{descriptors[1]}};
}

void send_text(int fd, const std::string& text)
{
    std::size_t sent = 0;
    while (sent < text.size()) {
        const auto count = ::send(fd, text.data() + sent, text.size() - sent, 0);
        if (count == -1 && errno == EINTR) {
            continue;
        }
        REQUIRE(count > 0);
        sent += static_cast<std::size_t>(count);
    }
}

bool readable(int fd, int timeout = 2000)
{
    pollfd event{fd, POLLIN, 0};
    int result;
    do {
        result = ::poll(&event, 1, timeout);
    } while (result == -1 && errno == EINTR);
    return result > 0;
}

std::string response(int fd)
{
    std::string text;
    while (text.empty() || text.back() != '\n') {
        REQUIRE(readable(fd));
        char byte;
        REQUIRE(::recv(fd, &byte, 1, 0) == 1);
        text += byte;
    }
    return text;
}

void check_disconnected(int fd)
{
    REQUIRE(readable(fd));
    char byte;
    const auto count = ::recv(fd, &byte, 1, 0);
    const int error = errno;
    CHECK((count == 0 || (count == -1 && error == ECONNRESET)));
}

} // namespace

TEST_CASE("A pool rejects zero workers", "[pool]")
{
    KeyValueStore store;
    REQUIRE_THROWS_AS((ThreadPool{0, 2, store}), std::invalid_argument);
}

TEST_CASE("An idle pool stops repeatedly and rejects new connections", "[pool]")
{
    KeyValueStore store;
    ThreadPool pool{3, 2, store};
    pool.stop();
    pool.stop();
    auto pair = socket_pair();
    CHECK_FALSE(pool.submit(std::move(pair[0])));
    CHECK(pair[0].get() == -1);
    check_disconnected(pair[1].get());
}

TEST_CASE("One worker queues clients rejects overflow and reuses itself", "[pool]")
{
    KeyValueStore store;
    ThreadPool pool{1, 1, store};
    auto active = socket_pair();
    auto queued = socket_pair();
    auto rejected = socket_pair();
    REQUIRE(pool.submit(std::move(active[0])));
    send_text(active[1].get(), "SET name Ahmad\n");
    REQUIRE(response(active[1].get()) == "OK\n");

    REQUIRE(pool.submit(std::move(queued[0])));
    send_text(queued[1].get(), "GET name\n");
    CHECK_FALSE(readable(queued[1].get(), 50));
    CHECK_FALSE(pool.submit(std::move(rejected[0])));
    check_disconnected(rejected[1].get());

    send_text(active[1].get(), "EXIT\n");
    CHECK(response(active[1].get()) == "Goodbye!\n");
    check_disconnected(active[1].get());
    CHECK(response(queued[1].get()) == "Ahmad\n");
    send_text(queued[1].get(), "EXIT\n");
    CHECK(response(queued[1].get()) == "Goodbye!\n");
    check_disconnected(queued[1].get());
}

TEST_CASE("Pool destruction stops idle active and queued connections", "[pool]")
{
    KeyValueStore store;
    auto first = socket_pair();
    auto second = socket_pair();
    auto pending = socket_pair();
    {
        ThreadPool pool{2, 4, store};
        REQUIRE(pool.submit(std::move(first[0])));
        send_text(first[1].get(), "SIZE\n");
        REQUIRE(response(first[1].get()) == "Size: 0\n");
        REQUIRE(pool.submit(std::move(second[0])));
        send_text(second[1].get(), "SIZE\n");
        REQUIRE(response(second[1].get()) == "Size: 0\n");
        REQUIRE(pool.submit(std::move(pending[0])));
        // No EXIT and no explicit stop: the destructor must wake and join.
    }
    check_disconnected(first[1].get());
    check_disconnected(second[1].get());
    check_disconnected(pending[1].get());
}

TEST_CASE("Concurrent stop calls safely join the same pool", "[pool]")
{
    KeyValueStore store;
    ThreadPool pool{2, 2, store};
    auto pair = socket_pair();
    REQUIRE(pool.submit(std::move(pair[0])));
    send_text(pair[1].get(), "SIZE\n");
    REQUIRE(response(pair[1].get()) == "Size: 0\n");
    std::jthread first{[&]() { pool.stop(); }};
    std::jthread second{[&]() { pool.stop(); }};
    first.join();
    second.join();
    check_disconnected(pair[1].get());
}
