#include "checkpoints.h"
#include "checkpoints_files.h"
#include "recipes/recipe.h"
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace devtools {
namespace {
using namespace checkpoint_controls;
constexpr int browse_offset = 100;
constexpr UINT_PTR refresh_timer = 1;
constexpr wchar_t save_filter[] = L"Saved games and recipes\0*.x;*.json\0Saved games\0*.x\0"
                                  L"Numeric recipes\0*.json\0";
constexpr wchar_t executable_filter[] = L"Original game executable\0*.exe\0";
constexpr wchar_t baseline_filter[] = L"Original game database\0*.gam\0";
constexpr wchar_t recipe_filter[] = L"Numeric recipe\0*.json\0";
constexpr wchar_t output_filter[] = L"Saved game\0*.x\0";

struct State {
    HWND window = nullptr;
    HMODULE module = nullptr;
    HFONT font = nullptr;
    std::filesystem::path root, exe, gam, input, pending_load;
    std::string build;
    std::vector<std::filesystem::path> recipes;
    CheckpointActions actions;
};

HWND control(State& state, const wchar_t* type, const wchar_t* caption, DWORD style, int id, int x,
             int y, int width, int height) {
    const auto child = CreateWindowExW(
        0, type, caption, WS_CHILD | WS_VISIBLE | style, x, y, width, height, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.module, nullptr);
    if (!child) {
        throw std::runtime_error("Cannot create checkpoint controls");
    }
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), FALSE);
    return child;
}

void path_row(State& state, const wchar_t* label, int id, int y) {
    control(state, L"STATIC", label, SS_LEFT, 0, 16, y, 740, 20);
    control(state, L"EDIT", L"", WS_BORDER | ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, id, 16,
            y + 23, 624, 25);
    control(state, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, id + browse_offset, 650,
            y + 23, 112, 25);
}

void show_path(State& state, int id, const std::filesystem::path& path) {
    SetDlgItemTextW(state.window, id, path.c_str());
}

std::filesystem::path choose_file(State& state, const wchar_t* title, const wchar_t* filter,
                                  bool save, const wchar_t* extension = nullptr) {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = state.window;
    picker.lpstrTitle = title;
    picker.lpstrFilter = filter;
    picker.lpstrFile = path.data();
    picker.nMaxFile = static_cast<DWORD>(path.size());
    picker.lpstrDefExt = extension;
    const auto initial = save && !state.exe.empty() ? state.exe.parent_path() : state.root;
    picker.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
    picker.Flags =
        OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&picker) : GetOpenFileNameW(&picker))) {
        return {};
    }
    path.resize(wcslen(path.c_str()));
    if (save && std::filesystem::exists(path)) {
        throw std::runtime_error("Choose a new filename. Existing files are kept.");
    }
    return path;
}

bool extension_is(const std::filesystem::path& path, const wchar_t* expected) {
    return _wcsicmp(path.extension().c_str(), expected) == 0;
}

void update_actions(State& state) {
    const bool original = !state.exe.empty() && !state.gam.empty();
    EnableWindow(GetDlgItem(state.window, extract), original && extension_is(state.input, L".x"));
    EnableWindow(GetDlgItem(state.window, reconstruct),
                 original && extension_is(state.input, L".json"));
    EnableWindow(GetDlgItem(state.window, load), checkpoint_can_load(state.input, state.actions));
    SetDlgItemTextW(state.window, load,
                    extension_is(state.input, L".json") ? L"&Load recipe" : L"&Load selected save");
    EnableWindow(GetDlgItem(state.window, bundled), !state.recipes.empty());
}

void find_recipes(State& state) {
    state.recipes.clear();
    SendDlgItemMessageW(state.window, bundled, CB_RESETCONTENT, 0, 0);
    try {
        const auto folder = checkpoint_recipe_folder(state.module);
        auto suffix = state.build;
        std::replace(suffix.begin(), suffix.end(), '_', '-');
        if (std::filesystem::is_directory(folder)) {
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (entry.is_regular_file() && extension_is(entry.path(), L".json") &&
                    (suffix.empty() || entry.path().stem().string().ends_with(suffix)) &&
                    state.recipes.size() < 128) {
                    state.recipes.push_back(entry.path());
                }
            }
        }
        std::sort(state.recipes.begin(), state.recipes.end());
    } catch (const std::exception&) {
    }
    for (const auto& path : state.recipes) {
        SendDlgItemMessageW(state.window, bundled, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(path.stem().c_str()));
    }
}

