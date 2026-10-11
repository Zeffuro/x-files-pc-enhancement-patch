#include "launcher/compatibility.h"
#include "ffmpeg_files.h"
#include "platform/handle.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    require(
        static_cast<bool>(output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))),
        "Cannot write fixture");
}

std::string dll_bytes() {
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = sizeof(dos);
    IMAGE_NT_HEADERS32 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt.FileHeader.Characteristics = IMAGE_FILE_DLL;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    return std::string(reinterpret_cast<const char*>(&dos), sizeof(dos)) +
           std::string(reinterpret_cast<const char*>(&nt), sizeof(nt));
}

std::map<fs::path, std::string> snapshot(const fs::path& root) {
    std::map<fs::path, std::string> result;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        result.emplace(entry.path().lexically_relative(root),
                       entry.is_regular_file() ? sha256(entry.path()) : "directory");
    }
    return result;
}

struct Fixture {
    fs::path root = fs::temp_directory_path() /
                    (L"xfiles-compatibility-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64()));
    fs::path game = root / L"Game \u00e9\u65e5";
    fs::path media = root / L"Media \u00e9\u65e5";
    Session session{media, true};
    Identity identity;
    InstallationCatalog catalog{{MediaSetId::cd_en, "English CD", 1201, false}, {}};

    Fixture() {
        require(fs::create_directories(game), "Cannot create fixture");
        fs::create_directory(media);
        for (const auto* name :
             {L"xfiles.exe", L"xfiles.hdb", L"xfiles.gam", L"xfilesc.dll", L"xfilese.dll",
              L"xfiless.dll", L"xfilest.dll", L"nav1.nmv", L"xv/late.xmv", L"xt/entry.xtx"}) {
            const std::string bytes = fs::path(name).generic_string() + " fixture bytes";
            write(game / name, bytes);
            catalog.files.emplace(name, MediaRecord{bytes.size(), sha256(game / name)});
        }
        identity = {sha256(game / L"xfiles.exe"), "CD", &native_game::build_cd_10012};
        for (const auto* name : {L"QuickTime.qts", L"ddraw.dll", L"XFilesMpeg.dll"}) {
            write(game / name, dll_bytes());
        }
        for (const auto* name : ffmpeg_files) {
            write(game / name, dll_bytes());
        }
        write(game / L"patch.ini", "[User]\nChoice=1\n");
        write(game / L"ddraw.ini", "[ddraw]\nfullscreen=false\n");
        write(game / L"preferences.ini", "[Game]\nVolume Dialog = 23\n");
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    CompatibilityReport check() const {
        const auto before = snapshot(root);
        const auto result = inspect_installation_files(game, identity, session, {&catalog, 1});
        require(snapshot(root) == before, "Preflight changed files or created a directory/lock");
        return result;
    }

    void move_to_media(const fs::path& name, const fs::path& layer = {}) {
        const auto destination = media / layer / name;
        fs::create_directories(destination.parent_path());
        fs::rename(game / name, destination);
    }
};

void clean_and_catalogs() {
    Fixture fixture;
    const auto good = fixture.check();
    require(good.problems.empty() && good.warnings.empty() && good.media_set == L"English CD" &&
                good.checked_files == fixture.catalog.files.size(),
            "Complete manual installation rejected or media coverage incomplete");
    for (const auto& set : media_sets()) {
        const auto files = media_catalog(set.id);
        require(files.size() >= 3031, "Packaged supported catalog missing entries");
        require(native_game::build_for_hash(files.at(L"xfiles.exe").sha256) != nullptr,
                "Catalog executable has no supported profile");
        fixture.catalog.set = set;
        const auto result = fixture.check();
        require(result.problems.empty() && result.checked_files == fixture.catalog.files.size(),
                "Catalog selection or full coverage failed for a supported language");
        require(result.media_set ==
                    std::wstring(set.name, set.name + std::char_traits<char>::length(set.name)),
                "Game language was inferred from executable or UI language");
    }
    const InstallationCatalog duplicates[] = {fixture.catalog, fixture.catalog};
    require(!inspect_installation_files(fixture.game, fixture.identity, fixture.session, duplicates)
                 .problems.empty(),
            "Ambiguous catalog identity accepted");
    auto other = fixture.catalog;
    other.set = {MediaSetId::cd_fr, "French CD", 1204, false};
    const auto altered = fixture.root / L"different-gam";
    write(altered, "different language GAM");
    other.files.at(L"xfiles.gam").sha256 = sha256(altered);
    other.files.at(L"xfiles.hdb").sha256 = std::string(64, 'a');
    fixture.catalog.set = {MediaSetId::cd_en, "English CD", 1201, false};
    const InstallationCatalog choices[] = {other, fixture.catalog};
    require(
        inspect_installation_files(fixture.game, fixture.identity, fixture.session, choices)
                .media_set == L"English CD",
        "Shared executable selected the first localized catalog instead of effective core files");
    write(fixture.game / L"xfiles.gam", "different language GAM");
    require(!inspect_installation_files(fixture.game, fixture.identity, fixture.session, choices)
                 .problems.empty(),
            "Mixed core selected a catalog sharing only the executable");
}

void media_checks() {
    {
        Fixture fixture;
        fixture.move_to_media(L"xfiles.hdb");
        fixture.move_to_media(L"nav1.nmv", L"MININST");
        fixture.move_to_media(L"xv/late.xmv", L"MEDINST");
        require(fixture.check().problems.empty(), "Runtime media layers or Unicode paths lost");
        write(fixture.game / L"xfiles.hdb", "broken overriding core");
        require(!fixture.check().problems.empty(), "Valid external core hid broken local override");
    }
    {
        Fixture fixture;
        fixture.move_to_media(L"xfiles.hdb");
        fs::create_directory(fixture.game / L"xfiles.hdb");
        require(!fixture.check().problems.empty(), "Directory override incorrectly fell through");
    }
    {
        Fixture fixture;
        fixture.move_to_media(L"xfilest.dll");
        require(!fixture.check().problems.empty(), "Resource DLL accepted only in external media");
    }
    {
        Fixture fixture;
        write(fixture.game / L"xfiles.gam", "different localized core");
        require(!fixture.check().problems.empty(),
                "Mixed language core accepted by executable match");
    }
    for (const bool empty : {false, true}) {
        Fixture fixture;
        if (empty) {
            write(fixture.game / L"xv/late.xmv", "");
        } else {
            fs::remove(fixture.game / L"xv/late.xmv");
        }
        const auto report = fixture.check();
        require(!report.problems.empty() && report.checked_files == fixture.catalog.files.size() &&
                    report.text().find(L"late.xmv") != std::wstring::npos,
                "Missing/empty late media not diagnosed or scan ended early");
    }
    {
        Fixture fixture;
        write(fixture.game / L"xv/late.xmv", "replacement or truncated movie");
        const auto report = fixture.check();
        require(report.problems.empty() && report.changed_sizes == 1 && report.warnings.size() == 1,
                "Replacement size falsely declared incompatible or silently verified");
    }
    {
        Fixture fixture;
        fixture.move_to_media(L"xv/late.xmv", L"MEDINST");
        write(fixture.media / L"MININST/xv/late.xmv", "");
        require(!fixture.check().problems.empty(), "Unreadable first media layer fell through");
    }
    {
        Fixture fixture;
        fixture.catalog.set = {MediaSetId::dvd_en, "English DVD", 1202, true};
        fixture.catalog.files.emplace(L"vob/teaser.vob", MediaRecord{8, std::string(64, 'a')});
        write(fixture.media / L"vob/teaser.vob", "MPEGdata");
        const auto external = fixture.check();
        require(external.problems.empty() && external.unavailable_dvd_files == 1 &&
                    external.warnings.size() == 1,
                "External VOB incorrectly proved native DVD availability or blocked fallback");
        write(fixture.game / L"vob/teaser.vob", "MPEGdata");
        const auto local = fixture.check();
        require(local.problems.empty() && local.warnings.empty() && local.dvd_files == 1,
                "Local DVD file incorrectly rejected");
    }
}

void settings_and_dependencies() {
    {
        Fixture fixture;
        fs::remove(fixture.game / L"patch.ini");
        write(fixture.game / L"defaults/patch.ini", "defaults");
        require(fixture.check().problems.empty() && !fs::exists(fixture.game / L"patch.ini"),
                "Read-only preflight initialized missing live config");
        fs::remove(fixture.game / L"defaults/patch.ini");
        require(!fixture.check().problems.empty(), "Missing live/default settings accepted");
        fs::create_directory(fixture.game / L"patch.ini");
        write(fixture.game / L"defaults/patch.ini", "defaults");
        require(!fixture.check().problems.empty(), "Invalid live config replaced by defaults");
    }
    {
        Fixture fixture;
        write(fixture.game / L"preferences.ini", "[Game]\nVolume = bad\n");
        require(!fixture.check().problems.empty() &&
                    !fs::exists(fixture.game / L"preferences.ini.lock"),
                "Malformed preferences accepted or lock file created");
        fs::remove(fixture.game / L"preferences.ini");
        require(!fixture.check().problems.empty(), "Missing portable preferences accepted");
        fixture.session.portable = false;
        require(fixture.check().problems.empty(), "Registry mode requires portable preferences");
    }
    std::vector<const wchar_t*> dependencies{L"QuickTime.qts", L"ddraw.dll", L"XFilesMpeg.dll"};
    dependencies.insert(dependencies.end(), std::begin(ffmpeg_files), std::end(ffmpeg_files));
    for (const auto* name : dependencies) {
        if (fs::path(name).extension() != L".dll" && fs::path(name).extension() != L".qts") {
            continue;
        }
        Fixture fixture;
        fs::remove(fixture.game / name);
        require(!fixture.check().problems.empty(), "Missing runtime DLL accepted");
    }
    for (const std::string& bytes :
         {std::string{}, std::string("not a DLL"), dll_bytes().substr(0, 80)}) {
        Fixture fixture;
        write(fixture.game / L"QuickTime.qts", bytes);
        require(!fixture.check().problems.empty(), "Empty or malformed DLL header accepted");
    }
    {
        Fixture fixture;
        auto bytes = dll_bytes();
        const WORD machine = IMAGE_FILE_MACHINE_AMD64;
        std::memcpy(bytes.data() + sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD), &machine,
                    sizeof(machine));
        write(fixture.game / L"ddraw.dll", bytes);
        require(!fixture.check().problems.empty(), "64-bit runtime DLL accepted");
    }
    {
        Fixture fixture;
        auto bytes = dll_bytes();
        const WORD size = 0xffff;
        std::memcpy(bytes.data() + sizeof(IMAGE_DOS_HEADER) +
                        offsetof(IMAGE_NT_HEADERS32, FileHeader) +
                        offsetof(IMAGE_FILE_HEADER, SizeOfOptionalHeader),
                    &size, sizeof(size));
        write(fixture.game / L"ddraw.dll", bytes);
        require(!fixture.check().problems.empty(),
                "Optional DLL header extent beyond EOF accepted");
    }
    {
        Fixture fixture;
        const auto before = snapshot(fixture.root);
        {
            Handle locked(CreateFileW((fixture.game / L"xv/late.xmv").c_str(), GENERIC_READ, 0,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            require(locked.get() != INVALID_HANDLE_VALUE, "Cannot lock unreadable media fixture");
            const auto report = inspect_installation_files(fixture.game, fixture.identity,
                                                           fixture.session, {&fixture.catalog, 1});
            require(!report.problems.empty(), "Sharing-denied media accepted");
        }
        require(snapshot(fixture.root) == before, "Unreadable-media rejection changed files");
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        for (const auto* command :
             {L"XFilesPlay.exe --check", L"\"C:\\Game Files\\XFilesPlay.exe\" \"--check\" ",
              L"XFilesPlay.exe\t --check  "}) {
            require(launcher_check_requested(command), "Quoted/whitespace check would start game");
        }
        require(!launcher_check_requested(L"\"C:\\Game Files\\XFilesPlay.exe\"  "),
                "No-argument launch incorrectly selected inspection");
        for (const auto* command : {L"XFilesPlay.exe --unknown", L"XFilesPlay.exe --check extra"}) {
            bool rejected = false;
            try {
                launcher_check_requested(command);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Unknown launcher arguments could start game");
        }
        clean_and_catalogs();
        media_checks();
        settings_and_dependencies();
        Fixture fixture;
        const auto before = snapshot(fixture.root);
        require(!inspect_installation(fixture.game).problems.empty(),
                "Unknown executable accepted");
        fs::remove(fixture.game / L"xfiles.exe");
        const auto missing = snapshot(fixture.root);
        require(!inspect_installation(fixture.game).problems.empty() &&
                    snapshot(fixture.root) == missing,
                "Missing executable rejection changed files");
        require(before.size() == missing.size() + 1, "Unexpected test mutation");
        for (int i = 1; i < argc; ++i) {
            const fs::path game(argv[i]);
            const auto report = inspect_installation(game);
            std::wcout << game.wstring() << L'\n' << report.text() << L'\n';
            require(report.problems.empty(), "Real installation failed compatibility check");
            require(report.checked_files >= 3031, "Real catalog coverage incomplete");
        }
        std::cout << "Read-only launcher compatibility checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
