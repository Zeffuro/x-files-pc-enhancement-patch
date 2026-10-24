#include "setup/iso.h"
#include "setup/repack.h"
#include "identity.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Workspace {
    fs::path path = fs::temp_directory_path() /
                    (L"xfiles-repack-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64()));

    Workspace() {
        fs::create_directory(path);
    }

    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
}

int wmain(int argc, wchar_t** argv) {
    try {
        Workspace workspace;
        const auto installer = workspace.path / L"Setup.exe";
        std::ofstream(installer, std::ios::binary) << "unrelated installer";
        require(SetFileAttributesW(installer.c_str(), FILE_ATTRIBUTE_READONLY) != 0,
                "Cannot create read-only disc-file fixture");
        const MediaFile disc_file{installer, L"test.bin", fs::file_size(installer), {}, {}};
        const auto writable = workspace.path / L"writable.bin";
        copy_media_file(disc_file, writable, [] { return true; });
        require(!(GetFileAttributesW(writable.c_str()) & FILE_ATTRIBUTE_READONLY),
                "Imported disc file remained read-only and cannot be replaced during staging");
        copy_media_file(disc_file, writable, [] { return true; });
        require(sha256(installer) == sha256(writable) &&
                    (GetFileAttributesW(installer.c_str()) & FILE_ATTRIBUTE_READONLY),
                "Writable import changed the read-only source");
        require(SetFileAttributesW(installer.c_str(), FILE_ATTRIBUTE_NORMAL) != 0,
                "Cannot restore fixture attributes");
        require(russian_repack_folder(workspace.path).empty(),
                "Unrelated installer was treated as a Russian source");
        fs::resize_file(installer, 10933776);
        std::vector<MediaFile> files{{installer, L"Setup.exe", 10933776, {}, {}}};
        expand_russian_repack(files, workspace.path);
        require(files.size() == 1 && !fs::exists(workspace.path / L"packed") &&
                    russian_repack_folder(workspace.path).empty(),
                "Installer size alone accepted unknown game data");
        const auto catalog = media_catalog(MediaSetId::ru_fan);
        require(catalog.size() == 3031 &&
                    catalog.at(L"xfiles.exe").sha256 == native_game::build_cd_10012.sha256 &&
                    catalog.at(L"xfiles.hdb").sha256 ==
                        media_catalog(false).at(L"xfiles.hdb").sha256 &&
                    catalog.at(L"xfilest.dll").sha256 !=
                        media_catalog(false).at(L"xfilest.dll").sha256 &&
                    !catalog.contains(L"setup.exe") && !catalog.contains(L"autorun.exe"),
                "Russian catalog lost its translated resources or imported bundled software");
        for (int argument = 1; argument < argc; ++argument) {
            const auto source = inspect_media(argv[argument]);
            require(source.set == MediaSetId::ru_fan && source.files.size() == catalog.size(),
                    "Russian source did not select the complete catalog");
            require(identify(source.game / L"XFiles.exe").build == &native_game::build_cd_10012,
                    "Russian repack selected a different native ABI");
            const auto copied = workspace.path / L"verified.bin";
            std::uintmax_t bytes = 0;
            for (const auto& file : source.files) {
                copy_media_file(file, copied, [] { return true; });
                require(fs::file_size(copied) == file.size && sha256(copied) == file.checksum,
                        "Russian source file did not survive import with its expected hash");
                require(SetFileAttributesW(copied.c_str(), FILE_ATTRIBUTE_NORMAL) != 0,
                        "Cannot reuse the verification output after a read-only disc file");
                bytes += file.size;
            }
            std::cout << "Verified " << source.files.size() << " imported files, " << bytes
                      << " bytes.\n";
        }
        std::cout << "Russian repack identity, catalog and unknown-installer rejection passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
