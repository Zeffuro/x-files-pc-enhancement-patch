#include "pff.h"
#include <fstream>

namespace game_assets {
namespace {
void fail(std::wstring* error, const wchar_t* message) {
    if (error) {
        *error = message;
    }
}

std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

void put(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t at = 0; at < 4; ++at) {
        bytes[offset + at] = static_cast<std::uint8_t>(value >> (at * 8));
    }
}
}

std::optional<PffArchive> PffArchive::parse(std::span<const std::uint8_t> bytes,
                                            std::wstring* error) {
    if (error) {
        error->clear();
    }
    if (bytes.size() > pff_file_limit || bytes.size() < 12) {
        fail(error, L"PFF file size is outside the supported bounds");
        return std::nullopt;
    }
    if (bytes[0] != 'P' || bytes[1] != 'F' || bytes[2] != 'F' || bytes[3] != ' ') {
        fail(error, L"PFF magic is invalid");
        return std::nullopt;
    }
    const auto count = word(bytes, 4);
    if (count > pff_entry_limit) {
        fail(error, L"PFF entry count exceeds the 65536-entry safety limit");
        return std::nullopt;
    }
    // The offsets are followed by one opaque header word per entry, not sector padding.
    const std::size_t header = 12 + std::size_t(count) * 8;
    if (header > bytes.size() || word(bytes, 8) < header) {
        fail(error, L"PFF tables are incomplete or overlap the payloads");
        return std::nullopt;
    }
    if (word(bytes, 8 + std::size_t(count) * 4) != bytes.size()) {
        fail(error, L"PFF final offset does not match the file size");
        return std::nullopt;
    }
    PffArchive result;
    result.entries_.reserve(count);
    const std::size_t values = 12 + std::size_t(count) * 4;
    for (std::size_t index = 0; index < count; ++index) {
        const auto start = word(bytes, 8 + index * 4);
        const auto end = word(bytes, 12 + index * 4);
        if (start > end || end > bytes.size()) {
            fail(error, L"PFF offsets are unordered or outside the file");
            return std::nullopt;
        }
        result.entries_.push_back({start, end - start, word(bytes, values + index * 4)});
    }
    result.bytes_.assign(bytes.begin(), bytes.end());
    return result;
}

std::optional<PffArchive> PffArchive::load(const std::filesystem::path& path, std::wstring* error) {
    if (error) {
        error->clear();
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < std::streampos(12) || size > std::streampos(pff_file_limit)) {
        fail(error, L"PFF file could not be read or exceeds the supported size bounds");
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())) ||
        file.peek() != std::char_traits<char>::eof()) {
        fail(error, L"PFF file changed or could not be read completely");
        return std::nullopt;
    }
    return parse(bytes, error);
}

std::span<const PffEntry> PffArchive::entries() const {
    return entries_;
}

std::span<const std::uint8_t> PffArchive::entry(std::size_t index) const {
    if (index >= entries_.size()) {
        return {};
    }
    const auto& value = entries_[index];
    return std::span(bytes_).subspan(value.offset, value.size);
}

std::span<const std::uint8_t> PffArchive::bytes() const {
    return bytes_;
}

std::optional<std::vector<std::uint8_t>> PffArchive::replace(std::size_t index,
                                                             std::span<const std::uint8_t> payload,
                                                             std::wstring* error) const {
    if (error) {
        error->clear();
    }
    if (index >= entries_.size()) {
        fail(error, L"PFF entry index is invalid");
        return std::nullopt;
    }
    const auto& old = entries_[index];
    const auto retained = bytes_.size() - old.size;
    if (payload.size() > pff_file_limit - retained) {
        fail(error, L"Replaced PFF would exceed the 512 MiB safety limit");
        return std::nullopt;
    }
    std::vector<std::uint8_t> result;
    result.reserve(retained + payload.size());
    const auto original = bytes();
    const auto before = original.first(old.offset);
    const auto after = original.subspan(std::size_t(old.offset) + old.size);
    result.insert(result.end(), before.begin(), before.end());
    result.insert(result.end(), payload.begin(), payload.end());
    result.insert(result.end(), after.begin(), after.end());
    const auto delta = std::int64_t(payload.size()) - old.size;
    for (std::size_t at = index + 1; at <= entries_.size(); ++at) {
        const auto offset = word(original, 8 + at * 4);
        put(result, 8 + at * 4, static_cast<std::uint32_t>(std::int64_t(offset) + delta));
    }
    return result;
}
}
