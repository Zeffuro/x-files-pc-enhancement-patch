#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <string_view>

namespace saves {
namespace compatibility_detail {
inline std::uint32_t integer(std::span<const unsigned char> bytes) {
    std::uint32_t value = 0;
    for (const auto byte : bytes) {
        value = (value << 8) | byte;
    }
    return value;
}

class Reader {
    std::ifstream input_;
    std::uint64_t size_ = 0;
    std::set<std::uint32_t> visited_;
    std::uint32_t previous_ = 0;

public:
    std::uint32_t records = 0;
    std::uint32_t version_record = 0;

    explicit Reader(const std::filesystem::path& path)
        : input_(path, std::ios::binary | std::ios::ate) {
        const auto end = input_.tellg();
        if (end >= 24 && end <= 64 * 1024 * 1024) {
            size_ = static_cast<std::uint64_t>(end);
        }
    }

    bool read(std::uint64_t offset, std::span<unsigned char> bytes) {
        if (offset > size_ || bytes.size() > size_ - offset) {
            return false;
        }
        input_.seekg(static_cast<std::streamoff>(offset));
        return bool(input_.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
    }

    bool index(std::uint32_t offset, unsigned depth = 0) {
        std::array<unsigned char, 8> header{};
        if (depth > 8 || visited_.size() >= 4096 || !visited_.insert(offset).second ||
            !read(offset, header) || integer(std::span(header).subspan(2, 4)) != 1) {
            return false;
        }
        const auto type = header[0] & 0xc0;
        const auto count = integer(std::span(header).subspan(6, 2));
        if ((type != 0x40 && type != 0x80) || !count || count > 32) {
            return false;
        }
        std::array<unsigned char, 256> entries{};
        const auto stride = type == 0x40 ? 4u : 8u;
        if (!read(std::uint64_t(offset) + 8, std::span(entries).first(count * stride))) {
            return false;
        }
        for (unsigned i = 0; i < count; ++i) {
            const auto entry = std::span(entries).subspan(i * stride, stride);
            const auto target = integer(entry.first(4));
            if (type == 0x40) {
                if (!index(target, depth + 1)) {
                    return false;
                }
            } else {
                const auto id = integer(entry.subspan(4, 4));
                if (!id || id <= previous_ || ++records > 65536 || target > size_ ||
                    size_ - target < 24) {
                    return false;
                }
                previous_ = id;
                if (id == 0x14d6) {
                    version_record = target;
                }
            }
        }
        return true;
    }
};
}

// Later native loaders reject old databases through a synchronous game dialog.
inline const char* load_compatibility_error(const std::filesystem::path& path,
                                            bool requires_current_database) {
    using namespace compatibility_detail;
    constexpr const char* invalid = "Cannot read this saved game's compatibility data.";
    Reader reader(path);
    std::array<unsigned char, 24> header{};
    if (!reader.read(0, header) || integer(std::span(header).first(4)) != 5 ||
        integer(std::span(header).subspan(20, 4)) != 0x501) {
        return "This file is not a supported X-Files PC saved game.";
    }
    if (!requires_current_database) {
        return nullptr;
    }
    const auto classes = integer(std::span(header).subspan(12, 4));
    const auto directory = integer(std::span(header).subspan(8, 4));
    std::array<unsigned char, 58> prefix{};
    if (!classes || classes > 256 || !reader.read(directory, prefix) ||
        integer(std::span(prefix).subspan(2, 4)) != 1 ||
        integer(std::span(prefix).subspan(6, 2)) != classes ||
        integer(std::span(prefix).subspan(16, 2)) != classes - 1 || prefix[18] != 1 ||
        integer(std::span(prefix).subspan(19, 4)) != 1) {
        return invalid;
    }
    std::uint32_t index = 0, expected_records = 0;
    for (std::uint32_t i = 1; i < classes; ++i) {
        std::array<unsigned char, 43> entry{};
        if (!reader.read(std::uint64_t(directory) + 58 + (i - 1) * 43, entry) ||
            integer(std::span(entry).first(4)) != 1) {
            return invalid;
        }
        const auto id = integer(std::span(entry).subspan(4, 4));
        if (integer(std::span(entry).subspan(28, 4)) != id) {
            return invalid;
        }
        if (id == 0x53) {
            if (index) {
                return invalid;
            }
            expected_records = integer(std::span(entry).subspan(8, 4));
            index = integer(std::span(entry).subspan(24, 4));
        }
    }
    if (!index || !expected_records || !reader.index(index) || reader.records != expected_records) {
        return invalid;
    }
    constexpr const char* older =
        "This save uses an older game database and cannot be loaded by this edition. "
        "Use the original CD 1.00.12 edition for this save.";
    if (!reader.version_record) {
        return older;
    }
    std::array<unsigned char, 24> record{};
    std::array<unsigned char, 16> name{};
    if (!reader.read(reader.version_record, record) ||
        integer(std::span(record).subspan(2, 4)) != 1 || (record[23] & 0x7f) != 1 ||
        integer(std::span(record).subspan(10, 4)) != name.size() ||
        !reader.read(integer(std::span(record).subspan(6, 4)), name) ||
        std::string_view(reinterpret_cast<const char*>(name.data()), name.size()) !=
            std::string_view("XFilesDbVersion", 16)) {
        return invalid;
    }
    const auto version = integer(std::span(record).subspan(14, 4));
    return version >= 9 &&
                   version <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
               ? nullptr
               : older;
}
}
