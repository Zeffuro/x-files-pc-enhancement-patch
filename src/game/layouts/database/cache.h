#pragma once
#include <cstddef>
#include <cstdint>

namespace native_game {
struct PersistentObject {
    std::uint32_t vtable;
    std::uint32_t id;
    std::byte flags[0x10];
    std::uint32_t version;
    std::uint32_t file_mark;
    std::uint32_t parent;
    std::uint16_t references;
    std::byte reserved[2];
};

struct DatabaseNode {
    PersistentObject object;
    std::byte reserved[5];
    std::uint8_t count;
    std::byte alignment[2];
};

struct DatabaseBranchEntry {
    std::uint32_t file_mark;
    std::uint32_t cached_node;
};

struct DatabaseObjectEntry {
    std::uint32_t id;
    std::uint32_t cached_object;
    std::uint32_t file_mark;
};

struct DatabaseIndex {
    std::uint32_t index_class;
    std::uint32_t base_class;
    std::uint32_t file_mark;
    std::uint32_t tag;
    std::uint32_t cached_root;
    std::uint16_t flags;
    std::byte reserved[2];
};

struct DatabaseClassEntry {
    std::uint32_t class_id;
    std::byte reserved[0x2c];
    std::uint8_t index_count;
    std::byte alignment[3];
    DatabaseIndex indexes[6];
    std::byte trailing[8];
};

static_assert(sizeof(PersistentObject) == 0x28);
static_assert(offsetof(PersistentObject, references) == 0x24);
static_assert(sizeof(DatabaseNode) == 0x30);
static_assert(offsetof(DatabaseNode, count) == 0x2d);
static_assert(sizeof(DatabaseBranchEntry) == 8);
static_assert(sizeof(DatabaseObjectEntry) == 12);
static_assert(sizeof(DatabaseIndex) == 0x18);
static_assert(offsetof(DatabaseClassEntry, indexes) == 0x34);
static_assert(sizeof(DatabaseClassEntry) == 0xcc);
}
