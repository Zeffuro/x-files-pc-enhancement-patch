#include "inspector_internal.h"
#include "easter_eggs.h"
#include "easter_egg_scores.h"
#include "easter_egg_request.h"
#include "database/browser.h"
#include "enhancements/game_ui.h"
#include "enhancements/quick_save.h"
#include "game/cheats.h"
#include "recipes/recipe.h"
#include "saves/database.h"
#include "runtime.h"
#include <sstream>
#include <stdexcept>
#include <utility>
#include <memory>

namespace devtools::inspector {
namespace {
thread_local std::optional<EasterEggRequest> pending;

struct StudioSnapshot {
    const std::filesystem::path source, target;

    StudioSnapshot(const std::filesystem::path& source_file,
                   const std::filesystem::path& target_file)
        : source(source_file), target(target_file) {}

    StudioSnapshot(const StudioSnapshot&) = delete;
    StudioSnapshot& operator=(const StudioSnapshot&) = delete;

    ~StudioSnapshot() {
        std::error_code error;
        std::filesystem::remove(source, error);
        std::filesystem::remove(target, error);
    }
};

thread_local std::unique_ptr<StudioSnapshot> studio;

std::optional<native_game::CheatContext> context(HWND game) {
    return enhancements::safe_save_available()
               ? native_game::cheat_context(enhancements::game::executable_image(),
                                            enhancements::game::edition(), game)
               : std::nullopt;
}

void request(std::optional<std::size_t> index, EasterEggRequestKind kind) {
    if (!leave_notes()) {
        return;
    }
    const auto current = context(state.game);
    if (!index || !current || pending) {
        throw std::runtime_error("Return to ordinary exploration before using a shortcut.");
    }
    const auto game = state.game;
    pending = EasterEggRequest{*current, *index, {}, kind};
    SendMessageW(state.window, WM_CLOSE, 0, 0);
    SetForegroundWindow(game);
    SetFocus(game);
}

std::unique_ptr<StudioSnapshot> capture_studio(const native_game::CheatContext& expected) {
    const auto root = enhancements::save_game_root() / L"saves";
    const auto tag =
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    const auto source = root / (L"STUDIO.SOURCE." + tag + L".x");
    const auto target = root / (L"STUDIO.VISIT." + tag + L".x");
    if (std::filesystem::exists(source) || std::filesystem::exists(target)) {
        throw std::runtime_error("Cannot create a temporary studio visit.");
    }

    auto snapshot = std::make_unique<StudioSnapshot>(source, target);
    enhancements::export_safe_save(source);
    const auto bytes = recipes::studio_visit_data(saves::database::read(source));
    recipes::write_new(target, bytes, {source});
    const auto image = enhancements::game::executable_image();
    const auto& profile = enhancements::game::edition();
    const auto current = native_game::cheat_identity(image, profile, expected.window);
    if (current != expected) {
        trace_value("studio_capture_application",
                    current && current->application == expected.application);
        trace_value("studio_capture_state", current && current->state == expected.state);
        trace_value("studio_capture_view", current && current->view == expected.view);
        throw std::runtime_error("The game scene changed before the studio visit.");
    }
    trace_value("studio_snapshot_ready",
                native_game::cheat_context(image, profile, expected.window).has_value());
    return snapshot;
}
}

void create_inspector_eggs() {
    state.easter_eggs = create_easter_eggs(
        state.window, state.module, state.font,
        {[](std::wstring_view code) {
             request(native_game::cheat_index(code), EasterEggRequestKind::cheat);
         },
         [](std::uint32_t id) {
             SendMessageW(state.window, WM_COMMAND, database_id, 0);
             if (!state.showing_database || !show_database_variable_paths(state.database, id)) {
                 throw std::runtime_error(
                     "This variable is not available in the current game snapshot.");
             }
         },
         [](EasterEggShortcut shortcut) {
             if (shortcut == EasterEggShortcut::studio) {
                 request(native_game::cheats().size(), EasterEggRequestKind::studio);
             }
         }});
    update_inspector_egg_scores();
}

bool process_inspector_egg(HWND game) {
    if (!pending) {
        return false;
    }
    const auto image = enhancements::game::executable_image();
    const auto& profile = enhancements::game::edition();
    const auto current = native_game::cheat_identity(image, profile, game);
    const auto ready = native_game::cheat_context(image, profile, game);
    const bool was_waiting = pending->waiting_since.has_value();
    const auto status =
        poll_easter_egg_request(*pending, current, enhancements::safe_save_available(),
                                ready.has_value(), GetTickCount64());
    if (status == EasterEggRequestStatus::waiting) {
        if (!was_waiting) {
            trace_value("easter_egg_deferred", static_cast<unsigned>(pending->index));
        }
        return true;
    }
    const auto request = std::exchange(pending, std::nullopt);
    const wchar_t* error = nullptr;
    if (status == EasterEggRequestStatus::scene_changed) {
        error = L"The game scene changed. Reopen Easter eggs in the current scene.";
    } else if (status == EasterEggRequestStatus::timed_out) {
        error = L"Game commands did not finish. Wait for movement or dialogue to finish, then try "
                L"again.";
    } else if (request->kind != EasterEggRequestKind::cheat) {
        try {
            if (request->kind == EasterEggRequestKind::studio) {
                studio = capture_studio(request->context);
                // Native saving queues its own final command before returning.
                pending = *request;
                pending->kind = EasterEggRequestKind::studio_load;
                pending->waiting_since.reset();
                return true;
            }
            if (!studio) {
                throw std::runtime_error("The studio snapshot is no longer available.");
            }
            enhancements::load_checkpoint(game, studio->target);
        } catch (const std::exception& failure) {
            studio.reset();
            trace_value("easter_egg_rejected", static_cast<unsigned>(request->index));
            MessageBoxA(game, failure.what(), "Easter eggs", MB_OK | MB_ICONERROR);
            return true;
        }
    } else if (!native_game::queue_cheat(image, profile, request->context, request->index)) {
        error = L"The original cheat could not be queued in this game state.";
    }
    studio.reset();
    trace_value(error ? "easter_egg_rejected" : "easter_egg_queued",
                static_cast<unsigned>(request->index));
    if (error) {
        MessageBoxW(game, error, L"Easter eggs", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

void clear_inspector_egg() {
    pending.reset();
    studio.reset();
}

void update_inspector_egg_scores() {
    const auto snapshot = capture_easter_egg_scores(enhancements::game::executable_image(),
                                                    enhancements::game::edition());
    std::wostringstream text;
    text << L"Live hidden scores (raw game values)\r\n";
    for (const auto& score : snapshot.scores) {
        text << score.label << L" | ID " << score.id << L" (0x" << std::hex << score.id << std::dec
             << L") | ";
        if (snapshot.available && score.raw_value) {
            text << score.name << L" = " << *score.raw_value;
        } else {
            text << L"unavailable";
        }
        text << L"\r\n";
    }
    text << L"Initial scores: 100. Score cheats set high to 150 or low to 50.";
    update_easter_egg_scores(state.easter_eggs, text.str());
    const bool exploring = enhancements::safe_save_available();
    const bool ready = context(state.game).has_value();
    enable_easter_egg_use(state.easter_eggs, ready);
    set_text(state.status,
             ready       ? L"Use in game runs the selected cheat or shortcut during exploration."
             : exploring ? L"Waiting for game commands to finish."
                         : L"Use in game requires ordinary exploration. Close the game's menus, "
                           L"dialogue or movie first.");
    state.state_updated = GetTickCount64();
}
}
