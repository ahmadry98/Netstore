#include "command_executor.hpp"

CommandResult execute_command(
    const Command& command,
    KeyValueStore& store
)
{
    switch (command.type) {
        case CommandType::Size:
            return CommandResult{
                "Size: " + std::to_string(store.size()),
                false
            };

        case CommandType::Exit:
            return CommandResult{"Goodbye!", true};

        case CommandType::Set:
            store.set(command.key, command.value);
            return CommandResult{"OK", false};

        case CommandType::Exists:
            return CommandResult{
                store.contains(command.key) ? "Yes" : "No",
                false
            };

        case CommandType::Erase:
            return CommandResult{
                store.erase(command.key) ? "Deleted" : "Key not found",
                false
            };

        case CommandType::Get: {
            const auto value = store.get(command.key);
            if (!value.has_value()) {
                return CommandResult{"Key not found", false};
            }

            return CommandResult{*value, false};
        }
    }

    return CommandResult{"Invalid command", false};
}
