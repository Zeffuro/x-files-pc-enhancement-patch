#include "shortcuts.h"

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <stdexcept>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

void write_shortcut(const fs::path& link, const fs::path& game) {
    if (fs::exists(link)) {
        throw std::runtime_error("A shortcut already exists at this location.");
    }
    ComPtr<IShellLinkW> shortcut;
    ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&shortcut))) ||
        FAILED(shortcut->SetPath((game / L"XFilesPlay.exe").c_str())) ||
        FAILED(shortcut->SetWorkingDirectory(game.c_str())) ||
        FAILED(shortcut->SetIconLocation((game / L"XFiles.exe").c_str(), 0)) ||
        FAILED(shortcut->SetDescription(L"Play The X-Files")) || FAILED(shortcut.As(&file)) ||
        FAILED(file->Save(link.c_str(), TRUE))) {
        throw std::runtime_error("Could not create a shortcut. The game is installed; "
                                 "you can run XFilesPlay.exe from its folder.");
    }
}

void create_shortcuts(const fs::path& game, bool desktop, bool start_menu) {
    const auto create = [&](REFKNOWNFOLDERID folder) {
        PWSTR location = nullptr;
        if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &location))) {
            throw std::runtime_error("Cannot locate your shortcut folder.");
        }
        const fs::path directory(location);
        CoTaskMemFree(location);
        auto link = directory / L"The X-Files.lnk";
        for (unsigned number = 2; fs::exists(link); ++number) {
            link = directory / (L"The X-Files (" + std::to_wstring(number) + L").lnk");
        }
        write_shortcut(link, game);
    };
    if (desktop) {
        create(FOLDERID_Desktop);
    }
    if (start_menu) {
        create(FOLDERID_Programs);
    }
}
