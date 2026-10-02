#include "database_fixture.h"
#include "devtools/story_edit.h"
#include "devtools/story_state.h"
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void verify(const native_game::Profile& profile) {
    DatabaseFixture fixture(profile);
    std::array<char, 1024> name{};
    name[0] = 'b';
    fixture.put(0x28000, name);
    fixture.put(0x16028, native_game::StoryString{0, fixture.address(0x28000), 1024, 0});
    auto native = fixture.snapshot();
    devtools::GameSnapshot snapshot;
    snapshot.manager = native.manager_address;
    snapshot.state = native.state_address;
    snapshot.hdb = native.hdb_address;
    snapshot.view = 1;
    snapshot.input = 2;
    snapshot.application = 3;
    snapshot.session = 4;
    snapshot.variables = devtools::story_state_variables(native);
    const auto found = std::find_if(snapshot.variables.begin(), snapshot.variables.end(),
                                    [](const auto& row) { return row.key.state_database; });
    require(found != snapshot.variables.end(), "State variable fixture absent");
    auto variable = *found;
    const auto before = std::vector<std::byte>(fixture.image, fixture.image + 0x300000);
    require(!devtools::validate_state_edit(snapshot, snapshot, variable, 1, false).empty(),
            "Off-default edit gate accepted mutation");
    require(devtools::validate_state_edit(snapshot, snapshot, variable, 1, true).empty(),
            "Valid native scalar edit rejected");
    auto changed = snapshot;
    changed.input++;
    require(!devtools::validate_state_edit(snapshot, changed, variable, 1, true).empty(),
            "Stale input context accepted");
    changed = snapshot;
    changed.variables.push_back(variable);
    require(!devtools::validate_state_edit(snapshot, changed, variable, 1, true).empty(),
            "Ambiguous identity accepted");
    changed = snapshot;
    changed.variables[static_cast<std::size_t>(found - snapshot.variables.begin())]
        .value->raw_value++;
    require(
        devtools::validate_state_edit(snapshot, changed, variable, 1, true).find(L"from 5 to 6") !=
            std::wstring::npos,
        "Stale cached value accepted or omitted current-value explanation");
    const auto selected = static_cast<std::size_t>(found - snapshot.variables.begin());
    changed = snapshot;
    changed.variables[selected].bytes[0x14] ^= 0x0c;
    changed.variables[selected].bytes[0x24] ^= 0x42;
    changed.variables[selected].bytes[0x25] ^= 0x01;
    std::memcpy(fixture.image + 0x16000, changed.variables[selected].bytes.data(),
                changed.variables[selected].bytes.size());
    require(devtools::validate_state_edit(snapshot, changed, variable, 1, true).empty(),
            "Native bookkeeping changes rejected an unchanged scalar");
    fixture.image[0x16024] ^= std::byte{1};
    require(!devtools::validate_state_edit(snapshot, changed, variable, 1, true).empty(),
            "Bookkeeping changed after fresh capture bypassed live-byte validation");
    for (const auto index : {0x14, 0x18, 0x1c, 0x20, 0x28, 0x41}) {
        changed = snapshot;
        changed.variables[selected].bytes[index] ^= index == 0x14 ? 0x01 : 0x80;
        std::memcpy(fixture.image + 0x16000, changed.variables[selected].bytes.data(),
                    changed.variables[selected].bytes.size());
        require(!devtools::validate_state_edit(snapshot, changed, variable, 1, true).empty(),
                "Native identity, owner, name, type or unrelated flags changed without rejection");
    }
    std::memcpy(fixture.image + 0x16000, variable.bytes.data(), variable.bytes.size());
    fixture.put(0x16038, std::int32_t{99});
    require(!devtools::validate_state_edit(snapshot, snapshot, variable, 1, true).empty(),
            "Native value changed after capture accepted");
    fixture.put(0x16038, variable.value->raw_value);
    for (const auto type : {0, 2, 127}) {
        variable.value->type_flags = static_cast<std::uint8_t>(type);
        require(!devtools::validate_state_edit(snapshot, snapshot, variable, 128, true).empty(),
                "Type-specific value boundary accepted");
    }
    require(std::memcmp(before.data(), fixture.image, before.size()) == 0,
            "Rejected edit modified native memory");
    variable = *found;
    std::vector<std::uint8_t> bridge{0x64, 0xa1, 0,    0,    0,    0,    0x55, 0x8b, 0xec,
                                     0x6a, 0xff, 0x68, 0,    0,    0,    0,    0x8b, 0x4d,
                                     0x08, 0x8b, 0x45, 0x0c, 0x89, 0x41, 0x38, 0xff, 0x05};
    const auto append = [&](std::uint32_t value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        bridge.insert(bridge.end(), bytes, bytes + sizeof(value));
    };
    append(fixture.address(0x29000));
    bridge.insert(bridge.end(), {0x89, 0x0d});
    append(fixture.address(0x29004));
    bridge.insert(bridge.end(), {0xa3});
    append(fixture.address(0x29008));
    bridge.insert(bridge.end(), {0x8b, 0x55, 0x10, 0x89, 0x15});
    append(fixture.address(0x2900c));
    bridge.insert(bridge.end(), {0x8b, 0xe5, 0x5d, 0xc2, 0x0c, 0});
    std::memcpy(fixture.image + profile.variable_set_value, bridge.data(), bridge.size());
    DWORD protection = 0;
    require(VirtualProtect(fixture.image + profile.variable_set_value, bridge.size(),
                           PAGE_EXECUTE_READWRITE, &protection) != 0,
            "Writer bridge cannot execute");
    FlushInstructionCache(GetCurrentProcess(), fixture.image + profile.variable_set_value,
                          bridge.size());
    changed = snapshot;
    changed.variables[selected].bytes[0x14] ^= 0x0c;
    changed.variables[selected].bytes[0x24] ^= 0x42;
    std::memcpy(fixture.image + 0x16000, changed.variables[selected].bytes.data(),
                changed.variables[selected].bytes.size());
    require(
        devtools::write_state_variable(fixture.image, profile, snapshot, changed, variable, 1, true)
            .empty(),
        "Native setter bridge or readback failed");
    std::uint32_t recorded[4]{};
    std::memcpy(recorded, fixture.image + 0x29000, sizeof(recorded));
    require(recorded[0] == 1 && recorded[1] == variable.address && recorded[2] == 1 &&
                recorded[3] == 0,
            "Writer ABI changed object/value/reserved or called more than once");
    require(!devtools::write_state_variable(fixture.image, profile, snapshot, snapshot, variable, 2,
                                            false)
                 .empty(),
            "Disabled writer called native bridge");
    std::memcpy(fixture.image + 0x16000, variable.bytes.data(), variable.bytes.size());
    fixture.image[profile.variable_set_value] = std::byte{0x90};
    require(!devtools::write_state_variable(fixture.image, profile, snapshot, snapshot, variable, 1,
                                            true)
                 .empty(),
            "Unverified writer prefix accepted");
    std::memcpy(recorded, fixture.image + 0x29000, sizeof(recorded));
    require(recorded[0] == 1, "Rejected mutation called native bridge");
}
}

