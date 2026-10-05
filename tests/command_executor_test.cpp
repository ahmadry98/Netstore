#include "command_executor.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Executor reports the initial store state", "[executor]")
{
    KeyValueStore store;

    const auto size = execute_command(
        Command{CommandType::Size, "", ""},
        store
    );
    CHECK(size.response == "Size: 0");
    CHECK_FALSE(size.should_exit);

    const auto get = execute_command(
        Command{CommandType::Get, "missing", ""},
        store
    );
    CHECK(get.response == "Key not found");

    const auto exists = execute_command(
        Command{CommandType::Exists, "missing", ""},
        store
    );
    CHECK(exists.response == "No");
}

TEST_CASE("Executor stores and retrieves values", "[executor]")
{
    KeyValueStore store;

    const auto set = execute_command(
        Command{CommandType::Set, "name", "Ahmad Rayan"},
        store
    );
    CHECK(set.response == "OK");
    CHECK_FALSE(set.should_exit);

    const auto get = execute_command(
        Command{CommandType::Get, "name", ""},
        store
    );
    CHECK(get.response == "Ahmad Rayan");

    const auto exists = execute_command(
        Command{CommandType::Exists, "name", ""},
        store
    );
    CHECK(exists.response == "Yes");

    const auto size = execute_command(
        Command{CommandType::Size, "", ""},
        store
    );
    CHECK(size.response == "Size: 1");
}

TEST_CASE("Executor reports deletion outcomes", "[executor]")
{
    KeyValueStore store;

    const auto missing = execute_command(
        Command{CommandType::Erase, "name", ""},
        store
    );
    CHECK(missing.response == "Key not found");

    store.set("name", "Ahmad");

    const auto erased = execute_command(
        Command{CommandType::Erase, "name", ""},
        store
    );
    CHECK(erased.response == "Deleted");
    CHECK_FALSE(store.contains("name"));
}

TEST_CASE("EXIT requests session termination", "[executor]")
{
    KeyValueStore store;

    const auto result = execute_command(
        Command{CommandType::Exit, "", ""},
        store
    );

    CHECK(result.response == "Goodbye!");
    CHECK(result.should_exit);
}
