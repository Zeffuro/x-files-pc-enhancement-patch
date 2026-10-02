#include "saves/recent.h"
#include "saves/catalog.h"
#include "platform/copy_file.h"
#include "dispatch.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>

namespace fs = std::filesystem;

namespace {
using Bytes = std::vector<unsigned char>;

void put(Bytes& bytes, unsigned offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) {
        bytes.at(offset + width - i - 1) = static_cast<unsigned char>(value);
        value >>= 8;
    }
}

Bytes fixture(bool current = true, unsigned marker = 0) {
    constexpr unsigned directory = 64, variable_class = directory + 101;
    constexpr unsigned tree = 512, first_leaf = 768, last_leaf = 1024;
    constexpr unsigned record = 1536, name = 1600;
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
    put(bytes, record + 14, 9);
    bytes[record + 23] = 0x81;
    const std::string_view label("XFilesDbVersion", 16);
    std::copy(label.begin(), label.end(), bytes.begin() + name);
    put(bytes, 2000, marker);
    return bytes;
}

void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    test::require(bool(output), "Cannot write test fixture");
}

Bytes read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

void set_time(const fs::path& path, std::uint64_t ticks) {
    const auto file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    test::require(file != INVALID_HANDLE_VALUE, "Cannot open fixture date");
    const FILETIME time{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    const bool changed = SetFileTime(file, nullptr, nullptr, &time) != FALSE;
    CloseHandle(file);
    test::require(changed, "Cannot set fixture date");
}

template <typename F> void rejects(F call) {
    bool rejected = false;
    try {
        call();
    } catch (const std::exception&) {
        rejected = true;
    }
    test::require(rejected, "Failed publication was accepted");
}

HANDLE lock(const fs::path& path) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    test::require(handle != INVALID_HANDLE_VALUE, "Cannot lock publication fixture");
    return handle;
}
}

