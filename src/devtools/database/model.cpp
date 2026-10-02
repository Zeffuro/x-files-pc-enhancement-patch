#include "model.h"
#include <map>
#include <iomanip>
#include <sstream>
#include <tuple>

namespace devtools {
std::wstring database_native_hex(std::span<const std::uint8_t> bytes) {
    std::wostringstream out;
    out << std::hex << std::setfill(L'0');
    for (std::size_t offset = 0; offset < bytes.size(); offset += 16) {
        out << std::setw(8) << offset << L"  ";
        for (std::size_t byte = offset; byte < std::min(offset + 16, bytes.size()); ++byte) {
            out << std::setw(2) << unsigned(bytes[byte]) << L' ';
        }
        out << L"\r\n";
    }
    return out.str();
}

std::wstring_view database_class_name(std::uint32_t class_id) {
    switch (class_id) {
        case 0x27:
            return L"VCTitle";
        case 0x28:
            return L"VCNode";
        case 0x29:
            return L"VCLocaton";
        case 0x2a:
            return L"VCViewPoint";
        case 0x2b:
            return L"VCView";
        case 0x2f:
            return L"VCName";
        case 0x2e:
            return L"VCCharViewList";
        case 0x32:
            return L"VCConversationList";
        case 0x33:
            return L"VCNav";
        case 0x34:
            return L"VCNav_List";
        case 0x35:
            return L"VCAssetRef";
        case 0x36:
            return L"VCAssetRefList";
        case 0x38:
            return L"VCExplorableList";
        case 0x3a:
            return L"VCHotSpotList";
        case 0x3b:
            return L"VCStdHotSpotList";
        case 0x3e:
            return L"VCDefaultCursors";
        case 0x40:
            return L"VCDefaultHotSpots";
        case 0x4d:
            return L"VCIdeaMapList";
        case 0x39:
            return L"VCHotSpot";
        case 0x41:
            return L"VCAction";
        case 0x42:
            return L"VCActionList";
        case 0x46:
            return L"VCEnabled";
        case 0x4e:
            return L"VCIFaceLayout";
        case 0x51:
            return L"VCTrigger";
        case 0x52:
            return L"VCTriggerList";
        case 0x53:
            return L"VCVariable";
        case 0x54:
            return L"VCStdAction";
        case 0x55:
            return L"VCStdActionList";
        case 0x2c:
            return L"VCCharacter";
        case 0x37:
            return L"VCExplorable";
        case 0x3c:
            return L"VCAssetCategory";
        case 0x3d:
            return L"VCCursor";
        case 0x4c:
            return L"VCIdeaMap";
        case 0x4f:
            return L"VCInterfaceItem";
        case 0x50:
            return L"VCString";
        case 0x47:
            return L"VCActionIcon";
        case 0x48:
            return L"VCEmotionIcon";
        case 0x49:
            return L"VCEvidenceIcon";
        case 0x4a:
            return L"VCIdeaIcon";
        case 0x4b:
            return L"VCInventory";
        case 0x2d:
            return L"VCCharView";
        case 0x31:
            return L"VCConversation";
        case 0x56:
            return L"VCConversationHistory";
        case 0x57:
            return L"VCGameState";
        case 0x58:
            return L"VCPDANotes";
        case 0x59:
            return L"VCPhoto";
        case 0x5a:
            return L"VCEmail";
        case 0x5b:
            return L"VCEmailRead";
        case 0x5c:
            return L"VCEmailPending";
        default:
            return L"Class";
    }
}

std::optional<std::size_t> database_find(const NativeDatabaseSnapshot& snapshot,
                                         DatabaseObjectKey key) {
    std::optional<std::size_t> found;
    for (std::size_t index = 0; index < snapshot.objects.size(); ++index) {
        if (snapshot.objects[index].key() == key) {
            if (found) {
                return {};
            }
            found = index;
        }
    }
    return found;
}

std::optional<std::size_t> database_resolve(const NativeDatabaseSnapshot& snapshot,
                                            const DatabaseReference& reference) {
    std::optional<std::size_t> found;
    if (!reference.id) {
        return {};
    }
    for (std::size_t index = 0; index < snapshot.objects.size(); ++index) {
        const auto& object = snapshot.objects[index];
        if (object.class_id == reference.class_id && object.id == reference.id) {
            if (found) {
                return {};
            }
            found = index;
        }
    }
    return found;
}

std::vector<DatabaseLink> database_links(const NativeDatabaseSnapshot& snapshot,
                                         std::size_t source) {
    std::vector<DatabaseLink> links;
    if (source >= snapshot.objects.size()) {
        return links;
    }
    const auto label = [](const NativeDatabaseObject& object) {
        return std::wstring(database_class_name(object.class_id)) + L" ID " +
               std::to_wstring(object.id) + (object.state_database ? L" (State)" : L" (HDB)");
    };
    using ReferenceKey = std::pair<std::uint32_t, std::uint32_t>;
    std::map<ReferenceKey, std::optional<std::size_t>> targets;
    for (std::size_t index = 0; index < snapshot.objects.size(); ++index) {
        const auto& object = snapshot.objects[index];
        const ReferenceKey key{object.class_id, object.id};
        if (!targets.emplace(key, index).second) {
            targets[key].reset();
        }
    }
    const auto resolve = [&](const DatabaseReference& reference) -> std::optional<std::size_t> {
        if (!reference.id) {
            return {};
        }
        const auto found = targets.find({reference.class_id, reference.id});
        return found == targets.end() ? std::optional<std::size_t>{} : found->second;
    };
    for (const auto& reference : snapshot.objects[source].relationships) {
        if (const auto target = resolve(reference)) {
            links.push_back(
                {reference.field + L" -> " + label(snapshot.objects[*target]), *target});
        }
    }
    for (std::size_t index = 0; index < snapshot.objects.size(); ++index) {
        for (const auto& reference : snapshot.objects[index].relationships) {
            if (resolve(reference) == source) {
                links.push_back(
                    {L"From " + label(snapshot.objects[index]) + L" / " + reference.field, index});
            }
        }
    }
    return links;
}

std::vector<DatabaseObjectKey> database_variable_changes(const NativeDatabaseSnapshot& before,
                                                         const NativeDatabaseSnapshot& after) {
    std::vector<DatabaseObjectKey> changed;
    if (!before.available || !after.available || before.manager_address != after.manager_address ||
        before.hdb_address != after.hdb_address || before.state_address != after.state_address) {
        return changed;
    }
    using Key = std::tuple<std::uint32_t, std::uint32_t, bool>;
    std::map<Key, std::optional<DatabaseVariable>> values;
    for (const auto& object : before.objects) {
        const Key key{object.class_id, object.id, object.state_database};
        if (!values.emplace(key, object.variable).second) {
            values[key].reset();
        }
    }
    std::map<Key, unsigned> counts;
    for (const auto& object : after.objects) {
        ++counts[{object.class_id, object.id, object.state_database}];
    }
    for (const auto& object : after.objects) {
        const auto found = values.find({object.class_id, object.id, object.state_database});
        if (object.variable && found != values.end() && found->second &&
            *object.variable != *found->second &&
            counts[{object.class_id, object.id, object.state_database}] == 1) {
            changed.push_back(object.key());
        }
    }
    return changed;
}
}