void create_controls(State& state) {
    SetWindowTextW(state.window, L"Checkpoints");
    SetWindowPos(state.window, nullptr, 0, 0, 800, 570, SWP_NOMOVE | SWP_NOZORDER);
    control(state, L"STATIC",
            L"Game files are detected when available. Choose a saved game or numeric recipe.",
            SS_LEFT, 0, 16, 12, 746, 22);
    path_row(state, L"Original game &executable", executable, 42);
    path_row(state, L"Original game &database (XFILES.GAM)", baseline, 106);
    path_row(state, L"Saved game or &recipe", source, 170);
    control(state, L"STATIC", L"&Bundled numeric recipes", SS_LEFT, 0, 16, 237, 746, 20);
    control(state, L"COMBOBOX", L"Bundled numeric recipes",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, bundled, 16, 260, 746, 180);
    control(state, L"BUTTON", L"&Create recipe...", BS_PUSHBUTTON | WS_TABSTOP, extract, 16, 307,
            175, 30);
    control(state, L"BUTTON", L"&Rebuild save...", BS_PUSHBUTTON | WS_TABSTOP, reconstruct, 201,
            307, 175, 30);
    control(state, L"BUTTON", L"&Load selected save", BS_PUSHBUTTON | WS_TABSTOP, load, 386, 307,
            185, 30);
    control(state, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 650, 307, 112, 30);
    control(state, L"STATIC",
            L"Create and rebuild use a new filename, including in your game folder. Recipes omit "
            L"saved pictures and changed text. Load recipe rebuilds a temporary save and loads it.",
            SS_LEFT, 0, 16, 351, 746, 44);
    control(state, L"EDIT", L"Choose a saved game (.x) to load, or choose inputs for recipe tools.",
            WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
            status, 16, 407, 746, 100);
    if (!state.actions.load) {
        ShowWindow(GetDlgItem(state.window, load), SW_HIDE);
    }
    find_recipes(state);
    show_path(state, executable, state.exe);
    show_path(state, baseline, state.gam);
    show_path(state, source, state.input);
    update_actions(state);
    SetTimer(state.window, refresh_timer, 150, nullptr);
    SetFocus(GetDlgItem(state.window, state.exe.empty() ? executable + browse_offset : bundled));
}

