#include "setup/iso.h"
#include "setup/shortcuts.h"
#include "dispatch.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <fstream>
#include <algorithm>

void verify_disc_import(const std::filesystem::path& base) {
    using test::require;
    std::vector<unsigned char> bytes(22 * 2048);
    const auto put = [&](std::size_t offset, unsigned value) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    const auto descriptor = 16 * 2048;
    bytes[descriptor] = 1;
    std::copy_n("CD001", 5, bytes.begin() + descriptor + 1);
    bytes[descriptor + 6] = 1;
    bytes[descriptor + 129] = 8;
    bytes[descriptor + 156] = 34;
    put(descriptor + 158, 20);
    put(descriptor + 166, 2048);
    const auto record = 20 * 2048;
    bytes[record] = 44;
    put(record + 2, 21);
    put(record + 10, 5);
    bytes[record + 32] = 10;
    std::copy_n("TEST.TXT;1", 10, bytes.begin() + record + 33);
    std::copy_n("hello", 5, bytes.begin() + 21 * 2048);
    const auto image = base / L"test.iso";
    const auto write = [&] {
        std::ofstream out(image, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    write();
    const auto files = read_iso(image);
    require(files.size() == 1 && files.front().relative == L"TEST.TXT",
            "ISO filename/version parsing failed");
    const auto output = base / L"imported.txt";
    copy_media_file(files.front(), output, [] { return true; });
    std::ifstream input(output);
    std::string text;
    input >> text;
    input.close();
    require(text == "hello", "ISO import copied the wrong extent");
    for (int malformed = 0; malformed < 3; ++malformed) {
        auto backup = bytes;
        if (malformed == 0) {
            put(record + 2, 1000);
        } else if (malformed == 1) {
            bytes[record + 33] = '/';
        } else {
            bytes[record + 25] = 2;
            put(record + 2, 20);
            put(record + 10, 2048);
        }
        write();
        bool rejected = false;
        try {
            read_iso(image);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Unsafe or malformed ISO accepted");
        bytes = std::move(backup);
    }
    std::filesystem::remove(image);
    std::filesystem::remove(output);

    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
            "Cannot initialize shortcut test");
    const auto link = base / L"test.lnk";
    write_shortcut(link, base);
    Microsoft::WRL::ComPtr<IShellLinkW> shortcut;
    Microsoft::WRL::ComPtr<IPersistFile> persistent;
    require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&shortcut))) &&
                SUCCEEDED(shortcut.As(&persistent)) &&
                SUCCEEDED(persistent->Load(link.c_str(), STGM_READ)),
            "Cannot read generated shortcut");
    wchar_t path[32768]{};
    require(SUCCEEDED(shortcut->GetPath(path, _countof(path), nullptr, SLGP_RAWPATH)) &&
                std::filesystem::path(path) == base / L"XFilesPlay.exe",
            "Shortcut bypasses launcher");
    int icon = -1;
    require(SUCCEEDED(shortcut->GetIconLocation(path, _countof(path), &icon)) && icon == 0 &&
                std::filesystem::path(path) == base / L"XFiles.exe",
            "Shortcut does not use original icon");
    persistent.Reset();
    shortcut.Reset();
    CoUninitialize();
    std::filesystem::remove(link);
}
