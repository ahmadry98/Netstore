#include "socket.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <sys/socket.h>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Socket>);
static_assert(!std::is_copy_assignable_v<Socket>);
static_assert(std::is_nothrow_move_constructible_v<Socket>);

TEST_CASE("A socket owns its descriptor until destruction", "[socket]")
{
    int descriptors[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    Socket peer{descriptors[1]};

    {
        Socket connection{descriptors[0]};
        CHECK(connection.get() == descriptors[0]);
        CHECK(::fcntl(connection.get(), F_GETFD) != -1);
    }

    errno = 0;
    const int result = ::fcntl(descriptors[0], F_GETFD);
    const int error = errno;
    CHECK(result == -1);
    CHECK(error == EBADF);
    CHECK(::fcntl(peer.get(), F_GETFD) != -1);
}

TEST_CASE("A socket can hold an invalid descriptor", "[socket]")
{
    Socket empty{-1};
    CHECK(empty.get() == -1);
}

TEST_CASE("Moving a socket transfers ownership until the new owner is destroyed", "[socket]")
{
    int descriptors[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    Socket peer{descriptors[1]};
    std::optional<Socket> moved;

    {
        Socket original{descriptors[0]};
        moved.emplace(std::move(original));

        CHECK(original.get() == -1);
        CHECK(moved->get() == descriptors[0]);
        CHECK(::fcntl(moved->get(), F_GETFD) != -1);
    }

    // Destroying the original must not close the transferred descriptor.
    CHECK(::fcntl(moved->get(), F_GETFD) != -1);
    moved.reset(); // Destroy the new owner.

    errno = 0;
    const int result = ::fcntl(descriptors[0], F_GETFD);
    const int error = errno;
    CHECK(result == -1);
    CHECK(error == EBADF);
    CHECK(::fcntl(peer.get(), F_GETFD) != -1);
}

TEST_CASE("Moving an invalid socket leaves both objects empty", "[socket]")
{
    Socket original{-1};
    Socket moved{std::move(original)};

    CHECK(original.get() == -1);
    CHECK(moved.get() == -1);
}
