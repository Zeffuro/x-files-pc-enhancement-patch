#include "compatibility.h"
#include "ffmpeg_files.h"
#include "platform/handle.h"
#include "preferences/store.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
constexpr const wchar_t* anchors[] = {L"xfiles.exe",  L"xfiles.hdb",  L"xfiles.gam", L"xfilesc.dll",
                                      L"xfilese.dll", L"xfiless.dll", L"xfilest.dll"};

std::wstring widen(const char* text) {
    const std::string value(text);
    return {value.begin(), value.end()};
}

fs::path effective_file(const fs::path& game, const Session& session, const fs::path& name) {
    for (const auto& layer :
         {game, session.media, session.media / L"MININST", session.media / L"MEDINST"}) {
        const auto path = layer / name;
        std::error_code error;
        if (fs::exists(path, error) || error) {
            return path;
        }
    }
    return game / name;
}

bool readable_file(const fs::path& path, std::uintmax_t& size, bool nonempty) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) {
        return false;
    }
    Handle file(CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER length{};
    if (file.get() == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.get(), &length) ||
        length.QuadPart < 0 || (nonempty && !length.QuadPart)) {
        return false;
    }
    size = static_cast<std::uintmax_t>(length.QuadPart);
    return true;
}

bool pe32_dll(const fs::path& path, std::uintmax_t size) {
    std::ifstream input(path, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    if (!input.read(reinterpret_cast<char*>(&dos), sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos))) {
        return false;
    }
    const auto offset = static_cast<std::uintmax_t>(dos.e_lfanew);
    if (offset > size || sizeof(IMAGE_NT_HEADERS32) > size - offset) {
        return false;
    }
    input.seekg(dos.e_lfanew);
    IMAGE_NT_HEADERS32 nt{};
    return input.read(reinterpret_cast<char*>(&nt), sizeof(nt)) &&
           nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) &&
           nt.FileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER32) &&
           nt.FileHeader.SizeOfOptionalHeader <=
               size - offset - offsetof(IMAGE_NT_HEADERS32, OptionalHeader) &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
}

void inspect_settings(CompatibilityReport& report, const fs::path& game) {
    for (const auto* name : {L"ddraw.ini", L"patch.ini"}) {
        auto path = game / name;
        std::error_code error;
        if (!fs::exists(path, error) && !error) {
            path = game / L"defaults" / name;
        }
        std::uintmax_t size = 0;
        if (!readable_file(path, size, false)) {
            report.problems.push_back(L"Settings file is missing or unreadable: " + path.wstring() +
                                      L". Restore it or extract the complete patch ZIP.");
        }
    }
    if (report.session.portable) {
        const auto path = game / L"preferences.ini";
        std::uintmax_t size = 0;
        try {
            if (!readable_file(path, size, false)) {
                throw std::runtime_error("Unreadable preferences");
            }
            preferences::read_file(path);
        } catch (const std::exception&) {
            report.problems.push_back(L"Game preferences are missing or invalid: " +
                                      path.wstring() + L". Restore preferences.ini from a backup.");
        }
    }
}

void inspect_patch(CompatibilityReport& report, const fs::path& game) {
    std::vector<const wchar_t*> names{L"QuickTime.qts", L"ddraw.dll", L"XFilesMpeg.dll"};
    names.insert(names.end(), std::begin(ffmpeg_files), std::end(ffmpeg_files));
    for (const auto* name : names) {
        if (fs::path(name).extension() != L".dll" && fs::path(name).extension() != L".qts") {
            continue;
        }
        const auto path = game / name;
        std::uintmax_t size = 0;
        if (!readable_file(path, size, true) || !pe32_dll(path, size)) {
            report.problems.push_back(L"Patch file is missing, unreadable or not a 32-bit DLL: " +
                                      path.wstring() + L". Extract the complete patch ZIP here.");
        }
    }
}

void list(std::wostringstream& text, const std::vector<std::wstring>& items) {
    for (std::size_t i = 0; i < std::min<std::size_t>(items.size(), 8); ++i) {
        text << L"\n- " << items[i];
    }
    if (items.size() > 8) {
        text << L"\n- " << items.size() - 8 << L" more files need attention.";
    }
}
}

