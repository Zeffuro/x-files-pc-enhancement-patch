#include "story_edit.h"
#include "database/memory.h"
#include "game/layouts/database/cache.h"
#include "game/layouts/database/variable.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>

namespace devtools {
namespace {
bool same_edit_object(const std::vector<std::uint8_t>& expected,
                      const std::vector<std::uint8_t>& current) {
    if (expected.size() != sizeof(native_game::Variable) || current.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        // Native dirty status and reference counts can change without replacing the scalar.
        if (index >= offsetof(native_game::PersistentObject, references) &&
            index < offsetof(native_game::PersistentObject, references) + sizeof(std::uint16_t)) {
            continue;
        }
        const auto mask = index == 0x14 ? 0xf3 : 0xff;
        if ((expected[index] & mask) != (current[index] & mask)) {
            return false;
        }
    }
    return true;
}
}

std::optional<std::int32_t> parse_state_value(std::wstring_view text) {
    if (text.empty() || text.size() > 32) {
        return std::nullopt;
    }
    if (text.size() == 3 && text.front() == L'\'' && text.back() == L'\'' && text[1] >= 32 &&
        text[1] <= 126) {
        return static_cast<std::int32_t>(text[1]);
    }
    if (text.size() == 1 &&
        ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z'))) {
        return static_cast<std::int32_t>(text[0]);
    }
    std::string ascii;
    for (const auto ch : text) {
        if (ch > 127) {
            return std::nullopt;
        }
        ascii.push_back(static_cast<char>(ch));
    }
    std::int32_t result = 0;
    const auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != ascii.data() + ascii.size()) {
        return std::nullopt;
    }
    return result;
}

std::wstring validate_state_edit(const GameSnapshot& expected, const GameSnapshot& current,
                                 const StateVariable& variable, std::int32_t value, bool enabled) {
    if (!enabled) {
        return L"Enable editing before applying a value.";
    }
    if (!expected.manager || !expected.state || expected.manager != current.manager ||
        expected.state != current.state || expected.hdb != current.hdb ||
        expected.view != current.view || expected.input != current.input || !expected.application ||
        !expected.session || expected.application != current.application ||
        expected.session != current.session) {
        return L"Game context changed. Refresh the snapshot before editing.";
    }
    if (!variable.key.state_database || !variable.value || variable.key.class_id != 0x53 ||
        variable.bytes.size() != sizeof(native_game::Variable)) {
        return L"Only available cached state variables can be edited.";
    }
    const auto type = variable.value->type_flags & 0x7f;
    if (type > 2) {
        return L"This native value type is read-only.";
    }
    if ((type == 0 && (value < -128 || value > 127)) || (type == 2 && value != 0 && value != 1)) {
        return type == 0 ? L"This value requires an integer from -128 to 127."
                         : L"This value requires 0 or 1.";
    }
    const auto same_key = [&](const auto& row) { return row.key == variable.key; };
    if (std::count_if(expected.variables.begin(), expected.variables.end(), same_key) != 1 ||
        std::count_if(current.variables.begin(), current.variables.end(), same_key) != 1) {
        return L"Variable identity is ambiguous. Refresh the snapshot before editing.";
    }
    const auto found = std::find_if(current.variables.begin(), current.variables.end(),
                                    [&](const auto& row) { return row.key == variable.key; });
    if (found != current.variables.end() && found->value &&
        found->value->raw_value != variable.value->raw_value) {
        return L"The game changed this value from " + std::to_wstring(variable.value->raw_value) +
               L" to " + std::to_wstring(found->value->raw_value) +
               L". Refresh the snapshot before editing.";
    }
    if (found == current.variables.end() || found->address != variable.address ||
        found->name != variable.name || found->value != variable.value ||
        !same_edit_object(variable.bytes, found->bytes) || variable.name.starts_with(L"<name ")) {
        return L"Selected variable changed. Refresh the snapshot before editing.";
    }
    native_game::Variable live{};
    if (!database_copy(variable.address, &live, sizeof(live)) ||
        std::memcmp(&live, found->bytes.data(), sizeof(live)) != 0 || live.id != variable.key.id ||
        live.raw_value != variable.value->raw_value ||
        live.type_flags != variable.value->type_flags) {
        return L"Selected variable changed. Refresh the snapshot before editing.";
    }
    return L"";
}

std::wstring write_state_variable(const std::byte* image, const native_game::Profile& profile,
                                  const GameSnapshot& expected, const GameSnapshot& current,
                                  const StateVariable& variable, std::int32_t value, bool enabled) {
    if (const auto failure = validate_state_edit(expected, current, variable, value, enabled);
        !failure.empty()) {
        return failure;
    }
    const auto rva = profile.variable_set_value;
    constexpr std::array<std::uint8_t, 12> prefix{0x64, 0xa1, 0,    0,    0,    0,
                                                  0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
    std::array<std::uint8_t, 12> code{};
    if (!image || !rva ||
        !database_copy(reinterpret_cast<std::uintptr_t>(image + rva), code.data(), code.size()) ||
        code != prefix) {
        return L"Native variable writer is unavailable.";
    }
    using Setter = int(__stdcall*)(std::uintptr_t, std::int32_t, int);
    // The scalar dispatcher preserves type and marks the native object dirty synchronously.
    reinterpret_cast<Setter>(const_cast<std::byte*>(image) + rva)(variable.address, value, 0);
    native_game::Variable after{};
    native_game::Variable before{};
    std::memcpy(&before, variable.bytes.data(), sizeof(before));
    if (!database_copy(variable.address, &after, sizeof(after)) || after.raw_value != value ||
        after.id != before.id || after.vtable != before.vtable ||
        after.type_flags != before.type_flags) {
        return L"Native writer did not return the requested value. Refresh the snapshot.";
    }
    return L"";
}
}
