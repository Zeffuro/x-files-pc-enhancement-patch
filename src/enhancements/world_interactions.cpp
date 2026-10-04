#include "world_interactions.h"
#include "world_cursors.h"
#include "game/layouts/database/cache.h"

#include <array>
#include <cstring>
#include <span>
#include <unordered_set>

namespace enhancements::game {
namespace {

bool copy(const void* address, void* target, std::size_t size) {
    SIZE_T bytes = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, target, size, &bytes) &&
           bytes == size;
}

const void* field(const void* object, unsigned offset) {
    return object ? reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(object) + offset)
                  : nullptr;
}

const void* address(std::uint32_t value) {
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(value));
}

class Snapshot {
public:
    bool copy(const void* source, void* target, std::size_t size) {
        if (reads.size() >= 4096 || bytes + size > 256 * 1024 ||
            !game::copy(source, target, size)) {
            return false;
        }
        reads.push_back({source, std::vector<std::byte>(size)});
        std::memcpy(reads.back().value.data(), target, size);
        bytes += size;
        return true;
    }

    template <class T> bool read(const void* source, T& value) {
        return copy(source, &value, sizeof(value));
    }

    bool coherent() const {
        std::vector<std::byte> value;
        for (const auto& entry : reads) {
            value.resize(entry.value.size());
            if (!game::copy(entry.source, value.data(), value.size()) || value != entry.value) {
                return false;
            }
        }
        return true;
    }

private:
    struct Entry {
        const void* source;
        std::vector<std::byte> value;
    };

    std::vector<Entry> reads;
    std::size_t bytes = 0;
};

bool click_events(const void* object, bool& present, Snapshot& snapshot) {
    List<std::byte> list{};
    if (!snapshot.read(field(object, 0x1c), list) || !list.vtable || list.count > 256) {
        return false;
    }
    auto node = list.first;
    List<std::byte>::Node* previous = nullptr;
    bool current = !list.current;
    for (unsigned i = 0; i < list.count; ++i) {
        List<std::byte>::Node entry{};
        void* type = nullptr;
        if (!snapshot.read(node, entry) || entry.previous != previous ||
            !snapshot.read(entry.value, type) || !type) {
            return false;
        }
        current = current || node == list.current;
        previous = node;
        node = entry.next;
    }
    present = list.count != 0;
    return !node && previous == list.last && current;
}

class CachedResource {
public:
    CachedResource(std::byte* image, const Edition& profile, unsigned id, unsigned class_id,
                   unsigned resource_type, Snapshot& snapshot)
        : image(image), profile(profile), id(id), class_id(class_id), resource_type(resource_type),
          snapshot(snapshot) {}

    const void* find(std::uint32_t pointer, unsigned depth = 0) {
        if (!pointer || depth >= 16 || visited.size() >= 512 || !visited.insert(pointer).second) {
            return nullptr;
        }
        native_game::DatabaseNode node{};
        const auto source = address(pointer);
        if (!snapshot.read(source, node) || node.count > 32) {
            return nullptr;
        }
        if (node.object.vtable == type(profile.database_branch_node)) {
            std::array<native_game::DatabaseBranchEntry, 32> entries{};
            if (!snapshot.copy(field(source, 0x30), entries.data(),
                               node.count * sizeof(entries[0]))) {
                return nullptr;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                if (const auto result = find(entries[i].cached_node, depth + 1)) {
                    return result;
                }
            }
        } else if (node.object.vtable == type(profile.database_class_node)) {
            if (node.count > 16) {
                return nullptr;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::DatabaseClassEntry entry{};
                if (!snapshot.read(field(source, 0x30 + i * sizeof(entry)), entry) ||
                    entry.class_id != class_id) {
                    continue;
                }
                if (entry.index_count > std::size(entry.indexes)) {
                    continue;
                }
                for (unsigned j = 0; j < entry.index_count; ++j) {
                    if (const auto result = find(entry.indexes[j].cached_root, depth + 1)) {
                        return result;
                    }
                }
            }
        } else if (node.object.vtable == type(profile.database_object_node)) {
            std::array<native_game::DatabaseObjectEntry, 32> entries{};
            if (!snapshot.copy(field(source, 0x30), entries.data(),
                               node.count * sizeof(entries[0]))) {
                return nullptr;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::PersistentObject object{};
                if (entries[i].id == id &&
                    snapshot.read(address(entries[i].cached_object), object) && object.id == id &&
                    object.vtable == type(resource_type)) {
                    return address(entries[i].cached_object);
                }
            }
        }
        return nullptr;
    }

private:
    std::uint32_t type(unsigned rva) const {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image) + rva);
    }

    std::byte* image;
    const Edition& profile;
    unsigned id, class_id, resource_type;
    Snapshot& snapshot;
    std::unordered_set<std::uint32_t> visited;
};

