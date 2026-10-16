#include "state.h"
#include "game/access/database.h"
#include "game/access/script.h"
#include "game/access/world.h"
#include "game/functions/runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace native_story {
std::string quote(std::string_view value) {
    std::string result = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 32 || ch > 126) {
            result += "\\u00";
            result += digits[ch >> 4];
            result += digits[ch & 15];
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result + '"';
}

namespace {
using namespace native_game;

std::uintptr_t pointer(const void* value) {
    return reinterpret_cast<std::uintptr_t>(value);
}

class Reader {
public:
    explicit Reader(HANDLE process)
        : process(process),
          deadline(
              GetTickCount64() +
              (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") ? 8000 : 1500)) {}

    void copy(std::uintptr_t source, void* destination, std::size_t count, std::size_t stable = 0) {
        stable = stable ? stable : count;
        if (!source || stable > count || count > 1024 * 1024 || source > UINT32_MAX - count) {
            throw std::runtime_error("invalid_remote_read_extent");
        }
        if (reads.size() >= 65536) {
            throw std::runtime_error("snapshot_read_count_bound");
        }
        if (bytes + count > 2 * 1024 * 1024) {
            throw std::runtime_error("snapshot_byte_count_bound");
        }
        if (GetTickCount64() > deadline) {
            throw std::runtime_error("snapshot_read_deadline");
        }
        exact(source, destination, count);
        reads.push_back({source, std::vector<std::byte>(stable)});
        std::memcpy(reads.back().bytes.data(), destination, stable);
        bytes += count;
    }

    template <class T> T at(std::uintptr_t source) {
        T value{};
        copy(source, &value, sizeof(value));
        return value;
    }

    template <class T> T field(const MemoryRef<T>& source) {
        return at<T>(source.address());
    }

    template <class T> List<T> list_header(const MemoryRef<List<T>>& source) {
        List<T> value{};
        // Native drawing moves this unused iterator without changing list membership.
        copy(source.address(), &value, sizeof(value), offsetof(List<T>, current));
        return value;
    }

    void coherent() {
        std::vector<std::byte> value;
        for (const auto& read : reads) {
            if (GetTickCount64() > deadline) {
                throw std::runtime_error("coherence_deadline");
            }
            value.resize(read.bytes.size());
            exact(read.source, value.data(), value.size());
            if (value != read.bytes) {
                throw std::runtime_error(
                    "state_changed_during_snapshot:" + std::to_string(read.source) + ":" +
                    std::to_string(read.bytes.size()));
            }
        }
    }

    std::size_t count() const {
        return reads.size();
    }

private:
    void exact(std::uintptr_t source, void* destination, std::size_t count) {
        SIZE_T copied = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<const void*>(source), destination, count,
                               &copied) ||
            copied != count) {
            throw std::runtime_error("remote_read_failed");
        }
    }

    struct Read {
        std::uintptr_t source;
        std::vector<std::byte> bytes;
    };

    HANDLE process;
    ULONGLONG deadline;
    std::size_t bytes = 0;
    std::vector<Read> reads;
};

std::string rectangle(const RECT& value) {
    return '[' + std::to_string(value.left) + ',' + std::to_string(value.top) + ',' +
           std::to_string(value.right) + ',' + std::to_string(value.bottom) + ']';
}

bool valid(const RECT& value) {
    return value.left >= 0 && value.top >= 0 && value.right <= 640 && value.bottom <= 480 &&
           value.right > value.left && value.bottom > value.top;
}

std::string_view build_id(BuildId value) {
    switch (value) {
        case BuildId::cd_10012:
            return "cd_10012";
        case BuildId::cd_10019:
            return "cd_10019";
        case BuildId::dvd_20000:
            return "dvd_20000";
        case BuildId::cd_10020:
            return "cd_10020";
        default:
            return "unknown";
    }
}

class Snapshot {
public:
    Snapshot(HANDLE process, std::uintptr_t base, const Build& build)
        : reader(process), image(base), build(build), profile(*build.profile),
          runtime(reinterpret_cast<const std::byte*>(base), profile) {}

