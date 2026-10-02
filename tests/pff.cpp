#include "game/assets/pff.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t at = 0; at < 4; ++at) {
        bytes[offset + at] = static_cast<std::uint8_t>(value >> (at * 8));
    }
}

bool same(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

std::vector<std::uint8_t> fixture(std::span<const std::uint32_t> sizes, std::size_t padding = 3) {
    const auto count = sizes.size();
    std::size_t total = 12 + count * 8 + padding;
    for (const auto size : sizes) {
        total += size;
    }
    std::vector<std::uint8_t> bytes(total, 0xa5);
    std::copy_n("PFF ", 4, bytes.begin());
    put(bytes, 4, static_cast<std::uint32_t>(count));
    auto offset = static_cast<std::uint32_t>(12 + count * 8 + padding);
    for (std::size_t at = 0; at < count; ++at) {
        put(bytes, 8 + at * 4, offset);
        put(bytes, 12 + count * 4 + at * 4, 0x12340000 + static_cast<std::uint32_t>(at));
        std::fill_n(bytes.begin() + offset, sizes[at], static_cast<std::uint8_t>(at + 1));
        offset += sizes[at];
    }
    put(bytes, 8 + count * 4, offset);
    return bytes;
}

void check_replacement(const game_assets::PffArchive& archive, std::size_t selected,
                       std::span<const std::uint8_t> payload) {
    std::wstring error = L"Stale error";
    const auto changed = archive.replace(selected, payload, &error);
    require(changed.has_value() && error.empty(), "Replacement failed or retained old error");
    const auto reparsed = game_assets::PffArchive::parse(*changed, &error);
    require(reparsed.has_value() && reparsed->entries().size() == archive.entries().size(),
            "Changed PFF did not parse with unchanged entry count");
    const auto delta = std::int64_t(payload.size()) - archive.entries()[selected].size;
    for (std::size_t at = 0; at < archive.entries().size(); ++at) {
        const auto& old = archive.entries()[at];
        const auto& value = reparsed->entries()[at];
        require(value.header_value == old.header_value &&
                    std::int64_t(value.offset) ==
                        std::int64_t(old.offset) + (at > selected ? delta : 0),
                "Replacement changed metadata, order or an unexpected offset");
        require(same(reparsed->entry(at), at == selected ? payload : archive.entry(at)),
                "Replacement changed an unrelated payload");
    }
    const auto prefix_start = 12 + archive.entries().size() * 4;
    const auto prefix_end = archive.entries()[0].offset;
    require(same(reparsed->bytes().subspan(prefix_start, prefix_end - prefix_start),
                 archive.bytes().subspan(prefix_start, prefix_end - prefix_start)),
            "Opaque header values or prefix padding changed");
}

struct TempFile {
    std::filesystem::path root, path;

    TempFile() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() / ("xfiles-pff-" + std::to_string(stamp));
        require(std::filesystem::create_directory(root), "Temporary PFF fixture creation failed");
        path = root / "test.PFF";
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void parser_checks() {
    using game_assets::PffArchive;
    const std::uint32_t sizes[] = {3, 0, 4};
    const auto original = fixture(sizes);
    std::wstring error = L"Stale error";
    const auto archive = PffArchive::parse(original, &error);
    require(archive.has_value() && error.empty() && archive->entries().size() == 3 &&
                archive->entries()[0].offset == 39 && archive->entries()[0].size == 3 &&
                archive->entries()[0].header_value == 0x12340000 && archive->entry(1).empty(),
            "Fixture fields, little endian or empty entry failed");
    require(same(archive->bytes(), original), "Unmodified PFF bytes changed");
    for (std::size_t at = 0; at < sizes[0]; ++at) {
        require(archive->entry(0)[at] == 1, "Payload span incorrect");
    }
    const auto noop = archive->replace(2, archive->entry(2));
    require(noop.has_value() && *noop == original, "No-op replacement was not byte-exact");
    require(!archive->replace(3, {}) && archive->entry(3).empty(), "Invalid index accepted");
    const std::uint8_t payload[] = {7, 8, 9, 10, 11};
    for (std::size_t at = 0; at < sizes[0]; ++at) {
        check_replacement(*archive, at, payload);
        check_replacement(*archive, at, {});
    }
    require(same(archive->bytes(), original), "Replacement mutated original archive");
    const std::vector<std::uint32_t> empty;
    require(PffArchive::parse(fixture(empty)).has_value(), "Empty PFF rejected");
    for (std::size_t length = 0; length < original.size(); ++length) {
        require(!PffArchive::parse(std::span(original).first(length)), "Truncated PFF accepted");
    }
    auto malformed = original;
    malformed[3] = 0;
    require(!PffArchive::parse(malformed, &error) && !error.empty(), "Invalid magic accepted");
    malformed = original;
    put(malformed, 4, 0xffffffff);
    require(!PffArchive::parse(malformed), "Overflowing entry count accepted");
    put(malformed, 4, 0x03000000);
    require(!PffArchive::parse(malformed), "Big-endian entry count accepted");
    put(malformed, 4, 4);
    require(!PffArchive::parse(malformed), "Invalid metadata table accepted");
    malformed = original;
    put(malformed, 8, 35);
    require(!PffArchive::parse(malformed), "Payload overlapping metadata accepted");
    malformed = original;
    put(malformed, 12, 0xffffffff);
    require(!PffArchive::parse(malformed), "Out-of-file payload offset accepted");
    put(malformed, 12, 38);
    require(!PffArchive::parse(malformed), "Descending payload offsets accepted");
    malformed = original;
    put(malformed, 20, static_cast<std::uint32_t>(original.size() - 1));
    require(!PffArchive::parse(malformed), "Sentinel before EOF accepted");
    put(malformed, 20, static_cast<std::uint32_t>(original.size() + 1));
    require(!PffArchive::parse(malformed), "Sentinel after EOF accepted");
    std::vector<std::uint32_t> many(game_assets::pff_entry_limit);
    require(PffArchive::parse(fixture(many, 0)).has_value(), "Exact count limit rejected");
    many.push_back(0);
    require(!PffArchive::parse(fixture(many, 0)), "Count beyond safety limit accepted");
    TempFile file;
    require(!PffArchive::load(file.path), "Missing PFF accepted");
    {
        std::ofstream output(file.path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(original.data()),
                     static_cast<std::streamsize>(original.size()));
    }
    const auto loaded = PffArchive::load(file.path);
    require(loaded.has_value() && same(loaded->bytes(), original), "PFF file loader failed");
}

void inventory(const std::filesystem::path& root) {
    std::size_t files = 0, entries = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(root)) {
        auto extension = file.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!file.is_regular_file() || extension != ".pff") {
            continue;
        }
        const auto archive = game_assets::PffArchive::load(file.path());
        require(archive.has_value(), "Installed PFF failed production parser");
        std::ifstream input(file.path(), std::ios::binary);
        const std::vector<std::uint8_t> original((std::istreambuf_iterator<char>(input)), {});
        require(same(archive->bytes(), original), "Installed PFF unmodified roundtrip failed");
        require(archive->entries()[0].offset == 12 + archive->entries().size() * 8,
                "Installed PFF metadata layout changed");
        for (std::size_t at = 0; at < archive->entries().size(); ++at) {
            const auto raw = archive->entry(at);
            require(raw.size() >= 14 && raw[10] == 0 && raw[11] == 0x11 && raw[12] == 2 &&
                        raw[13] == 0xff,
                    "Installed entry is not headerless PICT v2");
        }
        const auto noop = archive->replace(0, archive->entry(0));
        require(noop.has_value() && *noop == original,
                "Installed PFF no-op replacement changed bytes");
        auto payload =
            std::vector<std::uint8_t>(archive->entry(0).begin(), archive->entry(0).end());
        payload.insert(payload.end(), {1, 2, 3});
        check_replacement(*archive, 0, payload);
        check_replacement(*archive, archive->entries().size() - 1, {});
        ++files;
        entries += archive->entries().size();
    }
    require(files == 21 && entries == 809, "Installed PFF inventory changed");
    std::cout << root << ": " << files << " PFF files, " << entries
              << " PICT entries, exact no-op roundtrips and size changes pass\n";
}
}

int main(int argc, char** argv) {
    try {
        parser_checks();
        for (int at = 1; at < argc; ++at) {
            inventory(argv[at]);
        }
    } catch (const std::exception& error) {
        std::cerr << "PFF test failed: " << error.what() << '\n';
        return 1;
    }
}