bool association_ids(const void* source, std::byte* image, const Edition& profile,
                     std::vector<unsigned>& ids, std::unordered_set<const void*>& visited,
                     Snapshot& snapshot, unsigned depth = 0) {
    if (!source || depth >= 16 || visited.size() >= 256 || !visited.insert(source).second) {
        return false;
    }
    native_game::DatabaseNode node{};
    if (!snapshot.read(source, node) || !node.count || node.count > 32) {
        return false;
    }
    const auto type = address(node.object.vtable);
    if (type == image + profile.association_id_node) {
        std::array<unsigned, 32> entries{};
        if (ids.size() + node.count > 256 ||
            !snapshot.copy(field(source, 0x30), entries.data(), node.count * sizeof(entries[0]))) {
            return false;
        }
        for (unsigned i = 0; i < node.count; ++i) {
            if (!entries[i]) {
                return false;
            }
            ids.push_back(entries[i]);
        }
        return true;
    }
    if (type != image + profile.database_branch_node) {
        return false;
    }
    std::array<native_game::DatabaseBranchEntry, 32> entries{};
    if (!snapshot.copy(field(source, 0x30), entries.data(), node.count * sizeof(entries[0]))) {
        return false;
    }
    for (unsigned i = 0; i < node.count; ++i) {
        if (!association_ids(address(entries[i].cached_node), image, profile, ids, visited,
                             snapshot, depth + 1)) {
            return false;
        }
    }
    return true;
}

bool enabled_conversations(const void* manager, std::span<const unsigned> ids, std::byte* image,
                           const Edition& profile, std::vector<bool>& enabled, Snapshot& snapshot) {
    struct Registry {
        const void* type;
        unsigned count, iterator;
        const void* entries;
        unsigned capacity, growth;
    } registry{};

    struct Key {
        unsigned vtable, state_id, id;
        std::uint8_t enabled;
        std::byte reserved[3];
    };

    static_assert(sizeof(Registry) == 0x18 && sizeof(Key) == 0x10);
    if (!snapshot.read(field(manager, 0x94), registry) ||
        registry.type != image + profile.registry_container || registry.count > 4096 ||
        registry.capacity < registry.count || registry.capacity > 65536) {
        return false;
    }
    std::vector<Key> keys(registry.count);
    if (registry.count &&
        !snapshot.copy(registry.entries, keys.data(), keys.size() * sizeof(Key))) {
        return false;
    }
    unsigned previous = 0;
    for (const auto& key : keys) {
        if (!key.vtable || !key.id || key.id <= previous) {
            return false;
        }
        previous = key.id;
    }
    // Native lookup defaults missing keys to enabled and reads only the flag's low byte.
    for (const auto id : ids) {
        const auto key =
            std::lower_bound(keys.begin(), keys.end(), id,
                             [](const Key& entry, unsigned value) { return entry.id < value; });
        enabled.push_back(key == keys.end() || key->id != id || key->enabled);
    }
    return true;
}

enum class Association { unknown, absent, present };