    std::string run() {
        const auto app = reader.field(runtime.application());
        const auto pending = reader.field(runtime.pending_load());
        const auto session = reader.field(runtime.session_active());
        const auto scene = reader.field(runtime.scene_active());
        std::ostringstream fields;
        fields << "\"application\":" << pointer(app) << ",\"pending_load\":" << pointer(pending)
               << ",\"session_active\":" << session << ",\"scene_active\":" << scene;
        if (!app) {
            reader.coherent();
            return finish(fields.str());
        }
        const ApplicationRef application(app, &profile);
        const auto state = reader.field(application.state());
        const auto view = reader.field(application.view());
        const auto preferences = reader.field(application.preferences());
        const auto queue = reader.field(application.commands().count());
        fields << ",\"state\":" << pointer(state) << ",\"view\":" << pointer(view)
               << ",\"command_queue_count\":" << queue
               << ",\"current_view_id\":null"
                  ",\"current_view_status\":\"unknown_use_native_save_header\"";
        input(state, preferences, fields);
        collect_cache();
        fields << ",\"variables\":[";
        bool first = true;
        for (const auto& variable_record : variables) {
            if (!first) {
                fields << ',';
            }
            first = false;
            const VariableRef variable(variable_record.address, &profile);
            const auto raw = reader.field(variable.raw_value());
            const auto flags = reader.field(variable.type_flags());
            const auto name = reader.field(variable.name());
            fields << "{\"id\":" << variable_record.id << ",\"raw_value\":" << raw
                   << ",\"state_database\":" << (variable_record.state ? "true" : "false")
                   << ",\"type_flags\":" << static_cast<unsigned>(flags)
                   << ",\"name\":" << (name.data && name.capacity ? quote(string(name)) : "null")
                   << '}';
        }
        fields << "],\"database_status\":\"cached_only\",\"script_roots\":[";
        first = true;
        if (state) {
            for (auto object : list(ScriptStateRef(state).roots())) {
                if (pointer(reader.field(ObjectFlagsRef(object).type())) !=
                    image + profile.script_root) {
                    continue;
                }
                const auto resource = reader.field(ScriptInputRef(object).resource());
                if (resource) {
                    if (!first) {
                        fields << ',';
                    }
                    first = false;
                    fields << reader.field(ResourceHeaderRef(resource).id());
                }
            }
        }
        fields << "],\"script_controls\":[";
        first = true;
        if (state) {
            for (auto object : list(ScriptStateRef(state).controls())) {
                const ScriptControlRef control(object, &profile);
                if (pointer(reader.field(ObjectFlagsRef(object).type())) !=
                    image + profile.script_control) {
                    continue;
                }
                const auto enabled = reader.field(control.input().enabled());
                const auto blocked = reader.field(control.input().disabled());
                const auto bounds = reader.field(control.rectangle().bounds());
                const auto id = reader.field(control.id());
                if (!enabled || blocked || !valid(bounds)) {
                    continue;
                }
                if (!first) {
                    fields << ',';
                }
                first = false;
                fields << "{\"id\":" << id << ",\"address\":" << object
                       << ",\"rect\":" << rectangle(bounds)
                       << ",\"action_count\":" << reader.field(control.action_count()) << '}';
            }
        }
        fields << "],\"world_targets\":[";
        if (state) {
            world(state, fields);
        }
        fields << "],\"inventory\":[";
        if (view) {
            inventory(view, fields);
        }
        fields << "],\"modal\":";
        modal(view, fields);
        reader.coherent();
        return finish(fields.str());
    }

private:
    void input(const void* state, const void* preferences, std::ostringstream& fields) {
        if (!state) {
            fields << ",\"input_status\":\"no_state\",\"input_address\":null"
                      ",\"input_vtable_rva\":null,\"skip_available\":null";
            return;
        }
        const auto source = SceneStateRef(state, &profile).inputs();
        const auto header = reader.list_header(source);
        const auto type = profile.native_input_list;
        if (!type || pointer(header.vtable) != image + type) {
            throw std::runtime_error("unexpected_native_input_list_type");
        }
        // The native accessor selects the first value and resets only its iterator.
        const auto inputs = list(source);
        const auto address = inputs.empty() ? 0 : inputs.front();
        const auto vtable = address ? pointer(reader.field(ObjectFlagsRef(address).type())) : 0;
        if (address && (vtable < image + database_image_limit::first_rva ||
                        vtable >= image + database_image_limit::vtable_end)) {
            throw std::runtime_error("invalid_native_input_type");
        }
        const bool movie =
            vtable == image + profile.movie || vtable == image + profile.action_movie;
        bool skip = false;
        if (movie && preferences && reader.field(PreferencesRef(preferences).skip_movies())) {
            skip = reader.field(MovieInputRef(address).skippable()) != 0;
        }
        fields << ",\"input_status\":\"verified_list_first\",\"input_address\":" << address
               << ",\"input_vtable_rva\":" << (vtable ? vtable - image : 0) << ",\"main_menu\":"
               << (address && vtable == image + profile.main_menu ? "true" : "false")
               << ",\"movie_active\":" << (movie ? "true" : "false")
               << ",\"skip_available\":" << (skip ? "true" : "false");
    }

