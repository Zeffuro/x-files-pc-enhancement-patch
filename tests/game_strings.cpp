#include "enhancements/game_strings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void check(UINT id, std::wstring_view wide, std::string_view narrow) {
    const auto module = GetModuleHandleW(nullptr);
    std::array<wchar_t, 64> original{};
    const auto original_length =
        LoadStringW(module, id, original.data(), static_cast<int>(original.size()));
    require(original_length == static_cast<int>(wide.size()) &&
                std::wstring_view(original.data(), original_length) == wide,
            "Resource fixture has incorrect Unicode text");

    std::array<char, 64> converted{};
    SetLastError(12345);
    const auto length = enhancements::load_game_string(module, id, converted.data(),
                                                       static_cast<int>(converted.size()));
    require(length == static_cast<int>(narrow.size()) &&
                std::string_view(converted.data(), length) == narrow && converted[length] == '\0' &&
                GetLastError() == 12345,
            "Localized string did not convert to Windows-1252");
}

}

void verify() {
    check(4101, L"Música", "M\xfasica");
    check(4102, L"Guión", "Gui\xf3n");
    check(4103, L"Teléfono celular",
          "Tel\xe9"
          "fono celular");
    check(4141, L"Téléphone portable", "T\xe9l\xe9phone portable");
    check(4161, L"Überprüfung",
          "\xdc"
          "berpr\xfc"
          "fung");

    const auto module = GetModuleHandleW(nullptr);
    for (int capacity = 1; capacity <= 4; ++capacity) {
        std::array<char, 6> buffer{'X', 'X', 'X', 'X', 'X', 'X'};
        const auto length = enhancements::load_game_string(module, 4101, buffer.data(), capacity);
        require(length == capacity - 1 && buffer[length] == '\0' && buffer[capacity] == 'X',
                "Truncated string wrote outside caller capacity");
        require(std::string_view(buffer.data(), length) == std::string_view("M\xfasica", length),
                "Truncated string has incorrect bytes");
    }
    std::array<char, 6> absent{'X', 'X', 'X', 'X', 'X', 'X'};
    SetLastError(12345);
    require(enhancements::load_game_string(module, 4999, absent.data(),
                                           static_cast<int>(absent.size())) == 0 &&
                absent[0] == '\0' && GetLastError() == ERROR_RESOURCE_NAME_NOT_FOUND,
            "Missing resource did not preserve LoadStringA behavior");
    std::array<char, 6> edge{'X', 'X', 'X', 'X', 'X', 'X'};
    require(enhancements::load_game_string(module, 4101, edge.data(), 0) ==
                LoadStringA(module, 4101, edge.data(), 0),
            "Zero-capacity behavior changed");

    enhancements::set_game_string_code_page(932);
    check(4181, L"日本語", "\x93\xfa\x96\x7b\x8c\xea");
    for (int capacity = 1; capacity <= 7; ++capacity) {
        std::array<char, 9> buffer{'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
        const auto length = enhancements::load_game_string(module, 4181, buffer.data(), capacity);
        require(length == ((capacity - 1) / 2) * 2 && buffer[length] == '\0' &&
                    buffer[capacity] == 'X',
                "Japanese string split a character or exceeded its buffer");
    }
    enhancements::set_game_string_code_page(1252);

    std::array<wchar_t, 32768> path{};
    require(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())) != 0,
            "Cannot locate resource fixture");
    const auto fixture_path = std::filesystem::path(path.data()).parent_path() / L"xfilese.dll";
    const auto messages = LoadLibraryW(fixture_path.c_str());
    require(messages && messages == GetModuleHandleW(L"XFILESE.DLL"),
            "Cannot load English message fixture");
    std::array<char, 64> retry{};
    require(enhancements::load_game_string(messages, 39, retry.data(), 64) == 0,
            "Retry fallback changed a non-DVD resource lookup");
    enhancements::set_game_string_dvd_fallback(true);
    SetLastError(12345);
    require(enhancements::load_game_string(messages, 39, retry.data(), 64) == 9 &&
                std::string_view(retry.data()) == "Try again" && GetLastError() == 12345,
            "Missing DVD retry message did not preserve text and caller error");
    for (int capacity = 1; capacity <= 10; ++capacity) {
        retry.fill('X');
        const auto size = enhancements::load_game_string(messages, 39, retry.data(), capacity);
        require(size == capacity - 1 && retry[size] == '\0' && retry[capacity] == 'X' &&
                    std::string_view(retry.data(), size) == std::string_view("Try again", size),
                "Retry message did not respect the caller's buffer");
    }
    require(enhancements::load_game_string(messages, 12, retry.data(), 64) == 37 &&
                std::string_view(retry.data()) == "Do you want to save your current game",
            "Retry fallback changed an existing native message");
    require(enhancements::load_game_string(module, 39, retry.data(), 64) == 0 &&
                enhancements::load_game_string(messages, 40, retry.data(), 64) == 0,
            "Retry fallback supplied unrelated modules or message IDs");
    enhancements::set_game_string_dvd_fallback(false);
    require(enhancements::load_game_string(messages, 39, retry.data(), 64) == 0,
            "Retry fallback remained active after detach");
    FreeLibrary(messages);

    const auto fixtures = fixture_path.parent_path();
    const auto text_messages = LoadLibraryW((fixtures / L"xfilest.dll").c_str());
    require(text_messages && text_messages == GetModuleHandleW(L"XFILEST.DLL"),
            "Cannot load English laptop message fixture");
    require(enhancements::load_game_string(text_messages, 1101, retry.data(), 64) == 0,
            "Laptop fallback changed a non-DVD lookup");
    enhancements::set_game_string_dvd_fallback(true);
    constexpr std::string_view lockout = "Too many login attempts, system locked!";
    for (int capacity = 1; capacity <= static_cast<int>(lockout.size()) + 2; ++capacity) {
        retry.fill('X');
        SetLastError(12345);
        const auto size =
            enhancements::load_game_string(text_messages, 1101, retry.data(), capacity);
        const auto expected = std::min(static_cast<int>(lockout.size()), capacity - 1);
        require(size == expected && retry[size] == '\0' && retry[capacity] == 'X' &&
                    std::string_view(retry.data(), size) == lockout.substr(0, expected) &&
                    GetLastError() == 12345,
                "DVD laptop lockout did not preserve text, bounds and caller error");
    }
    require(enhancements::load_game_string(text_messages, 1100, retry.data(), 64) == 5 &&
                std::string_view(retry.data()) == "faith" &&
                enhancements::load_game_string(text_messages, 1102, retry.data(), 64) == 7 &&
                std::string_view(retry.data()) == "nuclear",
            "Laptop fallback replaced an existing adjacent message");
    require(enhancements::load_game_string(text_messages, 39, retry.data(), 64) == 0 &&
                enhancements::load_game_string(text_messages, 1104, retry.data(), 64) == 0 &&
                enhancements::load_game_string(module, 1101, retry.data(), 64) == 0,
            "Laptop fallback supplied unrelated modules or IDs");
    const auto error_messages = LoadLibraryW(fixture_path.c_str());
    require(error_messages &&
                enhancements::load_game_string(error_messages, 1101, retry.data(), 64) == 0,
            "Laptop fallback supplied an error-module message");
    FreeLibrary(error_messages);
    enhancements::set_game_string_dvd_fallback(false);
    require(enhancements::load_game_string(text_messages, 1101, retry.data(), 64) == 0,
            "Laptop fallback remained active after detach");
    FreeLibrary(text_messages);

    const auto localized_directory =
        std::filesystem::temp_directory_path() /
        (L"xfiles-game-strings-" + std::to_wstring(GetCurrentProcessId()));
    require(std::filesystem::create_directory(localized_directory),
            "Cannot create localized fixture directory");
    const auto localized_path = localized_directory / L"xfilest.dll";
    require(CopyFileW((fixtures / L"xfilest-localized.dll").c_str(), localized_path.c_str(), TRUE),
            "Cannot copy localized message fixture");
    const auto localized = LoadLibraryW(localized_path.c_str());
    require(localized && localized == GetModuleHandleW(L"XFILEST.DLL"),
            "Cannot load localized laptop message fixture");
    enhancements::set_game_string_dvd_fallback(true);
    constexpr std::string_view localized_lockout = "Zuviele Anmeldeversuche, System gesperrt.";
    SetLastError(12345);
    require(enhancements::load_game_string(localized, 1101, retry.data(), 64) ==
                    static_cast<int>(localized_lockout.size()) &&
                std::string_view(retry.data()) == localized_lockout && GetLastError() == 12345,
            "Laptop fallback replaced native localized lockout text");
    require(enhancements::load_game_string(localized, 39, retry.data(), 64) == 0,
            "DVD fallback supplied retry from localized text module");
    FreeLibrary(localized);
    require(CopyFileW((fixtures / L"xfilest-missing-localized.dll").c_str(), localized_path.c_str(),
                      FALSE),
            "Cannot copy missing localized lockout fixture");
    const auto missing_localized = LoadLibraryW(localized_path.c_str());
    require(missing_localized && missing_localized == GetModuleHandleW(L"XFILEST.DLL") &&
                LoadStringA(missing_localized, 1101, retry.data(), 64) == 0 &&
                enhancements::load_game_string(missing_localized, 1101, retry.data(), 64) == 0,
            "Laptop fallback supplied English text to a localized module");
    enhancements::set_game_string_dvd_fallback(false);
    FreeLibrary(missing_localized);
    std::filesystem::remove(localized_path);
    std::filesystem::remove(localized_directory);
}

int main() {
    try {
        verify();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