int main() {
    try {
        const auto root = fs::temp_directory_path() /
                          (L"xfiles-recent-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
        fs::create_directory(root);
        test::require(saves::read_autosaves(root).size() == saves::autosave_count &&
                          saves::read_quicksaves(root).empty() && !saves::newest_save(root, true) &&
                          !fs::exists(root / "saves"),
                      "Empty recent discovery changed disk");
        const auto prepared = root / "prepared.x";
        write(prepared, fixture());
        saves::write_slot(root, 600, L"Manual", prepared, {});
        const auto manual = saves::read_slot(root, 600);
        const auto quick = root / "saves" / "QUICKSAVE.x";
        write(quick, fixture(true, 100));
        set_time(quick, manual.saved_at - 10000000);
        const auto quick_bytes = read(quick);
        std::array<saves::Slot, 7> published{};
        for (unsigned i = 0; i < published.size(); ++i) {
            write(prepared, fixture(true, i + 1));
            published[i] = saves::write_autosave(root, prepared, {1, 1, {1, 2, 3, 4}});
            test::require(published[i].number == i % saves::autosave_count + 1 &&
                              published[i].readable && published[i].saved_at,
                          "Autosaves did not fill then rotate the oldest slot");
        }
        test::require(!fs::exists(published[0].file) && !fs::exists(published[1].file) &&
                          saves::read_autosaves(root).size() == 5 &&
                          saves::read_slot(root, 600).file == manual.file &&
                          read(quick) == quick_bytes,
                      "Rotation retained old generations or overwrote another save namespace");
        test::require(saves::newest_save(root, true)->file == published[6].file,
                      "Continue did not choose the newest autosave");
        test::require(saves::read_autosaves(root).front().file == published[6].file,
                      "Autosave browser ordering did not put the newest checkpoint first");
        set_time(published[2].file, published[6].saved_at + 1000000000);
        test::require(saves::newest_save(root, true)->file == published[6].file,
                      "Copied file time replaced authoritative slot metadata");

        const auto before = saves::read_autosaves(root);
        const auto locked = lock(published[2].file.parent_path() / "current");
        rejects([&] { saves::write_autosave(root, prepared, {}); });
        CloseHandle(locked);
        const auto after = saves::read_autosaves(root);
        for (unsigned i = 0; i < before.size(); ++i) {
            test::require(before[i].file == after[i].file && fs::exists(before[i].file),
                          "Failed autosave publication damaged a previous save");
        }
        std::ofstream(published[4].file, std::ios::binary | std::ios::trunc) << "damaged";
        const auto repaired = saves::write_autosave(root, prepared, {});
        test::require(repaired.number == published[4].number && repaired.readable,
                      "Unreadable autosave was not selected before a usable one");
        saves::delete_slot(root, repaired.number, saves::SlotKind::Autosave);
        test::require(saves::read_slot(root, 600).readable && read(quick) == quick_bytes,
                      "Autosave deletion crossed namespaces");

        saves::write_slot(root, 600, L"Newest manual", prepared, {});
        const auto last_manual = saves::read_slot(root, 600);
        test::require(saves::newest_save(root, true)->file == last_manual.file,
                      "Continue failed to scan the final manual slot");
        for (const auto name : {L"0", L"601", L"0600", L"600-extra"}) {
            const auto directory = root / L"saves" / L"slots" / name;
            fs::create_directory(directory);
            platform::copy_file(last_manual.file.parent_path() / L"current",
                                directory / L"current");
        }
        test::require(saves::newest_save(root, true)->file == last_manual.file,
                      "Noncanonical manual directories changed Continue discovery");
        const auto legacy = root / "saves" / "Legacy.x";
        write(legacy, fixture());
        set_time(legacy, last_manual.saved_at + 10000000);
        test::require(saves::newest_save(root, true)->file == legacy,
                      "Legacy save did not use its native file date");
        const auto incompatible = root / "saves" / "Older database.x";
        write(incompatible, fixture(false));
        set_time(incompatible, last_manual.saved_at + 20000000);
        test::require(saves::newest_save(root, true)->file == legacy &&
                          saves::newest_save(root, false)->file == incompatible &&
                          read(incompatible) == fixture(false),
                      "Continue converted or accepted a newer incompatible database");
        auto truncated = fixture();
        truncated.resize(24);
        const auto broken = root / "saves" / "Broken.x";
        write(broken, truncated);
        set_time(broken, last_manual.saved_at + 30000000);
        for (const auto name :
             {L"QUICKSAVE.pending.x", L"CHECKPOINT.LOAD.12.34.x", L"EXPORT.12.34.x",
              L"BROWSER.12.34.x", L"AUTOSAVE.pending.12.34.x"}) {
            write(root / "saves" / name, fixture());
            set_time(root / "saves" / name, last_manual.saved_at + 40000000);
        }
        test::require(saves::newest_save(root, true)->file == legacy,
                      "Corrupt save or an unpublished internal temporary reached Continue");
        const auto catalog = saves::read_catalog(root);
        test::require(std::none_of(catalog.entries.begin(), catalog.entries.end(),
                                   [](const saves::Entry& entry) {
                                       return entry.name.starts_with(L"EXPORT.") ||
                                              entry.name.starts_with(L"AUTOSAVE.");
                                   }),
                      "Internal saves were shown in the legacy catalog");
        const auto named_legacy = root / "saves" / "EXPORT.Notes.x";
        write(named_legacy, fixture());
        const auto named_catalog = saves::read_catalog(root);
        test::require(
            std::any_of(named_catalog.entries.begin(), named_catalog.entries.end(),
                        [&](const saves::Entry& entry) { return entry.path == named_legacy; }),
            "Legacy names resembling temporary prefixes were hidden");

        const auto quick_root = root / "quick-only";
        fs::create_directories(quick_root / "saves");
        const auto current = quick_root / "saves" / "QUICKSAVE.x";
        const auto previous = quick_root / "saves" / "QUICKSAVE.previous.x";
        write(current, fixture(true, 10));
        set_time(current, last_manual.saved_at - 100000000);
        test::require(saves::read_quicksaves(quick_root).front().saved_at ==
                              last_manual.saved_at - 100000000 &&
                          saves::newest_save(quick_root, true)->file == current,
                      "Old quick-save without metadata lost its native date fallback");
        saves::record_quicksave(quick_root);
        const auto first_quick = saves::read_quicksaves(quick_root).front();
        set_time(current, first_quick.saved_at + 1000000000);
        test::require(saves::newest_save(quick_root, true)->saved_at == first_quick.saved_at,
                      "Quick metadata was bypassed by loose catalog discovery");
        platform::copy_file(current, previous);
        const auto replacement = quick_root / "saves" / "replacement.pending";
        write(replacement, fixture(true, 11));
        test::require(MoveFileExW(replacement.c_str(), current.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                      "Cannot replace quick-save fixture");
        saves::record_quicksave(quick_root);
        const auto quicks = saves::read_quicksaves(quick_root);
        test::require(quicks.size() == 2 && quicks[0].saved_at > first_quick.saved_at &&
                          quicks[1].saved_at == first_quick.saved_at,
                      "Quick-save rotation lost the previous publication date");
        const auto metadata = quick_root / "saves" / "QUICKSAVE.current";
        const auto prior_metadata = read(metadata);
        const auto locked_metadata = lock(metadata);
        rejects([&] { saves::record_quicksave(quick_root); });
        CloseHandle(locked_metadata);
        test::require(read(metadata) == prior_metadata &&
                          saves::read_quicksaves(quick_root)[0].saved_at == quicks[0].saved_at &&
                          !fs::exists(quick_root / "saves" / "QUICKSAVE.metadata.pending"),
                      "Failed quick timestamp publication replaced metadata or retained debris");
        platform::copy_file(current, replacement);
        set_time(replacement, quicks[0].saved_at + 50000000);
        test::require(MoveFileExW(replacement.c_str(), current.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                      "Cannot publish unmatched quick-save fixture");
        test::require(saves::read_quicksaves(quick_root)[0].saved_at ==
                          quicks[0].saved_at + 50000000,
                      "Stale quick metadata was applied to another file publication");
        set_time(previous, quicks[1].saved_at);
        write(metadata, {});
        test::require(saves::newest_save(quick_root, true)->file == current,
                      "Unreadable quick metadata hid an otherwise valid quick-save");
        fs::remove_all(root);
        std::cout << "Rolling autosaves and Continue preserve publication dates and saved games.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
