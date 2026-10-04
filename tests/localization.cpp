#include "localization/ui.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("UI localization test failed");
    }
}

std::wstring text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}
}

int main() {
    namespace fs = std::filesystem;
    const auto path =
        fs::temp_directory_path() /
        (L"xfiles-localization-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    fs::remove(path);
    try {
        require(ui::read_language(path) == ui::system_language());
        WritePrivateProfileStringW(L"Interface", L"Language", L"xx", path.c_str());
        require(ui::read_language(path) == ui::system_language());
        for (int index = 0; index < 6; ++index) {
            const auto selected = static_cast<ui::Language>(index);
            ui::save_language(path, selected);
            require(ui::read_language(path) == selected);
            require(ui::language_name(selected) != nullptr);
            for (const auto label :
                 {L"Click", L"Interact", L"View", L"Talk", L"Use", L"Target", L"Hotspot",
                  L"Show hotspot labels", L"Mute", L"2x (natural pitch)", L"3x (natural pitch)",
                  L"4x (natural pitch)"}) {
                const std::wstring translated = ui::translate(label, selected);
                require(!translated.empty() && (index == 0 || translated != label));
            }
        }
        require(std::wstring(ui::translate(L"Install", ui::Language::German)) == L"Installieren");
        require(std::wstring(ui::translate(L"Install", ui::Language::Japanese)) == L"インストール");
        require(std::wstring(ui::translate(L"Unmapped text", ui::Language::French)) ==
                L"Unmapped text");

        const auto parent = CreateWindowExW(0, L"STATIC", L"The X-Files Setup", WS_POPUP, 0, 0, 360,
                                            300, nullptr, nullptr, nullptr, nullptr);
        require(parent != nullptr);
        const auto label = CreateWindowExW(0, L"STATIC", L"Install The X-Files", WS_CHILD, 0, 0,
                                           100, 20, parent, nullptr, nullptr, nullptr);
        const auto button = CreateWindowExW(0, L"BUTTON", L"Install", WS_CHILD, 0, 25, 100, 20,
                                            parent, nullptr, nullptr, nullptr);
        const auto source = CreateWindowExW(0, L"EDIT", L"C:\\my\\Install", WS_CHILD, 0, 50, 200,
                                            20, parent, nullptr, nullptr, nullptr);
        const auto languages = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0,
                                               75, 100, 100, parent, nullptr, nullptr, nullptr);
        require(label && button && source && languages);
        SendMessageW(languages, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(languages, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"日本語"));
        SendMessageW(languages, CB_SETCURSEL, 1, 0);
        ui::DialogTranslator translator(parent);
        translator.apply(ui::Language::German);
        require(text(label) == L"The X-Files installieren");
        require(text(button) == L"Installieren");
        translator.apply(ui::Language::Japanese);
        require(text(label) == L"The X-Files をインストール");
        require(text(source) == L"C:\\my\\Install");
        require(SendMessageW(languages, CB_GETCURSEL, 0, 0) == 1);
        translator.apply(ui::Language::English);
        require(text(button) == L"Install");
        DestroyWindow(parent);
    } catch (...) {
        fs::remove(path);
        throw;
    }
    fs::remove(path);
}
