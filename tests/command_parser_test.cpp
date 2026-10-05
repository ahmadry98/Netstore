#include "command_parser.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Empty and unknown inputs are rejected", "[parser]")
{
    CHECK_FALSE(parse_command("").has_value());
    CHECK_FALSE(parse_command("   \t").has_value());
    CHECK_FALSE(parse_command("UNKNOWN").has_value());
}

TEST_CASE("SIZE requires no arguments", "[parser]")
{
    const auto command = parse_command("  SIZE  ");

    REQUIRE(command.has_value());
    CHECK(command->type == CommandType::Size);
    CHECK(command->key.empty());
    CHECK(command->value.empty());
    CHECK_FALSE(parse_command("SIZE unexpected").has_value());
}

TEST_CASE("EXIT requires no arguments and is case-sensitive", "[parser]")
{
    const auto command = parse_command("  EXIT  ");

    REQUIRE(command.has_value());
    CHECK(command->type == CommandType::Exit);
    CHECK(command->key.empty());
    CHECK(command->value.empty());
    CHECK_FALSE(parse_command("EXIT unexpected").has_value());
    CHECK_FALSE(parse_command("exit").has_value());
}

TEST_CASE("GET requires exactly one key", "[parser]")
{
    const auto command = parse_command("  GET language  ");

    REQUIRE(command.has_value());
    CHECK(command->type == CommandType::Get);
    CHECK(command->key == "language");
    CHECK(command->value.empty());
    CHECK_FALSE(parse_command("GET").has_value());
    CHECK_FALSE(parse_command("GET name unexpected").has_value());
    CHECK_FALSE(parse_command("get name").has_value());
}

TEST_CASE("DELETE maps to Erase and requires exactly one key", "[parser]")
{
    const auto command = parse_command("  DELETE language  ");

    REQUIRE(command.has_value());
    CHECK(command->type == CommandType::Erase);
    CHECK(command->key == "language");
    CHECK(command->value.empty());
    CHECK_FALSE(parse_command("DELETE").has_value());
    CHECK_FALSE(parse_command("DELETE name unexpected").has_value());
    CHECK_FALSE(parse_command("delete name").has_value());
}

TEST_CASE("EXISTS requires exactly one key", "[parser]")
{
    const auto command = parse_command("  EXISTS language  ");

    REQUIRE(command.has_value());
    CHECK(command->type == CommandType::Exists);
    CHECK(command->key == "language");
    CHECK(command->value.empty());
    CHECK_FALSE(parse_command("EXISTS").has_value());
    CHECK_FALSE(parse_command("EXISTS name unexpected").has_value());
    CHECK_FALSE(parse_command("exists name").has_value());
}

TEST_CASE("SET accepts the rest of the line as its value", "[parser]")
{
    const auto command = parse_command("  SET message   Your order is ready");

    REQUIRE(command.has_value());
    CHECK(command->type == CommandType::Set);
    CHECK(command->key == "message");
    CHECK(command->value == "Your order is ready");
    CHECK_FALSE(parse_command("SET").has_value());
    CHECK_FALSE(parse_command("SET name").has_value());
    CHECK_FALSE(parse_command("SET name   ").has_value());
    CHECK_FALSE(parse_command("set name Ahmad").has_value());
}
