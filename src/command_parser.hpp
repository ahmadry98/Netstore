#pragma once

#include "command.hpp"

#include <optional>
#include <string>

std::optional<Command> parse_command(const std::string& input);