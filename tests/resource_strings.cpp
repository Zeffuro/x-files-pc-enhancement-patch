#include "game/assets/resource_strings.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Slots = std::array<std::u16string, 16>;
constexpr std::size_t raw = 512;

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

void put16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    put16(bytes, at, static_cast<std::uint16_t>(value));
    put16(bytes, at + 2, static_cast<std::uint16_t>(value >> 16));
}

std::uint32_t get32(const Bytes& bytes, std::size_t at) {
    return std::uint32_t(bytes.at(at)) | (std::uint32_t(bytes.at(at + 1)) << 8) |
           (std::uint32_t(bytes.at(at + 2)) << 16) | (std::uint32_t(bytes.at(at + 3)) << 24);
}

struct Fixture {
    Bytes bytes;
    std::size_t optional = 152, directory = 0, section = 0, language = 64;
    std::vector<std::size_t> data, payload;

    Fixture(bool pe64, const std::vector<Slots>& bundles, std::uint32_t bundle_id = 2) {
        directory = optional + (pe64 ? 112 : 96);
        const std::size_t optional_size = pe64 ? 240 : 224;
        section = optional + optional_size;
        const auto count = bundles.size();
        std::size_t next = 80 + count * 8 + count * 16;
        for (const auto& bundle : bundles) {
            next = (next + 3) & ~std::size_t(3);
            payload.push_back(raw + next);
            for (const auto& text : bundle) {
                next += 2 + text.size() * 2;
            }
        }
        const auto size = static_cast<std::uint32_t>(next);
        bytes.resize(raw + size);
        put16(bytes, 0, 0x5a4d);
        put32(bytes, 0x3c, 128);
        put32(bytes, 128, 0x00004550);
        put16(bytes, 132, pe64 ? 0x8664 : 0x14c);
        put16(bytes, 134, 1);
        put16(bytes, 148, static_cast<std::uint16_t>(optional_size));
        put16(bytes, optional, pe64 ? 0x20b : 0x10b);
        put32(bytes, optional + 60, raw);
        put32(bytes, directory - 4, 16);
        put32(bytes, directory + 16, 0x1000);
        put32(bytes, directory + 20, size);
        put32(bytes, section + 8, size);
        put32(bytes, section + 12, 0x1000);
        put32(bytes, section + 16, size);
        put32(bytes, section + 20, raw);
        put16(bytes, raw + 14, 1);
        put32(bytes, raw + 16, 6);
        put32(bytes, raw + 20, 0x80000020);
        put16(bytes, raw + 32 + 14, 1);
        put32(bytes, raw + 48, bundle_id);
        put32(bytes, raw + 52, 0x80000040);
        put16(bytes, raw + language + 14, static_cast<std::uint16_t>(count));
        for (std::size_t index = 0; index < count; ++index) {
            const auto entry = raw + 80 + index * 8;
            const auto descriptor = raw + 80 + count * 8 + index * 16;
            data.push_back(descriptor);
            put32(bytes, entry, static_cast<std::uint32_t>(0x409 + index));
            put32(bytes, entry + 4, static_cast<std::uint32_t>(descriptor - raw));
            put32(bytes, descriptor, static_cast<std::uint32_t>(0x1000 + payload[index] - raw));
            put32(bytes, descriptor + 8, 1200);
            auto at = payload[index];
            for (const auto& text : bundles[index]) {
                put16(bytes, at, static_cast<std::uint16_t>(text.size()));
                at += 2;
                for (char16_t unit : text) {
                    put16(bytes, at, static_cast<std::uint16_t>(unit));
                    at += 2;
                }
            }
            put32(bytes, descriptor + 4, static_cast<std::uint32_t>(at - payload[index]));
        }
    }
};

void invalid(const Bytes& bytes) {
    const auto result = game_assets::parse_resource_strings(bytes);
    require(!result.valid && result.strings.empty() && !result.status.empty() &&
                result.file_size == bytes.size(),
            "Malformed resource accepted or partial strings retained");
}

