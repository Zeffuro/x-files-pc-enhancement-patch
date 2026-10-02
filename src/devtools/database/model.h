#pragma once
#include "native.h"
#include <string_view>
#include <span>

namespace devtools {
std::wstring database_native_hex(std::span<const std::uint8_t> bytes);
std::wstring_view database_class_name(std::uint32_t class_id);
std::optional<std::size_t> database_find(const NativeDatabaseSnapshot& snapshot,
                                         DatabaseObjectKey key);
std::optional<std::size_t> database_resolve(const NativeDatabaseSnapshot& snapshot,
                                            const DatabaseReference& reference);

struct DatabaseLink {
    std::wstring label;
    std::size_t target;
};

std::vector<DatabaseLink> database_links(const NativeDatabaseSnapshot& snapshot,
                                         std::size_t source);
std::vector<DatabaseObjectKey> database_variable_changes(const NativeDatabaseSnapshot& before,
                                                         const NativeDatabaseSnapshot& after);
}
