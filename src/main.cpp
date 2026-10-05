#include "command_parser.hpp"
#include "key_value_store.hpp"
#include "command_executor.hpp"

#include <iostream>
#include <string>

int main()
{
    std::string input;

    KeyValueStore store;

    while (true) {
        std::cout << "netstore> " << std::flush;
        if (!std::getline(std::cin, input)) {
            std::cout << "\nInput stream failed or reached EOF. Exiting..." << '\n';
            break;
        }
        const auto command = parse_command(input);
        if (!command.has_value()) {
            std::cout << "Invalid command" << '\n';
            continue;
        }
        const auto result = execute_command(*command, store);

        std::cout << result.response << '\n';

        if (result.should_exit) {
            return 0;
        }
    }
    return 0;
}