void valid_checks() {
    Slots first{};
    first[0] = u"Hello";
    first[5] = u"\u00e9\U0001f600";
    first[15] = std::u16string{u'A', 0, u'B'};
    Slots second{};
    second[0] = u"Bonjour";
    for (bool pe64 : {false, true}) {
        const Fixture fixture(pe64, {first, second});
        const auto result = game_assets::parse_resource_strings(fixture.bytes);
        require(result.valid && result.file_size == fixture.bytes.size() &&
                    result.strings.size() == 4 && !result.status.empty(),
                "Valid PE32 or PE32+ multi-language resource failed");
        const auto& a = result.strings[0];
        require(a.id == 16 && a.language == 0x409 && a.code_page == 1200 &&
                    a.file_offset == fixture.payload[0] && a.text == L"Hello",
                "String ID, language, code page or file offset mismatch");
        require(result.strings[1].id == 16 && result.strings[1].language == 0x40a &&
                    result.strings[1].text == L"Bonjour" && result.strings[2].id == 21 &&
                    result.strings[2].text == L"\u00e9\U0001f600",
                "Language variants, slot identities or portable UTF-16 mismatch");
        require(result.strings[3].id == 31 && result.strings[3].text == std::wstring{L'A', 0, L'B'},
                "Counted embedded NUL was lost");
    }
    Fixture empty(false, {Slots{}});
    auto result = game_assets::parse_resource_strings(empty.bytes);
    require(result.valid && result.strings.empty() && !result.status.empty(),
            "Empty string bundle failed");
    put32(empty.bytes, raw + 16, 99);
    put32(empty.bytes, raw + 20, 0xffffffff);
    result = game_assets::parse_resource_strings(empty.bytes);
    require(result.valid && result.strings.empty(), "Unrelated resource type was decoded");
    Fixture high(false, {first}, 0x10000000);
    result = game_assets::parse_resource_strings(high.bytes);
    require(result.valid && result.strings.back().id == 0xffffffff,
            "Largest nonoverflowing bundle ID failed");
    Fixture zero(false, {first}, 1);
    require(game_assets::parse_resource_strings(zero.bytes).strings.front().id == 0,
            "First string ID zero failed");
}

