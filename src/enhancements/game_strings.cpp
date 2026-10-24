#include "game_strings.h"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace enhancements {
namespace {
std::atomic<UINT> native_code_page{1252};
std::atomic<bool> dvd_fallback{false};

struct ResourceLanguage {
    unsigned count = 0;
    bool english = false;
};

BOOL CALLBACK count_language(HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR context) {
    auto& result = *reinterpret_cast<ResourceLanguage*>(context);
    ++result.count;
    result.english = PRIMARYLANGID(language) == LANG_ENGLISH;
    return TRUE;
}

std::wstring_view missing_dvd_message(HINSTANCE module, UINT id) {
    if (!dvd_fallback.load(std::memory_order_relaxed) || !module) {
        return {};
    }
    std::wstring_view text;
    if (id == 39 && module == GetModuleHandleW(L"XFILESE.DLL")) {
        text = L"Try again";
    } else if (id == 1101 && module == GetModuleHandleW(L"XFILEST.DLL")) {
        text = L"Too many login attempts, system locked!";
    } else {
        return {};
    }
    ResourceLanguage language;
    if (!EnumResourceLanguagesW(module, RT_STRING, MAKEINTRESOURCEW(1), count_language,
                                reinterpret_cast<LONG_PTR>(&language)) ||
        language.count != 1 || !language.english) {
        return {};
    }
    return text;
}
}

void set_game_string_code_page(UINT code_page) noexcept {
    native_code_page.store(code_page, std::memory_order_relaxed);
}

UINT game_string_code_page() noexcept {
    return native_code_page.load(std::memory_order_relaxed);
}

UINT game_resource_code_page(std::string_view text_hash, bool japanese) noexcept {
    constexpr std::string_view russian =
        "183a9d506effe93bf87b1f10e681cfd79458904c17b08fd84e16235eeff6dab2";
    return text_hash == russian ? 1251 : japanese ? 932 : 1252;
}

void set_game_string_dvd_fallback(bool enabled) noexcept {
    dvd_fallback.store(enabled, std::memory_order_relaxed);
}

int WINAPI load_game_string(HINSTANCE module, UINT id, LPSTR buffer, int capacity) noexcept {
    if (!buffer) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (capacity <= 0) {
        return LoadStringA(module, id, buffer, capacity);
    }
    const auto previous_error = GetLastError();
    LPCWSTR source = nullptr;
    auto length = LoadStringW(module, id, reinterpret_cast<LPWSTR>(&source), 0);
    if (!length || !source) {
        const auto fallback = missing_dvd_message(module, id);
        if (fallback.empty()) {
            return LoadStringA(module, id, buffer, capacity);
        }
        // English DVD resources omit these messages. Retry confirmation adds its punctuation.
        source = fallback.data();
        length = static_cast<int>(fallback.size());
    }
    const auto code_page = native_code_page.load(std::memory_order_relaxed);
    int written = 0;
    for (int i = 0; i < length;) {
        const auto units = source[i] >= 0xd800 && source[i] <= 0xdbff && i + 1 < length &&
                                   source[i + 1] >= 0xdc00 && source[i + 1] <= 0xdfff
                               ? 2
                               : 1;
        char encoded[8]{};
        const auto count = WideCharToMultiByte(code_page, 0, source + i, units, encoded,
                                               sizeof(encoded), nullptr, nullptr);
        // Keep multibyte characters intact when the native buffer is small.
        if (!count || count >= capacity - written) {
            break;
        }
        for (int j = 0; j < count; ++j) {
            buffer[written++] = encoded[j];
        }
        i += units;
    }
    buffer[written] = '\0';
    SetLastError(previous_error);
    return written;
}

namespace {
bool russian_text(LPCSTR text, int length) noexcept {
    return game_string_code_page() == 1251 && text && length > 0;
}

std::wstring russian_wide(LPCSTR text, int length) {
    const auto count = MultiByteToWideChar(1251, 0, text, length, nullptr, 0);
    if (!count) {
        return {};
    }
    std::wstring result(count, L'\0');
    MultiByteToWideChar(1251, 0, text, length, result.data(), count);
    return result;
}
}

int WINAPI draw_game_text(HDC dc, LPCSTR text, int length, LPRECT bounds, UINT format) noexcept {
    if (game_string_code_page() != 1251 || !text || !bounds || length == 0 || length < -1) {
        return DrawTextA(dc, text, length, bounds, format);
    }
    try {
        const auto previous_error = GetLastError();
        const auto bytes = length < 0 ? std::strlen(text) : static_cast<std::size_t>(length);
        if (bytes > static_cast<std::size_t>((std::numeric_limits<int>::max)() - 4)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        if (!bytes) {
            return DrawTextA(dc, text, length, bounds, format);
        }
        auto wide = russian_wide(text, static_cast<int>(bytes));
        if (wide.empty()) {
            return 0;
        }
        const auto modify =
            (format & DT_MODIFYSTRING) && (format & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS));
        const auto original = modify ? wide : std::wstring{};
        wide.resize(bytes + 4, L'\0');
        SetLastError(previous_error);
        const auto result = DrawTextW(dc, wide.data(), static_cast<int>(bytes), bounds, format);
        const auto draw_error = GetLastError();
        if (modify &&
            (wide.compare(0, bytes, original) != 0 ||
             std::any_of(wide.begin() + bytes, wide.end(), [](wchar_t c) { return c != L'\0'; }))) {
            // DrawText permits four extra bytes in a caller-owned mutable buffer.
            const auto end = std::find(wide.begin(), wide.end(), L'\0');
            if (end == wide.end()) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            const auto count = static_cast<int>(end - wide.begin());
            std::vector<char> encoded(count + 1);
            if (count && !WideCharToMultiByte(1251, 0, wide.data(), count, encoded.data(), count,
                                              nullptr, nullptr)) {
                return 0;
            }
            std::memcpy(const_cast<LPSTR>(text), encoded.data(), encoded.size());
        }
        SetLastError(draw_error);
        return result;
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
}

BOOL WINAPI output_game_text(HDC dc, int x, int y, LPCSTR text, int length) noexcept {
    if (!russian_text(text, length)) {
        return TextOutA(dc, x, y, text, length);
    }
    try {
        const auto previous_error = GetLastError();
        const auto wide = russian_wide(text, length);
        if (wide.empty()) {
            return FALSE;
        }
        SetLastError(previous_error);
        return TextOutW(dc, x, y, wide.data(), static_cast<int>(wide.size()));
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

BOOL WINAPI measure_game_text(HDC dc, LPCSTR text, int length, LPSIZE size) noexcept {
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!russian_text(text, length)) {
        return GetTextExtentPoint32A(dc, text, length, size);
    }
    try {
        const auto previous_error = GetLastError();
        const auto wide = russian_wide(text, length);
        if (wide.empty()) {
            return FALSE;
        }
        SetLastError(previous_error);
        return GetTextExtentPoint32W(dc, wide.data(), static_cast<int>(wide.size()), size);
    } catch (...) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

}
