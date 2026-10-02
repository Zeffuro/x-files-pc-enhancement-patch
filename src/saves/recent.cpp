#include "recent.h"
#include "catalog.h"
#include "compatibility.h"
#include "file_date.h"
#include "header.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace saves {
namespace {
constexpr std::array<char, 8> quick_magic{'X', 'F', 'Q', 'U', 'I', 'C', 'K', '1'};
constexpr std::array<const wchar_t*, 2> quick_names{L"QUICKSAVE.x", L"QUICKSAVE.previous.x"};

bool ordinary(const std::filesystem::path& path, bool directory) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && !(flags & FILE_ATTRIBUTE_REPARSE_POINT) &&
           bool(flags & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

std::vector<unsigned> manual_numbers(const std::filesystem::path& game) {
    const auto directory = game / L"saves" / L"slots";
    std::vector<unsigned> result;
    if (!ordinary(game / L"saves", true) || !ordinary(directory, true)) {
        return result;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        if (name.empty() || name.size() > 3 ||
            !std::all_of(name.begin(), name.end(),
                         [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) {
            continue;
        }
        unsigned number = 0;
        for (const auto ch : name) {
            number = number * 10 + static_cast<unsigned>(ch - L'0');
        }
        if (number && number <= slots_per_page * slot_pages && name == std::to_wstring(number)) {
            result.push_back(number);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::uint64_t ticks(FILETIME time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

std::uint64_t modified(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)
               ? ticks(data.ftLastWriteTime)
               : 0;
}

struct QuickRecord {
    std::uint64_t saved_at = 0, bytes = 0, hash = 0, identity = 0, created = 0;
    std::uint32_t volume = 0, reserved = 0;
};

QuickRecord inspect_quick(const std::filesystem::path& path) {
    QuickRecord result;
    if (!ordinary(path, false)) {
        return result;
    }
    const auto handle = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return result;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid =
        GetFileInformationByHandle(handle, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    CloseHandle(handle);
    if (!valid || info.nFileSizeHigh || info.nFileSizeLow > 64 * 1024 * 1024) {
        return result;
    }
    result.bytes = info.nFileSizeLow;
    result.identity = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    result.volume = info.dwVolumeSerialNumber;
    result.created = ticks(info.ftCreationTime);
    result.saved_at = ticks(info.ftLastWriteTime);
    std::ifstream input(path, std::ios::binary);
    std::array<char, 4096> buffer{};
    result.hash = 14695981039346656037ull;
    std::uint64_t read = 0;
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        read += input.gcount();
        for (std::size_t i = 0; i < static_cast<std::size_t>(input.gcount()); ++i) {
            result.hash ^= static_cast<unsigned char>(buffer[i]);
            result.hash *= 1099511628211ull;
        }
    }
    return input.eof() && read == result.bytes ? result : QuickRecord{};
}

bool same_content(const QuickRecord& a, const QuickRecord& b) {
    return a.hash && b.hash && a.bytes == b.bytes && a.hash == b.hash;
}

bool same_file(const QuickRecord& a, const QuickRecord& b) {
    // Wine can report a changed creation time after an mtime-only edit.
    return same_content(a, b) && a.volume == b.volume && a.identity == b.identity;
}

std::array<QuickRecord, 2> quick_metadata(const std::filesystem::path& directory) {
    std::array<QuickRecord, 2> result{};
    const auto path = directory / L"QUICKSAVE.current";
    try {
        if (!ordinary(path, false) ||
            std::filesystem::file_size(path) != quick_magic.size() + sizeof(result)) {
            return result;
        }
        std::ifstream input(path, std::ios::binary);
        std::array<char, 8> magic{};
        input.read(magic.data(), magic.size());
        input.read(reinterpret_cast<char*>(result.data()), sizeof(result));
        return input && magic == quick_magic ? result : std::array<QuickRecord, 2>{};
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }
}

Slot loose_slot(const std::filesystem::path& path, std::wstring name) {
    Slot result;
    result.file = path;
    result.name = std::move(name);
    result.occupied = std::filesystem::exists(path);
    result.readable = ordinary(path, false) && supported_header(path);
    if (result.readable) {
        result.date = formatted_modified_date(path);
        result.saved_at = modified(path);
    }
    return result;
}
}

std::vector<Slot> read_autosaves(const std::filesystem::path& game) {
    std::vector<Slot> result;
    for (unsigned number = 1; number <= autosave_count; ++number) {
        try {
            result.push_back(read_slot(game, number, SlotKind::Autosave));
        } catch (const std::exception&) {
            Slot unavailable;
            unavailable.number = number;
            unavailable.occupied = true;
            result.push_back(std::move(unavailable));
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const Slot& a, const Slot& b) { return a.saved_at > b.saved_at; });
    return result;
}

std::vector<Slot> read_quicksaves(const std::filesystem::path& game) {
    const auto directory = game / L"saves";
    if (!ordinary(directory, true)) {
        return {};
    }
    const auto metadata = quick_metadata(directory);
    std::vector<Slot> result;
    for (std::size_t i = 0; i < quick_names.size(); ++i) {
        try {
            auto slot = loose_slot(directory / quick_names[i],
                                   i == 0 ? L"Quick-save" : L"Previous quick-save");
            if (slot.readable) {
                const auto identity = inspect_quick(slot.file);
                if (metadata[i].saved_at && same_file(identity, metadata[i])) {
                    slot.saved_at = metadata[i].saved_at;
                }
            }
            if (slot.occupied) {
                result.push_back(std::move(slot));
            }
        } catch (const std::filesystem::filesystem_error&) {
        }
    }
    return result;
}

void record_quicksave(const std::filesystem::path& game) {
    const auto directory = game / L"saves";
    if (!ordinary(directory, true) || !ordinary(directory / quick_names[0], false) ||
        !supported_header(directory / quick_names[0])) {
        throw std::runtime_error("Cannot record the quick-save date");
    }
    const auto old = quick_metadata(directory);
    std::array<QuickRecord, 2> records{};
    for (std::size_t i = 0; i < records.size(); ++i) {
        records[i] = inspect_quick(directory / quick_names[i]);
    }
    if (!records[0].hash) {
        throw std::runtime_error("Cannot read the quick-save date");
    }
    for (const auto& record : old) {
        if (same_content(records[1], record) && record.saved_at) {
            records[1].saved_at = record.saved_at;
            break;
        }
    }
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    records[0].saved_at = std::max(ticks(time), old[0].saved_at + 1);
    const auto current = directory / L"QUICKSAVE.current";
    const auto pending = directory / L"QUICKSAVE.metadata.pending";
    if ((std::filesystem::exists(current) && !ordinary(current, false)) ||
        std::filesystem::exists(pending)) {
        throw std::runtime_error("Cannot publish the quick-save date");
    }
    try {
        std::ofstream output(pending, std::ios::binary);
        output.write(quick_magic.data(), quick_magic.size());
        output.write(reinterpret_cast<const char*>(records.data()), sizeof(records));
        output.close();
        if (!output) {
            throw std::runtime_error("Cannot write the quick-save date");
        }
        const auto file = CreateFileW(pending.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool flushed = file != INVALID_HANDLE_VALUE && FlushFileBuffers(file);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        if (!flushed || !MoveFileExW(pending.c_str(), current.c_str(),
                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot publish the quick-save date");
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        throw;
    }
}

Slot write_autosave(const std::filesystem::path& game, const std::filesystem::path& prepared_save,
                    const Thumbnail& thumbnail) {
    const auto slots = read_autosaves(game);
    const auto selected =
        std::min_element(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
            if (a.readable != b.readable) {
                return !a.readable;
            }
            return a.saved_at < b.saved_at;
        });
    write_slot(game, selected->number, L"Autosave " + std::to_wstring(selected->number),
               prepared_save, thumbnail, SlotKind::Autosave);
    return read_slot(game, selected->number, SlotKind::Autosave);
}

std::optional<Slot> newest_save(const std::filesystem::path& game, bool requires_current_database) {
    std::optional<Slot> result;
    const auto consider = [&](Slot slot) {
        if (slot.readable && !load_compatibility_error(slot.file, requires_current_database) &&
            (!result || slot.saved_at > result->saved_at)) {
            result = std::move(slot);
        }
    };
    try {
        for (const auto number : manual_numbers(game)) {
            try {
                consider(read_slot(game, number));
            } catch (const std::exception&) {
            }
        }
    } catch (const std::exception&) {
    }
    for (unsigned number = 1; number <= autosave_count; ++number) {
        try {
            consider(read_slot(game, number, SlotKind::Autosave));
        } catch (const std::exception&) {
        }
    }
    try {
        for (auto& slot : read_quicksaves(game)) {
            consider(std::move(slot));
        }
    } catch (const std::exception&) {
    }
    try {
        for (const auto& entry : read_catalog(game).entries) {
            const auto name = entry.path.filename().wstring();
            if (!_wcsicmp(name.c_str(), quick_names[0]) ||
                !_wcsicmp(name.c_str(), quick_names[1])) {
                continue;
            }
            consider(loose_slot(entry.path, entry.name));
        }
    } catch (const std::exception&) {
    }
    return result;
}
}
