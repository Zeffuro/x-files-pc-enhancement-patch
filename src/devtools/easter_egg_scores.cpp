#include "easter_egg_scores.h"
#include "database/memory.h"
#include "game/access/database.h"
#include "game/functions/runtime.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devtools {
namespace {
using namespace native_game;
constexpr unsigned read_limit = 100;
// Class-tree descriptors keep the object-index class and tag read from the GAM.
constexpr std::uint32_t variable_index_class = 7, variable_index_tag = 0x6e756c6c;

EasterEggScores empty_scores() {
    return {{{{L"Asta", 1411, {}, {}},
              {L"Paranoia", 859, {}, {}},
              {L"Loss", 860, {}, {}},
              {L"X", 858, {}, {}},
              {L"Asta low threshold", 4683, {}, {}},
              {L"Asta high threshold", 4682, {}, {}}}},
            false};
}

class Reader {
public:
    bool copy(std::uintptr_t address, void* destination, std::size_t size) {
        return ++reads <= read_limit && database_copy(address, destination, size);
    }

    template <class T> bool read(std::uintptr_t address, T& value) {
        return copy(address, &value, sizeof(value));
    }

    template <class T> bool entries(std::uint32_t pointer, unsigned count, std::vector<T>& values) {
        values.resize(count);
        return !count || copy(std::uintptr_t(pointer) + sizeof(DatabaseNode), values.data(),
                              values.size() * sizeof(T));
    }

    bool exhausted() const {
        return reads > read_limit;
    }

private:
    unsigned reads = 0;
};

struct Identity {
    std::uint32_t manager = 0, root = 0;
    int session = 0;
    Application* application = nullptr;
    void* state = nullptr;
    bool operator==(const Identity&) const = default;
};

bool identity(Reader& reader, const NativeRuntime& native, Identity& value) {
    return reader.read(native.database_manager().address(), value.manager) && value.manager &&
           reader.read(DatabaseManagerRef(value.manager).state().root().address(), value.root) &&
           value.root && reader.read(native.session_active().address(), value.session) &&
           reader.read(native.application().address(), value.application) && value.application &&
           reader.read(ApplicationRef(value.application).state().address(), value.state) &&
           value.state;
}

class Scores {
public:
    Scores(Reader& reader, const std::byte* image, const Profile& profile)
        : reader(reader), base(reinterpret_cast<std::uintptr_t>(image)), native(image, profile) {}