int main() {
    try {
        for (const auto* profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            verify(*profile);
        }
        require(devtools::parse_state_value(L"-2147483648") == INT32_MIN &&
                    devtools::parse_state_value(L"2147483647") == INT32_MAX,
                "Signed integer boundary rejected");
        require(devtools::parse_state_value(L"A") == 65 &&
                    devtools::parse_state_value(L"'A'") == 65 &&
                    devtools::parse_state_value(L"z") == 122 &&
                    devtools::parse_state_value(L"'1'") == 49 &&
                    devtools::parse_state_value(L"1") == 1 &&
                    devtools::parse_state_value(L"' '") == 32 &&
                    devtools::parse_state_value(L"'~'") == 126 &&
                    devtools::parse_state_value(L"'''") == 39,
                "ASCII input or decimal digit distinction failed");
        for (const auto* value :
             {L"AB", L"'AB'", L"''", L"'A", L"A'", L"'\n'", L"\u00e9", L"'\u00e9'", L" "}) {
            require(!devtools::parse_state_value(value), "Invalid ASCII character accepted");
        }
        for (const auto* value : {L"", L"2147483648", L"-2147483649", L"1x", L"0x1", L" 1"}) {
            require(!devtools::parse_state_value(value), "Invalid decimal value accepted");
        }
        std::cout << "Native state edit gates and stale rejection passed four profiles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
