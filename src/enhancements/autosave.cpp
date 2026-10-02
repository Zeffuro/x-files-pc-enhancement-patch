#include "autosave.h"
#include "autosave_state.h"
#include "quick_save.h"
#include "game_ui.h"
#include "scene_overlay.h"
#include "saves/browser.h"
#include "saves/recent.h"
#include "saves/preview.h"
#include "playback/inspection.h"
#include "settings.h"
#include "runtime.h"
#include "ui/notification.h"
#include <algorithm>

namespace enhancements {
namespace {
AutosaveState state;
std::uint64_t last_poll = 0;

bool navigation_loop(const std::filesystem::path& path) {
    const auto name = path.stem().wstring();
    return !_wcsicmp(path.parent_path().filename().c_str(), L"XN") &&
           !_wcsicmp(path.extension().c_str(), L".xmv") && !name.empty() &&
           std::all_of(name.begin(), name.end(),
                       [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
}

std::optional<SaveIdentity> identity() {
    if (!safe_save_available()) {
        return std::nullopt;
    }
    const auto app = *reinterpret_cast<game::Application**>(game::executable_image() +
                                                            game::edition().application);
    if (!app || !app->state || !app->view) {
        return std::nullopt;
    }
    SaveIdentity result{reinterpret_cast<std::uintptr_t>(app->state),
                        reinterpret_cast<std::uintptr_t>(app->view),
                        {}};
    std::uint64_t newest = 0;
    const auto bounds = game::scene_bounds();
    const auto area = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
    for (const auto& movie : playback::inspect_movies()) {
        if (!movie.active || !movie.video || movie.width * movie.height < area * 3 / 4) {
            continue;
        }
        const bool loop = navigation_loop(movie.path);
        if (movie.playing && !media::navigation_archive(movie.path) && !loop) {
            return std::nullopt;
        }
        if (movie.image && movie.last_draw >= newest) {
            newest = movie.last_draw;
            // Ambient navigation loops stay in one scene while their frames advance.
            auto frame = *movie.image;
            if (loop) {
                frame.sample = 0;
            }
            result.frame = media::frame_key(movie.path, frame);
        }
    }
    return result;
}
}

void update_autosave(HWND window, bool blocked) {
    try {
        const auto now = GetTickCount64();
        const bool suspended =
            blocked || scene_overlay_active() || saves::browser_active() ||
            checkpoint_load_pending() ||
            (game::executable_image() &&
             (game::input_vtable() == game::edition().main_menu ||
              *reinterpret_cast<void**>(game::executable_image() + game::edition().pending_load)));
        if (suspended || !settings().autosaves || !safe_save_available()) {
            state.update(now, settings().autosaves, suspended, std::nullopt);
            last_poll = 0;
            return;
        }
        if (last_poll && now >= last_poll && now - last_poll < 100) {
            return;
        }
        last_poll = now;
        if (!state.update(now, true, false, identity())) {
            return;
        }
        const auto root = save_game_root();
        const auto temporary = root / L"saves" /
                               (L"AUTOSAVE.pending." + std::to_wstring(GetCurrentProcessId()) +
                                L"." + std::to_wstring(GetTickCount64()) + L".x");
        if (std::filesystem::exists(temporary)) {
            throw std::runtime_error("Cannot create a temporary autosave");
        }

        struct Cleanup {
            std::filesystem::path file;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove(file, error);
            }
        } cleanup{temporary};

        export_safe_save(temporary);
        const auto saved = saves::write_autosave(root, temporary, saves::browser_scene_thumbnail());
        try {
            saves::write_scene_reference(saved.file, saves::browser_scene_reference());
        } catch (const std::exception&) {
            trace_value("autosave_preview_failed", saved.number);
        }
        trace_value("autosave_slot", saved.number);
        notify_status(window, L"Autosave complete");
    } catch (const std::exception& error) {
        trace_value(error.what(), 0);
        notify_status(window, L"Autosave failed. Existing saves kept");
    }
}

void autosave_loaded() {
    state.loaded(GetTickCount64());
    last_poll = 0;
}

void release_autosave() {
    state = {};
    last_poll = 0;
}
}
