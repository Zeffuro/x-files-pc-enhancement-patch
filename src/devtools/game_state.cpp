#include "game_state.h"
#include "variables.h"
#include "story_state.h"
#include "enhancements/game_ui.h"
#include "enhancements/game_resources.h"
#include "enhancements/inventory.h"
#include "game/layouts/input/events.h"
#include <filesystem>
#include <sstream>

namespace devtools {
GameSnapshot inspect_game(bool include_story) {
    namespace game = enhancements::game;
    GameSnapshot result;
    auto* image = game::executable_image();
    if (!image) {
        result.groups.push_back({L"Game", {L"No supported game state is available."}});
        return result;
    }
    const auto& profile = game::edition();
    const auto flag = [&](std::uint32_t offset) {
        return std::to_wstring(*reinterpret_cast<const int*>(image + offset));
    };
    const auto script = game::script_controls();
    std::wostringstream input;
    input << L"Input class RVA: 0x" << std::hex << game::input_vtable();
    result.groups.push_back(
        {L"Session",
         {L"Session active: " + flag(profile.session_active),
          L"Scene active: " + flag(profile.scene_active),
          L"Save available: " + std::to_wstring(game::saving_available()),
          L"Navigation available: " + std::to_wstring(game::world_navigation_available()),
          input.str(), L"Script dialog: " + std::to_wstring(script.script_dialog),
          L"Text input: " + std::to_wstring(script.text_input)}});
    if (include_story) {
        for (auto& group : story_state_groups(inspect_database(image, profile))) {
            result.groups.push_back(std::move(group));
        }
    }
    StateGroup resources{L"Resources"};
    for (const auto id : script.resources) {
        resources.values.push_back(std::to_wstring(id));
    }
    result.groups.push_back(std::move(resources));
    StateGroup events{L"Script event targets"};
    for (const auto& target : script.event_targets) {
        const auto& r = target.bounds;
        std::wstring label = L"Control " + std::to_wstring(target.id) + L" at " +
                             std::to_wstring(r.left) + L", " + std::to_wstring(r.top) + L" - " +
                             std::to_wstring(r.right) + L", " + std::to_wstring(r.bottom) + L": ";
        bool first = true;
        for (unsigned event = 0; event < native_game::input_event_names.size(); ++event) {
            if (target.events & (1u << event)) {
                if (!first) {
                    label += L", ";
                }
                label += native_game::input_event_names[event];
                first = false;
            }
        }
        events.values.push_back(std::move(label));
        if (target.events & native_game::pointer_event_mask) {
            result.targets.push_back(r);
        }
    }
    result.groups.push_back(std::move(events));
    const auto add = [&](const wchar_t* name, const std::vector<RECT>& rectangles) {
        StateGroup group{name};
        for (const auto& rect : rectangles) {
            group.values.push_back(std::to_wstring(rect.left) + L", " + std::to_wstring(rect.top) +
                                   L" - " + std::to_wstring(rect.right) + L", " +
                                   std::to_wstring(rect.bottom));
            result.targets.push_back(rect);
        }
        result.groups.push_back(std::move(group));
    };
    add(L"World interaction targets (including overlaps)", game::world_hotspots(false, true));
    add(L"Inventory targets", enhancements::inventory_bounds(game::current_view()));
    add(L"Script buttons", script.buttons);
    add(L"Hover targets", script.hover_buttons);
    add(L"Acknowledgement buttons", script.acknowledgement_buttons);
    add(L"Dialog buttons", script.dialog_buttons);
    add(L"Dialog fields", script.dialog_fields);
    add(L"Modal buttons", game::modal_buttons());
    add(L"Emotion targets", game::emotion_targets());
    add(L"Aiming targets", game::aiming_targets());
    StateGroup inventory{L"Inventory"};
    for (const auto& item : enhancements::inventory_items(game::current_view())) {
        const auto name = item.resource == enhancements::resource::inventory_gun ? L"Willmore's gun"
                          : item.asset_path.empty()
                              ? L"Unnamed item"
                              : std::filesystem::path(item.asset_path).stem().wstring();
        inventory.values.push_back(
            std::wstring(name) + L" (" +
            (item.resource ? std::to_wstring(*item.resource) : L"resource unavailable") + L")");
        if (!item.asset_path.empty()) {
            inventory.values.push_back(L"  Asset: " + item.asset_path);
        }
    }
    result.groups.push_back(std::move(inventory));
    StateGroup variables{L"Registered story variables"};
    std::wistringstream lines(inspect_variables(image, profile));
    for (std::wstring line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (line.starts_with(L"Reg")) {
            if (line.find(L": ") != std::wstring::npos) {
                variables.values.push_back(std::move(line));
            }
        }
    }
    result.groups.push_back(std::move(variables));
    for (const auto& group : result.groups) {
        result.text += group.name + L"\r\n";
        for (const auto& value : group.values) {
            result.text += L"  " + value + L"\r\n";
        }
    }
    return result;
}
}
