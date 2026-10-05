#pragma once

#include <string>

enum class CommandType
{
    Set,
    Get,
    Erase,
    Exists,
    Size,
    Exit
};

struct Command
{
    CommandType type;
    std::string key;
    std::string value;
};