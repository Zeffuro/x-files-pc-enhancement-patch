#include "devtools/database/native.h"
#include "database_fixture.h"
#include "devtools/database/memory.h"
#include "game/layouts/database/cache.h"
#include "game/layouts/database/variable.h"
#include <windows.h>
#include <array>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
}

void verify() {
    using namespace native_game;

    struct Case {
        const Profile* profile;
        unsigned manager, classes, branch, objects;
    };

    const Case cases[] = {
        {&profile_cd_10012, 0x2b4670, 0x2624c0, 0x262930, 0x261958},
        {&profile_cd_10019, 0x2b7698, 0x264c48, 0x2650b8, 0x264018},
        {&profile_dvd_20000, 0x2b8490, 0x265bf0, 0x2660a8, 0x2650d0},
        {&profile_cd_10020, 0x2b8690, 0x265c50, 0x2660c0, 0x265020},
    };
    auto* image = static_cast<std::byte*>(
        VirtualAlloc(nullptr, 0x300000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(image, "Native database check failed: image");
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    const auto address = [&](unsigned offset) { return static_cast<unsigned>(base + offset); };
    const auto put = [&](unsigned offset, const auto& value) {
        std::memcpy(image + offset, &value, sizeof(value));
    };
    for (const auto& c : cases) {
        require(c.profile->database_manager == c.manager &&
                    c.profile->database_class_node == c.classes &&
                    c.profile->database_branch_node == c.branch &&
                    c.profile->database_object_node == c.objects,
                "Generated database profile differs from verified RVA matrix");
        std::memset(image, 0, 0x300000);
        put(c.profile->database_manager, address(0x10000));
        put(0x10000 + 4 + 0x18, address(0x11000));
        put(0x10000 + 0xd8 + 0x18, address(0x12000));
        DatabaseNode classes{};
        classes.object.vtable = address(c.profile->database_class_node);
        classes.count = 1;
        put(0x11000, classes);
        DatabaseClassEntry entry{};
        entry.class_id = 0x53;
        entry.index_count = 1;
        entry.indexes[0].cached_root = address(0x13000);
        put(0x11030, entry);
        DatabaseNode branch{};
        branch.object.vtable = address(c.profile->database_branch_node);
        branch.count = 3;
        put(0x13000, branch);
        put(0x13030, DatabaseBranchEntry{10, address(0x14000)});
        put(0x13038, DatabaseBranchEntry{11, address(0x13000)});
        put(0x13040, DatabaseBranchEntry{12, 1});
        DatabaseNode objects{};
        objects.object.vtable = address(c.profile->database_object_node);
        objects.count = 3;
        put(0x14000, objects);
        put(0x14030, DatabaseObjectEntry{123, address(0x15000), 20});
        put(0x1403c, DatabaseObjectEntry{124, address(0x15000), 21});
        put(0x14048, DatabaseObjectEntry{125, 0, 22});
        Variable variable{};
        variable.vtable = address(0x20000);
        variable.id = 123;
        variable.raw_value = -19;
        variable.type_flags = 1;
        put(0x15000, variable);
        put(0x15024, std::uint16_t{7});
        put(0x1501c, std::uint32_t{20});
        put(0x20008, address(0x21000));
        put(0x21000, std::array<std::uint8_t, 6>{0xb8, 0x53, 0, 0, 0, 0xc3});
        objects.count = 1;
        put(0x12000, objects);
        put(0x12030, DatabaseObjectEntry{126, address(0x16000), 23});
        variable.id = 126;
        put(0x16000, variable);
        std::vector<std::byte> before(image, image + 0x300000);
        const auto snapshot = devtools::inspect_database(image, *c.profile);
        require(snapshot.available && !snapshot.truncated,
                "Native database check failed: snapshot.available && !snapshot.truncated");
        require(snapshot.objects.size() == 2 && snapshot.skipped_nodes == 1,
                "Native database check failed: snapshot.objects.size() == 2 && "
                "snapshot.skipped_nodes == 1");
        require(snapshot.objects[0].id == 123 && snapshot.objects[0].class_id == 0x53,
                "Native database check failed: snapshot.objects[0].id == 123 && "
                "snapshot.objects[0].class_id == 0x53");
        require(snapshot.objects[0].refcount == 7 && !snapshot.objects[0].state_database,
                "Native database check failed: snapshot.objects[0].refcount == 7 && "
                "!snapshot.objects[0].state_database");
        require(snapshot.objects[0].description.find(L"-19") != std::wstring::npos,
                "Native database check failed: snapshot.objects[0].description.find(L\"-19\") != "
                "std::wstring::npos");
        require(
            snapshot.objects[0].bytes.size() == sizeof(Variable),
            "Native database check failed: snapshot.objects[0].bytes.size() == sizeof(Variable)");
        require(snapshot.objects[0].file_offset == 20u, "Stored HDB record offset missing");
        require(!snapshot.objects[1].file_offset, "State offset incorrectly linked to HDB");
        require(snapshot.objects[1].id == 126 && snapshot.objects[1].state_database,
                "Native database check failed: snapshot.objects[1].id == 126 && "
                "snapshot.objects[1].state_database");
        require(
            std::memcmp(before.data(), image, before.size()) == 0,
            "Native database check failed: std::memcmp(before.data(), image, before.size()) == 0");
        entry.index_count = 7;
        put(0x11030, entry);
        const auto invalid = devtools::inspect_database(image, *c.profile);
        require(invalid.available && invalid.objects.size() == 1 && invalid.skipped_nodes == 1,
                "Native database check failed: invalid.available && invalid.objects.size() == 1 && "
                "invalid.skipped_nodes == 1");
        put(c.profile->database_manager, 1u);
        require(!devtools::inspect_database(image, *c.profile).available,
                "Native database check failed: !devtools::inspect_database(image, "
                "*c.profile).available");
    }
    for (const auto missing : {&Profile::database_manager, &Profile::database_class_node,
                               &Profile::database_branch_node, &Profile::database_object_node}) {
        auto incomplete = profile_cd_10012;
        incomplete.*missing = 0;
        require(!devtools::inspect_database(image, incomplete).available,
                "Incomplete database profile was accepted");
    }
    Profile unknown{};
    require(!devtools::inspect_database(image, unknown).available,
            "Native database check failed: !devtools::inspect_database(image, unknown).available");
    require(!devtools::inspect_database(nullptr, profile_cd_10012).available,
            "Native database check failed: !devtools::inspect_database(nullptr, "
            "profile_cd_10012).available");
    VirtualFree(image, 0, MEM_RELEASE);
    for (const auto& c : cases) {
        DatabaseFixture fixture(*c.profile);
        const auto before = std::vector<std::byte>(fixture.image, fixture.image + 0x300000);
        const auto snapshot = fixture.snapshot();
        require(snapshot.available && snapshot.objects.size() == 8,
                "Typed fixture snapshot failed");
        require(snapshot.objects[0].bytes.size() == 0xa8 &&
                    snapshot.objects[0].relationships[1].id == 50,
                "Title trigger-list reference failed");
        require(snapshot.objects[1].description == L"Cook" &&
                    snapshot.objects[1].fields.find(L"Entry") != std::wstring::npos,
                "Native name strings failed");
        require(snapshot.objects[2].bytes.size() == 0x48 &&
                    snapshot.objects[2].description.find(L"-10,15") != std::wstring::npos,
                "Hotspot bounds failed");
        require(snapshot.objects[7].bytes.size() == 0x34 &&
                    snapshot.objects[7].relationships[1].id == 40,
                "Standard action reference failed");
        require(std::memcmp(before.data(), fixture.image, before.size()) == 0,
                "Typed snapshot changed native memory");
        fixture.put(0x24000, std::array<char, 32>{'N', 'e', 'w'});
        require(snapshot.objects[1].description == L"Cook", "Snapshot retained native string data");
        fixture.put(0x1422c, 1u);
        require(fixture.snapshot().objects[1].description == L"<unreadable>",
                "Unreadable name pointer accepted");
        fixture.put(0x1422c, fixture.address(0x24000));
        fixture.put(0x14230, 0x100000u);
        std::array<char, 1024> long_name{};
        long_name.fill('a');
        fixture.put(0x24000, long_name);
        require(fixture.snapshot().objects[1].description.find(L"unterminated") !=
                    std::wstring::npos,
                "Name read exceeded string bound");
        fixture.put(0x24000, std::array<unsigned char, 32>{0x81, 0});
        fixture.put(0x14230, 32u);
        require(fixture.snapshot().objects[1].description == L"<non-ASCII text>",
                "Non-ASCII name invented");
        fixture.put(0x14230, 0u);
        require(fixture.snapshot().objects[1].description == L"<unavailable>",
                "Empty descriptor accepted");
        std::uint32_t copied = 0;
        require(!devtools::database_copy(UINT32_MAX - 1, &copied, sizeof(copied)),
                "Overflowing memory span accepted");
        DWORD getter_protection = 0;
        require(
            VirtualProtect(fixture.image + 0x21000, 0x1000, PAGE_NOACCESS, &getter_protection) != 0,
            "Getter guard setup failed");
        require(fixture.snapshot().objects.empty(), "Protected class getter bytes accepted");
        require(VirtualProtect(fixture.image + 0x21000, 0x1000, getter_protection,
                               &getter_protection) != 0,
                "Getter protection restore failed");
        DWORD string_protection = 0;
        require(VirtualProtect(fixture.image + 0x24000, 0x1000, PAGE_READWRITE | PAGE_GUARD,
                               &string_protection) != 0,
                "String guard setup failed");
        fixture.put(0x14230, 32u);
        require(fixture.snapshot().objects[1].description == L"<unreadable>",
                "Protected name string accepted");
        MEMORY_BASIC_INFORMATION string_region{};
        require(VirtualQuery(fixture.image + 0x24000, &string_region, sizeof(string_region)) != 0 &&
                    (string_region.Protect & PAGE_GUARD),
                "Snapshot consumed native guard page");
        require(VirtualProtect(fixture.image + 0x24000, 0x1000, string_protection,
                               &string_protection) != 0,
                "String protection restore failed");
        PersistentObject partial{};
        partial.id = 10;
        partial.vtable = fixture.address(0x20000);
        fixture.put(0x2fefd8, partial);
        fixture.put(0x11034, fixture.address(0x2fefd8));
        DWORD protection = 0;
        require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, PAGE_NOACCESS, &protection) != 0,
                "Guard setup failed");
        require(fixture.snapshot().objects[0].fields == L"Typed fields unreadable.",
                "Partial object not rejected");
        fixture.put(0x11034, fixture.address(0x2fefd9));
        require(fixture.snapshot().objects.size() == 7, "Protected persistent header accepted");
    }
}

int main() {
    try {
        verify();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
