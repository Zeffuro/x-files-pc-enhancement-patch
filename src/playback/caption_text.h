#pragma once

#include <string>
#include <string_view>
#include <cwctype>

namespace playback {
inline std::wstring normalize_caption(std::wstring_view text) {
    std::wstring result;
    bool space = false;
    for (const auto c : text) {
        if (c == L' ' || c == L'\t') {
            space = !result.empty();
            continue;
        }
        if (space && c != L')' && c != L']' && c != L'\r' && c != L'\n' && result.back() != L'(' &&
            result.back() != L'[' && result.back() != L'\n' && result.back() != L'\r') {
            result += L' ';
        }
        result += c;
        space = false;
    }
    for (std::size_t start = 0; (start = result.find(L". . .", start)) != std::wstring::npos;) {
        if (start && std::iswalnum(result[start - 1])) {
            ++start;
            continue;
        }
        result.replace(start, 5, L"...");
        start += 3;
    }
    return result;
}
}