void run_recipe(State& state, bool extracting) {
    const auto output = choose_file(
        state, extracting ? L"Create a new numeric recipe" : L"Rebuild a new saved game",
        extracting ? recipe_filter : output_filter, true, extracting ? L"json" : L"x");
    if (output.empty()) {
        return;
    }
    std::vector<std::uint8_t> bytes;
    if (extracting) {
        const auto json = recipes::extract(state.input, state.exe, state.gam);
        bytes.assign(json.begin(), json.end());
    } else {
        bytes = recipes::reconstruct(state.input, state.exe, state.gam);
    }
    recipes::write_new(output, bytes, {state.input, state.exe, state.gam});
    state.input = output;
    show_path(state, source, state.input);
    SendDlgItemMessageW(state.window, bundled, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SetDlgItemTextW(state.window, status, (L"Created: " + output.wstring()).c_str());
    update_actions(state);
}

void browse(State& state, int id) {
    if (id == executable + browse_offset) {
        auto selected = choose_file(state, L"Choose the matching original game executable",
                                    executable_filter, false);
        if (!selected.empty()) {
            const auto build = recipes::identify_executable(selected);
            state.exe = std::move(selected);
            state.build = build;
            state.root = state.exe.parent_path();
            state.gam.clear();
            const auto candidate = state.root / L"XFILES.GAM";
            if (std::filesystem::is_regular_file(candidate)) {
                state.gam = candidate;
            }
            show_path(state, executable, state.exe);
            show_path(state, baseline, state.gam);
            find_recipes(state);
        }
    } else if (id == baseline + browse_offset) {
        const auto selected =
            choose_file(state, L"Choose the matching original XFILES.GAM", baseline_filter, false);
        if (!selected.empty()) {
            state.gam = selected;
            show_path(state, baseline, state.gam);
        }
    } else if (id == source + browse_offset) {
        const auto selected =
            choose_file(state, L"Choose a saved game or numeric recipe", save_filter, false);
        if (!selected.empty()) {
            state.input = selected;
            show_path(state, source, state.input);
            SendDlgItemMessageW(state.window, bundled, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        }
    }
    update_actions(state);
}

INT_PTR CALLBACK dialog_proc(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            state = reinterpret_cast<State*>(data);
            state->window = window;
            SetWindowLongPtrW(window, DWLP_USER, data);
            create_controls(*state);
            return FALSE;
        }
        if (!state) {
            return FALSE;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(value) == IDCANCEL)) {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (message == WM_DESTROY) {
            KillTimer(window, refresh_timer);
            return TRUE;
        }
        if (message == WM_TIMER) {
            update_actions(*state);
            return TRUE;
        }
        if (message == WM_COMMAND) {
            const auto id = LOWORD(value);
            if (id == executable + browse_offset || id == baseline + browse_offset ||
                id == source + browse_offset) {
                browse(*state, id);
            } else if (id == bundled && HIWORD(value) == CBN_SELCHANGE) {
                const auto index = SendDlgItemMessageW(window, bundled, CB_GETCURSEL, 0, 0);
                if (index >= 0 && static_cast<std::size_t>(index) < state->recipes.size()) {
                    state->input = state->recipes[index];
                    show_path(*state, source, state->input);
                    update_actions(*state);
                }
            } else if (id == extract || id == reconstruct) {
                run_recipe(*state, id == extract);
            } else if (id == load && checkpoint_can_load(state->input, state->actions)) {
                state->pending_load = state->input;
                EndDialog(window, IDOK);
            }
            return TRUE;
        }
    } catch (const std::exception& error) {
        if (message == WM_INITDIALOG) {
            MessageBoxA(window, error.what(), "Checkpoints", MB_OK | MB_ICONERROR);
            EndDialog(window, IDCANCEL);
        } else {
            if (state) {
                update_actions(*state);
            }
            SetDlgItemTextA(window, status, error.what());
        }
        return TRUE;
    }
    return FALSE;
}
}

bool checkpoint_can_load(const std::filesystem::path& selected, const CheckpointActions& actions) {
    std::error_code error;
    return actions.load && actions.available &&
           (extension_is(selected, L".x") || extension_is(selected, L".json")) &&
           std::filesystem::is_regular_file(selected, error) && actions.available();
}

void show_checkpoints(HWND owner, HMODULE module, HFONT font,
                      const std::filesystem::path& initial_root, CheckpointActions actions,
                      const std::filesystem::path& initial_source) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog{};
        WORD menu = 0, type = 0, title = 0;
    } resource;

    resource.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    resource.dialog.cx = 460;
    resource.dialog.cy = 290;
    State state;
    state.module = module;
    state.font = font;
    state.root = initial_root;
    const auto files = find_checkpoint_files(module, initial_root);
    state.exe = files.executable;
    state.gam = files.baseline;
    state.build = files.build;
    state.input = initial_source;
    state.actions = std::move(actions);
    if (DialogBoxIndirectParamW(module, &resource.dialog, owner, dialog_proc,
                                reinterpret_cast<LPARAM>(&state)) == -1 &&
        IsWindow(owner)) {
        throw std::runtime_error("Cannot open Checkpoints");
    }
    if (!state.pending_load.empty() && IsWindow(owner) &&
        checkpoint_can_load(state.pending_load, state.actions)) {
        state.actions.load(state.pending_load);
    }
}
}
