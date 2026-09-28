#include "slots.h"
#include "header.h"
#include "file_date.h"
#include "platform/copy_file.h"
#include <windows.h>
#include <array>
#include <fstream>
#include <stdexcept>

namespace saves {
namespace {
constexpr std::array<char, 8> magic{'X', 'F', 'S', 'L', 'O', 'T', '1', '\0'};

struct Metadata {
    std::uint64_t generation = 0;
    std::wstring name;
};

bool ordinary(const std::filesystem::path& path, bool directory) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags != INVALID_FILE_ATTRIBUTES && !(flags & FILE_ATTRIBUTE_REPARSE_POINT) &&
           bool(flags & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

std::filesystem::path folder(const std::filesystem::path& game, unsigned number, bool create) {
    if (!number || number > slots_per_page * slot_pages) {
        throw std::runtime_error("Invalid save slot");
    }
    auto path = game;
    for (const auto& part :
         {std::wstring(L"saves"), std::wstring(L"slots"), std::to_wstring(number)}) {
        path /= part;
        if (create) {
            std::filesystem::create_directory(path);
        }
        if (std::filesystem::exists(path) && !ordinary(path, true)) {
            throw std::runtime_error("The save slot folder is unavailable");
        }
    }
    return path;
}

Metadata read_metadata(const std::filesystem::path& path) {
    if (!ordinary(path, false) || std::filesystem::file_size(path) > 512) {
        throw std::runtime_error("Cannot read save slot details");
    }
    std::ifstream input(path, std::ios::binary);
    std::array<char, 8> signature{};
    Metadata result;
    std::uint32_t length = 0;
    input.read(signature.data(), signature.size());
    input.read(reinterpret_cast<char*>(&result.generation), sizeof(result.generation));
    input.read(reinterpret_cast<char*>(&length), sizeof(length));
    if (!input || signature != magic || !result.generation || length > 80) {
        throw std::runtime_error("Unrecognized save slot details");
    }
    result.name.resize(length);
    input.read(reinterpret_cast<char*>(result.name.data()), length * sizeof(wchar_t));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Incomplete save slot details");
    }
    return result;
}

std::filesystem::path generation_file(const std::filesystem::path& directory,
                                      std::uint64_t generation, const wchar_t* extension) {
    return directory / (std::to_wstring(generation) + extension);
}

void durable(const std::filesystem::path& path) {
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot finish writing the save slot");
    }
    const bool flushed = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!flushed) {
        throw std::runtime_error("Cannot finish writing the save slot");
    }
}

}

Slot read_slot(const std::filesystem::path& game, unsigned number) {
    Slot result;
    result.number = number;
    const auto directory = folder(game, number, false);
    const auto metadata = directory / L"current";
    result.occupied = std::filesystem::exists(metadata);
    if (!result.occupied) {
        return result;
    }
    try {
        const auto record = read_metadata(metadata);
        result.name = record.name;
        result.file = generation_file(directory, record.generation, L".x");
        result.thumbnail = generation_file(directory, record.generation, L".thumb");
        result.readable = ordinary(result.file, false) && supported_header(result.file);
        if (result.readable) {
            result.date = formatted_modified_date(result.file);
        }
    } catch (const std::exception&) {
        result.readable = false;
    }
    return result;
}

Thumbnail read_thumbnail(const std::filesystem::path& path) {
    Thumbnail result;
    if (path.empty() || !ordinary(path, false) ||
        std::filesystem::file_size(path) > 640 * 480 * 4 + 8) {
        return result;
    }
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(&result.width), sizeof(result.width));
    input.read(reinterpret_cast<char*>(&result.height), sizeof(result.height));
    if (!input || !result.width || !result.height || result.width > 640 || result.height > 480) {
        return {};
    }
    result.pixels.resize(result.width * result.height * 4);
    input.read(reinterpret_cast<char*>(result.pixels.data()), result.pixels.size());
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        return {};
    }
    return result;
}

