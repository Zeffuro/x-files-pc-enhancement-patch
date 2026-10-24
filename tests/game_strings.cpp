#include "enhancements/game_strings.h"
#include "platform/game_fonts.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <string>
#include <vector>
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
            "Localized string did not use the selected code page");
}

}

void verify() {
    constexpr std::string_view russian_hash =
        "183a9d506effe93bf87b1f10e681cfd79458904c17b08fd84e16235eeff6dab2";
    require(enhancements::game_resource_code_page(russian_hash, false) == 1251 &&
                enhancements::game_resource_code_page(russian_hash, true) == 1251 &&
                enhancements::game_resource_code_page({}, false) == 1252 &&
                enhancements::game_resource_code_page({}, true) == 932 &&
                enhancements::game_resource_code_page(russian_hash.substr(1), false) == 1252,
            "Resource hash did not bound the Russian encoding selection");
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

    enhancements::set_game_string_code_page(1251);
    require(enhancements::game_string_code_page() == 1251, "Shared code page was not updated");
    check(4201, L"А что это за шишка из Вашингтона?",
          "\xc0 \xf7\xf2\xee \xfd\xf2\xee \xe7\xe0 \xf8\xe8\xf8\xea\xe0 \xe8\xe7 "
          "\xc2\xe0\xf8\xe8\xed\xe3\xf2\xee\xed\xe0?");
    check(4202, L"Ёжик и ёлка", "\xa8\xe6\xe8\xea \xe8 \xb8\xeb\xea\xe0");
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

namespace {
class Canvas {
    static constexpr int width = 640, height = 160;
    HDC dc_ = CreateCompatibleDC(nullptr);
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ original_ = nullptr;
    void* pixels_ = nullptr;

public:
    Canvas() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
        require(dc_ && bitmap_ && pixels_, "Cannot allocate offscreen text canvas");
        original_ = SelectObject(dc_, bitmap_);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(0, 0, 0));
        clear();
    }

    ~Canvas() {
        SelectObject(dc_, original_);
        DeleteObject(bitmap_);
        DeleteDC(dc_);
    }

    HDC dc() const {
        return dc_;
    }

    void clear() {
        PatBlt(dc_, 0, 0, width, height, WHITENESS);
    }

    bool equals(const Canvas& other) const {
        GdiFlush();
        return std::memcmp(pixels_, other.pixels_, width * height * 4) == 0;
    }
};

