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

inline constexpr std::size_t persistent_dirty_status = offsetof(PersistentObject, flags) + 12;
inline constexpr std::uint8_t persistent_stable_status_mask = 0xf3;

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

struct DatabaseCachePrefix {
    std::byte reserved[0x18];
    std::uint32_t root;
};

struct ConversationRegistry {
    std::uint32_t vtable, count, iterator, entries, capacity, growth;
};

struct ConversationRegistryKey {
    std::uint32_t vtable, state_id, id;
    std::uint8_t enabled;
    std::byte reserved[3];
};

struct DatabaseManagerPrefix {
    std::uint32_t vtable;
    DatabaseCachePrefix hdb;
    std::byte before_conversations[0x94 - 0x20];
    ConversationRegistry conversations;
    std::byte before_state[0xd8 - 0xac];
    DatabaseCachePrefix state;
};

struct ConversationAssociationPrefix {
    PersistentObject object;
    std::uint32_t tree;
    std::byte before_count[0x3c - 0x2c];
    std::uint32_t count;
    std::byte before_index[4];
    std::uint32_t index_class, tag;
    std::byte before_root[0x58 - 0x4c];
    std::uint32_t cached_root;
};

namespace database_manager_offset {
inline constexpr auto hdb = offsetof(DatabaseManagerPrefix, hdb);
inline constexpr auto state = offsetof(DatabaseManagerPrefix, state);
inline constexpr auto conversation_registry = offsetof(DatabaseManagerPrefix, conversations);
}

inline constexpr std::size_t persistent_class_getter_offset = 2 * sizeof(std::uint32_t);

struct PersistentVTablePrefix {
    std::uint32_t reserved[2];
    std::uint32_t class_getter;
};

static_assert(offsetof(PersistentVTablePrefix, class_getter) == persistent_class_getter_offset);

namespace database_index {
inline constexpr std::uint32_t id_class = 0x0b;
inline constexpr std::uint32_t id_tag = 0x49442020;
}

namespace database_class {
inline constexpr unsigned conversation = 0x31;
inline constexpr unsigned conversation_association = 0x32;
}

namespace database_image_limit {
inline constexpr std::uintptr_t first_rva = 0x1000;
inline constexpr std::uintptr_t vtable_end = 0x300000;
inline constexpr std::uintptr_t class_getter_end = 0x260000;
}

namespace conversation_association_offset {
inline constexpr auto tree = offsetof(ConversationAssociationPrefix, tree);
inline constexpr auto count = offsetof(ConversationAssociationPrefix, count);
inline constexpr auto index_class = offsetof(ConversationAssociationPrefix, index_class);
inline constexpr auto tag = offsetof(ConversationAssociationPrefix, tag);
inline constexpr auto cached_root = offsetof(ConversationAssociationPrefix, cached_root);
}

static_assert(sizeof(PersistentObject) == 0x28);
static_assert(offsetof(PersistentObject, references) == 0x24);
static_assert(sizeof(DatabaseNode) == 0x30);
static_assert(offsetof(DatabaseNode, count) == 0x2d);
static_assert(sizeof(DatabaseBranchEntry) == 8);
static_assert(sizeof(DatabaseObjectEntry) == 12);
static_assert(sizeof(DatabaseIndex) == 0x18);
static_assert(offsetof(DatabaseClassEntry, indexes) == 0x34);
static_assert(sizeof(DatabaseClassEntry) == 0xcc);
static_assert(offsetof(DatabaseCachePrefix, root) == 0x18);
static_assert(sizeof(ConversationRegistry) == 0x18);
static_assert(sizeof(ConversationRegistryKey) == 0x10);
static_assert(database_manager_offset::hdb == 4);
static_assert(database_manager_offset::state == 0xd8);
static_assert(database_manager_offset::conversation_registry == 0x94);
static_assert(conversation_association_offset::tree == 0x28);
static_assert(conversation_association_offset::count == 0x3c);
static_assert(conversation_association_offset::index_class == 0x44);
static_assert(conversation_association_offset::tag == 0x48);
static_assert(conversation_association_offset::cached_root == 0x58);
}