    std::string finish(const std::string& fields) const {
        return "{\"status\":\"ok\",\"profile\":" + quote(build.label) +
               ",\"profile_id\":" + quote(build_id(build.id)) +
               ",\"edition\":" + quote(build.edition) + ",\"image_base\":" + std::to_string(image) +
               ",\"checked_reads\":" + std::to_string(reader.count()) + ',' + fields + '}';
    }

    std::string string(const StoryString& value) {
        if (!value.data || !value.capacity) {
            return {};
        }
        if (value.capacity > 1024) {
            throw std::runtime_error("variable_name_exceeds_bound");
        }
        std::string result(value.capacity, '\0');
        reader.copy(value.data, result.data(), result.size());
        const auto end = result.find('\0');
        if (end == std::string::npos) {
            throw std::runtime_error("unterminated_variable_name");
        }
        result.resize(end);
        return result;
    }

    template <class T> std::vector<std::uintptr_t> list(const MemoryRef<List<T>>& source) {
        const auto value = reader.list_header(source);
        if (!value.vtable || value.count > 256) {
            throw std::runtime_error("invalid_list_header");
        }
        auto node = value.first;
        typename List<T>::Node* previous = nullptr;
        bool current = !value.current;
        std::unordered_set<std::uintptr_t> nodes;
        std::vector<std::uintptr_t> result;
        for (unsigned index = 0; index < value.count; ++index) {
            if (!node || !nodes.insert(pointer(node)).second) {
                throw std::runtime_error("invalid_list_cycle_or_length");
            }
            const auto entry = reader.at<typename List<T>::Node>(pointer(node));
            if (!entry.value || entry.previous != previous) {
                throw std::runtime_error("invalid_list_links");
            }
            result.push_back(pointer(entry.value));
            current |= node == value.current;
            previous = node;
            node = entry.next;
        }
        if (node || previous != value.last || !current) {
            throw std::runtime_error("invalid_list_tail_or_current");
        }
        return result;
    }

    void collect_cache() {
        const auto manager = reader.field(runtime.database_manager());
        if (!manager) {
            throw std::runtime_error("database_manager_unavailable");
        }
        const DatabaseManagerRef database(manager, &profile);
        shape_class = cached_class(
            static_cast<std::uint32_t>(image + profile.world_navigation_shape_resource));
        visit(reader.field(database.hdb().root()), false, 0);
        visit(reader.field(database.state().root()), true, 0);
    }

