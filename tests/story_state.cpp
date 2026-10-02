#include "database_fixture.h"
#include "devtools/story_state.h"
#include "devtools/variables.h"
#include "game/profiles/variables.h"
#include <algorithm>
#include <iostream>
#include <limits>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

const devtools::StateGroup& group(const std::vector<devtools::StateGroup>& groups,
                                  std::wstring_view name) {
    const auto found = std::find_if(groups.begin(), groups.end(),
                                    [name](const auto& value) { return value.name == name; });
    require(found != groups.end(), "Story state group missing");
    return *found;
}

bool contains(const std::vector<devtools::StateGroup>& groups, std::wstring_view text) {
    for (const auto& value : groups) {
        for (const auto& row : value.values) {
            if (row.find(text) != std::wstring::npos) {
                return true;
            }
        }
    }
    return false;
}

void name(DatabaseFixture& fixture, std::string_view value, std::uint32_t size = 1024) {
    std::array<char, 2048> bytes{};
    std::copy(value.begin(), value.end(), bytes.begin());
    fixture.put(0x28000, bytes);
    fixture.put(0x16028, native_game::StoryString{0, fixture.address(0x28000), size, 0});
}

void verify(const native_game::Profile& profile) {
    DatabaseFixture fixture(profile);
    constexpr std::string_view authored =
        "cAIFieldOfficeWhereAreWe (P=Phone, A=APB/Case Files, N=None, D=Done)";
    name(fixture, authored);
    fixture.put(0x16038, std::int32_t{80});
    fixture.put(0x16041, std::uint8_t{0x81});
    const auto before = std::vector<std::byte>(fixture.image, fixture.image + 0x300000);
    const auto snapshot = fixture.snapshot();
    const auto groups = devtools::story_state_groups(snapshot);
    const auto& phases = group(groups, L"Variables named WhereAreWe");
    require(phases.values.size() == 1 &&
                phases.values[0].find(L"cAIFieldOfficeWhereAreWe (P=Phone") == 0 &&
                phases.values[0].find(L"80 (0x00000050) / type 1 / flags 0x81 / State ID 60") !=
                    std::wstring::npos,
            "Named phase lost literal authored text or raw native value");
    require(group(groups, L"Cached HDB variables").values.size() == 1 &&
                contains(groups, L"Cached variables: 1 state, 1 HDB") &&
                contains(groups, L"They do not identify your current location"),
            "Cache identity or coverage boundary missing");
    require(std::memcmp(before.data(), fixture.image, before.size()) == 0,
            "Story state read modified native memory");
    require(contains(groups, L" / ASCII projection: 'P'"),
            "Raw ASCII projection did not match the literal authored key");
    for (const auto raw : {32, 126, 31, 127, -1}) {
        fixture.put(0x16038, raw);
        fixture.put(0x16041, std::uint8_t{0xff});
        const auto projected = devtools::story_state_groups(fixture.snapshot());
        require(contains(projected, L" / ASCII projection: ") == (raw >= 32 && raw <= 126) &&
                    contains(projected, L" / type 127 / flags 0xff"),
                "ASCII projection escaped full-value bounds or changed unknown type flags");
        if (raw == 32 || raw == 126) {
            require(contains(projected,
                             raw == 32 ? L"ASCII projection: ' '" : L"ASCII projection: '~'"),
                    "ASCII boundary projection was not preserved");
        }
    }
    fixture.put(0x16038, std::int32_t{80});
    fixture.put(0x16041, std::uint8_t{0x81});
    name(fixture, "bAIFieldOffice_Phone");
    require(phases.values[0].find(L"cAIFieldOfficeWhereAreWe") == 0,
            "Returned story state retained native name memory");
    const auto ordinary = devtools::story_state_groups(fixture.snapshot());
    require(group(ordinary, L"Variables named WhereAreWe").values.empty() &&
                group(ordinary, L"Other cached state variables").values.size() == 1 &&
                !contains(ordinary, L"ASCII projection"),
            "Checkpoint-like name was interpreted as a phase");
    fixture.put(0x16038, std::numeric_limits<std::int32_t>::min());
    fixture.put(0x16041, std::uint8_t{0xff});
    require(contains(devtools::story_state_groups(fixture.snapshot()),
                     L"-2147483648 (0x80000000) / type 127 / flags 0xff"),
            "Unknown type or signed raw bits were reinterpreted");
    const auto stale = fixture.snapshot();
    fixture.put(0x16038, 7);
    require(contains(devtools::story_state_groups(stale), L"<name changed since snapshot>"),
            "Changed native object was accepted as the copied variable");
    fixture.put(0x16028, native_game::StoryString{});
    require(contains(devtools::story_state_groups(fixture.snapshot()), L"<name uncached>"),
            "Uncached name was loaded or invented");
    fixture.put(0x16028, native_game::StoryString{0, 1, 1024, 0});
    require(contains(devtools::story_state_groups(fixture.snapshot()), L"<name unreadable>"),
            "Unreadable name pointer was accepted");
    fixture.put(0x16028, native_game::StoryString{0, 0xfffffff0, 1024, 0});
    require(contains(devtools::story_state_groups(fixture.snapshot()), L"<name unreadable>"),
            "Name address overflow was accepted");
    name(fixture, "");
    require(contains(devtools::story_state_groups(fixture.snapshot()), L"<name empty>"),
            "Empty name was interpreted");
    name(fixture, "bad\nname");
    require(contains(devtools::story_state_groups(fixture.snapshot()),
                     L"<name contains unsupported text>"),
            "Control characters entered story labels");
    name(fixture, std::string(1, '\xff'));
    require(contains(devtools::story_state_groups(fixture.snapshot()),
                     L"<name contains unsupported text>"),
            "Unverified name encoding was interpreted");
    name(fixture, "unterminated", 4);
    require(contains(devtools::story_state_groups(fixture.snapshot()),
                     L"<name unterminated / exceeds 1024 bytes>"),
            "Name capacity boundary was ignored");
    name(fixture, std::string(1023, 'z'));
    require(contains(devtools::story_state_groups(fixture.snapshot()), std::wstring(1023, L'z')),
            "Maximum terminated name was shortened");
    name(fixture, std::string(1024, 'z'), 2048);
    require(contains(devtools::story_state_groups(fixture.snapshot()),
                     L"<name unterminated / exceeds 1024 bytes>"),
            "Oversized name escaped copy limit");
    name(fixture, authored, UINT32_MAX);
    require(group(devtools::story_state_groups(fixture.snapshot()), L"Variables named WhereAreWe")
                    .values.size() == 1,
            "Large capacity with bounded terminated text was rejected");
    DWORD previous = 0;
    require(VirtualProtect(fixture.image + 0x28000, 0x1000, PAGE_READWRITE | PAGE_GUARD,
                           &previous) != 0,
            "Name guard setup failed");
    require(contains(devtools::story_state_groups(fixture.snapshot()), L"<name unreadable>"),
            "Guarded name was accepted");
    MEMORY_BASIC_INFORMATION region{};
    require(VirtualQuery(fixture.image + 0x28000, &region, sizeof(region)) == sizeof(region) &&
                (region.Protect & PAGE_GUARD),
            "Story name read consumed guard page");
    require(VirtualProtect(fixture.image + 0x28000, 0x1000, previous, &previous) != 0,
            "Name guard restore failed");
    const auto guarded = fixture.snapshot();
    require(VirtualProtect(fixture.image + 0x16000, 0x1000, PAGE_READWRITE | PAGE_GUARD,
                           &previous) != 0,
            "Object guard setup failed");
    require(contains(devtools::story_state_groups(guarded), L"<name unreadable>"),
            "Guarded stale object was accepted");
    const auto pointer = fixture.address(0x16000);
    fixture.put(native_game::registered_variables[0].rva(profile), pointer);
    require(devtools::inspect_variables(fixture.image, profile)
                    .find(L"RegUberVars [1]: unavailable (unreadable object)") !=
                std::wstring::npos,
            "Registered variable read accepted guarded object");
    require(VirtualQuery(fixture.image + 0x16000, &region, sizeof(region)) == sizeof(region) &&
                (region.Protect & PAGE_GUARD),
            "Registered variable read consumed guard page");
    require(VirtualProtect(fixture.image + 0x16000, 0x1000, previous, &previous) != 0,
            "Object guard restore failed");
    auto partial = fixture.snapshot();
    partial.truncated = true;
    partial.skipped_nodes = 2;
    auto found = std::find_if(partial.objects.begin(), partial.objects.end(),
                              [](const auto& object) { return object.variable.has_value(); });
    require(found != partial.objects.end(), "Variable fixture absent");
    found->variable.reset();
    require(contains(devtools::story_state_groups(partial), L"Values unavailable: 1") &&
                contains(devtools::story_state_groups(partial),
                         L"Cache traversal incomplete. Truncated: 1. Skipped nodes: 2."),
            "Incomplete cache or unreadable values appeared complete");
    partial.available = false;
    require(devtools::story_state_groups(partial).size() == 1 &&
                contains(devtools::story_state_groups(partial), L"unavailable"),
            "Unavailable cache displayed stale values");
    auto many = fixture.snapshot();
    const auto state = std::find_if(many.objects.begin(), many.objects.end(),
                                    [](const auto& object) { return object.state_database; });
    require(state != many.objects.end(), "State variable fixture absent");
    const auto item = *state;
    many.objects.assign(919, item);
    require(
        group(devtools::story_state_groups(many), L"Variables named WhereAreWe").values.size() ==
            919,
        "Large cached variable collection was sampled or capped");
}
}

int main() {
    try {
        for (const auto* profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            verify(*profile);
        }
        require(contains(devtools::story_state_groups({}), L"unavailable"),
                "Absent cache failed closed");
        std::cout << "Named story state passed all four native profiles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
