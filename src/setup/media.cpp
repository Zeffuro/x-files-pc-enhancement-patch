#include "iso.h"
#include "media.h"
#include "catalog.h"
#include "identity.h"
#include "launcher/staging.h"
#include "launcher/session.h"
#include "launcher/preferences.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return text;
}

bool linked(const fs::path& path) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool asset(const fs::path& path) {
    const auto extension = lower(path.extension().wstring());
    const auto name = lower(path.filename().wstring());
    for (const auto* core :
         {L"xfiles.exe", L"xfiles.hdb", L"xfiles.gam", L"dlg.ttr", L"hcd.ttr", L"jrn.ttr",
          L"phn.ttr", L"xfilesc.dll", L"xfilese.dll", L"xfiless.dll", L"xfilest.dll"}) {
        if (name == core) {
            return true;
        }
    }
    return extension == L".amv" || extension == L".dmv" || extension == L".hot" ||
           extension == L".mus" || extension == L".nmv" || extension == L".pff" ||
           extension == L".xmv" || extension == L".xtx";
}
}

MediaSource inspect_media(const fs::path& selected) {
    if (fs::is_regular_file(selected) && lower(selected.extension().wstring()) == L".iso") {
        return inspect_dvd_iso(selected);
    }
    if (!fs::is_directory(selected)) {
        throw std::runtime_error(
            "Choose a DVD ISO or a folder containing your English PC game "
            "files or all seven CD ISOs. PlayStation discs are not supported.");
    }
    const auto chosen = fs::canonical(selected);
    MediaSource result;
    for (const auto& root : {chosen, chosen / L"English"}) {
        for (const auto& game : {root, root / L"MININST"}) {
            if (fs::is_regular_file(game / L"XFiles.exe") &&
                identify(game / L"XFiles.exe").edition) {
                result.root = root;
                result.game = game;
                break;
            }
        }
        if (!result.game.empty()) {
            break;
        }
    }
    if (result.game.empty()) {
        return inspect_iso_folder(chosen);
    }
    std::map<std::wstring, MediaFile> files;
    const auto add = [&](const fs::path& path, const fs::path& relative) {
        if (linked(path) || !fs::is_regular_file(path)) {
            throw std::runtime_error("Cannot import linked or unreadable game file: " +
                                     path.string());
        }
        files.try_emplace(lower(relative.generic_wstring()),
                          MediaFile{path, relative, fs::file_size(path), {}, {}});
    };
    for (const auto& layer : {result.game, result.root, result.root / L"MEDINST"}) {
        if (!fs::is_directory(layer)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(layer)) {
            if (entry.is_regular_file() && asset(entry.path())) {
                add(entry.path(), entry.path().filename());
            }
        }
        for (const auto* name : {L"XG", L"XN", L"XS", L"XT", L"XV"}) {
            const auto directory = layer / name;
            if (!fs::is_directory(directory)) {
                continue;
            }
            if (linked(directory)) {
                throw std::runtime_error("Game media folders must not be links.");
            }
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (linked(entry.path())) {
                    throw std::runtime_error("Game media folders must not contain links.");
                }
                if (entry.is_regular_file() && asset(entry.path())) {
                    add(entry.path(), entry.path().lexically_relative(layer));
                }
            }
        }
    }
    const auto catalog =
        media_catalog(std::string(identify(result.game / L"XFiles.exe").edition) == "DVD");
    for (const auto& [name, record] : catalog) {
        const auto found = files.find(name);
        if (found == files.end() || found->second.size != record.size) {
            throw std::runtime_error("Missing or incorrect game file: " + fs::path(name).string() +
                                     ". Use a complete English DVD or extracted seven-CD set.");
        }
        auto file = found->second;
        file.checksum = record.sha256;
        result.bytes += file.size;
        result.files.push_back(std::move(file));
    }
    return result;
}

void install_media(const MediaSource& source, const fs::path& requested, bool windowed,
                   const std::function<bool(unsigned)>& progress) {
    const auto destination = fs::absolute(requested).lexically_normal();
    validate_destination(source, destination);

    struct ParentFolders {
        std::vector<fs::path> created;
        bool complete = false;

        ~ParentFolders() {
            if (!complete) {
                for (auto path = created.rbegin(); path != created.rend(); ++path) {
                    std::error_code ignored;
                    fs::remove(*path,
                               ignored); // Only remove empty folders created by this attempt.
                }
            }
        }
    } parents;

    std::vector<fs::path> missing;
    for (auto path = destination.parent_path(); !fs::exists(path); path = path.parent_path()) {
        missing.push_back(path);
    }
    for (auto path = missing.rbegin(); path != missing.rend(); ++path) {
        std::error_code error;
        if (fs::create_directory(*path, error)) {
            parents.created.push_back(*path);
        } else if (error || !fs::is_directory(*path)) {
            throw std::runtime_error("Cannot create the installation folder. Choose a writable "
                                     "folder, for example a Games folder in your user folder.");
        }
    }
    const auto temporary =
        destination.parent_path() / (L".xfiles-install-" + std::to_wstring(GetCurrentProcessId()) +
                                     L"-" + std::to_wstring(GetTickCount64()));
    const auto stage =
        stage_game(source.game, temporary, source.root,
                   windowed ? DisplayMode::Windowed : DisplayMode::Borderless, false);

    struct Transaction {
        fs::path path;
        bool complete = false;

        ~Transaction() {
            if (!complete) {
                std::error_code ignored;
                fs::remove_all(path, ignored);
            }
        }
    } transaction{temporary};

    std::ofstream(temporary / L"installing.txt")
        << "Setup is incomplete. Do not launch this folder.\n";
    std::uintmax_t copied = 0;
    for (const auto& file : source.files) {
        if (!progress(
                static_cast<unsigned>(copied * 100 / std::max<std::uintmax_t>(1, source.bytes)))) {
            throw std::runtime_error("Setup cancelled; "
                                     "your source files were not changed.");
        }
        const auto output = temporary / file.relative;
        fs::create_directories(output.parent_path());
        copy_media_file(file, output, [&] {
            return progress(
                static_cast<unsigned>(copied * 100 / std::max<std::uintmax_t>(1, source.bytes)));
        });
        if (fs::file_size(output) != file.size || sha256(output) != file.checksum) {
            throw std::runtime_error("Game file failed verification: " + file.relative.string() +
                                     ". Check the source media before retrying.");
        }
        copied += file.size;
    }
    if (!progress(99)) {
        throw std::runtime_error("Setup cancelled; your source files were not changed.");
    }
    fs::rename(temporary, destination);
    transaction.path = destination;
    stage_preferences(source.game, destination, false);
    save_session(destination, {destination, true});
    fs::remove(destination / L"installing.txt");
    transaction.complete = true;
    parents.complete = true;
    progress(100);
}