void check_gdi_font(const wchar_t* face) {
    const auto font =
        CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    require(font != nullptr, "Cannot create selected text font");
    Canvas actual, expected;
    const auto actual_font = SelectObject(actual.dc(), font);
    const auto expected_font = SelectObject(expected.dc(), font);
    try {
        for (const auto page : {1251u, 1252u, 932u}) {
            enhancements::set_game_string_code_page(page);
            const auto text = page == 1251  ? L"А что это за шишка из Вашингтона? Ёжик и ёлка"
                              : page == 932 ? L"日本語"
                                            : L"Überprüfung Música";
            const auto units = static_cast<int>(std::wcslen(text));
            std::string bytes(
                WideCharToMultiByte(page, 0, text, units, nullptr, 0, nullptr, nullptr), '\0');
            WideCharToMultiByte(page, 0, text, units, bytes.data(), static_cast<int>(bytes.size()),
                                nullptr, nullptr);
            for (const auto length : {-1, static_cast<int>(bytes.size()), 4}) {
                actual.clear();
                expected.clear();
                RECT left{3, 5, 220, 155}, right = left;
                const auto result = enhancements::draw_game_text(actual.dc(), bytes.c_str(), length,
                                                                 &left, DT_WORDBREAK);
                const auto reference =
                    page == 1251
                        ? DrawTextW(expected.dc(), text, length, &right, DT_WORDBREAK)
                        : DrawTextA(expected.dc(), bytes.c_str(), length, &right, DT_WORDBREAK);
                require(result && result == reference && EqualRect(&left, &right) &&
                            actual.equals(expected),
                        "Native text drawing differs from its selected Unicode or ANSI path");
                left = right = {3, 5, 220, 155};
                require(enhancements::draw_game_text(actual.dc(), bytes.c_str(), length, &left,
                                                     DT_CALCRECT | DT_WORDBREAK) ==
                                (page == 1251 ? DrawTextW(expected.dc(), text, length, &right,
                                                          DT_CALCRECT | DT_WORDBREAK)
                                              : DrawTextA(expected.dc(), bytes.c_str(), length,
                                                          &right, DT_CALCRECT | DT_WORDBREAK)) &&
                            EqualRect(&left, &right),
                        "Text layout changed selected-font wrapping bounds");
            }
            const auto length = static_cast<int>(bytes.size());
            SIZE left{}, right{};
            require(enhancements::measure_game_text(actual.dc(), bytes.data(), length, &left) &&
                        (page == 1251 ? GetTextExtentPoint32W(expected.dc(), text, units, &right)
                                      : GetTextExtentPoint32A(expected.dc(), bytes.data(), length,
                                                              &right)) &&
                        left.cx == right.cx && left.cy == right.cy,
                    "Native text width did not use the selected font and encoding");
            actual.clear();
            expected.clear();
            require(enhancements::output_game_text(actual.dc(), 2, 3, bytes.data(), length) &&
                        (page == 1251 ? TextOutW(expected.dc(), 2, 3, text, units)
                                      : TextOutA(expected.dc(), 2, 3, bytes.data(), length)) &&
                        actual.equals(expected),
                    "Native text output differs from the Unicode or ANSI reference");
        }
        enhancements::set_game_string_code_page(1251);
        for (const auto format : {DT_END_ELLIPSIS, DT_PATH_ELLIPSIS}) {
            for (const auto explicit_length : {false, true}) {
                std::array<char, 32> bytes;
                bytes.fill('X');
                std::memcpy(bytes.data(), "\xc0\xe1", 3);
                std::array<wchar_t, 6> wide{L'А', L'б', L'\0', L'\0', L'\0', L'\0'};
                RECT left{0, 0, 1, 100}, right = left;
                const auto flags = format | DT_MODIFYSTRING | DT_SINGLELINE;
                const auto result = enhancements::draw_game_text(
                    actual.dc(), bytes.data(), explicit_length ? 2 : -1, &left, flags);
                const auto reference =
                    DrawTextW(expected.dc(), wide.data(), explicit_length ? 2 : -1, &right, flags);
                std::array<char, 16> encoded{};
                const auto count =
                    WideCharToMultiByte(1251, 0, wide.data(), -1, encoded.data(),
                                        static_cast<int>(encoded.size()), nullptr, nullptr);
                require(result == reference && EqualRect(&left, &right) && count > 0 &&
                            std::memcmp(bytes.data(), encoded.data(), count) == 0 &&
                            bytes[6] == 'X',
                        "Mutable ellipsis text or caller buffer boundary changed");
            }
        }
        std::array<char, 8> untouched{'A', 'B', 'X', 'X', 'X', 'X', 'X', 'X'};
        RECT bounds{0, 0, 600, 100};
        enhancements::draw_game_text(actual.dc(), untouched.data(), 2, &bounds,
                                     DT_MODIFYSTRING | DT_END_ELLIPSIS);
        require(untouched[2] == 'X', "Unchanged explicit-length text gained a terminator");
        SIZE left{}, right{};
        require(enhancements::measure_game_text(actual.dc(), nullptr, 0, &left) ==
                        GetTextExtentPoint32A(actual.dc(), nullptr, 0, &right) &&
                    left.cx == right.cx && left.cy == right.cy &&
                    enhancements::output_game_text(actual.dc(), 0, 0, nullptr, 0) ==
                        TextOutA(actual.dc(), 0, 0, nullptr, 0),
                "Empty ANSI API contracts changed");
    } catch (...) {
        SelectObject(actual.dc(), actual_font);
        SelectObject(expected.dc(), expected_font);
        DeleteObject(font);
        throw;
    }
    SelectObject(actual.dc(), actual_font);
    SelectObject(expected.dc(), expected_font);
    DeleteObject(font);
}
}

void verify_gdi(const std::filesystem::path& fonts) {
    check_gdi_font(L"Arial");
    if (!fonts.empty()) {
        constexpr std::array names{L"Dlg.ttr", L"Hcd.ttr", L"Jrn.ttr", L"Phn.ttr"};
        constexpr std::array faces{L"Schmutz ICG Cleaned", L"Typist", L"ZonkersHand",
                                   L"Bubbledot Coarse Positive"};
        for (unsigned i = 0; i < names.size(); ++i) {
            const auto path = fonts / names[i];
            platform::font_detail::Registry registry;
            require(registry.add(path), "Cannot load Russian native font fixture");
            check_gdi_font(faces[i]);
        }
    }
    enhancements::set_game_string_code_page(1252);
}

int main(int argc, char** argv) {
    try {
        verify();
        verify_gdi(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
