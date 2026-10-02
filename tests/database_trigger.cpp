#include "database_fixture.h"
#include <algorithm>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

const devtools::NativeDatabaseObject& trigger(const devtools::NativeDatabaseSnapshot& snapshot) {
    const auto found = std::find_if(snapshot.objects.begin(), snapshot.objects.end(),
                                    [](const auto& object) { return object.class_id == 0x51; });
    require(found != snapshot.objects.end(), "Trigger fixture missing");
    return *found;
}

void setup(DatabaseFixture& fixture) {
    fixture.put(0x1102d, std::uint8_t{8});
    fixture.put(0x11084, native_game::DatabaseObjectEntry{80, fixture.address(0x17000), 0});
    fixture.put(0x20108, fixture.address(0x21200));
    fixture.put(0x21200, std::array<std::uint8_t, 6>{0xb8, 0x51, 0, 0, 0, 0xc3});
    native_game::StoryTrigger value{};
    value.object.vtable = fixture.address(0x20100);
    value.object.id = 80;
    value.action_list_id = 90;
    value.raw_type = 8;
    value.integers.length = value.integers.capacity = 8;
    value.integers.data = fixture.address(0x1703c);
    value.integers.inline_values = {0, 1, 2, 3, 4, 5, 0x80000000u, 0xffffffffu};
    value.booleans.length = value.booleans.capacity = 12;
    value.booleans.data = fixture.address(0x17070);
    value.booleans.inline_values = {0, 1, 2, 0xffffffffu, 0, 0, 0, 0, 0, 0, 0, 0x80000000u};
    value.bytes.length = value.bytes.capacity = 4;
    value.bytes.data = fixture.address(0x170b4);
    value.bytes.inline_values = {-128, 127, -1, 0};
    fixture.put(0x17000, value);
}

void verify(const native_game::Profile& profile) {
    DatabaseFixture fixture(profile);
    setup(fixture);
    const auto before = std::vector<std::byte>(fixture.image, fixture.image + 0x300000);
    const auto snapshot = fixture.snapshot();
    const auto& copied = trigger(snapshot);
    require(copied.description == L"Object Activation 8 / Action list ID 90" &&
                copied.bytes.size() == 0xec && copied.relationships.size() == 1 &&
                copied.relationships[0].class_id == 0x42 && copied.relationships[0].id == 90,
            "Trigger type or action list link incorrect");
    require(copied.fields.find(L"Integer parameter[7] (selector 7): raw 4294967295") !=
                    std::wstring::npos &&
                copied.fields.find(L"Boolean32 parameter[3] (selector 10003): raw 4294967295") !=
                    std::wstring::npos &&
                copied.fields.find(L"Signed byte parameter[0] (selector 20000): raw -128") !=
                    std::wstring::npos &&
                copied.fields.find(L"Signed byte parameter[1] (selector 20001): raw 127") !=
                    std::wstring::npos,
            "Parameter width, signedness or raw value lost");
    require(std::memcmp(before.data(), fixture.image, before.size()) == 0,
            "Trigger inspection modified native memory");
    fixture.put(0x1703c, 999u);
    require(copied.fields.find(L"selector 0): raw 0") != std::wstring::npos,
            "Snapshot retained mutable parameter storage");
    const std::array<std::uint32_t, 8> external{100, 101, 102, 103, 104, 105, 106, 107};
    fixture.put(0x28000, external);
    fixture.put(0x17034, fixture.address(0x28000));
    require(trigger(fixture.snapshot()).fields.find(L"selector 0): raw 100") != std::wstring::npos,
            "Backing pointer ignored in favor of stale inline storage");
    for (const auto offset : {0x1702cu, 0x17060u, 0x170a4u}) {
        for (const auto length : {0u, 1u, 7u, 9u, 0xffffffffu}) {
            setup(fixture);
            fixture.put(offset, length);
            require(trigger(fixture.snapshot()).fields.find(L"unsupported metadata") !=
                        std::wstring::npos,
                    "Unsupported parameter length accepted");
        }
    }
    for (const auto offset : {0x17038u, 0x1706cu, 0x170b0u}) {
        setup(fixture);
        fixture.put(offset, 0u);
        require(trigger(fixture.snapshot()).fields.find(L"unsupported metadata") !=
                    std::wstring::npos,
                "Insufficient parameter capacity accepted");
    }
    for (const auto offset : {0x17034u, 0x17068u, 0x170acu}) {
        for (const auto pointer : {0u, 1u, 0xfffffff0u}) {
            setup(fixture);
            fixture.put(offset, pointer);
            const auto result = fixture.snapshot();
            require(trigger(result).fields.find(L"unsupported metadata") != std::wstring::npos &&
                        trigger(result).relationships.size() == 1,
                    "Invalid pointer read or independent action link lost");
        }
    }
    setup(fixture);
    fixture.put(0x17034, fixture.address(0x28000));
    DWORD protection = 0;
    require(VirtualProtect(fixture.image + 0x28000, 0x1000, PAGE_READWRITE | PAGE_GUARD,
                           &protection) != 0,
            "Guard setup failed");
    require(trigger(fixture.snapshot()).fields.find(L"unsupported metadata") != std::wstring::npos,
            "Guarded parameters accepted");
    MEMORY_BASIC_INFORMATION region{};
    require(VirtualQuery(fixture.image + 0x28000, &region, sizeof(region)) == sizeof(region) &&
                (region.Protect & PAGE_GUARD),
            "Parameter guard consumed");
    require(VirtualProtect(fixture.image + 0x28000, 0x1000, protection, &protection) != 0,
            "Guard restore failed");
    fixture.put(0x17034, fixture.address(0x2feff0));
    require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, PAGE_NOACCESS, &protection) != 0,
            "Partial parameter setup failed");
    require(trigger(fixture.snapshot()).fields.find(L"unsupported metadata") != std::wstring::npos,
            "Partial parameter copy accepted");
    require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, protection, &protection) != 0,
            "Partial parameter restore failed");
    setup(fixture);
    native_game::StoryTrigger incomplete{};
    std::memcpy(&incomplete, fixture.image + 0x17000, sizeof(incomplete));
    fixture.put(0x2fefc0, incomplete);
    fixture.put(0x11084, native_game::DatabaseObjectEntry{80, fixture.address(0x2fefc0), 0});
    require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, PAGE_NOACCESS, &protection) != 0,
            "Partial trigger setup failed");
    const auto partial = fixture.snapshot();
    require(trigger(partial).fields == L"Typed fields unreadable." &&
                trigger(partial).relationships.empty(),
            "Partial trigger header produced parameters or links");
    require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, protection, &protection) != 0,
            "Partial trigger restore failed");
    for (unsigned type = 0; type <= 255; ++type) {
        setup(fixture);
        fixture.put(0x170e8, static_cast<std::uint8_t>(type));
        const auto result = fixture.snapshot();
        require(trigger(result).description.starts_with(type == 8 ? L"Object Activation 8"
                                                                  : L"Unsupported trigger type ") &&
                    trigger(result).fields.find(L"Raw trigger type +0xe8: " +
                                                std::to_wstring(type)) != std::wstring::npos,
                "Unknown trigger type acquired a label or lost raw value");
    }
}
}

int main() {
    try {
        for (const auto* profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            verify(*profile);
        }
        std::cout << "Trigger parameters and failure paths passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
