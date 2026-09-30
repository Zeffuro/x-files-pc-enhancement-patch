#include "launcher/welcome.h"
#include "dispatch.h"
#include <fstream>
#include <iostream>
#include <windows.h>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      (L"xfiles-welcome-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(root);
        const auto path = root / L"patch.ini";
        std::ofstream(path) << "[Accessibility]\nCaptionScale=125\n[Interface]\nLanguage=ja\n";
        const WelcomeChoices selected{
            {true, false, true, false, true, false, true, false, true, false, true, true}, 4, 2, 3};
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
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 0, path.c_str()) == 1,
            "Failed settings publication damaged prior choices");
        test::require(std::distance(std::filesystem::directory_iterator(root),
                                    std::filesystem::directory_iterator{}) == 1,
                      "Failed first-launch save left a temporary file");
        auto without_menu = selected;
        without_menu.enabled.back() = false;
        test::require(
            save_welcome_choices(path, without_menu) &&
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 1, path.c_str()) == 0 &&
                GetPrivateProfileIntW(L"Enhancements", L"SaveBrowser", 0, path.c_str()) == 1 &&
                GetPrivateProfileIntW(L"Enhancements", L"DialogueTranscript", 0, path.c_str()) == 1,
            "Disabling the welcome quick menu changed independent enhancements");
        test::require(
            save_welcome_choices(path, {}) &&
                GetPrivateProfileIntW(L"Enhancements", L"DialogueTranscript", 1, path.c_str()) ==
                    0 &&
                GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 1, path.c_str()) == 0,
            "Original game choices did not disable the transcript and quick menu");
        std::filesystem::remove_all(root);
        std::cout << "First-launch choices preserve unrelated settings and survive publication "
                     "failure.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
