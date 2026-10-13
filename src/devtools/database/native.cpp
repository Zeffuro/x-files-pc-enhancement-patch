#include "memory.h"
#include "native.h"
#include "game/layouts/database/cache.h"
#include "game/access/database.h"
#include "game/functions/runtime.h"
#include "types.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>

namespace devtools {
namespace {
template <class T> bool read(std::uintptr_t address, T& value) {
    return database_copy(address, &value, sizeof(value));
}

class Walker {
public:
    Walker(const std::byte* image, const native_game::Profile& profile)
        : image(reinterpret_cast<std::uintptr_t>(image)), profile(profile), native(image, profile) {
    }

    void visit(std::uint32_t pointer, bool state, unsigned depth = 0) {
        if (!pointer || visited.contains(pointer)) {
            return;
        }
        if (depth >= 32 || visited.size() >= 8192 || result.objects.size() >= 16384) {
            result.truncated = true;
            return;
        }
        visited.insert(pointer);
        ++result.visited_nodes;
        native_game::DatabaseNode node{};
        const native_game::DatabaseNodeRef node_ref(pointer, &profile);
        if (!read(pointer, node)) {
            ++result.skipped_nodes;
            return;
        }
        if (node.object.vtable ==
            reinterpret_cast<std::uintptr_t>(native.database_branch_node_type())) {
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::DatabaseBranchEntry entry{};
                if (read(node_ref.branch_entry(i).address(), entry)) {
                    visit(entry.cached_node, state, depth + 1);
                } else {
                    ++result.skipped_nodes;
                }
            }
        } else if (node.object.vtable ==
                   reinterpret_cast<std::uintptr_t>(native.database_class_node_type())) {
            if (node.count > 16) {
                ++result.skipped_nodes;
                return;
            }
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::DatabaseClassEntry entry{};
                if (!read(node_ref.class_entry(i).address(), entry) ||
                    entry.index_count > std::size(entry.indexes)) {
                    ++result.skipped_nodes;
                    continue;
                }
                for (unsigned j = 0; j < entry.index_count; ++j) {
                    visit(entry.indexes[j].cached_root, state, depth + 1);
                }
            }
        } else if (node.object.vtable ==
                   reinterpret_cast<std::uintptr_t>(native.database_object_node_type())) {
            for (unsigned i = 0; i < node.count; ++i) {
                native_game::DatabaseObjectEntry entry{};
                if (read(node_ref.object_entry(i).address(), entry)) {
                    object(entry, state);
                } else {
                    ++result.skipped_nodes;
                }
            }
        } else {
            ++result.skipped_nodes;
        }
    }

    NativeDatabaseSnapshot result;

private:
    void object(const native_game::DatabaseObjectEntry& entry, bool state) {
        if (!entry.cached_object || objects.contains(entry.cached_object)) {
            return;
        }
        if (result.objects.size() >= 16384) {
            result.truncated = true;
            return;
        }
        native_game::PersistentObject object{};
        if (!read(entry.cached_object, object) || object.id != entry.id ||
            object.vtable < image + native_game::database_image_limit::first_rva ||
            object.vtable >= image + native_game::database_image_limit::vtable_end) {
            return;
        }
        std::uint32_t getter = 0;
        std::array<std::uint8_t, 6> code{};
        if (!read(native_game::PersistentVTableRef(object.vtable).class_getter().address(),
                  getter) ||
            getter < image + native_game::database_image_limit::first_rva ||
            getter >= image + native_game::database_image_limit::class_getter_end ||
            !read(getter, code) || code[0] != 0xb8 || code[5] != 0xc3) {
            return;
        }
        std::uint32_t class_id = 0;
        std::memcpy(&class_id, code.data() + 1, sizeof(class_id));
        if (class_id < 0x27 || class_id > 0x5c) {
            return;
        }
        objects.insert(entry.cached_object);
        NativeDatabaseObject value{object.id,
                                   class_id,
                                   entry.cached_object,
                                   static_cast<std::uint32_t>(object.vtable - image),
                                   object.references,
                                   state,
                                   {},
                                   {},
                                   {},
                                   {},
                                   {},
                                   {}};
        const auto* prefix = reinterpret_cast<const std::uint8_t*>(&object);
        value.bytes.assign(prefix, prefix + sizeof(object));
        if (!state && entry.file_mark && entry.file_mark == object.file_mark) {
            value.file_offset = entry.file_mark;
        }
        inspect_database_fields(value, reinterpret_cast<const std::byte*>(image), profile);
        result.objects.push_back(std::move(value));
    }

    std::uintptr_t image;
    const native_game::Profile& profile;
    const native_game::NativeRuntime native;
    std::unordered_set<std::uint32_t> visited;
    std::unordered_set<std::uint32_t> objects;
};
}

NativeDatabaseSnapshot inspect_database(const std::byte* image,
                                        const native_game::Profile& profile) {
    if (!image || !profile.database_manager || !profile.database_class_node ||
        !profile.database_branch_node || !profile.database_object_node) {
        return {};
    }
    const native_game::NativeRuntime native(image, profile);
    std::uint32_t manager = 0, hdb_root = 0, state_root = 0;
    if (!read(native.database_manager().address(), manager) || !manager ||
        !read(native_game::DatabaseManagerRef(manager).hdb().root().address(), hdb_root) ||
        !read(native_game::DatabaseManagerRef(manager).state().root().address(), state_root)) {
        return {};
    }
    Walker walker(image, profile);
    walker.result.manager_address = manager;
    walker.result.hdb_address = native_game::DatabaseManagerRef(manager).hdb().address();
    walker.result.state_address = native_game::DatabaseManagerRef(manager).state().address();
    walker.visit(hdb_root, false);
    walker.visit(state_root, true);
    std::uint32_t after = 0, root_after = 0, state_after = 0;
    if (!read(native.database_manager().address(), after) || after != manager ||
        !read(native_game::DatabaseManagerRef(manager).hdb().root().address(), root_after) ||
        root_after != hdb_root ||
        !read(native_game::DatabaseManagerRef(manager).state().root().address(), state_after) ||
        state_after != state_root) {
        return {};
    }
    walker.result.available = true;
    std::sort(walker.result.objects.begin(), walker.result.objects.end(),
              [](const auto& a, const auto& b) {
                  if (a.class_id != b.class_id) {
                      return a.class_id < b.class_id;
                  }
                  if (a.id != b.id) {
                      return a.id < b.id;
                  }
                  return a.state_database < b.state_database;
              });
    return std::move(walker.result);
}
}
