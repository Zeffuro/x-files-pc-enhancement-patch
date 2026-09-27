#include "enhancements/game_strings.h"

#include <array>
#include <cstdio>
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
