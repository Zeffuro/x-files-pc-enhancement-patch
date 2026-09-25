#include "staging.h"
#include "configuration.h"
#include "ddraw_version.h"
#include "ffmpeg_files.h"

#include <stdexcept>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

namespace {

fs::path own_directory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size == buffer.size()) {
        throw std::runtime_error("Cannot locate the patch executable.");
    }
    return fs::path(buffer.data()).parent_path();
}

}

StagedGame stage_game(const fs::path& source, const fs::path& destination, const fs::path& media,
                      DisplayMode mode, bool preserve_settings) {
    const auto game = fs::canonical(source);
    const auto executable = game / L"XFiles.exe";
    const auto identity = identify(executable);
    if (!identity.edition) {
        throw std::runtime_error("Unsupported executable; no game was launched.");
    }

    const auto package = own_directory();
    if (sha256(package / L"ddraw.dll") != ddraw_sha256 ||
        sha256(package / L"defaults" / L"ddraw.ini") != ddraw_config_sha256) {
        throw std::runtime_error("The DirectDraw files do not match this build.");
    }
    if (!fs::is_regular_file(package / L"QuickTime.qts")) {
        throw std::runtime_error("The patch package is incomplete: QuickTime.qts is missing.");
    }

    std::vector<fs::path> assets;
    for (const wchar_t* name :
         {L"XFILES.HDB", L"XFILES.GAM", L"X.PFF", L"DLG.TTR", L"HCD.TTR", L"JRN.TTR", L"PHN.TTR",
          L"XFILESC.DLL", L"XFILESE.DLL", L"XFILESS.DLL", L"XFILEST.DLL", L"NAV1.NMV", L"NAVM.NMV",
          L"X7.PFF"}) {
        fs::path found;
        for (const auto& layer : {game, media, media / L"MININST", media / L"MEDINST"}) {
            if (fs::is_regular_file(layer / name)) {
                found = layer / name;
                break;
            }
        }
        if (found.empty()) {
            throw std::runtime_error("Missing game file: " + fs::path(name).string() +
                                     ". Select the complete game/disc data folder.");
        }
        assets.push_back(found);
    }
    for (const auto* name : ffmpeg_files) {
        if (!fs::is_regular_file(package / name)) {
            throw std::runtime_error("The patch package is incomplete: " + fs::path(name).string());
        }
    }

    const auto output = fs::absolute(destination).lexically_normal();
    if (!fs::create_directory(output)) {
        throw std::runtime_error("The destination must be a new folder.");
    }

    struct IncompleteStage {
        fs::path path;
        bool complete = false;

        ~IncompleteStage() {
            if (!complete) {
                std::error_code ignored;
                fs::remove_all(path, ignored);
            }
        }
    } stage{output};

    fs::copy_file(executable, output / L"XFiles.exe");
    if (sha256(output / L"XFiles.exe") != identity.sha256) {
        throw std::runtime_error("Staged executable hash differs from source.");
    }

    for (const auto& asset : assets) {
        fs::copy_file(asset, output / asset.filename());
    }
    for (const wchar_t* name :
         {L"XFilesPlay.exe", L"QuickTime.qts", L"ddraw.dll", L"cnc-ddraw.LICENSE", L"LICENSE",
          L"THIRD_PARTY.md", L"zlib.LICENSE", L"README.md"}) {
        fs::copy_file(package / name, output / name);
    }
    for (const auto* name : ffmpeg_files) {
        fs::copy_file(package / name, output / name);
    }
    if (sha256(output / L"ddraw.dll") != ddraw_sha256) {
        throw std::runtime_error("Staged DirectDraw hash differs from this build.");
    }
    fs::create_directory(output / L"docs");
    for (const auto* name : {L"controls.md", L"building.md"}) {
        fs::copy_file(package / L"docs" / name, output / L"docs" / name);
    }
    fs::create_directory(output / L"defaults");
    for (const auto* name : {L"ddraw.ini", L"patch.ini"}) {
        fs::copy_file(package / L"defaults" / name, output / L"defaults" / name);
    }
    if (preserve_settings && fs::is_regular_file(game / L"patch.ini")) {
        fs::copy_file(game / L"patch.ini", output / L"patch.ini");
    }
    initialize_configuration(output);
    if (mode == DisplayMode::Windowed &&
        !WritePrivateProfileStringW(L"ddraw", L"fullscreen", L"false",
                                    (output / L"ddraw.ini").c_str())) {
        throw std::runtime_error("Cannot write windowed display settings.");
    }

    stage.complete = true;
    return {output, identity};
}
