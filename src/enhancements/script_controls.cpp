#include "script_controls.h"
#include "game_resources.h"
#include "game/layouts/input/events.h"

#include <algorithm>
#include <cstring>

namespace enhancements::game {

ScriptControls read_script_controls(void* native_state, std::byte* image, const Edition& profile) {
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
    collect(0x270, [&](std::byte* object) {
        if (*reinterpret_cast<void**>(object) != image + profile.script_control) {
            return;
        }
        const auto id = *reinterpret_cast<unsigned*>(object + 0x144);
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
    return result;
}

}
