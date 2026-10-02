#include "memory.h"
#include "types.h"
#include "payload.h"
#include "trigger.h"
#include "game/layouts/database/story.h"
#include "game/layouts/database/variable.h"
#include "game/layouts/database/asset_reference.h"
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace devtools {
namespace {
template <class T> bool prefix(NativeDatabaseObject& object, T& value) {
    if (!database_copy(object.address, &value, sizeof(value))) {
        object.fields = L"Typed fields unreadable.";
        return false;
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(&value);
    object.bytes.assign(data, data + sizeof(value));
    return true;
}

std::wstring string(const native_game::StoryString& value) {
    if (!value.data || !value.capacity) {
        return L"<unavailable>";
    }
    std::array<char, 1024> bytes{};
    const auto size = std::min<std::size_t>(bytes.size(), value.capacity);
    if (!database_copy(value.data, bytes.data(), size)) {
        return L"<unreadable>";
    }
    const auto end = std::find(bytes.begin(), bytes.begin() + size, '\0');
    if (end == bytes.begin() + size) {
        return L"<unterminated / exceeds limit>";
    }
    std::wstring result;
    for (auto at = bytes.begin(); at != end; ++at) {
        const auto ch = static_cast<unsigned char>(*at);
        if (ch < 0x20 || ch > 0x7e) {
            return L"<non-ASCII text>";
        }
        result.push_back(ch);
    }
    return result;
}

void reference(NativeDatabaseObject& object, std::wstring field, std::uint32_t type,
               std::uint32_t id) {
    object.relationships.push_back({std::move(field), type, id});
}

bool id_resource(const native_game::DatabaseNode& resource, const std::byte* image) {
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    std::uint32_t getter = 0;
    std::array<std::uint8_t, 6> code{};
    if (resource.object.vtable < base + 0x1000 || resource.object.vtable >= base + 0x300000 ||
        !database_copy(std::uintptr_t(resource.object.vtable) + 8, &getter, sizeof(getter)) ||
        getter < base + 0x1000 || getter >= base + 0x260000 ||
        !database_copy(getter, code.data(), code.size())) {
        return false;
    }
    return code == std::array<std::uint8_t, 6>{0xb8, 0x0b, 0, 0, 0, 0xc3};
}

void inspect_list(NativeDatabaseObject& object, const std::byte* image) {
    native_game::StoryReferenceList list{};
    if (!prefix(object, list)) {
        return;
    }
    object.description = L"Cached ID list";
    if (!list.cached_resource) {
        object.fields = L"List contents uncached.";
        return;
    }
    native_game::DatabaseNode resource{};
    if (!database_copy(list.cached_resource, &resource, sizeof(resource)) ||
        !id_resource(resource, image)) {
        object.fields = L"List resource unreadable or unsupported.";
        return;
    }
    std::vector<std::uint32_t> ids(resource.count);
    native_game::DatabaseNode after{};
    native_game::StoryReferenceList list_after{};
    if ((!ids.empty() && !database_copy(std::uintptr_t(list.cached_resource) + sizeof(resource),
                                        ids.data(), ids.size() * sizeof(ids[0]))) ||
        !database_copy(list.cached_resource, &after, sizeof(after)) ||
        !database_copy(object.address, &list_after, sizeof(list_after)) ||
        std::memcmp(&resource, &after, sizeof(resource)) != 0 ||
        list.cached_resource != list_after.cached_resource) {
        object.fields = L"List contents unreadable or changed during copy.";
        return;
    }
    const auto target_class = object.class_id == 0x52 ? 0x51u : 0x41u;
    const auto label = target_class == 0x51 ? L"Trigger" : L"Action";
    object.description = std::to_wstring(ids.size()) + L" " + label + L" IDs";
    object.fields = L"Copied cached list entries: " + std::to_wstring(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto field = std::wstring(label) + L"[" + std::to_wstring(index) + L"]";
        object.fields += L"\r\n" + field + L" ID: " + std::to_wstring(ids[index]);
        reference(object, field, target_class, ids[index]);
    }
}

void operands(NativeDatabaseObject& object, const DatabaseExpression& expression,
              std::wstring_view label) {
    for (const auto& [operand, side] :
         {std::pair{expression.left, L" left"}, std::pair{expression.right, L" right"}}) {
        if (operand.kind == 0) {
            reference(object, std::wstring(label) + side, 0x53, operand.raw);
        }
    }
}

void inspect_action(NativeDatabaseObject& object) {
    native_game::StoryAction action{};
    if (!prefix(object, action)) {
        return;
    }
    object.description = L"Raw action type " + std::to_wstring(action.raw_type & 0x7f);
    object.fields = L"Raw type byte: " + std::to_wstring(action.raw_type) +
                    L"\r\nPayload byte length: " + std::to_wstring(action.payload_size);
    if (action.payload_size > 4096) {
        object.fields += L"\r\nPayload exceeds copy limit.";
        return;
    }
    if (!action.cached_payload) {
        object.fields += L"\r\nPayload uncached.";
        return;
    }
    std::vector<std::uint8_t> bytes(action.payload_size);
    native_game::StoryAction after{};
    if ((!bytes.empty() && !database_copy(action.cached_payload, bytes.data(), bytes.size())) ||
        !database_copy(object.address, &after, sizeof(after)) ||
        action.cached_payload != after.cached_payload ||
        action.payload_size != after.payload_size || action.raw_type != after.raw_type) {
        object.fields += L"\r\nPayload unreadable or changed during copy.";
        return;
    }
    const auto payload = database_action_payload(action.raw_type, bytes);
    if (payload.condition) {
        object.fields +=
            L"\r\nEncoded condition: " + database_expression_text(*payload.condition, true);
        operands(object, *payload.condition, L"Condition");
    }
    if (payload.statement) {
        object.description = L"Statement: " + database_expression_text(*payload.statement, false);
        object.fields += L"\r\n" + object.description;
        operands(object, *payload.statement, L"Statement");
    } else {
        object.fields += L"\r\nAction body unsupported or invalid length.";
    }
    std::wostringstream raw;
    raw << L"\r\nCopied payload bytes:" << std::hex << std::setfill(L'0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        raw << (index % 16 == 0 ? L"\r\n" : L" ") << std::setw(2) << unsigned(bytes[index]);
    }
    object.fields += raw.str();
}
}

void inspect_database_fields(NativeDatabaseObject& object, const std::byte* image,
                             const native_game::Profile& profile) {
    switch (object.class_id) {
        case 0x41:
            inspect_action(object);
            break;
        case 0x42:
        case 0x52:
            inspect_list(object, image);
            break;
        case 0x51:
            inspect_database_trigger(object);
            break;
        case 0x27: {
            native_game::StoryTitle action{};
            if (!prefix(object, action)) {
                break;
            }
            object.description = L"Name ID " + std::to_wstring(action.name_id) +
                                 L" / Trigger list ID " + std::to_wstring(action.trigger_list_id);
            object.fields = L"Raw word +0xa4: " + std::to_wstring(action.raw_a4) +
                            L"\r\nRaw word +0xa6: " + std::to_wstring(action.raw_a6);
            reference(object, L"Name", 0x2f, action.name_id);
            reference(object, L"Trigger list", 0x52, action.trigger_list_id);
            break;
        }
        case 0x2f: {
            native_game::StoryName name{};
            if (!prefix(object, name)) {
                break;
            }
            object.description = string(name.strings[0]);
            object.fields = L"Text +0x28: " + object.description + L"\r\nText +0x38: " +
                            string(name.strings[1]);
            break;
        }
        case 0x39: {
            native_game::StoryHotspot hotspot{};
            if (!prefix(object, hotspot)) {
                break;
            }
            object.description =
                L"Name ID " + std::to_wstring(hotspot.name_id) + L" / Bounds " +
                std::to_wstring(hotspot.left) + L"," + std::to_wstring(hotspot.top) + L" to " +
                std::to_wstring(hotspot.right) + L"," + std::to_wstring(hotspot.bottom);
            object.fields = L"Raw shape value +0x40: " + std::to_wstring(hotspot.shape_value);
            reference(object, L"Name", 0x2f, hotspot.name_id);
            break;
        }
        case 0x54: {
            native_game::StoryStandardAction view{};
            if (!prefix(object, view)) {
                break;
            }
            object.description = L"Name ID " + std::to_wstring(view.name_id) + L" / Action ID " +
                                 std::to_wstring(view.action_id);
            object.fields = L"Raw value +0x2c: " + std::to_wstring(view.raw_value);
            reference(object, L"Name", 0x2f, view.name_id);
            reference(object, L"Action", 0x41, view.action_id);
            break;
        }
        case 0x53: {
            native_game::Variable variable{};
            if (!prefix(object, variable)) {
                break;
            }
            object.variable = DatabaseVariable{variable.raw_value, variable.type_flags};
            object.description = L"Value " + std::to_wstring(variable.raw_value) + L" / type " +
                                 std::to_wstring(variable.type_flags & 0x7f);
            object.fields = L"Raw value: " + std::to_wstring(variable.raw_value) + L"\r\nType: " +
                            std::to_wstring(variable.type_flags & 0x7f) + L"\r\nType flags: " +
                            std::to_wstring(variable.type_flags);
            break;
        }
        case 0x35:
            object.description = native_game::read_asset_path(
                reinterpret_cast<const void*>(object.address), image, profile);
            break;
        default:
            break;
    }
}
}
