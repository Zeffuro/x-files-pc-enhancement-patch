#include "database_fixture.h"
#include "devtools/database/model.h"
#include <algorithm>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

const devtools::NativeDatabaseObject& find(const devtools::NativeDatabaseSnapshot& snapshot,
                                           unsigned type, unsigned id) {
    const auto index = devtools::database_find(snapshot, {type, id, false});
    require(index.has_value(), "Story fixture object missing");
    return snapshot.objects[*index];
}

void getter(DatabaseFixture& fixture, unsigned table, unsigned function, unsigned type) {
    fixture.put(table + 8, fixture.address(function));
    fixture.put(function,
                std::array<std::uint8_t, 6>{0xb8, static_cast<std::uint8_t>(type), 0, 0, 0, 0xc3});
}

void setup(DatabaseFixture& fixture) {
    fixture.put(0x1102d, std::uint8_t{9});
    getter(fixture, 0x20100, 0x21200, 0x51);
    getter(fixture, 0x20120, 0x21220, 0x42);
    getter(fixture, 0x20200, 0x21300, 0x0b);
    native_game::StoryTrigger trigger{};
    trigger.object.vtable = fixture.address(0x20100);
    trigger.object.id = 80;
    trigger.action_list_id = 90;
    trigger.raw_type = 8;
    fixture.put(0x17000, trigger);
    native_game::StoryReferenceList actions{};
    actions.object.vtable = fixture.address(0x20120);
    actions.object.id = 90;
    actions.cached_resource = fixture.address(0x25000);
    fixture.put(0x17200, actions);
    fixture.put(0x11084, native_game::DatabaseObjectEntry{80, fixture.address(0x17000), 0});
    fixture.put(0x11090, native_game::DatabaseObjectEntry{90, fixture.address(0x17200), 0});
    fixture.put(0x14858, fixture.address(0x26000));
    native_game::DatabaseNode resource{};
    resource.object.vtable = fixture.address(0x20200);
    resource.count = 1;
    fixture.put(0x25000, resource);
    fixture.put(0x25030, 40u);
    fixture.put(0x26000, resource);
    fixture.put(0x26030, 80u);
    fixture.put(0x1465c, 23u);
    fixture.put(0x14660, fixture.address(0x27000));
    fixture.put(0x14664, std::uint8_t{0x80});
    fixture.put(0x27000, 60u);
    fixture.put(0x27004, 1u);
    fixture.put(0x27008, std::array<std::uint8_t, 4>{0, 2, 0, 0});
    fixture.put(0x2700c, 60u);
    fixture.put(0x27010, 0u);
    fixture.put(0x27014, std::array<std::uint8_t, 3>{0, 2, 0});
}

