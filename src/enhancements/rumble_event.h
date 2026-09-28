#pragma once

#include <string_view>

namespace enhancements::rumble {

inline bool asset_matches(std::wstring_view path, std::wstring_view expected) noexcept {
    if (path.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < path.size(); ++index) {
        auto character = path[index];
        if (character >= L'A' && character <= L'Z') {
            character += L'a' - L'A';
        } else if (character == L'\\') {
            character = L'/';
        }
        if (character != expected[index]) {
            return false;
        }
    }
    return true;
}

inline bool gunfire_asset(std::wstring_view path) noexcept {
    return asset_matches(path, L"xs/92148.amv");
}

}
