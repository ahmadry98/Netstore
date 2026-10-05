#include "command_parser.hpp"

#include <sstream>

std::optional<Command> parse_command(const std::string& input){
    std::istringstream stream(input);
    std::string command_name;

    if (!(stream >> command_name)) {
        return std::nullopt;
    }
    if (command_name == "SIZE"){
        std::string extra;
        if (stream >> extra) {
            return std::nullopt;
        }
        return Command{CommandType::Size, "", ""};
    }
    if (command_name == "EXIT"){
        std::string extra;
        if (stream >> extra) {
            return std::nullopt;
        }
        return Command{CommandType::Exit, "", ""};
    }
    if (command_name == "GET"){
        std::string key;
        if (!(stream >> key)) {
            return std::nullopt;
        }
        std::string extra;
        if (stream >> extra) {
            return std::nullopt;
        }
        return Command{CommandType::Get, key, ""};
    }
    if (command_name == "DELETE"){
        std::string key;
        if (!(stream >> key)) {
            return std::nullopt;
        }
        std::string extra;
        if (stream >> extra) {
            return std::nullopt;
        }
        return Command{CommandType::Erase, key, ""};
    }
    if (command_name == "EXISTS"){
        std::string key;
        if (!(stream >> key)) {
            return std::nullopt;
        }
        std::string extra;
        if (stream >> extra) {
            return std::nullopt;
        }
        return Command{CommandType::Exists, key, ""};
    }
    if (command_name == "SET"){
        std::string key;
        if (!(stream >> key)) {
            return std::nullopt;
        }
        stream >> std::ws;

        std::string value;
        if (!std::getline(stream, value) || value.empty()) {
            return std::nullopt;
        }
        return Command{CommandType::Set, key, value};
    }
    return std::nullopt;
}