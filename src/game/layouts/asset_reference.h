#pragma once
#include "game/profiles/generated.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace native_game {
struct AssetReference {
    const void* vtable;
    unsigned id;
    std::byte reserved[0x20];

    struct String {
        const void* vtable;
        const char* data;
        unsigned capacity;
        unsigned reserved;
    } descriptor;
};

inline std::wstring asset_path(std::string_view descriptor) {
    if (descriptor.empty()) {
        return {};
    }
    const auto digits = static_cast<unsigned char>(descriptor.front());
    if (!digits || digits > 3 || descriptor.size() <= digits) {
        return {};
    }
    unsigned length = 0;
    for (unsigned i = 1; i <= digits; ++i) {
        const auto ch = descriptor[i];
        if (ch < '0' || ch > '9') {
            return {};
        }
        length = length * 10 + unsigned(ch - '0');
    }
    if (!length || length > descriptor.size() - digits - 1) {
        return {};
    }
    const auto path = descriptor.substr(digits + 1, length);
    std::wstring result;
    for (const unsigned char ch : path) {
        if (ch < 0x20 || ch > 0x7f) {
            return {};
        }
        result.push_back(ch == 0x7f || ch == '\\' ? L'/' : wchar_t(ch));
    }
    return result;
}

inline std::wstring read_asset_path(const void* address, const std::byte* image,
                                    const Profile& profile) {
    if (!image || !address) {
        return {};
    }
    const auto read = [](const void* source, void* target, std::size_t size) {
        SIZE_T count = 0;
        return ReadProcessMemory(GetCurrentProcess(), source, target, size, &count) &&
               count == size;
    };
    AssetReference resource{};
    if (!read(address, &resource, sizeof(resource)) ||
        resource.vtable != image + profile.asset_reference || !resource.descriptor.data ||
        !resource.descriptor.capacity) {
        return {};
    }
    std::array<char, 1024> bytes{};
    const auto size = std::min<std::size_t>(bytes.size(), resource.descriptor.capacity);
    if (!read(resource.descriptor.data, bytes.data(), size)) {
        return {};
    }
    // The native path is a decimal length field followed by that many path bytes.
    return asset_path({bytes.data(), size});
}

static_assert(offsetof(AssetReference, descriptor) == 0x28);
static_assert(sizeof(AssetReference::String) == 0x10);
}
