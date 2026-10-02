#pragma once
#include "game/profiles/generated.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace devtools {
struct DatabaseObjectKey {
    std::uint32_t class_id = 0, id = 0;
    bool state_database = false;
    bool operator==(const DatabaseObjectKey&) const = default;
};

struct DatabaseReference {
    std::wstring field;
    std::uint32_t class_id = 0, id = 0;
};

struct DatabaseVariable {
    std::int32_t raw_value = 0;
    std::uint8_t type_flags = 0;
    bool operator==(const DatabaseVariable&) const = default;
};

struct NativeDatabaseObject {
    std::uint32_t id = 0;
    std::uint32_t class_id = 0;
    std::uintptr_t address = 0;
    std::uint32_t vtable_rva = 0;
    std::uint16_t refcount = 0;
    bool state_database = false;
    std::wstring description;
    std::vector<std::uint8_t> bytes;
    std::optional<std::uint32_t> file_offset;
    std::wstring fields;
    std::vector<DatabaseReference> relationships;
    std::optional<DatabaseVariable> variable;

    DatabaseObjectKey key() const {
        return {class_id, id, state_database};
    }
};

struct NativeDatabaseSnapshot {
    std::uintptr_t manager_address = 0;
    std::uintptr_t hdb_address = 0;
    std::uintptr_t state_address = 0;
    std::vector<NativeDatabaseObject> objects;
    std::size_t visited_nodes = 0;
    std::size_t skipped_nodes = 0;
    bool truncated = false;
    bool available = false;
};

// Call on the game thread. Results own their values and never retain native objects.
NativeDatabaseSnapshot inspect_database(const std::byte* image,
                                        const native_game::Profile& profile);
}
