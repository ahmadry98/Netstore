#pragma once

#include "command.hpp"
#include "key_value_store.hpp"

#include <string>

struct CommandResult
{
    std::string response;
    bool should_exit;
};

CommandResult execute_command(
    const Command& command,
    KeyValueStore& store
);