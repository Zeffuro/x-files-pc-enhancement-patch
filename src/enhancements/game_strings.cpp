#include "game_strings.h"

#include <atomic>

namespace enhancements {
namespace {
std::atomic<UINT> native_code_page{1252};
}

void set_game_string_code_page(UINT code_page) noexcept {
    native_code_page.store(code_page, std::memory_order_relaxed);
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
    const auto length = LoadStringW(module, id, reinterpret_cast<LPWSTR>(&source), 0);
    if (!length || !source) {
        return LoadStringA(module, id, buffer, capacity);
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
