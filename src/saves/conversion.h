#pragma once

#include "database.h"
#include "platform/copy_file.h"

#include <array>
#include <string_view>

namespace saves {
inline constexpr wchar_t conversion_warning[] =
    L"This older save needs experimental conversion for the DVD edition.\n\n"
    L"A temporary copy is ready. Your original file will be kept unchanged. "
    L"Missing PDA history and password counters use the newer game's defaults. "
    L"Some notes or later progress may differ. Save to a new file after testing.\n\n"
    L"Load the converted copy?";

namespace conversion_detail {
using namespace database;
inline constexpr std::array<std::string_view, 17> added_names{
    "PDANOTES_iFieldOfficeN3PageNumber",
    "PDANOTES_iCrimeLabN3PageNumber",
    "PDANOTES_bWentToHaulingYardFirst",
    " iScullyPasswordCount",
    " iFBIPasswordCount",
    "PDANOTES_bWentToWongBeforeFO",
    "PDANOTES_bWentToTarakanBeforeFO",
    "PDANOTES_bWentToCoronersBeforeFO",
    "PDANOTES_bWentToCoronersBeforeCrimeLab",
    "PDANOTES_bBeenToFON3",
    "PDANOTES_bBeenToCoronersN5",
    "XFilesDbVersion",
    "iFuture1JaneDoeHospFix",
    "iFuture2",
    "iFuture3",
    "iFuture4",
    "iFuture5"};
inline constexpr std::array<unsigned char, 17> added_types{1, 1, 2,    1, 1, 2, 2, 2, 2,
                                                           2, 2, 0x81, 1, 1, 1, 1, 1};

inline bool equal(std::span<const unsigned char> a, std::span<const unsigned char> b) {
    return std::ranges::equal(a, b);
}

inline void validate_pair(const Container& old, const Container& current) {
    for (const auto id : {0x46, 0x50, 0x53}) {
        require(old.classes.contains(id) && current.classes.contains(id));
    }
    require(current.classes.size() == 3 && old.classes.size() >= 9 &&
            current.classes.at(0x53).records.size() == 919 &&
            old.classes.at(0x53).records.size() == 902 &&
            current.classes.at(0x46).records.size() == 3115 &&
            old.classes.at(0x46).records.size() == 3115 &&
            old.classes.at(0x50).records.size() == 11);
    for (const auto id : {0x56, 0x57, 0x58, 0x5a, 0x5b, 0x5c}) {
        require(old.classes.contains(id) && old.classes.at(id).records.size() == 1);
    }
    require(old.classes.at(0x57).records.begin()->first == 0x14cb);
    unsigned legacy_id = 0x14cc;
    for (const auto id : {0x56, 0x58, 0x5a, 0x5b, 0x5c}) {
        require(old.classes.at(id).records.begin()->first == legacy_id++);
    }
    require(old.classes.at(0x50).records.contains(0x14d1));
    if (const auto photos = old.classes.find(0x59); photos != old.classes.end()) {
        require(photos->second.records.begin()->first >= 0x14d2);
    }
    for (const auto& [id, record] : old.classes.at(0x46).records) {
        require(current.classes.at(0x46).records.contains(id));
        const auto other = current.classes.at(0x46).records.at(id);
        require(equal(slice(old.bytes, std::uint64_t(record) + 6, 4),
                      slice(current.bytes, std::uint64_t(other) + 6, 4)) &&
                get(old.bytes, std::uint64_t(record) + 11, 1) ==
                    get(current.bytes, std::uint64_t(other) + 11, 1));
    }
    for (const auto& [id, record] : old.classes.at(0x53).records) {
        require(current.classes.at(0x53).records.contains(id));
        const auto other = current.classes.at(0x53).records.at(id);
        require(
            id < 0x14cb &&
            equal(old.blob(std::uint64_t(record) + 6), current.blob(std::uint64_t(other) + 6)) &&
            equal(slice(old.bytes, std::uint64_t(record) + 18, 6),
                  slice(current.bytes, std::uint64_t(other) + 18, 6)));
        if ((get(old.bytes, std::uint64_t(record) + 23, 1) & 0x7f) == 3) {
            require(
                old.classes.at(0x50).records.contains(get(old.bytes, std::uint64_t(record) + 14)));
        }
    }
    for (unsigned i = 0; i < added_names.size(); ++i) {
        require(current.classes.at(0x53).records.contains(0x14cb + i));
        const auto record = current.classes.at(0x53).records.at(0x14cb + i);
        const auto name = current.blob(std::uint64_t(record) + 6);
        const auto text = std::string_view(reinterpret_cast<const char*>(name.data()));
        require(text == added_names[i] && name.size() == text.size() + 1 &&
                get(current.bytes, std::uint64_t(record) + 14) == (i == 11 ? 9u : 0u) &&
                get(current.bytes, std::uint64_t(record) + 18) == 0x12b &&
                get(current.bytes, std::uint64_t(record) + 22, 1) == 0x27 &&
                get(current.bytes, std::uint64_t(record) + 23, 1) == added_types[i]);
    }
    const auto state = old.classes.at(0x57).records.begin()->second;
    unsigned field = 22;
    for (const auto id : {0x56, 0x58, 0x5a, 0x5b, 0x5c}) {
        require(get(old.bytes, std::uint64_t(state) + field) ==
                old.classes.at(id).records.begin()->first);
        field += 4;
    }
}
}

// Serialized fields agree across CD 1.00.12 and DVD, including the packed scene state.
inline database::Bytes convert_old_save(database::Bytes bytes, database::Bytes initial) {
    using namespace conversion_detail;
    Container old(std::move(bytes));
    const Container current(std::move(initial));
    validate_pair(old, current);
    auto next_id = (std::max)(get(old.bytes, 16), get(current.bytes, 16));
    std::map<std::uint32_t, std::uint32_t> moved;
    std::set<std::uint32_t> changed{0x53};
    std::size_t relocated_photos = 0;
    for (auto& [class_id, item] : old.classes) {
        // Photos are enumerated by object ID, so keep their relative order.
        const bool move_photos = class_id == 0x59 && item.records.begin()->first <= 0x14db;
        if (move_photos) {
            relocated_photos = item.records.size();
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> replacements;
        for (const auto& [id, record] : item.records) {
            if ((id >= 0x14cb && id <= 0x14db) || move_photos) {
                require(class_id == 0x50 || (class_id >= 0x56 && class_id <= 0x5c));
                require(next_id < 0x7fffffff);
                moved.emplace(id, next_id);
                replacements.emplace_back(next_id++, record);
                changed.insert(class_id);
            }
        }
        for (const auto& [id, replacement] : moved) {
            item.records.erase(id);
        }
        for (const auto& replacement : replacements) {
            item.records.insert(replacement);
        }
    }
    require(moved.size() == 7 + relocated_photos);
    const auto relocate = [&](std::uint64_t offset) {
        if (const auto found = moved.find(get(old.bytes, offset)); found != moved.end()) {
            put(old.bytes, offset, found->second);
        }
    };
    const auto state = old.classes.at(0x57).records.begin()->second;
    for (unsigned field = 22; field <= 38; field += 4) {
        relocate(std::uint64_t(state) + field);
    }
    for (const auto& [id, record] : old.classes.at(0x53).records) {
        if ((get(old.bytes, std::uint64_t(record) + 23, 1) & 0x7f) == 3) {
            relocate(std::uint64_t(record) + 14);
        }
    }
    for (unsigned id = 0x14cb; id <= 0x14db; ++id) {
        const auto original = current.classes.at(0x53).records.at(id);
        const auto record = allocate(old.bytes, 24);
        const auto data = slice(current.bytes, original, 24);
        std::copy(data.begin(), data.end(), old.bytes.begin() + record);
        const auto text = current.blob(std::uint64_t(original) + 6);
        const auto name = allocate(old.bytes, text.size());
        std::copy(text.begin(), text.end(), old.bytes.begin() + name);
        put(old.bytes, std::uint64_t(record) + 6, name);
        old.classes.at(0x53).records.emplace(id, record);
    }
    for (const auto id : changed) {
        rebuild_index(old.bytes, old.classes.at(id));
    }
    put(old.bytes, 16, next_id);
    Container verified(old.bytes);
    require(verified.classes.at(0x53).records.size() == 919);
    return std::move(old.bytes);
}

inline void write_converted_copy(const std::filesystem::path& source,
                                 const std::filesystem::path& initial,
                                 const std::filesystem::path& destination) {
    const auto bytes = convert_old_save(database::read(source), database::read(initial));
    // Refuse an existing destination, including the original path.
    platform::copy_file(source, destination);
    try {
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.close();
        if (!output) {
            throw std::runtime_error("Could not write the converted save copy.");
        }
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(destination, error);
        throw;
    }
}
}
