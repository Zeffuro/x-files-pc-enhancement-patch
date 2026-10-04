#include "script_controls.h"
#include "game_resources.h"
#include "game/layouts/input/events.h"

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
    if (!read_native(view + profile.children, children) || !children.count ||
        children.count > 256) {
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
        found |= child.object == view + 0x4c8;
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
    const auto owner = static_cast<std::byte*>(native_view) + 0x4c8;
    unsigned count = 0, current = 0;
    std::byte** entries = nullptr;
    std::byte* selected = nullptr;
    const auto group = image + profile.main_text;
    if (!read_native(owner + 8, count) || !count || count > 256 ||
        !read_native(owner + 12, current) || current >= count ||
        !read_native(owner + 16, entries) || !entries ||
        !read_native(entries + current, selected) || selected != group) {
        return {};
    }
    List<std::byte> list{};
    RECT bounds{};
    if (!read_native(group + 4, list) || !list.count || list.count > 256 ||
        !read_native(group + profile.main_text_rectangle + 4, bounds) || bounds.left < 0 ||
        bounds.top < 0 || bounds.right > 640 || bounds.bottom > 480 ||
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
            !read_native(text, type) || type != image + profile.text ||
            !read_native(text + 0x28, string)) {
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
    const auto collect = [&](std::size_t offset, auto callback) {
        const auto& list = *reinterpret_cast<List<std::byte>*>(state + offset);
        if (list.count > 256) {
            return;
        }
        auto node = list.first;
        for (unsigned index = 0; node && index < list.count; ++index, node = node->next) {
            if (node->value) {
                callback(node->value);
            }
        }
    };
    collect(0x25c, [&](std::byte* object) {
        if (*reinterpret_cast<void**>(object) == image + profile.script_root) {
            const auto resource = *reinterpret_cast<std::byte**>(object + 0x18);
            if (resource) {
                result.resources.push_back(*reinterpret_cast<unsigned*>(resource + 4));
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
    collect(0x270, [&](std::byte* object) {
        if (*reinterpret_cast<void**>(object) != image + profile.script_control) {
            return;
        }
        const auto id = *reinterpret_cast<unsigned*>(object + 0x144);
        if (id == script_control::dialog_text) {
            dialog_text = true;
        }
        if (id == script_control::text_cursor || id == script_control::dialog_text) {
            result.text_input = true;
        }
        const auto rectangle =
            reinterpret_cast<Rectangle*>(object + profile.control_rectangle)->bounds;
        if (*reinterpret_cast<unsigned*>(object + 8) &&
            !*reinterpret_cast<unsigned*>(object + 12) && rectangle.left >= 0 &&
            rectangle.top >= 0 && rectangle.right <= 640 && rectangle.bottom <= 480 &&
            rectangle.right > rectangle.left && rectangle.bottom > rectangle.top &&
            (rectangle.right - rectangle.left < 640 || rectangle.bottom - rectangle.top < 480)) {
            unsigned events = 0;
            const auto& actions =
                reinterpret_cast<const native_game::InputEvents*>(object)->actions;
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
            if (*reinterpret_cast<unsigned*>(object + 0x20)) {
                result.dialog_buttons.push_back(rectangle);
            } else if (!*reinterpret_cast<void**>(object + 0x140) &&
                       rectangle.right - rectangle.left > 50) {
                result.dialog_fields.push_back(rectangle);
            }
        }
        if (id == script_control::acknowledgement && *reinterpret_cast<unsigned*>(object + 0x20)) {
            const auto bounds =
                reinterpret_cast<Rectangle*>(object + profile.control_rectangle)->bounds;
            if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 &&
                bounds.bottom <= 480 && bounds.right > bounds.left && bounds.bottom > bounds.top) {
                result.acknowledgement_buttons.push_back(bounds);
            }
        }
        if (document && !native_document && *reinterpret_cast<unsigned*>(object + 8) &&
            !*reinterpret_cast<unsigned*>(object + 12)) {
            auto field = *reinterpret_cast<std::byte**>(object + 0x140);
            if (field && *reinterpret_cast<void**>(field) == image + profile.input_graphic) {
                field = *reinterpret_cast<std::byte**>(field + 0x154);
            }
            if (field && *reinterpret_cast<void**>(field) == image + profile.text) {
                const auto value = complete_text(*reinterpret_cast<char**>(field + 0x28));
                if (value && !value->empty()) {
                    result.document_text.push_back({rectangle, *value});
                }
            }
        }
        if (!script_control::selectable(id) && id != script_control::dialog_text) {
            return;
        }
        const auto bounds =
            reinterpret_cast<Rectangle*>(object + profile.control_rectangle)->bounds;
        if (bounds.left >= 0 && bounds.top >= 0 && bounds.right <= 640 && bounds.bottom <= 480 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top &&
            (bounds.right - bounds.left < 640 || bounds.bottom - bounds.top < 480)) {
            const auto graphic = *reinterpret_cast<std::byte**>(object + 0x140);
            // Native options retain an invisible input after removing its artwork.
            if (options && !graphic && id >= script_control::input_first &&
                id < script_control::hover) {
                return;
            }
            if (graphic && *reinterpret_cast<void**>(graphic) == image + profile.input_graphic) {
                const auto field = *reinterpret_cast<std::byte**>(graphic + 0x154);
                if (field && *reinterpret_cast<void**>(field) == image + profile.text) {
                    const auto text = *reinterpret_cast<char**>(field + 0x28);
                    if (text) {
                        result.fields.push_back({bounds, std::string(text, strnlen(text, 128))});
                    }
                    if (text && *text) {
                        result.text.push_back(bounds);
                    }
                }
            }
            if (reinterpret_cast<List<void>*>(object + 0x1c)->count) {
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
