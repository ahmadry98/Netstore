#pragma once

#include "key_value_store.hpp"

#include <mutex>

int handle_client(
    int client_fd,
    KeyValueStore& store,
    std::mutex& store_mutex
);