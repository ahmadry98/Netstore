#include "key_value_store.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A new store is empty", "[store]")
{
    KeyValueStore store;

    CHECK(store.size() == 0);
    CHECK_FALSE(store.contains("name"));
    CHECK_FALSE(store.get("missing").has_value());
}

TEST_CASE("Values can be stored and replaced", "[store]")
{
    KeyValueStore store;

    store.set("name", "Ahmad");

    REQUIRE(store.contains("name"));
    REQUIRE(store.get("name").has_value());
    CHECK(store.get("name").value() == "Ahmad");
    CHECK(store.size() == 1);

    store.set("name", "Another value");

    REQUIRE(store.get("name").has_value());
    CHECK(store.get("name").value() == "Another value");
    CHECK(store.size() == 1);

    store.set("language", "C++");
    CHECK(store.size() == 2);
}

TEST_CASE("Erasing reports whether a key was removed", "[store]")
{
    KeyValueStore store;
    store.set("name", "Ahmad");
    store.set("language", "C++");

    CHECK(store.erase("name"));
    CHECK_FALSE(store.contains("name"));
    CHECK_FALSE(store.get("name").has_value());
    CHECK(store.size() == 1);

    CHECK_FALSE(store.erase("name"));
    CHECK(store.size() == 1);
}
