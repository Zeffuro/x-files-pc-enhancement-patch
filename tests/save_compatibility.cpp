#include "saves/compatibility.h"
#include "dispatch.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
constexpr unsigned directory = 64;
constexpr unsigned variable_class = directory + 101;
constexpr unsigned tree = 512, first_leaf = 768, last_leaf = 1024;
constexpr unsigned record = 1536, name = 1600;

void put(Bytes& bytes, unsigned offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) {
        bytes.at(offset + width - i - 1) = static_cast<unsigned char>(value);
        value >>= 8;
    }
}

Bytes fixture(bool current, unsigned version = 9) {
    Bytes bytes(2048);
    put(bytes, 0, 5);
    put(bytes, 8, directory);
    put(bytes, 12, 3);
    put(bytes, 20, 0x501);
    put(bytes, directory + 2, 1);
    put(bytes, directory + 6, 3, 2);
    put(bytes, directory + 16, 2, 2);
    bytes[directory + 18] = 1;
    put(bytes, directory + 19, 1);
    put(bytes, directory + 58, 1);
    put(bytes, directory + 62, 0x46);
    put(bytes, directory + 86, 0x46);
    put(bytes, variable_class, 1);
    put(bytes, variable_class + 4, 0x53);
    put(bytes, variable_class + 8, 2);
    put(bytes, variable_class + 24, tree);
    put(bytes, variable_class + 28, 0x53);
    bytes[tree] = 0x40;
    put(bytes, tree + 2, 1);
    put(bytes, tree + 6, 2, 2);
    put(bytes, tree + 8, first_leaf);
    put(bytes, tree + 12, last_leaf);
    for (const auto leaf : {first_leaf, last_leaf}) {
        bytes[leaf] = 0x80;
        put(bytes, leaf + 2, 1);
        put(bytes, leaf + 6, 1, 2);
        put(bytes, leaf + 8, record);
    }
    put(bytes, first_leaf + 12, 0x100);
    put(bytes, last_leaf + 12, current ? 0x14d6 : 0x14c6);
    bytes[record] = 0xc0;
    put(bytes, record + 2, 1);
    put(bytes, record + 6, name);
    put(bytes, record + 10, 16);
    put(bytes, record + 14, version);
    bytes[record + 23] = 0x81;
    const std::string_view label("XFilesDbVersion", 16);
    std::copy(label.begin(), label.end(), bytes.begin() + name);
    return bytes;
}

void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1) {
            for (int i = 1; i < argc; ++i) {
                const auto error = saves::load_compatibility_error(argv[i], true);
                std::wcout << argv[i] << L": ";
                std::cout << (error ? error : "Compatible database") << '\n';
            }
            return 0;
        }
        const auto path = std::filesystem::temp_directory_path() /
                          (L"xfiles-save-compatibility-" + std::to_wstring(GetCurrentProcessId()) +
                           L"-" + std::to_wstring(GetTickCount64()) + L".x");
        const auto check = [&](const Bytes& bytes, bool modern, bool accepted, const char* reason) {
            write(path, bytes);
            const auto error = saves::load_compatibility_error(path, modern);
            test::require((error == nullptr) == accepted, reason);
            std::ifstream input(path, std::ios::binary);
            const Bytes after{std::istreambuf_iterator<char>(input), {}};
            test::require(after == bytes, "Compatibility inspection changed the saved game");
            return error;
        };
        check(fixture(true), true, true, "Current save was rejected");
        check(fixture(true, 10), true, true, "Native-compatible later database was rejected");
        const auto old = fixture(false);
        const auto reason = check(old, true, false, "Old CD save reached the later native loader");
        test::require(std::string_view(reason).find("older game database") !=
                          std::string_view::npos,
                      "Old saves lack an explanatory compatibility error");
        check(old, false, true, "Original CD loader compatibility was changed");
        check(fixture(true, 8), true, false, "Database version below native minimum was accepted");
        check(fixture(true, 0xffffffff), true, false, "Signed negative version was accepted");
        for (const auto size : {0u, 23u, 24u, 100u, 800u, 1540u, 1610u}) {
            auto bytes = fixture(true);
            bytes.resize(size);
            check(bytes, true, false, "Truncated save was accepted");
        }
        for (const auto offset : {8u, variable_class + 24, tree + 8, last_leaf + 8, record + 6}) {
            auto bytes = fixture(true);
            put(bytes, offset, 0xfffffff0);
            check(bytes, true, false, "Out-of-range save pointer was accepted");
        }
        auto bytes = fixture(true);
        put(bytes, tree + 8, tree);
        check(bytes, true, false, "Cyclic index was accepted");
        bytes = fixture(true);
        put(bytes, tree + 12, first_leaf);
        check(bytes, true, false, "Aliased index node was accepted");
        bytes = fixture(true);
        put(bytes, variable_class + 8, 1);
        check(bytes, true, false, "Incomplete variable index was accepted");
        bytes = fixture(true);
        put(bytes, last_leaf + 12, 0x100);
        check(bytes, true, false, "Duplicate variable ID was accepted");
        bytes = fixture(true);
        put(bytes, first_leaf + 6, 33, 2);
        check(bytes, true, false, "Oversized index node was accepted");
        bytes = fixture(true);
        bytes[name] = 'Y';
        check(bytes, true, false, "Wrong version-variable name was accepted");
        bytes = fixture(true);
        bytes[record + 23] = 2;
        check(bytes, true, false, "Non-integer database version was accepted");
        bytes = fixture(false);
        check(bytes, true, false, "Unindexed version-like payload bypassed the check");
        bytes = fixture(true);
        put(bytes, record + 14, 10);
        Bytes converted;
        for (const auto byte : bytes) {
            if (byte == '\n') {
                converted.push_back('\r');
            }
            converted.push_back(byte);
        }
        check(converted, true, false, "Text-converted binary save was accepted");
        std::filesystem::remove(path);
        std::cout
            << "Save database compatibility preserves old CD loading and rejects unsafe loads.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
