#include "launcher/welcome.h"
#include "dispatch.h"
#include "localization/ui.h"
#include <exception>
#include <fstream>
#include <iostream>
#include <windows.h>

namespace {
std::exception_ptr dialog_error;
unsigned dialog_count = 0;
int dialog_preset = 2106;
bool dialog_accept = false;

std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

LRESULT CALLBACK initialized_dialog(int code, WPARAM parameter, LPARAM data) {
    const auto* message = reinterpret_cast<CWPRETSTRUCT*>(data);
    if (code >= 0 && message->message == WM_INITDIALOG && GetDlgItem(message->hwnd, 2119)) {
        const auto window = message->hwnd;
        ++dialog_count;
        try {
            test::require(IsDlgButtonChecked(window, 2119) == BST_CHECKED,
                          "Startup Return is not recommended at first launch");
            SendMessageW(window, WM_COMMAND, 2107, 0);
            test::require(IsDlgButtonChecked(window, 2119) == BST_UNCHECKED,
                          "Original preset did not disable startup Return");
            SendMessageW(window, WM_COMMAND, 2106, 0);
            test::require(IsDlgButtonChecked(window, 2119) == BST_CHECKED,
                          "Recommended preset did not restore startup Return");
            CheckDlgButton(window, 2118, BST_UNCHECKED);
            test::require(IsWindowEnabled(GetDlgItem(window, 2119)) &&
                              IsDlgButtonChecked(window, 2119) == BST_CHECKED,
                          "Startup Return depends on autosaves in the welcome dialog");
            SendMessageW(window, WM_COMMAND, dialog_preset, 0);
        } catch (...) {
            dialog_error = std::current_exception();
        }
        SendMessageW(window, WM_COMMAND, dialog_accept && !dialog_error ? IDOK : IDCANCEL, 0);
    }
    return CallNextHookEx(nullptr, code, parameter, data);
}

void verify_dialog(const std::filesystem::path& root, int preset, bool accept) {
    const auto path = root / L"patch.ini";
    test::require(
        WritePrivateProfileStringW(L"Interface", L"WelcomeVersion", nullptr, path.c_str()),
        "Cannot reset first-launch marker");
    const auto before = bytes(path);
    dialog_preset = preset;
    dialog_accept = accept;
    dialog_error = nullptr;
    const auto count_before = dialog_count;
    const auto hook =
        SetWindowsHookExW(WH_CALLWNDPROCRET, initialized_dialog, nullptr, GetCurrentThreadId());
    test::require(hook != nullptr, "Cannot intercept production welcome dialog");
    const auto accepted = show_welcome(root);
    UnhookWindowsHookEx(hook);
    if (dialog_error) {
        std::rethrow_exception(dialog_error);
    }
    test::require(dialog_count == count_before + 1 && accepted == accept,
                  "Production welcome dialog did not complete as requested");
    if (accept) {
        test::require(GetPrivateProfileIntW(L"Enhancements", L"ContinueLatest", 2, path.c_str()) ==
                          (preset == 2106 ? 1u : 0u),
                      "Welcome preset did not persist startup Return");
    } else {
        test::require(bytes(path) == before, "Canceled welcome published startup Return edits");
    }
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"xfiles-welcome-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(root);
        const auto path = root / L"patch.ini";
        std::ofstream(path) << "[Accessibility]\nCaptionScale=125\n[Interface]\nLanguage=ja\n";
        const WelcomeChoices selected{{true, false, true, false, true, false, true, false, true,
                                       false, true, true, true, true},
                                      4,
                                      2,
                                      3};
        test::require(save_welcome_choices(path, selected),
                      "First-launch choices could not be saved");
        test::require(
            GetPrivateProfileIntW(L"Input", L"Gamepad", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Input", L"AnalogCursor", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Input", L"Vibration", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Input", L"ControllerHints", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Video", L"DVDMovies", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Video", L"DVDDeinterlace", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Audio", L"MovieSpeedMute", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Video", L"MovieSpeed", 0, path.c_str()) == 4 &&
                GetPrivateProfileIntW(L"Accessibility", L"Captions", 0, path.c_str()) == 2 &&
                GetPrivateProfileIntW(L"Video", L"MovieContrast", 0, path.c_str()) == 3 &&
                GetPrivateProfileIntW(L"Enhancements", L"SaveBrowser", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"DialogueTranscript", 0, path.c_str()) ==
                    1 &&
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"Autosaves", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"ContinueLatest", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Interface", L"WelcomeVersion", 0, path.c_str()) == 1,
            "First-launch choices were not applied");
        wchar_t language[16]{};
        GetPrivateProfileStringW(L"Interface", L"Language", L"", language, 16, path.c_str());
        test::require(
            std::wstring_view(language) == L"ja" &&
                GetPrivateProfileIntW(L"Accessibility", L"CaptionScale", 0, path.c_str()) == 125,
            "First-launch options replaced unrelated settings");
        const auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        test::require(lock != INVALID_HANDLE_VALUE, "Cannot lock test settings");
        const auto changed = save_welcome_choices(path, {});
        CloseHandle(lock);
        test::require(
            !changed && GetPrivateProfileIntW(L"Input", L"Gamepad", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"ContinueLatest", 0, path.c_str()) == 1,
            "Failed settings publication damaged prior choices");
        test::require(std::distance(std::filesystem::directory_iterator(root),
                                    std::filesystem::directory_iterator{}) == 1,
                      "Failed first-launch save left a temporary file");
        auto without_menu = selected;
        without_menu.enabled[11] = false;
        test::require(
            save_welcome_choices(path, without_menu) &&
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Enhancements", L"SaveBrowser", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"DialogueTranscript", 0, path.c_str()) == 1,
            "Disabling the welcome quick menu changed independent enhancements");
        auto without_autosaves = selected;
        without_autosaves.enabled[12] = false;
        test::require(
            save_welcome_choices(path, without_autosaves) &&
                GetPrivateProfileIntW(L"Enhancements", L"Autosaves", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Enhancements", L"ContinueLatest", 0, path.c_str()) == 1,
            "Startup Return depends on the welcome autosave choice");
        test::require(
            save_welcome_choices(path, {}) &&
                GetPrivateProfileIntW(L"Enhancements", L"DialogueTranscript", 1, path.c_str()) ==
                    0 &&
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Enhancements", L"Autosaves", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Enhancements", L"ContinueLatest", 1, path.c_str()) == 0,
            "Original game choices did not disable the transcript and quick menu");
        verify_dialog(root, 2106, false);
        verify_dialog(root, 2106, true);
        verify_dialog(root, 2107, true);
        for (const auto label_language :
             {ui::Language::German, ui::Language::French, ui::Language::Spanish,
              ui::Language::Italian, ui::Language::Japanese}) {
            test::require(std::wstring_view(ui::translate(L"Previous loads latest save at startup",
                                                          label_language)) !=
                              L"Previous loads latest save at startup",
                          "Startup Return label is missing a translation");
        }
        std::filesystem::remove_all(root);
        std::cout << "First-launch choices preserve unrelated settings and survive publication "
                     "failure.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
