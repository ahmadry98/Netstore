#pragma once
#include <optional>

#include <cstddef>
#include <string>
#include <unordered_map>
class KeyValueStore
{
public:
    void set(const std::string& key, const std::string& value);
    std::size_t size() const;
    std::optional<std::string> get(const std::string& key) const;
    bool contains(const std::string& key) const;
    bool erase(const std::string& key);
private:
    std::unordered_map<std::string, std::string> data_;
};