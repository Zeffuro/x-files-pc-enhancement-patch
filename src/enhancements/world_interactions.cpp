#include "world_interactions.h"
#include "world_cursors.h"
#include "game/layouts/database/cache.h"
#include "game/layouts/native_objects.h"
#include "game/layouts/input/events.h"
#include "game/access/world.h"
#include "game/access/database.h"
#include "game/functions/runtime.h"

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
    if (!snapshot.read(native_game::InputEventsRef(object).actions().data(), list) ||
        !list.vtable || list.count > 256) {
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
                   const void* resource_type, Snapshot& snapshot)
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
        if (address(node.object.vtable) ==
            native_game::NativeRuntime(image, profile).database_branch_node_type()) {
            std::array<native_game::DatabaseBranchEntry, 32> entries{};
            if (!snapshot.copy(native_game::DatabaseNodeRef(source).branch_entry(0).data(),
                               entries.data(), node.count * sizeof(entries[0]))) {
                return nullptr;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                if (const auto result = find(entries[i].cached_node, depth + 1)) {
                    return result;
                }
            }
        } else if (address(node.object.vtable) ==
                   native_game::NativeRuntime(image, profile).database_class_node_type()) {
            if (node.count > 16) {
                return nullptr;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::DatabaseClassEntry entry{};
                if (!snapshot.read(native_game::DatabaseNodeRef(source).class_entry(i).data(),
                                   entry) ||
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
        } else if (address(node.object.vtable) ==
                   native_game::NativeRuntime(image, profile).database_object_node_type()) {
            std::array<native_game::DatabaseObjectEntry, 32> entries{};
            if (!snapshot.copy(native_game::DatabaseNodeRef(source).object_entry(0).data(),
                               entries.data(), node.count * sizeof(entries[0]))) {
                return nullptr;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::PersistentObject object{};
                if (entries[i].id == id &&
                    snapshot.read(address(entries[i].cached_object), object) && object.id == id &&
                    address(object.vtable) == resource_type) {
                    return address(entries[i].cached_object);
                }
            }
        }
        return nullptr;
    }

private:
    std::byte* image;
    const Edition& profile;
    unsigned id, class_id;
    const void* resource_type;
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
    if (type == native_game::NativeRuntime(image, profile).association_id_node_type()) {
        std::array<unsigned, 32> entries{};
        if (ids.size() + node.count > 256 ||
            !snapshot.copy(native_game::DatabaseNodeRef(source).id_list().data(), entries.data(),
                           node.count * sizeof(entries[0]))) {
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
    if (type != native_game::NativeRuntime(image, profile).database_branch_node_type()) {
        return false;
    }
    std::array<native_game::DatabaseBranchEntry, 32> entries{};
    if (!snapshot.copy(native_game::DatabaseNodeRef(source).branch_entry(0).data(), entries.data(),
                       node.count * sizeof(entries[0]))) {
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
    native_game::ConversationRegistry registry{};
    using Key = native_game::ConversationRegistryKey;

    if (!snapshot.read(native_game::DatabaseManagerRef(manager).conversations().data(), registry) ||
        address(registry.vtable) !=
            native_game::NativeRuntime(image, profile).registry_container_type() ||
        registry.count > 4096 || registry.capacity < registry.count || registry.capacity > 65536) {
        return false;
    }
    std::vector<Key> keys(registry.count);
    if (registry.count &&
        !snapshot.copy(address(registry.entries), keys.data(), keys.size() * sizeof(Key))) {
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
    if (!snapshot.read(native_game::NativeRuntime(image, profile).database_manager().data(),
                       cache) ||
        !snapshot.read(native_game::DatabaseManagerRef(address(cache)).hdb().root().data(), root)) {
        return Association::unknown;
    }
    CachedResource lookup(
        image, profile, id, native_game::database_class::conversation_association,
        native_game::NativeRuntime(image, profile).conversation_association_type(), snapshot);
    const auto association = lookup.find(root);
    std::uint32_t type = 0, count = 0, index_class = 0, tag = 0, node = 0;
    if (!association ||
        !snapshot.read(native_game::ConversationAssociationRef(association).tree().data(), type) ||
        address(type) !=
            native_game::NativeRuntime(image, profile).conversation_association_tree_type() ||
        !snapshot.read(native_game::ConversationAssociationRef(association).count().data(),
                       count) ||
        count > 256 ||
        !snapshot.read(native_game::ConversationAssociationRef(association).index_class().data(),
                       index_class) ||
        index_class != native_game::database_index::id_class ||
        !snapshot.read(native_game::ConversationAssociationRef(association).tag().data(), tag) ||
        tag != native_game::database_index::id_tag ||
        !snapshot.read(native_game::ConversationAssociationRef(association).cached_root().data(),
                       node) ||
        (!count && node)) {
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
        CachedResource child(
            image, profile, ids[i], native_game::database_class::conversation,
            native_game::NativeRuntime(image, profile).conversation_resource_type(), snapshot);
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
    const bool picture = type == native_game::NativeRuntime(image, profile).world_picture_type();
    if (!picture && type != native_game::NativeRuntime(image, profile).world_hotspot_type()) {
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

    native_game::PictureActions actions{};

    const void* manager = nullptr;
    const void* own_resource = nullptr;
    const void* resource_type = nullptr;
    unsigned resource_id = 0;
    unsigned conversation = 0, evidence = 0;
    if (!profile.world_picture_resource ||
        !snapshot.read(native_game::ResourceObjectRef(object).resource().data(), own_resource) ||
        own_resource != resource || !snapshot.read(resource, resource_type) ||
        resource_type != native_game::NativeRuntime(image, profile).world_picture_resource_type() ||
        !snapshot.read(native_game::ResourceHeaderRef(resource).id().data(), resource_id) ||
        !resource_id ||
        !snapshot.read(native_game::WorldPictureRef(object).actions().data(), actions) ||
        !snapshot.read(native_game::PictureResourceRef(resource).conversation().data(),
                       conversation) ||
        !snapshot.read(native_game::PictureResourceRef(resource).evidence().data(), evidence) ||
        !snapshot.read(native_game::ApplicationRef(app).database().data(), manager)) {
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
