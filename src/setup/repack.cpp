#include "repack.h"
#include "catalog.h"
#include "disc_image.h"
#include "identity.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
constexpr std::uintmax_t installer_size = 10933776;
constexpr const char* installer_hash =
    "511a18c4aabc3976e12e3e38fb36046ce496cfc0261a2f9c5e86c5936e73dc28";

struct PackedFile {
    const wchar_t* name;
    std::uint32_t offset, size, continuation = 0;
};

constexpr PackedFile core[] = {
    {L"xfiles.exe", 1570930, 3360768},
    {L"xfilesc.dll", 257651, 22528},
    {L"xfilese.dll", 280184, 25088},
    {L"xfiless.dll", 305277, 28160},
    {L"xfilest.dll", 333442, 80896},
    {L"dlg.ttr", 1086063, 207172},
    {L"hcd.ttr", 1293240, 56884},
    {L"jrn.ttr", 1350129, 119184},
    {L"phn.ttr", 1469318, 82920},
    {L"xfiles.gam", 4931703, 154656},
    {L"xfiles.hdb", 5086364, 6042472, 62586},
};

bool linked(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
}

void expand_russian_repack(std::vector<MediaFile>& files, const fs::path& workspace) {
    std::optional<MediaFile> installer;
    for (const auto& file : files) {
        if (media_relative(file.relative) == L"setup.exe" && file.size == installer_size &&
            media_file_sha256(file) == installer_hash) {
            if (installer) {
                throw std::runtime_error("Multiple Russian repacks found. Select one disc image.");
            }
            installer = file;
        }
    }
    if (!installer) {
        return;
    }
    const auto packed = workspace / L"packed";
    fs::create_directory(packed);
    const auto archive = packed / L"installer.bin";
    copy_media_file(*installer, archive, [] { return true; });
    if (sha256(archive) != installer_hash) {
        throw std::runtime_error("The Russian repack installer changed during import.");
    }
    std::ifstream input(archive, std::ios::binary);
    const auto catalog = media_catalog(MediaSetId::ru_fan);
    std::vector<char> buffer(65536);
    for (const auto& file : core) {
        const auto path = packed / file.name;
        std::ofstream output(path, std::ios::binary);
        auto remaining = file.size;
        auto offset = file.offset;
        // This exact Gentee archive wraps the final database block to its first payload block.
        while (remaining) {
            const auto count =
                std::min<std::uint32_t>({remaining, static_cast<std::uint32_t>(buffer.size()),
                                         static_cast<std::uint32_t>(installer_size - offset)});
            input.seekg(offset);
            if (!count || !input.read(buffer.data(), count) ||
                !output.write(buffer.data(), count)) {
                throw std::runtime_error("Cannot extract the Russian repack game files.");
            }
            remaining -= count;
            offset += count;
            if (offset == installer_size) {
                offset = file.continuation;
            }
        }
        output.close();
        if (!output || fs::file_size(path) != file.size ||
            sha256(path) != catalog.at(file.name).sha256) {
            throw std::runtime_error("Russian repack game file failed verification.");
        }
        files.push_back({path, file.name, file.size, {}, {}});
    }
}

std::vector<MediaFile> russian_repack_folder(const fs::path& folder) {
    const auto installer = folder / L"Setup.exe";
    if (!fs::is_regular_file(installer) || fs::file_size(installer) != installer_size) {
        return {};
    }
    if (linked(installer)) {
        throw std::runtime_error("Game media files must not be links.");
    }
    if (sha256(installer) != installer_hash) {
        return {};
    }
    std::vector<MediaFile> files{{installer, L"setup.exe", installer_size, {}, {}}};
    for (const auto& [name, record] : media_catalog(MediaSetId::ru_fan)) {
        if (std::ranges::any_of(core, [&](const auto& file) { return name == file.name; })) {
            continue;
        }
        const auto path = folder / name;
        if (fs::is_regular_file(path)) {
            if (linked(path) || linked(path.parent_path())) {
                throw std::runtime_error("Game media folders and files must not be links.");
            }
            files.push_back({path, name, fs::file_size(path), {}, {}});
        }
    }
    return files;
}
