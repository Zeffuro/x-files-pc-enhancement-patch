#include "quick_save.h"
#include "dialogue.h"
#include "game_ui.h"
#include "runtime.h"
#include "saves/header.h"
#include "ui/notification.h"

#include <array>
#include <filesystem>
#include <fstream>

namespace enhancements {
namespace {
bool busy = false;
bool resume_pending = false;
std::filesystem::path checkpoint_file;
constexpr char filename[] = "QUICKSAVE.x";

bool exploration_available() {
    return game::world_navigation_available() && !current_dialogue() &&
           game::emotion_targets().empty() && game::script_controls().buttons.empty();
}

template <typename T> T function(std::uint32_t rva) {
    return reinterpret_cast<T>(game::executable_image() + rva);
}

struct NativeString {
    std::array<std::uint32_t, 4> storage{};

    explicit NativeString(const char* text) {
        function<void(__thiscall*)(void*, const char*)>(game::edition().string_create)(this, text);
    }

    NativeString(const NativeString&) = delete;
    NativeString& operator=(const NativeString&) = delete;

    ~NativeString() {
        function<void(__thiscall*)(void*)>(game::edition().string_destroy)(this);
    }
};

}

void quick_save(HWND window, bool load) {
    if (busy || resume_pending || !game::executable_image() || game::menu_confirmation_active() ||
        *reinterpret_cast<void**>(game::executable_image() + game::edition().pending_load)) {
        return;
    }
    const bool menu = game::input_vtable() == game::edition().main_menu;
    const bool scene = exploration_available();
    if ((!load && (!scene || !game::saving_available())) || (load && !scene && !menu)) {
        notify_status(window, load ? L"Cannot quick-load here" : L"Cannot quick-save here");
        return;
    }
    const auto app = *reinterpret_cast<game::Application**>(game::executable_image() +
                                                            game::edition().application);
    if (!app || !app->state) {
        return;
    }
    busy = true;

    struct Reset {
        ~Reset() {
            busy = false;
        }
    } reset;

    try {
        if (load && !saves::supported_header(filename)) {
            MessageBoxW(window,
                        L"No quick-save is available yet. Press F5 during gameplay to create one.",
                        L"The X-Files", MB_OK | MB_ICONINFORMATION);
            return;
        }
        constexpr char temporary[] = "QUICKSAVE.pending.x";
        NativeString name(load ? filename : temporary);
        const auto queue = reinterpret_cast<std::byte*>(app) + 0x268;
        if (load) {
            const auto result =
                function<int(__stdcall*)(void*, void*)>(game::edition().load_file)(&name, queue);
            trace_value("quick_load", result);
            if (result) {
                notify_status(window, L"Quick-save loaded");
            }
            if (result && menu) {
                resume_pending = true;
            }
        } else {
            std::filesystem::remove(temporary);
            function<void(__stdcall*)(void*, void*)>(game::edition().save_state)(queue, nullptr);
            function<void(__stdcall*)(void*, void*)>(game::edition().save_file)(&name, queue);
            bool saved = saves::supported_header(temporary);
            if (saved) {
                if (saves::supported_header(filename)) {
                    std::filesystem::copy_file(filename, "QUICKSAVE.previous.x",
                                               std::filesystem::copy_options::overwrite_existing);
                }
                saved = MoveFileExA(temporary, filename,
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            }
            trace_value("quick_save", saved);
            if (saved) {
                notify_status(window, L"Quick-save complete");
            }
            if (!saved) {
                MessageBoxW(window,
                            L"The quick-save could not be written. Your existing quick-save "
                            L"has been kept.",
                            L"The X-Files", MB_OK | MB_ICONERROR);
            }
        }
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "The X-Files quick-save", MB_OK | MB_ICONERROR);
    }
}

bool checkpoint_available() {
    return !busy && !resume_pending && game::executable_image() &&
           !game::menu_confirmation_active() &&
           (game::input_vtable() == game::edition().main_menu || exploration_available()) &&
           !*reinterpret_cast<void**>(game::executable_image() + game::edition().pending_load);
}

bool export_save_available() {
    return !busy && !resume_pending && game::executable_image() &&
           !game::menu_confirmation_active() && exploration_available() &&
           game::saving_available() &&
           !*reinterpret_cast<void**>(game::executable_image() + game::edition().pending_load);
}

void export_save(const std::filesystem::path& path) {
    if (!export_save_available()) {
        throw std::runtime_error("Return to exploration before saving to a file");
    }
    const auto app = *reinterpret_cast<game::Application**>(game::executable_image() +
                                                            game::edition().application);
    if (!app || !app->state) {
        throw std::runtime_error("The game is not ready to save");
    }
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("Choose a new filename; existing saves are kept");
    }
    const auto temporary = "EXPORT." + std::to_string(GetCurrentProcessId()) + "." +
                           std::to_string(GetTickCount64()) + ".x";
    if (std::filesystem::exists(temporary)) {
        throw std::runtime_error("Cannot create a temporary save file");
    }
    busy = true;

