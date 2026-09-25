#include "report.h"
#include "log_file.h"
#include "saves/header.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace diagnostics {
namespace {
struct Entry {
    std::string name, data;
    std::uint32_t crc = 0, offset = 0;
};

std::uint32_t crc32(std::string_view data) {
    std::uint32_t crc = 0xffffffff;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void number(std::string& data, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        data.push_back(static_cast<char>(value >> (i * 8)));
    }
}

std::string tail(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot read a diagnostic log");
    }
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("Cannot read diagnostic log size");
    }
    const auto size = std::min(static_cast<std::uintmax_t>(end), log_limit);
    stream.seekg(end - static_cast<std::streamoff>(size));
    std::string data(static_cast<std::size_t>(size), '\0');
    stream.read(data.data(), static_cast<std::streamsize>(size));
    data.resize(static_cast<std::size_t>(stream.gcount()));
    return data;
}
}

void create_report(const std::filesystem::path& directory, const std::filesystem::path& output,
                   std::string_view details, const std::filesystem::path& save) {
    if (details.size() > 64 * 1024) {
        throw std::runtime_error("Diagnostic summary is too large");
    }
    std::vector<Entry> entries{{"report.txt", std::string(details)}};
    if (!save.empty()) {
        if (!saves::supported_header(save) || std::filesystem::file_size(save) > 16 * 1024 * 1024) {
            throw std::runtime_error("Select an X-Files PC save smaller than 16 MiB");
        }
        std::ifstream input(save, std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (size < 0 || size > 16 * 1024 * 1024) {
            throw std::runtime_error("Cannot read the selected save");
        }
        std::string data(static_cast<std::size_t>(size), '\0');
        input.seekg(0);
        if (!input.read(data.data(), size)) {
            throw std::runtime_error("Cannot read the complete save");
        }
        entries.push_back({"reproduction.x", std::move(data)});
    }
    for (const auto name :
         {"launcher.log", "quicktime.log", "quicktime.log.previous", "desktop.log", "crash.log",
          "crash-quicktime.log", "crash-desktop.log"}) {
        const auto path = directory / name;
        if (std::filesystem::is_regular_file(path)) {
            entries.push_back({name, tail(path)});
        }
    }
    // Stored ZIP entries keep support archives readable without another runtime dependency.
    std::string zip;
    for (auto& entry : entries) {
        entry.crc = crc32(entry.data);
        entry.offset = static_cast<std::uint32_t>(zip.size());
        number(zip, 0x04034b50, 4);
        number(zip, 20, 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 33, 2);
        number(zip, entry.crc, 4);
        number(zip, static_cast<std::uint32_t>(entry.data.size()), 4);
        number(zip, static_cast<std::uint32_t>(entry.data.size()), 4);
        number(zip, static_cast<std::uint32_t>(entry.name.size()), 2);
        number(zip, 0, 2);
        zip += entry.name;
        zip += entry.data;
    }
    const auto central = static_cast<std::uint32_t>(zip.size());
    for (const auto& entry : entries) {
        number(zip, 0x02014b50, 4);
        number(zip, 20, 2);
        number(zip, 20, 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 33, 2);
        number(zip, entry.crc, 4);
        number(zip, static_cast<std::uint32_t>(entry.data.size()), 4);
        number(zip, static_cast<std::uint32_t>(entry.data.size()), 4);
        number(zip, static_cast<std::uint32_t>(entry.name.size()), 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 0, 2);
        number(zip, 0, 4);
        number(zip, entry.offset, 4);
        zip += entry.name;
    }
    const auto central_size = static_cast<std::uint32_t>(zip.size()) - central;
    number(zip, 0x06054b50, 4);
    number(zip, 0, 2);
    number(zip, 0, 2);
    number(zip, static_cast<std::uint32_t>(entries.size()), 2);
    number(zip, static_cast<std::uint32_t>(entries.size()), 2);
    number(zip, central_size, 4);
    number(zip, central, 4);
    number(zip, 0, 2);
    const auto file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(
            "Cannot create report. Choose a new filename in a writable folder.");
    }
    DWORD written = 0;
    const bool saved =
        WriteFile(file, zip.data(), static_cast<DWORD>(zip.size()), &written, nullptr) &&
        written == zip.size();
    const bool flushed = saved && FlushFileBuffers(file);
    CloseHandle(file);
    if (!flushed) {
        DeleteFileW(output.c_str());
        throw std::runtime_error("Cannot finish writing the troubleshooting report");
    }
}
}