    void visit(std::uint32_t address, bool state, unsigned depth) {
        if (!address || visited.contains(address)) {
            return;
        }
        if (depth >= 32 || visited.size() >= 8192 || objects.size() >= 16384) {
            throw std::runtime_error("database_traversal_bound");
        }
        visited.insert(address);
        const auto node = reader.at<DatabaseNode>(address);
        const DatabaseNodeRef source(address, &profile);
        if (node.object.vtable == image + profile.database_branch_node) {
            for (unsigned i = 0; i < node.count; ++i) {
                visit(reader.field(source.branch_entry(i)).cached_node, state, depth + 1);
            }
        } else if (node.object.vtable == image + profile.database_class_node) {
            if (node.count > 16) {
                throw std::runtime_error("database_class_count_bound");
            }
            for (unsigned i = 0; i < node.count; ++i) {
                const auto entry = reader.field(source.class_entry(i));
                if (entry.class_id != 0x53 && entry.class_id != shape_class) {
                    continue;
                }
                if (entry.index_count > std::size(entry.indexes)) {
                    throw std::runtime_error("database_index_count_bound");
                }
                for (unsigned j = 0; j < entry.index_count; ++j) {
                    visit(entry.indexes[j].cached_root, state, depth + 1);
                }
            }
        } else if (node.object.vtable == image + profile.database_object_node) {
            for (unsigned i = 0; i < node.count; ++i) {
                const auto entry = reader.field(source.object_entry(i));
                if (!entry.cached_object) {
                    continue;
                }
                const PersistentObjectRef object(entry.cached_object);
                const auto id = reader.field(object.id());
                const auto vtable = reader.field(object.vtable());
                if (id != entry.id || vtable < image + database_image_limit::first_rva ||
                    vtable >= image + database_image_limit::vtable_end) {
                    throw std::runtime_error("invalid_cached_object_identity");
                }
                const auto class_id = cached_class(vtable);
                const auto key = std::pair{class_id, id};
                const auto existing = objects.find(key);
                if (existing != objects.end() && existing->second != entry.cached_object) {
                    if (class_id != 0x53) {
                        throw std::runtime_error("ambiguous_cached_object");
                    }
                }
                objects[key] = entry.cached_object;
                if (class_id == 0x53) {
                    variables.push_back({id, entry.cached_object, state});
                }
            }
        } else {
            throw std::runtime_error("unsupported_database_node_type");
        }
    }

    std::uint32_t cached_class(std::uint32_t vtable) {
        if (const auto found = classes.find(vtable); found != classes.end()) {
            return found->second;
        }
        const auto getter = reader.field(PersistentVTableRef(vtable).class_getter());
        if (getter < image + database_image_limit::first_rva ||
            getter >= image + database_image_limit::class_getter_end) {
            throw std::runtime_error("invalid_class_getter_address");
        }
        const auto code = reader.at<std::array<std::uint8_t, 6>>(getter);
        if (code.front() != 0xb8 || code.back() != 0xc3) {
            throw std::runtime_error("unsupported_class_getter_code");
        }
        std::uint32_t class_id = 0;
        std::memcpy(&class_id, code.data() + 1, sizeof(class_id));
        if (class_id < 0x27 || class_id > 0x5c) {
            throw std::runtime_error("unsupported_cached_object_class");
        }
        classes[vtable] = class_id;
        return class_id;
    }

    void world(const void* state, std::ostringstream& fields) {
        bool first = true;
        const SceneStateRef scene(state, &profile);
        for (const auto& [group, type] :
             {std::pair{scene.hotspots(), profile.world_hotspot},
              std::pair{scene.navigation(), profile.world_navigation}}) {
            for (auto address : list(group)) {
                const ResourceObjectRef object(address, &profile);
                const auto flags = reader.field(object.object());
                if (pointer(flags.type) != image + type || !flags.enabled || flags.blocked) {
                    continue;
                }
                const auto resource = reader.field(object.resource());
                if (!resource) {
                    throw std::runtime_error("missing_world_resource");
                }
                const auto header = reader.field(ResourceHeaderRef(resource));
                const bool navigation = type == profile.world_navigation;
                const auto expected =
                    navigation ? profile.world_navigation_resource : profile.world_hotspot_resource;
                if (pointer(header.type) != image + expected) {
                    throw std::runtime_error("unexpected_world_resource_type");
                }
                auto shape = pointer(resource);
                if (navigation) {
                    const auto id = reader.field(NavigationResourceRef(resource).shape());
                    shape = 0;
                    for (const auto& [key, candidate] : objects) {
                        if (key.second == id &&
                            reader.field(PersistentObjectRef(candidate).vtable()) ==
                                image + profile.world_navigation_shape_resource) {
                            shape = candidate;
                            break;
                        }
                    }
                }
                if (!first) {
                    fields << ',';
                }
                first = false;
                fields << "{\"id\":" << header.id << ",\"address\":" << address
                       << ",\"navigation\":" << (navigation ? "true" : "false");
                if (shape) {
                    const auto bounds = reader.field(StoryHotspotRef(shape).rectangle().bounds());
                    if (!valid(bounds)) {
                        throw std::runtime_error("invalid_world_rectangle");
                    }
                    fields << ",\"rect\":" << rectangle(bounds);
                } else {
                    fields << ",\"rect\":null,\"rect_status\":\"shape_not_cached\"";
                }
                fields << '}';
            }
        }
    }

