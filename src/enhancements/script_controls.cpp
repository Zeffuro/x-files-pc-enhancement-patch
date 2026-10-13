#include "game/functions/runtime.h"
#include "script_controls.h"
#include "game_resources.h"
#include "game/layouts/input/events.h"
#include "game/layouts/script.h"
#include "game/layouts/native_objects.h"
#include "game/access/script.h"
#include "game/access/world.h"

#include <algorithm>
#include <cstring>

namespace enhancements::game {
namespace {
template <class T> bool read_native(const void* address, T& value) {
    SIZE_T copied = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &copied) &&
           copied == sizeof(value);
}

std::optional<std::string> complete_text(const char* address) {
    if (!address) {
        return std::nullopt;
    }
    std::string result;
    while (result.size() < 65536) {
        MEMORY_BASIC_INFORMATION region{};
        const auto current = address + result.size();
        if (!VirtualQuery(current, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            return std::nullopt;
        }
        const auto remaining =
            static_cast<const char*>(region.BaseAddress) + region.RegionSize - current;
        const auto count = std::min<std::size_t>(
            {static_cast<std::size_t>(remaining), 512, 65536 - result.size()});
        std::string chunk(count, '\0');
        SIZE_T copied = 0;
        if (!count ||
            !ReadProcessMemory(GetCurrentProcess(), current, chunk.data(), count, &copied) ||
            copied != count) {
            return std::nullopt;
        }
        if (const auto end = chunk.find('\0'); end != std::string::npos) {
            result.append(chunk, 0, end);
            return result;
        }
        result += chunk;
    }
    return std::nullopt;
}

bool visible_text_owner(std::byte* view, const Edition& profile) {
    List<ChildView> children{};
    if (!read_native(native_game::MainViewRef(view, &profile).children().data(), children) ||
        !children.count || children.count > 256) {
        return false;
    }
    bool found = false;
    auto node = children.first;
    std::vector<List<ChildView>::Node*> visited;
    for (unsigned index = 0; index < children.count; ++index) {
        List<ChildView>::Node record{};
        ChildView child{};
        if (!node || std::find(visited.begin(), visited.end(), node) != visited.end() ||
            !read_native(node, record) || !read_native(record.value, child)) {
            return false;
        }
        found |= child.object == native_game::MainViewScriptRef(view).text_owner().data();
        visited.push_back(node);
        node = record.next;
    }
    return found && !node;
}

std::vector<ScriptControls::Text> main_text(void* native_view, std::byte* image,
                                            const Edition& profile) {
    if (!native_view || !visible_text_owner(static_cast<std::byte*>(native_view), profile)) {
        return {};
    }
    const auto owner = native_game::MainViewScriptRef(native_view).text_owner().data();
    unsigned count = 0, current = 0;
    std::byte** entries = nullptr;
    std::byte* selected = nullptr;
    const auto group = native_game::NativeRuntime(image, profile).main_text().data();
    if (!read_native(native_game::ScriptTextOwnerRef(owner).count().data(), count) || !count ||
        count > 256 ||
        !read_native(native_game::ScriptTextOwnerRef(owner).current().data(), current) ||
        current >= count ||
        !read_native(native_game::ScriptTextOwnerRef(owner).entries().data(), entries) ||
        !entries || !read_native(entries + current, selected) || selected != group) {
        return {};
    }
    List<std::byte> list{};
    RECT bounds{};
    if (!read_native(native_game::MainTextGroupRef(group).rows().data(), list) || !list.count ||
        list.count > 256 ||
        !read_native(native_game::MainTextGroupRef(group, &profile).viewport().bounds().data(),
                     bounds) ||
        bounds.left < 0 || bounds.top < 0 || bounds.right > 640 || bounds.bottom > 480 ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return {};
    }
    std::vector<ScriptControls::Text> result;
    std::vector<List<std::byte>::Node*> visited;
    auto node = list.first;
    std::size_t bytes = 0;
    for (unsigned index = 0; index < list.count; ++index) {
        List<std::byte>::Node record{};
        std::byte* text = nullptr;
        void* type = nullptr;
        char* string = nullptr;
        if (!node || std::find(visited.begin(), visited.end(), node) != visited.end() ||
            !read_native(node, record) || !read_native(record.value, text) ||
            !read_native(text, type) ||
            type != native_game::NativeRuntime(image, profile).text_type() ||
            !read_native(native_game::TextRef(text).string().data(), string)) {
            return {};
        }
        const auto value = complete_text(string);
        if (!value || bytes + value->size() >= 65536) {
            return {};
        }
        bytes += value->size();
        if (!value->empty()) {
            result.push_back({bounds, *value});
        }
        visited.push_back(node);
        node = record.next;
    }
    return node ? std::vector<ScriptControls::Text>{} : result;
}

}

ScriptControls read_script_controls(void* native_state, std::byte* image, const Edition& profile,
                                    void* native_view) {
    ScriptControls result;
    if (!native_state || !image) {
        return result;
    }
    const auto state = static_cast<std::byte*>(native_state);
    const auto collect = [&](const auto& source, auto callback) {
        const auto& list = *source.get();
        if (list.count > 256) {
            return;
        }
        auto node = list.first;
        for (unsigned index = 0; node && index < list.count; ++index, node = node->next) {
            if (node->value) {
                callback(static_cast<std::byte*>(node->value));
            }
        }
    };
    collect(native_game::ScriptStateRef(state).roots(), [&](std::byte* object) {
        if (*native_game::ObjectFlagsRef(object).type().get() ==
            native_game::NativeRuntime(image, profile).script_root_type()) {
            const auto resource =
                static_cast<std::byte*>(*native_game::ScriptInputRef(object).resource().get());
            if (resource) {
                result.resources.push_back(*native_game::ResourceHeaderRef(resource).id().get());
            }
        }
    });
    const bool options = std::find(result.resources.begin(), result.resources.end(),
                                   resource::options) != result.resources.end();
    const bool document =
        std::any_of(result.resources.begin(), result.resources.end(), [](unsigned id) {
            return id == resource::pda_message || id == resource::workstation_message;
        });
    const bool readonly_document =
        std::find(result.resources.begin(), result.resources.end(), resource::pda_message) !=
            result.resources.end() ||
        std::find(result.resources.begin(), result.resources.end(),
                  resource::workstation_message) != result.resources.end();
    if (document) {
        result.document_text = main_text(native_view, image, profile);
    }
    const bool native_document = !result.document_text.empty();
    bool dialog_text = false;
    collect(native_game::ScriptStateRef(state).controls(), [&](std::byte* object) {
        if (*native_game::ObjectFlagsRef(object).type().get() !=
            native_game::NativeRuntime(image, profile).script_control_type()) {
            return;
        }
        const auto id = *native_game::ScriptControlRef(object).id().get();
        if (id == script_control::dialog_text) {
            dialog_text = true;
        }
        if (id == script_control::text_cursor || id == script_control::dialog_text) {
            result.text_input = true;
        }
        const auto rectangle =
            native_game::ScriptControlRef(object, &profile).rectangle().get()->bounds;
        if (*native_game::ScriptInputRef(object).enabled().get() &&
            !*native_game::ScriptInputRef(object).disabled().get() && rectangle.left >= 0 &&
            rectangle.top >= 0 && rectangle.right <= 640 && rectangle.bottom <= 480 &&
            rectangle.right > rectangle.left && rectangle.bottom > rectangle.top &&
            (rectangle.right - rectangle.left < 640 || rectangle.bottom - rectangle.top < 480)) {
            unsigned events = 0;
            const auto& actions = *native_game::InputEventsRef(object).actions().get();
            for (unsigned event = 0; event < actions.size(); ++event) {
                if (actions[event].count && actions[event].count <= 256) {
                    events |= 1u << event;
                }
            }
            if (events) {
                result.event_targets.push_back({rectangle, id, events});
            }
        }
        // Options panels share the dialog artwork type, but keep their outer navigation.
        if (id == script_control::dialog_background && !options) {
            result.script_dialog = true;
        }
        if (id == script_control::auxiliary_first && rectangle.left >= 0 && rectangle.top >= 0 &&
            rectangle.right <= 640 && rectangle.bottom <= 480 && rectangle.right > rectangle.left &&
            rectangle.bottom > rectangle.top) {
            if (*native_game::ScriptControlRef(object).action_count().get()) {
                result.dialog_buttons.push_back(rectangle);
            } else if (!*native_game::ScriptControlRef(object).graphic().get() &&
                       rectangle.right - rectangle.left > 50) {
                result.dialog_fields.push_back(rectangle);
            }
        }
        if (id == script_control::acknowledgement &&
            *native_game::ScriptControlRef(object).action_count().get()) {
            const auto bounds =
                native_game::ScriptControlRef(object, &profile).rectangle().get()->bounds;
            if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 &&
                bounds.bottom <= 480 && bounds.right > bounds.left && bounds.bottom > bounds.top) {
                result.acknowledgement_buttons.push_back(bounds);
            }
        }
        if (document && !native_document && *native_game::ScriptInputRef(object).enabled().get() &&
            !*native_game::ScriptInputRef(object).disabled().get()) {
            auto field =
                static_cast<std::byte*>(*native_game::ScriptControlRef(object).graphic().get());
            if (field && *native_game::ObjectFlagsRef(field).type().get() ==
                             native_game::NativeRuntime(image, profile).input_graphic_type()) {
                field =
                    static_cast<std::byte*>(*native_game::InputGraphicTextRef(field).text().get());
            }
            if (field && *native_game::ObjectFlagsRef(field).type().get() ==
                             native_game::NativeRuntime(image, profile).text_type()) {
                const auto value = complete_text(*native_game::TextRef(field).string().get());
                if (value && !value->empty()) {
                    result.document_text.push_back({rectangle, *value});
                }
            }
        }
        if (!script_control::selectable(id) && id != script_control::dialog_text) {
            return;
        }
        const auto bounds =
            native_game::ScriptControlRef(object, &profile).rectangle().get()->bounds;
        if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top &&
            (bounds.right - bounds.left < 640 || bounds.bottom - bounds.top < 480)) {
            const auto graphic =
                static_cast<std::byte*>(*native_game::ScriptControlRef(object).graphic().get());
            // Native options retain an invisible input after removing its artwork.
            if (options && !graphic && id >= script_control::input_first &&
                id < script_control::hover) {
                return;
            }
            if (graphic && *native_game::ObjectFlagsRef(graphic).type().get() ==
                               native_game::NativeRuntime(image, profile).input_graphic_type()) {
                const auto field = static_cast<std::byte*>(
                    *native_game::InputGraphicTextRef(graphic).text().get());
                if (field && *native_game::ObjectFlagsRef(field).type().get() ==
                                 native_game::NativeRuntime(image, profile).text_type()) {
                    const auto text = *native_game::TextRef(field).string().get();
                    if (text) {
                        result.fields.push_back({bounds, std::string(text, strnlen(text, 128))});
                    }
                    if (text && *text) {
                        result.text.push_back(bounds);
                    }
                }
            }
            if (*native_game::ScriptControlRef(object).action_count().get()) {
                result.buttons.push_back(bounds);
            } else if (id == script_control::hover) {
                result.hover_buttons.push_back(bounds);
            }
        }
    });
    if (readonly_document && !dialog_text && native_document && !result.script_dialog &&
        result.acknowledgement_buttons.empty()) {
        result.text_input = false;
    }
    return result;
}

}