    bool classes(std::uint32_t pointer, unsigned depth = 0) {
        if (!pointer) {
            return true;
        }
        DatabaseNode node{};
        if (!node_at(pointer, depth, node)) {
            return false;
        }
        if (node.object.vtable == address(native.database_branch_node_type())) {
            std::vector<DatabaseBranchEntry> entries;
            if (!reader.entries(pointer, node.count, entries)) {
                return false;
            }
            for (const auto& entry : entries) {
                if (!classes(entry.cached_node, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        if (node.object.vtable != address(native.database_class_node_type()) || node.count > 16) {
            return false;
        }
        std::vector<DatabaseClassEntry> entries;
        if (!reader.entries(pointer, node.count, entries)) {
            return false;
        }
        for (const auto& entry : entries) {
            if (entry.index_count > std::size(entry.indexes)) {
                return false;
            }
            if (entry.class_id != 0x53) {
                continue;
            }
            if (variable_class_seen) {
                return false;
            }
            variable_class_seen = true;
            for (unsigned i = 0; i < entry.index_count; ++i) {
                const auto& index = entry.indexes[i];
                if (index.index_class == variable_index_class && index.base_class == 0x53 &&
                    index.tag == variable_index_tag) {
                    if (id_index_seen) {
                        return false;
                    }
                    id_index_seen = true;
                    object_root = index.cached_root;
                }
            }
        }
        return true;
    }

    bool objects(std::uint32_t pointer, unsigned depth = 0) {
        if (!pointer) {
            return true;
        }
        DatabaseNode node{};
        if (!node_at(pointer, depth, node)) {
            return false;
        }
        if (node.object.vtable == address(native.database_branch_node_type())) {
            std::vector<DatabaseBranchEntry> entries;
            if (!reader.entries(pointer, node.count, entries)) {
                return false;
            }
            for (const auto& entry : entries) {
                if (!objects(entry.cached_node, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        if (node.object.vtable != address(native.database_object_node_type())) {
            return false;
        }
        std::vector<DatabaseObjectEntry> entries;
        if (!reader.entries(pointer, node.count, entries)) {
            return false;
        }
        for (const auto& entry : entries) {
            const auto found =
                std::find_if(result.scores.begin(), result.scores.end(),
                             [&](const auto& score) { return score.id == entry.id; });
            if (found == result.scores.end()) {
                continue;
            }
            if (!ids.insert(entry.id).second) {
                return false;
            }
            Variable variable{};
            if (!entry.cached_object || !reader.read(entry.cached_object, variable) ||
                variable.id != entry.id || !variable_type(variable.vtable)) {
                continue;
            }
            auto name = variable_name(variable.name);
            if (name) {
                found->name = std::move(*name);
                found->raw_value = variable.raw_value;
            }
        }
        return !reader.exhausted();
    }

    std::uint32_t object_root = 0;
    EasterEggScores result = empty_scores();

private:
    static std::uint32_t address(const void* value) {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
    }

    bool node_at(std::uint32_t pointer, unsigned depth, DatabaseNode& value) {
        return depth < 16 && nodes.size() < 40 && nodes.insert(pointer).second &&
               reader.read(pointer, value);
    }

    bool variable_type(std::uint32_t vtable) {
        if (const auto found = types.find(vtable); found != types.end()) {
            return found->second;
        }
        std::uint32_t getter = 0;
        std::array<std::uint8_t, 6> code{};
        const bool valid =
            vtable >= base + database_image_limit::first_rva &&
            vtable < base + database_image_limit::vtable_end &&
            reader.read(PersistentVTableRef(vtable).class_getter().address(), getter) &&
            getter >= base + database_image_limit::first_rva &&
            getter < base + database_image_limit::class_getter_end && reader.read(getter, code) &&
            code == std::array<std::uint8_t, 6>{0xb8, 0x53, 0, 0, 0, 0xc3};
        types.emplace(vtable, valid);
        return valid;
    }

    std::optional<std::wstring> variable_name(const StoryString& value) {
        if (!value.data || !value.capacity || value.capacity > 1024) {
            return {};
        }
        std::array<char, 128> bytes{};
        const auto size = std::min<std::size_t>(bytes.size(), value.capacity);
        if (!reader.copy(value.data, bytes.data(), size)) {
            return {};
        }
        const auto end = std::find(bytes.begin(), bytes.begin() + size, '\0');
        if (end == bytes.begin() + size || !std::all_of(bytes.begin(), end, [](unsigned char ch) {
                return ch >= 0x20 && ch <= 0x7e;
            })) {
            return {};
        }
        return std::wstring(bytes.begin(), end);
    }

    Reader& reader;
    std::uintptr_t base;
    NativeRuntime native;
    std::unordered_set<std::uint32_t> nodes, ids;
    std::unordered_map<std::uint32_t, bool> types;
    bool variable_class_seen = false, id_index_seen = false;
};
}

EasterEggScores capture_easter_egg_scores(const std::byte* image, const Profile& profile) {
    if (!image || !profile.application || !profile.session_active || !profile.database_manager ||
        !profile.database_branch_node || !profile.database_class_node ||
        !profile.database_object_node) {
        return empty_scores();
    }
    const NativeRuntime native(image, profile);
    Reader reader;
    Identity before, after;
    if (!identity(reader, native, before)) {
        return empty_scores();
    }
    Scores scores(reader, image, profile);
    if (!scores.classes(before.root) || !scores.objects(scores.object_root) ||
        !identity(reader, native, after) || before != after) {
        return empty_scores();
    }
    scores.result.available = true;
    return std::move(scores.result);
}
}
