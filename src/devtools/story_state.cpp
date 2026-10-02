#include "story_state.h"
#include "database/memory.h"
#include "game/layouts/database/story.h"
#include "game/layouts/database/variable.h"
#include "game/profiles/variables.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace devtools {
namespace {
std::wstring hex(std::uint32_t value, int width) {
    std::wostringstream text;
    text << L"0x" << std::hex << std::setfill(L'0') << std::setw(width) << value;
    return text.str();
}

}

std::wstring story_variable_name(const NativeDatabaseObject& object) {
    if (object.bytes.size() < sizeof(native_game::Variable)) {
        return L"<name unavailable>";
    }
    native_game::Variable copied{};
    native_game::StoryString descriptor{};
    std::memcpy(&copied, object.bytes.data(), sizeof(copied));
    std::memcpy(&descriptor, object.bytes.data() + 0x28, sizeof(descriptor));
    if (!descriptor.data || !descriptor.capacity) {
        return L"<name uncached>";
    }
    std::array<char, 1024> bytes{};
    const auto size = std::min<std::size_t>(bytes.size(), descriptor.capacity);
    native_game::Variable current{};
    if (!database_copy(descriptor.data, bytes.data(), size) ||
        !database_copy(object.address, &current, sizeof(current))) {
        return L"<name unreadable>";
    }
    if (std::memcmp(&copied, &current, sizeof(copied)) != 0) {
        return L"<name changed since snapshot>";
    }
    const auto end = std::find(bytes.begin(), bytes.begin() + size, '\0');
    if (end == bytes.begin() + size) {
        return L"<name unterminated / exceeds 1024 bytes>";
    }
    if (end == bytes.begin()) {
        return L"<name empty>";
    }
    std::wstring result;
    for (auto at = bytes.begin(); at != end; ++at) {
        const auto value = static_cast<unsigned char>(*at);
        if (value < 0x20 || value > 0x7e) {
            return L"<name contains unsupported text>";
        }
        result.push_back(value);
    }
    return result;
}

std::vector<StateVariable> story_state_variables(const NativeDatabaseSnapshot& snapshot,
                                                 const std::byte* image,
                                                 const native_game::Profile* profile) {
    std::vector<StateVariable> result;
    if (!snapshot.available) {
        return result;
    }
    std::vector<std::pair<std::uint32_t, std::wstring>> registrations;
    if (image && profile) {
        for (const auto& slot : native_game::registered_variables) {
            std::uint32_t pointer = 0;
            const auto rva = slot.rva(*profile);
            if (rva &&
                database_copy(reinterpret_cast<std::uintptr_t>(image + rva), &pointer,
                              sizeof(pointer)) &&
                pointer) {
                registrations.emplace_back(pointer, slot.registration);
            }
        }
    }
    for (const auto& object : snapshot.objects) {
        if (object.class_id != 0x53) {
            continue;
        }
        StateVariable row{
            object.key(), story_variable_name(object), {}, object.variable, object.address,
            object.bytes};
        for (const auto& [pointer, registration] : registrations) {
            if (pointer == object.address) {
                if (!row.registration.empty()) {
                    row.registration += L", ";
                }
                row.registration += registration;
            }
        }
        result.push_back(std::move(row));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.name != right.name) {
            return left.name < right.name;
        }
        if (left.key.state_database != right.key.state_database) {
            return left.key.state_database;
        }
        return left.key.id < right.key.id;
    });
    return result;
}

std::vector<StateGroup> story_state_groups(const NativeDatabaseSnapshot& snapshot,
                                           bool include_values) {
    StateGroup coverage{L"Story variable coverage", {}};
    if (!snapshot.available) {
        coverage.values.push_back(L"Cached story variables are unavailable.");
        return {std::move(coverage)};
    }
    StateGroup phases{L"Variables named WhereAreWe", {}};
    StateGroup state{L"Other cached state variables", {}};
    StateGroup hdb{L"Cached HDB variables", {}};
    std::size_t state_count = 0, hdb_count = 0, unreadable = 0;
    for (const auto& object : snapshot.objects) {
        if (object.class_id != 0x53) {
            continue;
        }
        (object.state_database ? state_count : hdb_count)++;
        auto& group = object.state_database ? state : hdb;
        const auto identity = std::wstring(object.state_database ? L"State" : L"HDB") + L" ID " +
                              std::to_wstring(object.id);
        if (!object.variable) {
            group.values.push_back(identity + L": value unavailable");
            ++unreadable;
            continue;
        }
        if (!include_values) {
            continue;
        }
        const auto name = story_variable_name(object);
        const auto& value = *object.variable;
        auto text = name + L": " + std::to_wstring(value.raw_value) + L" (" +
                    hex(static_cast<std::uint32_t>(value.raw_value), 8) + L") / type " +
                    std::to_wstring(value.type_flags & 0x7f) + L" / flags " +
                    hex(value.type_flags, 2) + L" / " + identity;
        if (object.state_database && name.find(L"WhereAreWe") != std::wstring::npos) {
            if (value.raw_value >= 32 && value.raw_value <= 126) {
                text += L" / ASCII projection: '";
                text.push_back(static_cast<wchar_t>(value.raw_value));
                text += L"'";
            }
            phases.values.push_back(std::move(text));
        } else {
            group.values.push_back(std::move(text));
        }
    }
    coverage.values.push_back(L"Cached variables: " + std::to_wstring(state_count) + L" state, " +
                              std::to_wstring(hdb_count) + L" HDB. Values unavailable: " +
                              std::to_wstring(unreadable) + L".");
    coverage.values.push_back(L"Names and key text come from cached variables. Values and type "
                              L"flags remain raw.");
    coverage.values.push_back(L"WhereAreWe groups match variable names. They do not identify "
                              L"your current location or prove completed objectives.");
    coverage.values.push_back(L"Only cached variables are shown. Uncached variables are not "
                              L"loaded and a live snapshot is not a saved game.");
    if (snapshot.truncated || snapshot.skipped_nodes) {
        coverage.values.push_back(L"Cache traversal incomplete. Truncated: " +
                                  std::to_wstring(snapshot.truncated) + L". Skipped nodes: " +
                                  std::to_wstring(snapshot.skipped_nodes) + L".");
    }
    const auto sort = [](StateGroup& group) {
        std::sort(group.values.begin(), group.values.end());
    };
    sort(phases);
    sort(state);
    sort(hdb);
    std::vector<StateGroup> groups{std::move(coverage), std::move(phases), std::move(state)};
    if (!hdb.values.empty()) {
        groups.push_back(std::move(hdb));
    }
    return groups;
}
}