void malformed_checks() {
    Slots strings{};
    strings[0] = u"Valid";
    const Fixture fixture(false, {strings, strings});
    for (std::size_t size = 0; size < fixture.bytes.size(); ++size) {
        invalid(Bytes(fixture.bytes.begin(),
                      fixture.bytes.begin() + static_cast<std::ptrdiff_t>(size)));
    }
    const std::vector<std::pair<std::size_t, std::uint32_t>> mutations = {
        {0x3c, 0xfffffff0},
        {128, 0},
        {fixture.optional + 60, 1},
        {fixture.directory - 4, 17},
        {fixture.directory + 16, 0xfffffff0},
        {fixture.directory + 20, 0xffffffff},
        {fixture.section + 12, 0xfffffff0},
        {fixture.section + 16, 0xffffffff},
        {fixture.section + 20, 100},
        {raw + 20, 0x80000000},
        {raw + 20, 0x80000021},
        {raw + 20, 0xfffffff0},
        {raw + 20, 32},
        {raw + 48, 0},
        {raw + 48, 0x10000001},
        {raw + 48, 0x80000000},
        {raw + 52, 0x80000000},
        {raw + 52, 64},
        {raw + 80, 0x80000000},
        {raw + 84, 0x80000040},
        {raw + 88, 0x409},
        {fixture.data[1], 0x2000},
        {fixture.data[1] + 4, 0},
        {fixture.data[1] + 4, 3},
        {fixture.data[1] + 4, 0xffffffff},
        {fixture.data[1] + 4, 2},
        {fixture.data[1] + 4, get32(fixture.bytes, fixture.data[1] + 4) + 2}};
    for (const auto& [at, value] : mutations) {
        auto bytes = fixture.bytes;
        put32(bytes, at, value);
        invalid(bytes);
    }
    for (const auto& [at, value] : std::vector<std::pair<std::size_t, std::uint16_t>>{
             {134, std::uint16_t{0}},
             {134, std::uint16_t{97}},
             {148, std::uint16_t{1}},
             {fixture.optional, std::uint16_t{0x107}},
             {raw + 14, std::uint16_t{0xffff}},
             {raw + 64 + 14, std::uint16_t{0xffff}},
             {fixture.payload[1], std::uint16_t{0xffff}},
             {fixture.payload[1] + 2, std::uint16_t{0xd800}},
             {fixture.payload[1] + 2, std::uint16_t{0xdc00}}}) {
        auto bytes = fixture.bytes;
        put16(bytes, at, value);
        invalid(bytes);
    }
    auto bytes = fixture.bytes;
    put32(bytes, fixture.directory + 16, 0);
    put32(bytes, fixture.directory + 20, 0);
    invalid(bytes);
    put32(bytes, fixture.directory - 4, 2);
    invalid(bytes);
    bytes = fixture.bytes;
    put16(bytes, 134, 2);
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(fixture.section), 40,
                bytes.begin() + static_cast<std::ptrdiff_t>(fixture.section + 40));
    invalid(bytes);
    put32(bytes, fixture.section + 40 + 20, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, fixture.section + 40 + 16, 0);
    invalid(bytes);
    bytes = fixture.bytes;
    put16(bytes, 134, 2);
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(fixture.section), 40,
                bytes.begin() + static_cast<std::ptrdiff_t>(fixture.section + 40));
    const auto old_size = bytes.size();
    bytes.resize(old_size * 2);
    put32(bytes, fixture.section + 40 + 20, static_cast<std::uint32_t>(old_size));
    invalid(bytes);
    put32(bytes, fixture.section + 40 + 12, 0x1001);
    invalid(bytes);
    bytes = fixture.bytes;
    put32(bytes, fixture.section + 16, get32(bytes, fixture.section + 16) - 2);
    invalid(bytes);
    bytes = fixture.bytes;
    put16(bytes, raw + 32 + 14, 2);
    put32(bytes, raw + 56, 2);
    put32(bytes, raw + 60, 0x80000040);
    invalid(bytes);
    bytes = fixture.bytes;
    put16(bytes, raw + 14, 2);
    put32(bytes, raw + 24, 6);
    put32(bytes, raw + 28, 0x80000020);
    invalid(bytes);
    Slots incomplete{};
    incomplete[15] = std::u16string{0xd800};
    invalid(Fixture(false, {strings, incomplete}).bytes);
    incomplete[15] = std::u16string{0xd800, u'A'};
    invalid(Fixture(false, {strings, incomplete}).bytes);
    bytes = fixture.bytes;
    put16(bytes, raw + 32 + 12, 1);
    put16(bytes, raw + 32 + 14, 0);
    put32(bytes, raw + 48, 0x80000000);
    invalid(bytes);
    bytes = fixture.bytes;
    put16(bytes, raw + 64 + 12, 1);
    put16(bytes, raw + 64 + 14, 1);
    put32(bytes, raw + 80, 0x80000000);
    invalid(bytes);
    std::uint32_t random = 0x12345678;
    for (int iteration = 0; iteration < 2048; ++iteration) {
        bytes = fixture.bytes;
        random = random * 1664525 + 1013904223;
        const auto at = std::size_t(random) % bytes.size();
        random = random * 1664525 + 1013904223;
        bytes[at] ^= static_cast<std::uint8_t>(random >> 24);
        const auto result = game_assets::parse_resource_strings(bytes);
        require(!result.status.empty() && (result.valid || result.strings.empty()),
                "Mutation produced missing status or partial failure output");
    }
}

