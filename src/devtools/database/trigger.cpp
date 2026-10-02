#include "trigger.h"
#include "memory.h"
#include "payload.h"
#include "game/layouts/database/story.h"
#include <array>
#include <cstring>

namespace devtools {
namespace {
template <class T, std::size_t N>
std::optional<std::array<T, N>> parameters(const native_game::StoryParameterArray<T, N>& array) {
    if (array.length != N || array.capacity < N || !array.data) {
        return {};
    }
    std::array<T, N> values{};
    if (!database_copy(array.data, values.data(), sizeof(values))) {
        return {};
    }
    return values;
}

template <class T, std::size_t N>
void describe(NativeDatabaseObject& object, const std::optional<std::array<T, N>>& values,
              std::uint32_t first_selector) {
    if (!values) {
        object.fields += L"\r\n" + database_parameter_text(first_selector) +
                         L" array unreadable or unsupported metadata.";
        return;
    }
    for (std::size_t index = 0; index < N; ++index) {
        object.fields +=
            L"\r\n" + database_parameter_text(first_selector + static_cast<std::uint32_t>(index)) +
            L": raw " + std::to_wstring((*values)[index]);
    }
}
}

void inspect_database_trigger(NativeDatabaseObject& object) {
    native_game::StoryTrigger trigger{};
    if (!database_copy(object.address, &trigger, sizeof(trigger))) {
        object.fields = L"Typed fields unreadable.";
        return;
    }
    const auto* header = reinterpret_cast<const std::uint8_t*>(&trigger);
    object.bytes.assign(header, header + sizeof(trigger));
    object.description = trigger.raw_type == 8 ? L"Object Activation" : L"Unsupported trigger type";
    object.description += L" " + std::to_wstring(trigger.raw_type) + L" / Action list ID " +
                          std::to_wstring(trigger.action_list_id);
    object.fields = L"Raw trigger type +0xe8: " + std::to_wstring(trigger.raw_type);
    object.relationships.push_back({L"Action list", 0x42, trigger.action_list_id});
    const auto integers = parameters(trigger.integers);
    const auto booleans = parameters(trigger.booleans);
    const auto bytes = parameters(trigger.bytes);
    native_game::StoryTrigger after{};
    if (!database_copy(object.address, &after, sizeof(after)) ||
        std::memcmp(&trigger, &after, sizeof(trigger)) != 0) {
        object.relationships.clear();
        object.fields += L"\r\nTrigger changed during copy. Parameters unavailable.";
        return;
    }
    object.fields += L"\r\nCopied runtime parameters. Values depend on dispatch context.";
    describe(object, integers, 0);
    describe(object, booleans, 10000);
    describe(object, bytes, 20000);
}
}