void verify(const native_game::Profile& profile) {
    DatabaseFixture fixture(profile);
    setup(fixture);
    const auto before = std::vector<std::byte>(fixture.image, fixture.image + 0x300000);
    const auto snapshot = fixture.snapshot();
    const auto& action = find(snapshot, 0x41, 40);
    require(action.bytes.size() == 0x6c && action.relationships.size() == 2 &&
                action.relationships[0].id == 60 && action.relationships[1].class_id == 0x53 &&
                action.fields.find(L"Encoded condition:") != std::wstring::npos &&
                action.description.find(L"Statement:") != std::wstring::npos,
            "Conditional assignment payload failed");
    require(find(snapshot, 0x52, 50).relationships[0].id == 80 &&
                find(snapshot, 0x51, 80).relationships[0].id == 90 &&
                find(snapshot, 0x42, 90).relationships[0].id == 40,
            "Trigger/action chain failed");
    require(std::memcmp(before.data(), fixture.image, before.size()) == 0,
            "Story decoding modified native data");
    const auto action_index = devtools::database_find(snapshot, {0x41, 40, false});
    const auto links = devtools::database_links(snapshot, *action_index);
    require(std::none_of(links.begin(), links.end(),
                         [](const auto& link) {
                             return link.label.starts_with(L"Condition left") ||
                                    link.label.starts_with(L"Statement left");
                         }),
            "Payload operand silently selected ambiguous HDB/State variable");
    fixture.put(0x27010, 7u);
    require(action.fields.find(L"00 00 00 00") != std::wstring::npos,
            "Snapshot retained payload memory");
    fixture.put(0x14858, 0u);
    require(find(fixture.snapshot(), 0x52, 50).fields == L"List contents uncached.",
            "Uncached list was loaded");
    fixture.put(0x14858, 1u);
    require(find(fixture.snapshot(), 0x52, 50).relationships.empty(),
            "Unreadable list resource produced links");
    fixture.put(0x14858, fixture.address(0x26000));
    fixture.put(0x21301, std::uint8_t{0x53});
    require(find(fixture.snapshot(), 0x52, 50).relationships.empty(),
            "Wrong resource class accepted");
    getter(fixture, 0x20200, 0x21300, 0x0b);
    fixture.put(0x2602d, std::uint8_t{0});
    require(find(fixture.snapshot(), 0x52, 50).description == L"0 Trigger IDs",
            "Empty cached list lost");
    fixture.put(0x2602d, std::uint8_t{255});
    std::array<std::uint32_t, 255> ids{};
    ids.fill(80);
    fixture.put(0x26030, ids);
    require(find(fixture.snapshot(), 0x52, 50).relationships.size() == 255,
            "Maximum native ID-list count truncated");
    fixture.put(0x14660, 0u);
    require(find(fixture.snapshot(), 0x41, 40).relationships.empty() &&
                find(fixture.snapshot(), 0x41, 40).fields.find(L"uncached") != std::wstring::npos,
            "Uncached payload was loaded");
    fixture.put(0x14660, 1u);
    require(find(fixture.snapshot(), 0x41, 40).relationships.empty(),
            "Unreadable payload produced links");
    fixture.put(0x14660, fixture.address(0x27000));
    fixture.put(0x1465c, 4097u);
    require(find(fixture.snapshot(), 0x41, 40).fields.find(L"exceeds") != std::wstring::npos,
            "Oversized payload copied");
    fixture.put(0x1465c, 22u);
    require(find(fixture.snapshot(), 0x41, 40).relationships.size() == 1,
            "Short statement invented or complete condition lost");
    fixture.put(0x1465c, 11u);
    require(find(fixture.snapshot(), 0x41, 40).relationships.empty(), "Short condition accepted");
    fixture.put(0x1465c, 23u);
    DWORD protection = 0;
    require(VirtualProtect(fixture.image + 0x27000, 0x1000, PAGE_READWRITE | PAGE_GUARD,
                           &protection) != 0,
            "Payload guard setup failed");
    require(find(fixture.snapshot(), 0x41, 40).relationships.empty(), "Guarded payload accepted");
    MEMORY_BASIC_INFORMATION region{};
    require(VirtualQuery(fixture.image + 0x27000, &region, sizeof(region)) == sizeof(region) &&
                (region.Protect & PAGE_GUARD),
            "Payload guard consumed");
    require(VirtualProtect(fixture.image + 0x27000, 0x1000, protection, &protection) != 0,
            "Payload protection restore failed");
    native_game::DatabaseNode resource{};
    resource.object.vtable = fixture.address(0x20200);
    resource.count = 2;
    fixture.put(0x2fefcc, resource);
    fixture.put(0x2feffc, 80u);
    fixture.put(0x14858, fixture.address(0x2fefcc));
    require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, PAGE_NOACCESS, &protection) != 0,
            "List truncation setup failed");
    require(find(fixture.snapshot(), 0x52, 50).relationships.empty(), "Partial ID list accepted");
    fixture.put(0x14660, fixture.address(0x2feff8));
    require(find(fixture.snapshot(), 0x41, 40).relationships.empty(), "Partial payload accepted");
    require(VirtualProtect(fixture.image + 0x2ff000, 0x1000, protection, &protection) != 0,
            "Truncation protection restore failed");
    setup(fixture);
    fixture.put(0x27000, 7u);
    fixture.put(0x27004, 60u);
    fixture.put(0x27008, std::array<std::uint8_t, 4>{1, 0, 0, 0});
    fixture.put(0x2700c, 10000u);
    fixture.put(0x27014, std::array<std::uint8_t, 3>{1, 2, 0});
    const auto parameters = fixture.snapshot();
    const auto& parameter_action = find(parameters, 0x41, 40);
    require(parameter_action.relationships.size() == 1 &&
                parameter_action.relationships[0].field == L"Condition right" &&
                parameter_action.relationships[0].class_id == 0x53 &&
                parameter_action.relationships[0].id == 60 &&
                parameter_action.fields.find(L"Integer parameter[7] (selector 7)") !=
                    std::wstring::npos &&
                parameter_action.description.find(L"Boolean32 parameter[0] (selector 10000)") !=
                    std::wstring::npos,
            "Context parameter produced a variable link or genuine variable link was lost");
    fixture.put(0x27000, 0xffffffffu);
    fixture.put(0x27008, std::array<std::uint8_t, 4>{4, 0, 0, 0});
    fixture.put(0x2700c, 4u);
    fixture.put(0x27010, 0x80000000u);
    fixture.put(0x27014, std::array<std::uint8_t, 3>{4, 4, 0});
    fixture.put(0x170c0, 0xffffffffu);
    fixture.put(0x170c8, fixture.address(0x28000));
    require(VirtualProtect(fixture.image + 0x28000, 0x1000, PAGE_READWRITE | PAGE_GUARD,
                           &protection) != 0,
            "Context guard setup failed");
    const auto context = fixture.snapshot();
    const auto& context_action = find(context, 0x41, 40);
    require(context_action.relationships.size() == 1 &&
                context_action.relationships[0].field == L"Condition right" &&
                context_action.relationships[0].id == 60 &&
                context_action.fields.find(L"Trigger context index 4294967295 "
                                           L"(kind 4, value unavailable)") != std::wstring::npos &&
                context_action.description.find(L"Ignored Statement destination kind 4 "
                                                L"(raw value 4)") != std::wstring::npos &&
                context_action.description.find(L"Trigger context index 2147483648 "
                                                L"(kind 4, value unavailable)") !=
                    std::wstring::npos,
            "Kind 4 created a variable link, parameter selector or evaluated context");
    fixture.put(0x27000, 60u);
    fixture.put(0x2700c, 60u);
    fixture.put(0x27010, 60u);
    const auto same_id = fixture.snapshot();
    const auto& indexed_action = find(same_id, 0x41, 40);
    require(indexed_action.relationships.size() == 1 &&
                indexed_action.relationships[0].field == L"Condition right" &&
                indexed_action.relationships[0].id == 60 &&
                indexed_action.fields.find(
                    L"Trigger context index 60 (kind 4, value unavailable)") != std::wstring::npos,
            "Kind 4 index matching a cached variable ID created a variable relationship");
    require(VirtualQuery(fixture.image + 0x28000, &region, sizeof(region)) == sizeof(region) &&
                (region.Protect & PAGE_GUARD),
            "Context storage guard consumed");
    require(VirtualProtect(fixture.image + 0x28000, 0x1000, protection, &protection) != 0,
            "Context protection restore failed");
}
}

int main() {
    try {
        for (const auto* profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            verify(*profile);
        }
        std::cout << "Cached story lists and payloads passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