Association conversation_status(unsigned id, const void* manager, std::byte* image,
                                const Edition& profile) {
    if (!id) {
        return Association::absent;
    }
    Snapshot snapshot;
    std::uint32_t cache = 0, root = 0;
    if (!snapshot.read(image + profile.database_manager, cache) ||
        !snapshot.read(field(address(cache), 0x1c), root)) {
        return Association::unknown;
    }
    CachedResource lookup(image, profile, id, 0x32, profile.conversation_association, snapshot);
    const auto association = lookup.find(root);
    std::uint32_t type = 0, count = 0, index_class = 0, tag = 0, node = 0;
    if (!association || !snapshot.read(field(association, 0x28), type) ||
        address(type) != image + profile.conversation_association_tree ||
        !snapshot.read(field(association, 0x3c), count) || count > 256 ||
        !snapshot.read(field(association, 0x44), index_class) || index_class != 0xb ||
        !snapshot.read(field(association, 0x48), tag) || tag != 0x49442020 ||
        !snapshot.read(field(association, 0x58), node) || (!count && node)) {
        return Association::unknown;
    }
    std::vector<unsigned> ids;
    std::unordered_set<const void*> visited;
    std::vector<bool> enabled;
    if (count && (!association_ids(address(node), image, profile, ids, visited, snapshot) ||
                  ids.size() != count ||
                  !enabled_conversations(manager, ids, image, profile, enabled, snapshot))) {
        return Association::unknown;
    }
    Association result = Association::absent;
    for (unsigned i = 0; i < ids.size(); ++i) {
        if (!enabled[i]) {
            continue;
        }
        result = Association::unknown;
        CachedResource child(image, profile, ids[i], 0x31, profile.conversation_resource, snapshot);
        if (profile.conversation_resource && child.find(root)) {
            result = Association::present;
            break;
        }
    }
    return snapshot.coherent() ? result : Association::unknown;
}

}

Interaction world_interaction(const void* object, const void* resource, Application* app,
                              std::byte* image, const Edition& profile) {
    Snapshot snapshot;
    const void* type = nullptr;
    if (!image || !snapshot.read(object, type)) {
        return Interaction::unknown;
    }
    const bool picture = type == image + profile.world_picture;
    if (!picture && type != image + profile.world_hotspot) {
        return Interaction::unknown;
    }
    bool clicks = false;
    if (!click_events(object, clicks, snapshot)) {
        return Interaction::unknown;
    }
    if (clicks) {
        return snapshot.coherent() ? world_cursor_interaction(object, resource, app, image, profile,
                                                              Interaction::click)
                                   : Interaction::unknown;
    }
    if (!picture) {
        return Interaction::unknown;
    }

    struct Actions {
        const void* slots[6];
        unsigned eligible;
    } actions{};

    const void* manager = nullptr;
    const void* own_resource = nullptr;
    const void* resource_type = nullptr;
    unsigned resource_id = 0;
    unsigned conversation = 0, evidence = 0;
    if (!profile.world_picture_resource || !snapshot.read(field(object, 0x18), own_resource) ||
        own_resource != resource || !snapshot.read(resource, resource_type) ||
        resource_type != image + profile.world_picture_resource ||
        !snapshot.read(field(resource, 4), resource_id) || !resource_id ||
        !snapshot.read(field(object, 0x148), actions) ||
        !snapshot.read(field(resource, 0x48), conversation) ||
        !snapshot.read(field(resource, 0x4c), evidence) ||
        !snapshot.read(field(app, 0xe8), manager)) {
        return Interaction::unknown;
    }
    const auto status = conversation_status(conversation, manager, image, profile);
    if (!snapshot.coherent()) {
        return Interaction::unknown;
    }
    if (status == Association::absent && !evidence && actions.eligible &&
        std::any_of(std::begin(actions.slots), std::end(actions.slots),
                    [](const void* action) { return action; })) {
        return Interaction::item;
    }
    return status == Association::present
               ? world_cursor_interaction(object, resource, app, image, profile, Interaction::click)
               : Interaction::unknown;
}

}
