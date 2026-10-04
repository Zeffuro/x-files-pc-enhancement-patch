#include "game_strings.h"

#include <atomic>
#include <string_view>

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

}
