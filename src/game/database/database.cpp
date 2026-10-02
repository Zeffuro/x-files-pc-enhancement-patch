#include "database.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace game_assets {
namespace {
constexpr std::size_t maximum_size = 128u * 1024u * 1024u;
constexpr std::size_t maximum_strings = 200000;

bool text_byte(std::uint8_t byte) {
    return (byte >= 0x20 && byte <= 0x7f) || byte == '\r' || byte == '\n' || byte == '\t';
}
}

Database Database::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot read the game database");
    }
    const auto length = file.tellg();
    if (length < 32 || length > static_cast<std::streamoff>(maximum_size)) {
        throw std::runtime_error("Game database size is outside the supported range");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
        throw std::runtime_error("Cannot read the complete game database");
    }
    return parse(bytes);
}

Database Database::parse(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 32 || bytes.size() > maximum_size) {
        throw std::runtime_error("Invalid game database size");
    }
    Database result;
    for (std::size_t word = 0; word < result.header_.size(); ++word) {
        const auto offset = word * 4;
        result.header_[word] = (std::uint32_t(bytes[offset]) << 24) |
                               (std::uint32_t(bytes[offset + 1]) << 16) |
                               (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
    }
    if (result.header_[0] != 5 || result.header_[7] != 256) {
        throw std::runtime_error("Unsupported game database header");
    }
    result.bytes_.assign(bytes.begin(), bytes.end());
    for (std::size_t cursor = 32; cursor < bytes.size();) {
        const auto start = cursor;
        while (cursor < bytes.size() && text_byte(bytes[cursor])) {
            ++cursor;
        }
        const auto length = cursor - start;
        if (length >= 4 && length <= 4096) {
            if (result.strings_.size() == maximum_strings) {
                throw std::runtime_error("Too many text candidates in the game database");
            }
            std::string text(reinterpret_cast<const char*>(bytes.data() + start), length);
            std::replace(text.begin(), text.end(), '\x7f', '/');
            result.strings_.push_back(
                {static_cast<std::uint32_t>(start),
                 static_cast<std::uint32_t>(length +
                                            (cursor < bytes.size() && !bytes[cursor] ? 1 : 0)),
                 std::move(text)});
        }
        if (cursor < bytes.size()) {
            ++cursor;
        }
    }
    return result;
}

std::wstring Database::hex(std::size_t offset, std::size_t count) const {
    if (offset >= bytes_.size()) {
        return L"Offset is outside the database.";
    }
    count = std::min({count, bytes_.size() - offset, std::size_t{4096}});
    std::wostringstream out;
    out << std::hex << std::setfill(L'0');
    for (std::size_t row = 0; row < count; row += 16) {
        out << std::setw(8) << offset + row << L"  ";
        for (std::size_t col = 0; col < 16; ++col) {
            if (row + col < count) {
                out << std::setw(2) << unsigned(bytes_[offset + row + col]) << L' ';
            } else {
                out << L"   ";
            }
        }
        out << L" ";
        for (std::size_t col = 0; col < 16 && row + col < count; ++col) {
            const auto byte = bytes_[offset + row + col];
            out << (byte >= 0x20 && byte <= 0x7e ? wchar_t(byte) : L'.');
        }
        out << L"\r\n";
    }
    return out.str();
}
}