    struct Cleanup {
        const std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
            busy = false;
        }
    } cleanup{temporary};

    NativeString name(temporary.c_str());
    const auto queue = reinterpret_cast<std::byte*>(app) + 0x268;
    function<void(__stdcall*)(void*, void*)>(game::edition().save_state)(queue, nullptr);
    function<void(__stdcall*)(void*, void*)>(game::edition().save_file)(&name, queue);
    if (!saves::supported_header(temporary)) {
        throw std::runtime_error("The game could not write the save file");
    }
    std::filesystem::copy_file(temporary, path);
}

void load_checkpoint(HWND window, const std::filesystem::path& path) {
    if (!checkpoint_available()) {
        throw std::runtime_error(
            "Return to exploration or the main menu before loading a saved game");
    }
    if (!saves::supported_header(path)) {
        throw std::runtime_error("This file is not a supported X-Files PC saved game");
    }
    const auto app = *reinterpret_cast<game::Application**>(game::executable_image() +
                                                            game::edition().application);
    if (!app || !app->state) {
        throw std::runtime_error("The game is not ready to load a saved game");
    }
    const auto name = "CHECKPOINT.LOAD." + std::to_string(GetCurrentProcessId()) + ".x";
    // A local ASCII name also supports checkpoints selected from Unicode paths.
    std::filesystem::copy_file(path, name);
    checkpoint_file = name;
    NativeString native(name.c_str());
    const auto queue = reinterpret_cast<std::byte*>(app) + 0x268;
    const auto loaded =
        function<int(__stdcall*)(void*, void*)>(game::edition().load_file)(&native, queue);
    if (!loaded) {
        std::filesystem::remove(checkpoint_file);
        checkpoint_file.clear();
        throw std::runtime_error("The game could not load this saved game");
    }
    resume_pending = true;
    notify_status(window, L"Saved game loaded");
}

void update_quick_load() {
    if (!resume_pending || !game::executable_image() ||
        *reinterpret_cast<void**>(game::executable_image() + game::edition().pending_load)) {
        return;
    }
    resume_pending = false;
    if (!checkpoint_file.empty()) {
        std::error_code error;
        std::filesystem::remove(checkpoint_file, error);
        checkpoint_file.clear();
    }
    const auto app = *reinterpret_cast<game::Application**>(game::executable_image() +
                                                            game::edition().application);
    if (app && app->state && app->view) {
        // Apply after the native loader has replaced the paused menu and scene objects.
        const auto queue = reinterpret_cast<std::byte*>(app) + 0x268;
        function<void(__stdcall*)(void*, int, void*)>(game::edition().pause)(app->state, 0, queue);
        function<void(__stdcall*)(game::MainView*)>(game::edition().restore_view)(app->view);
    }
}
}