void write_slot(const std::filesystem::path& game, unsigned number, const std::wstring& name,
                const std::filesystem::path& prepared_save, const Thumbnail& thumbnail) {
    if (name.size() > 80 || name.find_first_of(L"\r\n\t") != std::wstring::npos ||
        name.find(L'\0') != std::wstring::npos || !ordinary(prepared_save, false) ||
        !supported_header(prepared_save)) {
        throw std::runtime_error("Cannot save this slot with the supplied name or game data");
    }
    if ((!thumbnail.pixels.empty() && (!thumbnail.width || !thumbnail.height)) ||
        thumbnail.width > 640 || thumbnail.height > 480 ||
        thumbnail.pixels.size() !=
            static_cast<std::size_t>(thumbnail.width) * thumbnail.height * 4) {
        throw std::runtime_error("Invalid save thumbnail");
    }
    const auto directory = folder(game, number, true);
    const auto current = directory / L"current";
    if (std::filesystem::exists(current) && !ordinary(current, false)) {
        throw std::runtime_error("Cannot replace this save slot");
    }
    std::uint64_t old_generation = 0;
    if (std::filesystem::exists(current)) {
        try {
            old_generation = read_metadata(current).generation;
        } catch (const std::exception&) {
        }
    }
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    const auto generation =
        (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    const auto file = generation_file(directory, generation, L".x");
    const auto image = generation_file(directory, generation, L".thumb");
    const auto pending = generation_file(directory, generation, L".pending");
    if (std::filesystem::exists(file) || std::filesystem::exists(image) ||
        std::filesystem::exists(pending)) {
        throw std::runtime_error("Try saving again");
    }
    try {
        platform::copy_file(prepared_save, file);
        durable(file);
        if (!thumbnail.pixels.empty()) {
            std::ofstream output(image, std::ios::binary);
            output.write(reinterpret_cast<const char*>(&thumbnail.width), sizeof(thumbnail.width));
            output.write(reinterpret_cast<const char*>(&thumbnail.height),
                         sizeof(thumbnail.height));
            output.write(reinterpret_cast<const char*>(thumbnail.pixels.data()),
                         thumbnail.pixels.size());
            output.close();
            if (!output) {
                throw std::runtime_error("Cannot write the save thumbnail");
            }
            durable(image);
        }
        std::ofstream output(pending, std::ios::binary);
        const auto length = static_cast<std::uint32_t>(name.size());
        output.write(magic.data(), magic.size());
        output.write(reinterpret_cast<const char*>(&generation), sizeof(generation));
        output.write(reinterpret_cast<const char*>(&length), sizeof(length));
        output.write(reinterpret_cast<const char*>(name.data()), length * sizeof(wchar_t));
        output.close();
        if (!output) {
            throw std::runtime_error("Cannot write the save name");
        }
        durable(pending);
        if (!MoveFileExW(pending.c_str(), current.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error(
                "Cannot publish the new save. The previous save has been kept.");
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        std::filesystem::remove(image, ignored);
        std::filesystem::remove(file, ignored);
        throw;
    }
    if (old_generation && old_generation != generation) {
        std::error_code ignored;
        for (const auto extension : {L".x", L".thumb", L".preview"}) {
            const auto old = generation_file(directory, old_generation, extension);
            if (ordinary(old, false)) {
                std::filesystem::remove(old, ignored);
            }
        }
    }
}

void delete_slot(const std::filesystem::path& game, unsigned number) {
    const auto directory = folder(game, number, false);
    const auto current = directory / L"current";
    if (!std::filesystem::exists(current)) {
        return;
    }
    const auto record = read_metadata(current);
    const auto removed = generation_file(directory, record.generation, L".deleted");
    if (!MoveFileExW(current.c_str(), removed.c_str(), MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Cannot delete this save. It has been kept.");
    }
    // Unpublish first so interrupted cleanup never leaves a loadable partial save.
    std::error_code ignored;
    for (const auto extension : {L".x", L".thumb", L".preview", L".deleted"}) {
        const auto file = generation_file(directory, record.generation, extension);
        if (ordinary(file, false)) {
            std::filesystem::remove(file, ignored);
        }
    }
}
}