CompatibilityReport inspect_installation_files(const fs::path& game, const Identity& identity,
                                               const Session& session,
                                               std::span<const InstallationCatalog> catalogs) {
    CompatibilityReport report;
    report.identity = identity;
    report.session = session;
    inspect_settings(report, game);
    inspect_patch(report, game);
    if (!identity.edition || !identity.build || !identity.build->profile) {
        report.problems.push_back(
            L"The game executable is not supported. Run Setup with a supported PC CD or DVD copy.");
        return report;
    }

    std::map<std::wstring, std::string> hashes;
    hashes.emplace(L"xfiles.exe", identity.sha256);
    for (const auto* name : anchors) {
        if (std::wstring_view(name) == L"xfiles.exe") {
            continue;
        }
        const bool local = fs::path(name).extension() == L".dll";
        const auto path = local ? game / name : effective_file(game, session, name);
        std::uintmax_t size = 0;
        try {
            if (!readable_file(path, size, true)) {
                throw std::runtime_error("Unreadable core file");
            }
            hashes.emplace(name, sha256(path));
        } catch (const std::exception&) {
            report.problems.push_back(L"Required game file is missing or unreadable: " +
                                      path.wstring() +
                                      L". Restore it from the same game release and language.");
        }
    }
    if (hashes.size() != std::size(anchors)) {
        return report;
    }
    const InstallationCatalog* selected = nullptr;
    for (const auto& catalog : catalogs) {
        const bool matches =
            std::all_of(std::begin(anchors), std::end(anchors), [&](const auto* name) {
                const auto found = catalog.files.find(name);
                return found != catalog.files.end() && found->second.sha256 == hashes.at(name);
            });
        if (matches) {
            if (selected) {
                report.problems.push_back(L"The game data matches multiple releases. Run Setup "
                                          L"again with one complete game set.");
                return report;
            }
            selected = &catalog;
        }
    }
    if (!selected) {
        report.problems.push_back(
            L"The executable, game data and language DLLs do not match a supported set. Run Setup "
            L"with one complete release and language.");
        return report;
    }
    report.media_set = widen(selected->set.name);
    for (const auto& [name, record] : selected->files) {
        ++report.checked_files;
        const bool dvd = name.starts_with(L"vob/");
        const bool local = dvd || fs::path(name).extension() == L".dll" || name == L"xfiles.exe";
        const auto path = local ? game / name : effective_file(game, session, name);
        std::uintmax_t size = 0;
        const bool available = readable_file(path, size, record.size != 0);
        if (dvd) {
            ++report.dvd_files;
            report.unavailable_dvd_files += !available;
        } else if (!available) {
            report.problems.push_back(L"Game media is missing, empty or unreadable: " +
                                      path.wstring() + L". Restore it from your game copy.");
        }
        if (available && size != record.size) {
            ++report.changed_sizes;
            report.warnings.push_back(L"File size differs from the original: " + path.wstring() +
                                      L". It may be a replacement or a damaged copy.");
        }
    }
    if (report.unavailable_dvd_files) {
        report.warnings.push_back(L"Some DVD MPEG files are not readable in the local vob folder. "
                                  L"External media paths do not supply these files. QuickTime "
                                  L"fallback remains available for supported scenes.");
    }
    return report;
}

CompatibilityReport inspect_installation(const fs::path& directory) {
    CompatibilityReport report;
    fs::path game;
    try {
        game = fs::canonical(directory);
    } catch (const std::exception&) {
        report.problems.push_back(L"The game folder cannot be read: " + directory.wstring());
        return report;
    }
    if (fs::exists(game / L"installing.txt")) {
        report.problems.push_back(L"Setup did not finish. Run Setup again using a new folder.");
        return report;
    }
    try {
        report.identity = identify(game / L"XFiles.exe");
    } catch (const std::exception&) {
        report.problems.push_back(L"The game executable is missing or unreadable: " +
                                  (game / L"XFiles.exe").wstring() +
                                  L". Put the patch in the installed game folder or run Setup.");
        return report;
    }
    if (!report.identity.edition) {
        report.problems.push_back(
            L"The game executable is not supported. Run Setup with a supported PC CD or DVD copy.");
        return report;
    }
    try {
        report.session = read_session(game);
    } catch (const std::exception&) {
        report.problems.push_back(
            L"Session settings or the configured media folder cannot be read. Restore session.ini "
            L"and portable preferences.ini, or run Setup again.");
        return report;
    }
    std::vector<InstallationCatalog> catalogs;
    for (const auto& set : media_sets()) {
        catalogs.push_back({set, media_catalog(set.id)});
    }
    return inspect_installation_files(game, report.identity, report.session, catalogs);
}

std::wstring CompatibilityReport::text() const {
    std::wostringstream output;
    output << (problems.empty() ? L"Installation checks passed."
                                : L"The installation needs attention.");
    if (!media_set.empty()) {
        output << L"\n\nGame files: " << media_set << L"\nExecutable: "
               << widen(identity.build->label.data()) << L"\nMedia paths checked: "
               << checked_files;
        if (dvd_files) {
            output << L"\nLocal DVD MPEG files available: " << dvd_files - unavailable_dvd_files
                   << L"/" << dvd_files;
        }
    }
    list(output, problems);
    if (!warnings.empty()) {
        output << L"\n\nWarnings (" << warnings.size() << L"):";
        list(output, warnings);
    }
    output << L"\n\nThese checks verify core game identities, required patch DLL headers, settings "
              L"and media file availability. Movie contents and full gameplay are not verified. No "
              L"files were changed.";
    return output.str();
}

int show_compatibility(const fs::path& directory) {
    const auto report = inspect_installation(directory);
    MessageBoxW(nullptr, report.text().c_str(), L"The X-Files installation check",
                MB_OK | (report.problems.empty() ? MB_ICONINFORMATION : MB_ICONERROR));
    return report.problems.empty() ? 0 : 1;
}

bool launcher_check_requested(std::wstring_view command_line) {
    const std::wstring text(command_line);
    int count = 0;
    auto arguments = CommandLineToArgvW(text.c_str(), &count);
    if (!arguments) {
        throw std::runtime_error("Cannot read launcher arguments.");
    }

    struct Cleanup {
        wchar_t** arguments;

        ~Cleanup() {
            LocalFree(arguments);
        }
    } cleanup{arguments};

    if (count == 1) {
        return false;
    }
    if (count == 2 && std::wstring_view(arguments[1]) == L"--check") {
        return true;
    }
    throw std::runtime_error("Unknown launcher option. Use XFilesPlay.exe with no arguments to "
                             "play, or --check to inspect the installation.");
}
