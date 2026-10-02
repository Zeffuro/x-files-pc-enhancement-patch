#include "game_state.h"
#include "variables.h"
#include "story_state.h"
#include "story_edit.h"
#include "state_capture.h"
#include "database/memory.h"
#include "enhancements/game_ui.h"
#include "enhancements/game_resources.h"
#include "enhancements/inventory.h"
#include "enhancements/quick_save.h"
#include "enhancements/scene_overlay.h"
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
    std::uint32_t pending_load = 0;
    database_copy(reinterpret_cast<std::uintptr_t>(image + profile.pending_load), &pending_load,
                  sizeof(pending_load));
    const auto script = game::script_controls();
    result.view = reinterpret_cast<std::uintptr_t>(game::current_view());
    result.input = reinterpret_cast<std::uintptr_t>(game::current_input());
    const auto* application =
        *reinterpret_cast<const game::Application* const*>(image + profile.application);
    result.application = reinterpret_cast<std::uintptr_t>(application);
    result.session = application ? reinterpret_cast<std::uintptr_t>(application->state) : 0;
    std::wostringstream input;
    input << L"Input class RVA: 0x" << std::hex << game::input_vtable();
    result.groups.push_back(
        {L"Session",
         {L"Session active: " + flag(profile.session_active),
          L"Scene active: " + flag(profile.scene_active),
          L"Save available: " + std::to_wstring(game::saving_available()),
          L"Navigation available: " + std::to_wstring(game::world_navigation_available()),
          L"Safe save available: " + std::to_wstring(enhancements::safe_save_available()),
          L"Save export available: " + std::to_wstring(enhancements::export_save_available()),
          L"Native pending load: " + std::to_wstring(pending_load != 0),
          L"Checkpoint load pending: " + std::to_wstring(enhancements::checkpoint_load_pending()),
          L"Movie skip available: " + std::to_wstring(game::movie_skippable()),
          L"Scene overlay active: " + std::to_wstring(enhancements::scene_overlay_active()),
          L"Input mode: " + std::wstring(game::input_vtable() == profile.main_menu ? L"Main menu"
                                         : game::input_vtable() == profile.movie   ? L"Movie"
                                         : game::input_vtable() == profile.action_movie
                                             ? L"Action movie"
                                             : L"Other native input"),
          input.str(), L"Script dialog: " + std::to_wstring(script.script_dialog),
          L"Text input: " + std::to_wstring(script.text_input)}});
    if (include_story) {
        const auto database = inspect_database(image, profile);
        result.manager = database.manager_address;
        result.state = database.state_address;
        result.hdb = database.hdb_address;
        result.variables = story_state_variables(database, image, &profile);
        const auto groups = story_state_groups(database, false);
        if (!groups.empty()) {
            result.groups.push_back(groups.front());
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
    const auto viewport = game::scene_bounds();
    result.groups.push_back(
        {L"Scene viewport",
         {std::to_wstring(viewport.left) + L", " + std::to_wstring(viewport.top) + L" - " +
          std::to_wstring(viewport.right) + L", " + std::to_wstring(viewport.bottom)}});
    StateGroup variables{L"Registered story variables"};
    std::wistringstream lines(inspect_variables(image, profile));
    for (std::wstring line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (line.starts_with(L"Reg")) {
            if (line.find(L": ") != std::wstring::npos) {
                const auto registration = line.substr(0, line.find(L": "));
                const auto short_name = registration.substr(0, registration.find(L" - "));
                const auto found = std::find_if(
                    result.variables.begin(), result.variables.end(), [&](const auto& variable) {
                        return variable.registration.find(short_name) != std::wstring::npos;
                    });
                if (found != result.variables.end()) {
                    line += L" / Literal name: " + found->name;
                }
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

std::vector<RECT> collect_interaction_targets() {
    namespace game = enhancements::game;
    std::vector<RECT> result;
    if (!game::executable_image()) {
        return result;
    }
    const auto script = game::script_controls();
    for (const auto& target : script.event_targets) {
        if (target.events & native_game::pointer_event_mask) {
            result.push_back(target.bounds);
        }
    }
    const auto add = [&](const auto& bounds) {
        result.insert(result.end(), bounds.begin(), bounds.end());
    };
    add(game::world_hotspots(false, true));
    add(enhancements::inventory_bounds(game::current_view()));
    add(script.buttons);
    add(script.hover_buttons);
    add(script.acknowledgement_buttons);
    add(script.dialog_buttons);
    add(script.dialog_fields);
    add(game::modal_buttons());
    add(game::emotion_targets());
    add(game::aiming_targets());
    return result;
}

std::wstring edit_game_variable(const GameSnapshot& snapshot, const StateVariable& variable,
                                std::wstring_view value, bool enabled) {
    namespace game = enhancements::game;
    const auto parsed = parse_state_value(value);
    if (!parsed) {
        return L"Enter a decimal integer or an ASCII character, such as A or '1'.";
    }
    if (!game::executable_image() || !enhancements::export_save_available() ||
        enhancements::checkpoint_load_pending()) {
        return L"Editing requires an active stable game session.";
    }
    const auto failure = write_state_variable(game::executable_image(), game::edition(), snapshot,
                                              inspect_game(), variable, *parsed, enabled);
    if (failure.empty()) {
        record_inspector_write(snapshot, variable, *parsed);
    }
    return failure;
}
}
