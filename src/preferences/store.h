#pragma once

#include "platform/handle.h"

#include <filesystem>
#include <map>
#include <string>
#include <variant>

namespace preferences {

struct IgnoreCase {
    bool operator()(const std::string& left, const std::string& right) const;
};

using Value = std::variant<DWORD, std::string>;
using Values = std::map<std::string, Value, IgnoreCase>;

class Store {
public:
    explicit Store(const std::filesystem::path& path);

    const Value* find(const std::string& name) const;
    void set(const std::string& name, Value value);
    bool erase(const std::string& name);

private:
    void save(const Values& values);

    std::filesystem::path path_;
    Handle lock_;
    Values values_;
};

}