    void inventory(const void* view, std::ostringstream& fields) {
        const MainViewRef owner(view, &profile);
        bool visible = false;
        for (auto child : list(owner.children())) {
            visible |= pointer(reader.at<ChildView>(child).object) == owner.inventory().address();
        }
        bool first = true;
        for (auto address : list(owner.inventory().items())) {
            const auto item = reader.at<InventoryItem>(address);
            if (!item.icon) {
                throw std::runtime_error("missing_inventory_icon");
            }
            const InventoryIconRef icon(item.icon, &profile);
            const auto bounds = reader.field(icon.rectangle().bounds());
            const auto graphic = reader.field(icon.graphic());
            if (!graphic) {
                throw std::runtime_error("missing_inventory_graphic");
            }
            const auto resource = reader.field(GraphicRef(graphic).resource());
            if (!resource) {
                throw std::runtime_error("missing_inventory_resource");
            }
            const auto id = reader.field(ResourceHeaderRef(resource).id());
            if (!first) {
                fields << ',';
            }
            first = false;
            fields << "{\"id\":" << id << ",\"rect\":" << rectangle(bounds)
                   << ",\"visible\":" << (visible && valid(bounds) ? "true" : "false") << '}';
        }
    }

    void modal(const void* view, std::ostringstream& fields) {
        if (!view) {
            fields << "null";
            return;
        }
        const MainViewRef owner(view, &profile);
        const auto background = reader.field(owner.modal_background());
        const auto foreground = reader.list_header(owner.modal_foreground());
        if (!background.count || !foreground.count) {
            fields << "null";
            return;
        }
        if (background.count > 256 || foreground.count > 4) {
            throw std::runtime_error("native_modal_count_bound");
        }
        std::string text;
        std::ostringstream items;
        bool first = true;
        for (auto address : list(owner.modal_foreground())) {
            const auto item = reader.at<InventoryItem>(address);
            const auto type = pointer(reader.field(ObjectFlagsRef(item.icon).type()));
            if (type != image + profile.text && type != image + profile.picture) {
                throw std::runtime_error("unexpected_native_modal_item_type");
            }
            if (!first) {
                items << ',';
            }
            first = false;
            items << "{\"type_rva\":" << type - image << ",\"position\":[" << item.position.x << ','
                  << item.position.y << ']';
            if (type == image + profile.text) {
                const auto source = pointer(reader.field(TextRef(item.icon).string()));
                std::string value;
                for (unsigned i = 0; i < 1024; ++i) {
                    const auto byte = reader.at<char>(source + i);
                    if (!byte) {
                        break;
                    }
                    value += byte;
                }
                if (value.size() == 1024) {
                    throw std::runtime_error("native_modal_text_bound");
                }
                if (!text.empty()) {
                    text += '\n';
                }
                text += value;
                items << ",\"text\":" << quote(value);
            }
            items << '}';
        }
        fields << "{\"text\":" << quote(text) << ",\"items\":[" << items.str() << "]}";
    }

    Reader reader;
    std::uintptr_t image;
    const Build& build;
    const Profile& profile;
    NativeRuntime runtime;
    std::unordered_set<std::uint32_t> visited;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uintptr_t> objects;
    std::map<std::uint32_t, std::uint32_t> classes;
    std::uint32_t shape_class = 0;

    struct CachedVariable {
        std::uint32_t id;
        std::uintptr_t address;
        bool state;
    };

    std::vector<CachedVariable> variables;
};
}

std::string snapshot(HANDLE process, std::uintptr_t image, const native_game::Build& build) {
    try {
        return Snapshot(process, image, build).run();
    } catch (const std::exception& error) {
        return "{\"status\":\"unavailable\",\"reason\":" + quote(error.what()) + '}';
    }
}
}
