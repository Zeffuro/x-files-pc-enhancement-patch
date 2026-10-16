#include "devtools/checkpoints.h"
#include "devtools/checkpoints_files.h"
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool available = false, load_requested = false;
std::filesystem::path expected_source, expected_executable;
std::string dialog_error;
enum class DialogTest { load, recipe, standalone, parent_close };
DialogTest dialog_test = DialogTest::load;

LRESULT CALLBACK dialog_hook(int code, WPARAM value, LPARAM data) {
    const auto* message = reinterpret_cast<CWPRETSTRUCT*>(data);
    if (code >= 0 && message->message == WM_INITDIALOG &&
        GetDlgItem(message->hwnd, devtools::checkpoint_controls::source)) {
        const auto window = message->hwnd;
        try {
            using namespace devtools::checkpoint_controls;
            require(!IsWindowVisible(window), "dialog check must run before showing");
            require(GetNextDlgTabItem(window, nullptr, FALSE) != nullptr, "keyboard tab order");
            require(!IsWindowEnabled(GetDlgItem(window, load)), "unavailable load disabled");
            if (expected_executable.empty()) {
                require(!IsWindowEnabled(GetDlgItem(window, extract)), "extract needs originals");
                require(!IsWindowEnabled(GetDlgItem(window, reconstruct)),
                        "rebuild needs originals");
            } else {
                wchar_t text[32768]{};
                GetDlgItemTextW(window, executable, text, static_cast<int>(std::size(text)));
                require(std::filesystem::path(text) == expected_executable, "executable prefilled");
                GetDlgItemTextW(window, baseline, text, static_cast<int>(std::size(text)));
                require(
                    std::filesystem::equivalent(std::filesystem::path(text),
                                                expected_executable.parent_path() / L"XFILES.GAM"),
                    "database prefilled");
                require(IsWindowEnabled(GetDlgItem(
                            window, dialog_test == DialogTest::recipe ? reconstruct : extract)),
                        "detected originals enable recipe tools");
            }
            if (dialog_test == DialogTest::load || dialog_test == DialogTest::recipe) {
                SendMessageW(window, WM_COMMAND, load, 0);
                require(!load_requested, "disabled load must not call callback");
                available = true;
                SendMessageW(window, WM_TIMER, 1, 0);
                require(IsWindowEnabled(GetDlgItem(window, load)), "save and recipe loads enabled");
                wchar_t label[64]{};
                GetDlgItemTextW(window, load, label, static_cast<int>(std::size(label)));
                require((std::wstring(label).find(L"recipe") != std::wstring::npos) ==
                            (dialog_test == DialogTest::recipe),
                        "load action describes selection");
                SendMessageW(window, WM_COMMAND, load, 0);
                require(!load_requested, "load callback waits until modal dialog closes");
            } else if (dialog_test == DialogTest::parent_close) {
                DestroyWindow(GetWindow(window, GW_OWNER));
            } else {
                require(!(GetWindowLongPtrW(GetDlgItem(window, load), GWL_STYLE) & WS_VISIBLE),
                        "standalone load hidden");
                SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
            }
        } catch (const std::exception& error) {
            dialog_error = error.what();
            EndDialog(window, IDCANCEL);
        }
    }
    return CallNextHookEx(nullptr, code, value, data);
}

void dialogs(const std::filesystem::path& root, const std::filesystem::path& folder) {
    const auto save = folder / L"checkpoint λ.X";
    const auto recipe = folder / L"recipe.JSON";
    std::ofstream(save) << "fixture";
    std::ofstream(recipe) << "{}";
    const auto module = GetModuleHandleW(nullptr);
    const auto hook =
        SetWindowsHookExW(WH_CALLWNDPROCRET, dialog_hook, nullptr, GetCurrentThreadId());
    require(hook != nullptr, "dialog hook");

    struct Unhook {
        HHOOK hook;

        ~Unhook() {
            UnhookWindowsHookEx(hook);
        }
    } cleanup{hook};

    for (const auto test :
         {DialogTest::load, DialogTest::recipe, DialogTest::standalone, DialogTest::parent_close}) {
        dialog_test = test;
        available = load_requested = false;
        expected_source = test == DialogTest::recipe ? recipe : save;
        const auto owner = CreateWindowExW(0, L"STATIC", L"Checkpoint test", WS_OVERLAPPED, 0, 0, 1,
                                           1, nullptr, nullptr, module, nullptr);
        require(owner != nullptr, "hidden dialog owner");
        devtools::CheckpointActions actions;
        if (test != DialogTest::standalone) {
            actions.available = [] { return available; };
            actions.load = [](const auto& path) {
                require(path == expected_source, "exact selected path passed to deferred loader");
                load_requested = true;
            };
        }
        devtools::show_checkpoints(owner, module,
                                   static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), root,
                                   actions, expected_source);
        if (IsWindow(owner)) {
            require(IsWindowEnabled(owner), "modal owner reenabled");
            DestroyWindow(owner);
        }
        require(dialog_error.empty(), dialog_error.c_str());
        require(load_requested == (test == DialogTest::load || test == DialogTest::recipe),
                "explicit load callback count");
    }
    require(
        !devtools::checkpoint_can_load(folder / L"missing.x", {[] { return true; }, [](auto) {}}),
        "missing inputs disabled");
    require(!devtools::checkpoint_can_load(folder, {[] { return true; }, [](auto) {}}),
            "directories cannot load");
}
}

int main() {
    try {
        const auto folder = std::filesystem::temp_directory_path() /
                            (L"xfiles-checkpoint-test-" + std::to_wstring(GetCurrentProcessId()));
        require(std::filesystem::create_directory(folder), "new owned test directory");

        struct Folder {
            std::filesystem::path path;

            ~Folder() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{folder};

        std::ofstream(folder / L"XFiles.exe") << "unverified executable";
        std::ofstream(folder / L"XFILES.GAM") << "unverified database";
        require(devtools::find_checkpoint_files(nullptr, folder).executable.empty(),
                "unsupported executable cannot be prefilled");
        dialogs(folder, folder);
        int count = 0;
        auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
        require(args != nullptr, "arguments");
        std::filesystem::path game;
        bool preview = false;
        for (int i = 1; i < count; ++i) {
            if (std::wstring_view(args[i]) == L"--game" && i + 1 < count) {
                game = args[++i];
            } else if (std::wstring_view(args[i]) == L"--preview") {
                preview = true;
            }
        }
        LocalFree(args);
        if (!game.empty()) {
            const auto files = devtools::find_checkpoint_files(nullptr, game / L"tools");
            require(!files.executable.empty() && !files.baseline.empty(), "game-folder discovery");
            expected_executable = files.executable;
            dialogs(game / L"tools", folder);
        }
        if (preview) {
            const auto module = GetModuleHandleW(nullptr);
            const auto owner = CreateWindowExW(0, L"STATIC", L"Checkpoint preview", WS_OVERLAPPED,
                                               0, 0, 1, 1, nullptr, nullptr, module, nullptr);
            devtools::show_checkpoints(
                owner, module, static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                game.empty() ? folder : game, {[] { return true; }, [](auto) {}});
            DestroyWindow(owner);
        }
        std::cout << "Checkpoint discovery and hidden dialog checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