void limit_checks() {
    using namespace game_assets;
    Slots strings{};
    strings[0] = u"A";
    auto fixture = Fixture(false, std::vector<Slots>(resource_string_limit / 16, strings));
    require(parse_resource_strings(fixture.bytes).valid, "String slot limit boundary failed");
    invalid(Fixture(false, std::vector<Slots>(resource_string_limit / 16 + 1, strings)).bytes);
    for (auto& text : strings) {
        text.assign(resource_unit_limit / 16, u'B');
    }
    require(parse_resource_strings(Fixture(false, {strings}).bytes).valid,
            "Aggregate UTF-16 unit limit boundary failed");
    strings[15].push_back(u'B');
    invalid(Fixture(false, {strings}).bytes);
    invalid(Bytes(resource_file_limit + 1));
    fixture = Fixture(false, {Slots{}});
    fixture.bytes.resize(resource_file_limit);
    require(parse_resource_strings(fixture.bytes).valid, "Input size limit boundary failed");
    fixture = Fixture(false, {Slots{}});
    fixture.bytes.resize(raw + 16 + 65536 * 8);
    const auto size = static_cast<std::uint32_t>(fixture.bytes.size() - raw);
    put32(fixture.bytes, fixture.section + 8, size);
    put32(fixture.bytes, fixture.section + 16, size);
    put32(fixture.bytes, fixture.directory + 20, size);
    put16(fixture.bytes, raw + 12, 1);
    put16(fixture.bytes, raw + 14, 65535);
    put32(fixture.bytes, raw + 16, 0x80000000);
    put32(fixture.bytes, raw + 24, 6);
    put32(fixture.bytes, raw + 28, 0x80000020);
    invalid(fixture.bytes);
}

void file_checks() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("xfiles-resource-" + std::to_string(stamp) + ".dll");
    require(!game_assets::load_resource_strings(path).valid, "Missing PE file accepted");
    Slots slots{};
    slots[0] = u"Read only";
    const Fixture fixture(true, {slots});
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixture.bytes.data()),
                     static_cast<std::streamsize>(fixture.bytes.size()));
        require(bool(output), "Fixture file write failed");
    }
    const auto result = game_assets::load_resource_strings(path);
    std::ifstream input(path, std::ios::binary);
    const Bytes after((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    std::error_code error;
    std::filesystem::remove(path, error);
    require(result.valid && result.strings.size() == 1 && result.strings[0].text == L"Read only" &&
                after == fixture.bytes,
            "File loader failed or modified its input");
}

void report(const std::filesystem::path& path) {
    const auto result = game_assets::load_resource_strings(path);
    if (!result.valid) {
        std::wcerr << path.wstring() << L": " << result.status << L'\n';
    }
    require(result.valid && !result.strings.empty(), "Installed resource decoding failed");
    std::set<std::uint32_t> languages;
    std::size_t characters = 0;
    for (const auto& string : result.strings) {
        languages.insert(string.language);
        characters += string.text.size();
        require(!string.text.empty() && string.file_offset + 2 < result.file_size,
                "Installed resource metadata failed");
    }
    std::cout << path << ": " << result.file_size << " bytes, " << result.strings.size()
              << " nonempty strings, " << characters << " wchar units, ID "
              << result.strings.front().id << ".." << result.strings.back().id << ", language";
    for (auto language : languages) {
        std::cout << ' ' << language;
    }
    std::cout << '\n';
}
}

int main(int argc, char** argv) {
    try {
        valid_checks();
        malformed_checks();
        limit_checks();
        file_checks();
        for (int at = 1; at < argc; ++at) {
            const std::filesystem::path path(argv[at]);
            if (std::filesystem::is_directory(path)) {
                for (const auto* name :
                     {"XFILESC.DLL", "XFILESE.DLL", "XFILESS.DLL", "XFILEST.DLL"}) {
                    report(path / name);
                }
            } else {
                report(path);
            }
        }
        std::cout << "Resource string parser checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Resource string parser test failed: " << error.what() << '\n';
        return 1;
    }
}
