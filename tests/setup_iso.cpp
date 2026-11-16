#include "setup/iso.h"
#include "setup/shortcuts.h"
#include "dispatch.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <fstream>
#include <algorithm>
#include <iostream>

void verify_disc_import(const std::filesystem::path& base) {
    using test::require;
    std::vector<unsigned char> bytes(22 * 2048);
    const auto put = [&](std::size_t offset, unsigned value) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    const auto put_both = [&](std::size_t offset, unsigned value) {
        put(offset, value);
        for (unsigned i = 0; i < 4; ++i) {
            bytes[offset + 4 + i] = static_cast<unsigned char>(value >> (8 * (3 - i)));
        }
    };
    const auto descriptor = 16 * 2048;
    bytes[descriptor] = 1;
    std::copy_n("CD001", 5, bytes.begin() + descriptor + 1);
    bytes[descriptor + 6] = 1;
    bytes[descriptor + 129] = 8;
    bytes[descriptor + 156] = 34;
    put_both(descriptor + 158, 20);
    put_both(descriptor + 166, 2048);
    const auto record = 20 * 2048;
    bytes[record] = 44;
    put_both(record + 2, 21);
    put_both(record + 10, 5);
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
            put_both(record + 2, 1000);
        } else if (malformed == 1) {
            bytes[record + 33] = '/';
        } else {
            bytes[record + 25] = 2;
            put_both(record + 2, 20);
            put_both(record + 10, 2048);
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

void verify_french_dvd_sources(const std::filesystem::path& image,
                               const std::filesystem::path& folder,
                               const std::filesystem::path& french_cd) {
    namespace fs = std::filesystem;
    using test::require;
    const auto catalog = media_catalog(MediaSetId::dvd_fr);
    const auto verify = [&](const MediaSource& source, bool cd_image = true) {
        require(source.set == MediaSetId::dvd_fr && source.files.size() == catalog.size(),
                "French DVD source did not produce its complete catalog");
        for (const auto& file : source.files) {
            const auto& expected = catalog.at(file.relative.generic_wstring());
            require(file.size == expected.size && file.checksum == expected.sha256,
                    "French DVD source selection changed its catalog identity");
            if (file.relative == L"xv/19668.xmv" || file.relative == L"xv/56001.xmv") {
                require(media_file_sha256(file) == expected.sha256,
                        "French title or teaser selected bytes do not match provenance");
            }
            if (file.relative == L"xv/56001.xmv") {
                require(cd_image ? file.offset && file.geometry.sector_size == 2352 : !file.offset,
                        "French DVD title did not retain its selected source representation");
            }
        }
    };
    verify(inspect_media(image, {true, french_cd}));
    verify(inspect_media(folder, {true, french_cd}));
    require(inspect_media(image).set == MediaSetId::dvd_en,
            "Default bilingual DVD import stopped selecting English");
    const auto primary = read_iso(image);
    const auto supplemental = read_iso(french_cd);
    const auto scratch = fs::temp_directory_path() /
                         (L"xfiles-french-source-" + std::to_wstring(GetCurrentProcessId()));
    require(fs::create_directory(scratch), "Cannot create French CD folder fixture");

    struct Cleanup {
        fs::path path;

        ~Cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{scratch};

    const auto french_catalog = media_catalog(MediaSetId::cd_fr);
    for (const auto* name : {L"xfiles.exe", L"xfiles.hdb", L"xfiles.gam", L"xfilesc.dll",
                             L"xfilese.dll", L"xfiless.dll", L"xfilest.dll", L"xv/56001.xmv"}) {
        const auto found =
            std::find_if(supplemental.begin(), supplemental.end(), [&](const MediaFile& file) {
                return media_relative(file.relative) == name &&
                       media_file_sha256(file) == french_catalog.at(name).sha256;
            });
        require(found != supplemental.end(), "Original French CD fixture file is missing");
        fs::create_directories((scratch / name).parent_path());
        copy_media_file(*found, scratch / name, [] { return true; });
    }
    auto folder_source = inspect_media(folder, {true, scratch});
    verify(folder_source, false);
    bool protected_source = false;
    try {
        validate_destination(folder_source, scratch / L"installed");
    } catch (const std::exception&) {
        protected_source = true;
    }
    require(protected_source, "French CD source folder accepted as an install destination");
    const auto reject = [&](const std::vector<MediaFile>& dvd, const std::vector<MediaFile>& cd) {
        bool rejected = false;
        try {
            french_dvd_files(dvd, cd);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid French DVD source combination was accepted");
    };
    auto missing = primary;
    std::erase_if(missing, [](const MediaFile& file) {
        return media_relative(file.relative) == L"xv/19668tst.xmv";
    });
    reject(missing, supplemental);
    missing = supplemental;
    std::erase_if(missing, [](const MediaFile& file) {
        return media_relative(file.relative) == L"xv/56001.xmv";
    });
    reject(primary, missing);
    for (auto file : primary) {
        if (media_relative(file.relative) == L"xv/56001.xmv") {
            file.relative = L"XV/56001.XMV";
            missing.push_back(std::move(file));
        }
    }
    reject(primary, missing);
    reject(supplemental, supplemental);
    reject(primary, primary);
    auto ambiguous = supplemental;
    ambiguous.insert(ambiguous.end(), primary.begin(), primary.end());
    reject(primary, ambiguous);
    std::cout << "French DVD image/folder, original CD title, missing, cross-locale and "
                 "ambiguous-source checks passed.\n";
}
