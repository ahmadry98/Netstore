#include "key_value_store.hpp"

std::size_t KeyValueStore::size() const
{
    return data_.size();
}

void KeyValueStore::set(const std::string& key, const std::string& value)
{
    data_[key] = value;
}

std::optional<std::string> KeyValueStore::get(const std::string& key) const
{
    const auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
} 

bool KeyValueStore::contains(const std::string& key) const
{
    return data_.contains(key);
}

bool KeyValueStore::erase(const std::string& key)
{
    // Erase the key and return whether an entry was removed.
    return data_.erase(key) > 0;
}