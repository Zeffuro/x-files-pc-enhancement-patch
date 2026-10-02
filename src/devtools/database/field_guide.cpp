#include "field_guide.h"
#include <optional>

namespace devtools {
namespace {
bool contains(std::wstring_view text, std::wstring_view part) {
    return text.find(part) != std::wstring_view::npos;
}

bool list_class(std::uint32_t cls) {
    switch (cls) {
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2a:
        case 0x2e:
        case 0x32:
        case 0x34:
        case 0x36:
        case 0x38:
        case 0x3a:
        case 0x3b:
        case 0x42:
        case 0x4d:
        case 0x4e:
        case 0x52:
        case 0x55:
            return true;
        default:
            return false;
    }
}

std::optional<unsigned> field_offset(std::wstring_view field) {
    if (!field.starts_with(L"Field +")) {
        return {};
    }
    unsigned offset = 0;
    std::size_t at = 7;
    const auto first = at;
    while (at < field.size() && field[at] >= L'0' && field[at] <= L'9') {
        if (offset > 1000) {
            return {};
        }
        offset = offset * 10 + unsigned(field[at++] - L'0');
    }
    return at == first ? std::nullopt : std::optional(offset);
}

std::wstring list_field(unsigned offset, std::uint32_t cls) {
    switch (offset) {
        case 6:
            return L"Total number of ordered member IDs across the complete resource tree.";
        case 10:
            return L"Absolute file position of the resource tree that stores the member IDs.";
        case 14:
            return L"Resource class recorded by the list descriptor. ID leaves use class 0x0b and "
                   L"branches use class 0x06.";
        case 18:
            return L"Raw resource type tag. Its bytes are preserved. A gameplay meaning is "
                   L"unknown.";
        case 22:
            return cls == 0x42 || cls == 0x52
                       ? L"Raw descriptor metadata word. The action or trigger member class is "
                         L"established separately by native accessors."
                       : L"Raw query-class word forwarded by the native list iterator. A fixed "
                         L"member class and class-1 wildcard meaning are unverified.";
        case 26:
            return L"Serialized needs-release byte used by the native resource descriptor. Runtime "
                   L"ownership is not inspected.";
        case 27:
            return L"Serialized duplicate byte used by the native resource descriptor. Runtime "
                   L"copy behavior is not inspected.";
        case 28:
            return L"Raw resource identity in the descriptor. It is distinct from this record's ID "
                   L"and the member IDs.";
        default:
            return {};
    }
}

std::wstring scalar_meaning(std::uint32_t cls, unsigned offset, bool signed_value) {
    if (list_class(cls)) {
        if (auto meaning = list_field(offset, cls); !meaning.empty()) {
            return meaning;
        }
    }
    if (cls == 0x46) {
        if (offset == 6) {
            return L"Full target record ID. Native lookup pairs this ID with the query-class byte "
                   L"at +11.";
        }
        if (offset == 10) {
            return L"Raw control byte in VCEnabled. Its purpose is unknown. Values 0 and 1 alone "
                   L"do not prove a Boolean meaning.";
        }
        if (offset == 11) {
            return L"Unsigned class requested when resolving the target ID. A unique same-file "
                   L"match is a stored candidate.";
        }
    }
    if ((cls == 0x27 && offset == 60) || (cls == 0x39 && offset == 18) ||
        (cls == 0x54 && offset == 6)) {
        return L"Name record ID, referring to VCName class 0x2f. It identifies a name definition "
               L"rather than localized UI text.";
    }
    if (cls == 0x27 && offset == 64) {
        return L"Trigger-list record ID, referring to VCTriggerList class 0x52.";
    }
    if (cls == 0x54 && offset == 14) {
        return L"Action record ID, referring to VCAction class 0x41. Execution is not evaluated.";
    }
    const bool rect = ((cls == 0x37 || cls == 0x39 || cls == 0x4f) && offset >= 10 &&
                       offset <= 16 && offset % 2 == 0) ||
                      (cls == 0x2d && offset >= 14 && offset <= 20 && offset % 2 == 0) ||
                      (cls == 0x59 && offset >= 22 && offset <= 28 && offset % 2 == 0);
    if (rect) {
        return L"Component of the inline rectangle. Native readers interpret these 16-bit bits as "
               L"signed values. Coordinate space and runtime use are not established.";
    }
    if ((cls == 0x3d && (offset == 6 || offset == 8)) ||
        (cls == 0x2a && (offset == 32 || offset == 34))) {
        return L"Component of the inline point. Native readers interpret these 16-bit bits as "
               L"signed values. Units and runtime use are not established.";
    }
    if (cls == 0x31 && offset == 30) {
        return L"Raw conversation Boolean byte. The native reader tests nonzero as true. What the "
               L"flag controls is unknown.";
    }
    if (cls == 0x3e || cls == 0x40) {
        return L"One of three words in a fixed ordered array. Member purpose and target class are "
               L"unknown. No list count is stored.";
    }
    if (cls == 0x27 && (offset == 68 || offset == 70)) {
        return L"Unsigned 16-bit Title field. The native reader preserves it without sign "
               L"extension. Purpose unknown.";
    }
    if (signed_value) {
        return L"Signed 16-bit interpretation proved by the native reader. The unsigned bits are "
               L"also retained. Purpose unknown.";
    }
    return L"Unsigned scalar read at this byte offset in the stored descriptor. Multi-byte values "
           L"are big endian. Purpose unknown.";
}
}

std::wstring database_class_purpose(std::uint32_t cls) {
    switch (cls) {
        case 0x27:
            return L"Title definition combining ordered members, three byte resources, a name ID "
                   L"and a trigger-list ID. Remaining field purposes are unknown.";
        case 0x28:
            return L"Node definition in the scene family. Contains an ordered member list and two "
                   L"scalar words whose purposes are unknown.";
        case 0x29:
            return L"Location definition in the scene family. Contains ordered members, three "
                   L"scalar words and a byte. Their detailed purposes are unknown.";
        case 0x2a:
            return L"Viewpoint definition with ordered members, an inline point and two words. "
                   L"Coordinate use and scalar purposes are unknown.";
        case 0x2b:
            return L"View definition referenced by Set View actions. Stores six words. Their "
                   L"individual purposes are unknown.";
        case 0x2c:
            return L"Character definition. Stores one word whose purpose is unknown. A character's "
                   L"identity is not inferred from this value.";
        case 0x2d:
            return L"Character-view definition with inline rectangle geometry and five words. The "
                   L"words and geometry's runtime use are unknown.";
        case 0x2e:
            return L"Character-view list definition. Its complete ordered member IDs are "
                   L"available. A fixed member-class contract is unverified.";
        case 0x2f:
            return L"Name definition holding two counted byte strings. Useful for inspecting "
                   L"authoring names. Encoding and localization roles are unverified.";
        case 0x31:
            return L"Conversation definition with six words and a Boolean byte. Speaker, dialogue "
                   L"text and branch mappings are unverified.";
        case 0x32:
            return L"Conversation list definition. Stores ordered member IDs, including duplicates "
                   L"and nulls. Member-class routing remains unverified.";
        case 0x33:
            return L"Navigation definition. Stores three complete words. Destination, media and "
                   L"runtime movement meanings are unknown.";
        case 0x34:
            return L"Navigation list definition. Stores complete ordered member IDs. A fixed "
                   L"member-class contract is unverified.";
        case 0x35:
            return L"Asset reference with counted name data and four trailing fields. Cached "
                   L"instances can expose an asset path. Stored name bytes may contain binary "
                   L"data.";
        case 0x36:
            return L"Asset-reference list reached by Asset and 3D Sound actions. Stores ordered "
                   L"IDs through a resource tree. Individual member routing remains unverified.";
        case 0x37:
            return L"Explorable definition with inline rectangle geometry and two words. "
                   L"Interaction behavior and word purposes are unknown.";
        case 0x38:
            return L"Explorable list definition with ordered member IDs. A fixed member-class "
                   L"contract is unverified.";
        case 0x39:
            return L"Hotspot definition with inline rectangle geometry, a raw shape value and a "
                   L"name ID. Stored geometry does not establish click effects.";
        case 0x3a:
            return L"Hotspot list definition with ordered IDs. Native readers are verified, but "
                   L"this class has no indexed records in the checked installed files.";
        case 0x3b:
            return L"Standard hotspot list definition using the shared ordered ID resource tree. "
                   L"Member routing and runtime priority are unverified.";
        case 0x3c:
            return L"Asset-category definition with one word and a byte. Category classification "
                   L"and field purposes are unknown.";
        case 0x3d:
            return L"Cursor definition with an inline point and two words. Image mapping and point "
                   L"units are unverified.";
        case 0x3e:
            return L"Default cursor definition holding exactly three ordered words. Their cursor "
                   L"roles and target classes are unknown.";
        case 0x40:
            return L"Default hotspot definition holding exactly three ordered words. Their hotspot "
                   L"roles and target classes are unknown.";
        case 0x41:
            return L"Encoded action definition. Inspect its optional condition and Statement, "
                   L"Asset, Timer, Enable, Set View, Interface, C++ Function or 3D Sound body. "
                   L"Nothing is executed.";
        case 0x42:
            return L"Ordered action list. Each member ID refers to VCAction class 0x41. Unique "
                   L"nonzero same-file definitions can be followed.";
        case 0x46:
            return L"Enabled-state record pairing a target ID with a query class and a raw control "
                   L"byte. Unique same-file targets are candidates, not proof of current enabled "
                   L"state.";
        case 0x47:
            return L"Action-icon definition storing seven words. Icon artwork, associated action "
                   L"and field purposes are unverified.";
        case 0x48:
            return L"Emotion-icon definition storing seven words. Emotion selection, artwork and "
                   L"field purposes are unverified.";
        case 0x49:
            return L"Evidence-icon definition storing seven words. Evidence selection, artwork and "
                   L"field purposes are unverified.";
        case 0x4a:
            return L"Idea-icon definition storing seven words. Idea selection, artwork and field "
                   L"purposes are unverified.";
        case 0x4b:
            return L"Inventory definition storing seven words. Item identity, availability and "
                   L"field purposes are unverified.";
        case 0x4c:
            return L"Idea-map definition with two words. Their relation to story choices or other "
                   L"records is unknown.";
        case 0x4d:
            return L"Idea-map list definition with ordered member IDs. A fixed member-class "
                   L"contract and story-graph meaning are unverified.";
        case 0x4e:
            return L"Interface layout referenced by Interface actions. Combines ordered member "
                   L"IDs, an extra word and a byte. Widget routing and control meanings are "
                   L"unknown.";
        case 0x4f:
            return L"Interface-item definition with inline rectangle geometry, three words and two "
                   L"additional shorts. Widget behavior and scalar purposes are unknown.";
        case 0x50:
            return L"String definition containing one counted byte resource. Complete bytes are "
                   L"available. Encoding and display purpose are unverified.";
        case 0x51:
            return L"Trigger definition binding an event byte to an action-list ID. Event 8 means "
                   L"Object Activation. Other event meanings remain unknown. Runtime parameters "
                   L"are separate.";
        case 0x52:
            return L"Ordered trigger list. Each member ID refers to VCTrigger class 0x51. Unique "
                   L"nonzero same-file definitions can be followed.";
        case 0x53:
            return L"Variable with a stored name, 32-bit value, owner word and raw flag/type "
                   L"bytes. Actions can refer to its ID. Meaning depends on the registered "
                   L"variable.";
        case 0x54:
            return L"Standard action associates a name ID and action ID with a raw word. Native "
                   L"readers are verified, but no indexed records occur in the checked installed "
                   L"files.";
        case 0x55:
            return L"Standard-action list definition. Its ordered resource tree is decoded. Both "
                   L"lists in the checked installed files are empty.";
        case 0x56:
            return L"Conversation-history state containing counted bytes consumed natively as "
                   L"ordered little-endian words. Entry meanings and fixed target classes are "
                   L"unknown.";
        case 0x57:
            return L"Serialized game-state record with scalar fields and five opaque byte "
                   L"resources. Field meanings and current scene mapping are unverified.";
        case 0x58:
            return L"PDA-notes state containing counted bytes consumed natively as ordered "
                   L"little-endian words. Entry meanings and text-file mapping are unverified.";
        case 0x59:
            return L"Photo state containing one counted byte resource, inline rectangle geometry, "
                   L"two words and two bytes. Image identity and field purposes are unverified.";
        case 0x5a:
            return L"Email state containing counted bytes consumed natively as ordered "
                   L"little-endian words. Email identity and text-file mapping are unverified.";
        case 0x5b:
            return L"Email-read state containing counted bytes consumed natively as ordered "
                   L"little-endian words. Individual read-marker meanings are unverified.";
        case 0x5c:
            return L"Email-pending state containing counted bytes consumed natively as ordered "
                   L"little-endian words. Delivery conditions and entry meanings are unverified.";
        default:
            return L"Record class without a verified purpose guide. Its identity and available raw "
                   L"data can still be inspected.";
    }
}

std::wstring database_field_meaning(std::uint32_t cls, std::wstring_view group,
                                    std::wstring_view field) {
    if (field == L"Class") {
        return L"Registered native record type. Class and ID together identify a record within a "
               L"database.";
    }
    if (field == L"ID") {
        return L"Record identity from this database's index. Equal IDs in different classes or "
               L"files need not identify the same object.";
    }
    if (field == L"File offset" || field == L"Stored HDB offset") {
        return L"Absolute byte position of the indexed definition in the opened file. It is not a "
               L"record length.";
    }
    if (field == L"Address") {
        return L"Address of the copied cached object in the current game process.";
    }
    if (field == L"Vtable RVA") {
        return L"Virtual-method table position relative to the game executable's loaded base.";
    }
    if (field == L"Database") {
        return L"Native cache source copied for this object, either the definition HDB or mutable "
               L"state database.";
    }
    if (field == L"References") {
        return L"Copied native reference count. Inspection does not change object ownership.";
    }
    if (contains(field, L"Flags (raw)")) {
        return L"Two original persistent flag bytes. Native stream flags have a different bit "
               L"representation. Other flag meanings remain unknown.";
    }
    if (field.starts_with(L"Decoded descriptor") || field.starts_with(L"Decoded extent")) {
        return L"Number of descriptor bytes consumed by the verified reader for stored version 1. "
               L"Allocation length and padding are not established.";
    }
    if (const auto offset = field_offset(field)) {
        return scalar_meaning(cls, *offset, contains(field, L"signed"));
    }
    if (field == L"Count") {
        return L"Total ordered member count. The decoder checks this against every ID reached "
               L"through the resource tree.";
    }
    if (field == L"ID-resource mark") {
        return list_field(10, cls);
    }
    if (field == L"Resource class") {
        return list_field(14, cls);
    }
    if (field == L"Resource type (raw)") {
        return list_field(18, cls);
    }
    if (field == L"Descriptor word +22 (raw)" || field == L"Resource owner (raw)") {
        return list_field(22, cls);
    }
    if (field == L"Needs-release byte (raw)") {
        return list_field(26, cls);
    }
    if (field == L"Duplicate byte (raw)") {
        return list_field(27, cls);
    }
    if (field == L"Resource ID (raw)") {
        return list_field(28, cls);
    }
    if (field == L"Resource nodes") {
        return L"Number of complete ID leaves and child-mark branches read to reconstruct the "
               L"list.";
    }
    if (field == L"Decoded resource bytes") {
        return L"Sum of consumed bytes across the resource tree. This excludes unknown allocation "
               L"slack.";
    }
    if (field.starts_with(L"Action[") || field.starts_with(L"Trigger[")) {
        return cls == 0x52 ? L"Trigger ID at this position in stored order. The target class is "
                             L"VCTrigger 0x51. Nulls and duplicates are retained."
                           : L"Action ID at this position in stored order. The target class is "
                             L"VCAction 0x41. Nulls and duplicates are retained.";
    }
    if (field.starts_with(L"Member[") || field.starts_with(L"Asset[")) {
        return L"Raw member ID at this position in stored order. Nulls and duplicates are "
               L"retained. A fixed target class is unverified.";
    }
    if (field.starts_with(L"String[") || field.starts_with(L"Resource[")) {
        if (contains(field, L" mark")) {
            return L"Absolute file position of the counted byte resource. A zero mark with zero "
                   L"length represents no data.";
        }
        if (contains(field, L" size")) {
            return L"Counted resource length in bytes, including any NUL or binary bytes. It is "
                   L"not a character or record count.";
        }
        if (contains(field, L" data")) {
            return cls == 0x57 || cls == 0x59
                       ? L"Escaped preview of an opaque resource. Its contents' purpose is "
                         L"unknown. Complete original bytes remain in Raw."
                       : L"Escaped preview of counted bytes. Encoding and content meaning are "
                         L"unverified. Search and Raw retain the complete data.";
        }
    }
    if (field.starts_with(L"LE32[")) {
        return L"Complete little-endian 32-bit word from the counted resource, in stored order. "
               L"Entry purpose and target class are unknown.";
    }
    if (field == L"Trailing raw bytes") {
        return L"Resource bytes left after the complete four-byte words. Native collection readers "
               L"floor the word count. These bytes remain preserved.";
    }
    if (field == L"Name-data mark" || field == L"Name file offset") {
        return L"Absolute file position of the counted name bytes.";
    }
    if (field == L"Name-data size") {
        return L"Number of stored name bytes, including any terminal NUL or embedded binary data.";
    }
    if (field == L"Name data (escaped)") {
        return L"Counted asset-name bytes shown with escapes. Some installed names contain binary "
               L"fields rather than a simple filename. Complete bytes remain in Raw.";
    }
    if (cls == 0x53) {
        if (field == L"Name") {
            return L"Stored variable registration name. This decoder accepts counted printable "
                   L"ASCII ending in NUL. A friendly gameplay meaning is not inferred.";
        }
        if (field == L"Value bits" || field == L"Raw value") {
            return L"All 32 bits of the variable value. Stored values describe this file, while "
                   L"cached values describe the copied runtime instance.";
        }
        if (field == L"Value (signed 32-bit)") {
            return L"The same 32 value bits interpreted as a signed integer. No bits are "
                   L"discarded.";
        }
        if (field == L"Owner ID (raw)") {
            return L"Raw stored owner word. Its target class and gameplay purpose are unverified.";
        }
        if (field == L"Flag (raw)") {
            return L"Original variable flag byte at stored +22. Its gameplay purpose is unknown.";
        }
        if (field == L"Type (raw)" || field == L"Type" || field == L"Type flags") {
            return L"Copied variable metadata byte. Native display separates the low seven type "
                   L"bits from the full byte. Gameplay meaning remains unverified.";
        }
        if (field == L"Previous refresh") {
            return L"Prior copied value for the same class, ID and database. Only unique readable "
                   L"matches establish this comparison.";
        }
    }
    if (cls == 0x51) {
        if (field == L"Action-list ID") {
            return L"VCActionList class 0x42 identity to run for this trigger. A unique nonzero "
                   L"same-file definition can be followed.";
        }
        if (field == L"Event type (raw)" || field.starts_with(L"Raw trigger type")) {
            return L"Event selector stored directly in the trigger. Value 8 is Object Activation. "
                   L"Other values remain numeric because their meanings are unverified.";
        }
        if (field == L"Event") {
            return L"Native event label for value 8, Object Activation. This does not prove that "
                   L"the trigger has fired.";
        }
        if (contains(group, L"Runtime parameters") || contains(field, L"parameter[")) {
            return L"Copied dispatch-context slot. Kind-1 operands select integers 0..7, Boolean32 "
                   L"values 10000..10011 or signed bytes 20000..20003. Stored triggers contain no "
                   L"parameter arrays.";
        }
    }
    if (cls == 0x46 && field == L"Target ID") {
        return scalar_meaning(cls, 6, false);
    }
    if (cls == 0x46 && field == L"Target query class") {
        return scalar_meaning(cls, 11, false);
    }
    if (cls == 0x41) {
        if (field == L"Raw type byte") {
            return L"Low seven bits select body subtype 0..7. Bit 7 adds a 12-byte condition "
                   L"before the body. Unknown or extended bodies remain raw.";
        }
        if (field == L"Payload file offset") {
            return L"Absolute mark of the external action bytes. They have no additional "
                   L"persistent header.";
        }
        if (field == L"Payload byte length") {
            return L"Number of encoded action bytes, including any condition prefix. Stored "
                   L"operand words are big endian. Cached operand words are little endian.";
        }
        if (field == L"Encoded condition") {
            return L"Predicate before the action body. Opcodes 0..7 mean equal, unequal, greater, "
                   L"less, greater-or-equal, less-or-equal, AND and OR. Evaluation is not "
                   L"performed.";
        }
        if (field == L"Statement") {
            return L"Encoded variable operation. Opcodes 0..7 are assignment, increment, "
                   L"decrement, addition, subtraction, multiplication, division and remainder. "
                   L"Kind-4 destinations are ignored natively.";
        }
        if (contains(field, L"operand") || contains(field, L"Operand") || field == L"Left kind" ||
            field == L"Right kind") {
            return L"Operand kind 0 is a variable ID, kind 1 a runtime parameter selector, kind 2 "
                   L"a signed integer and kind 4 a trigger-context index. Other kinds are unknown. "
                   L"Values are not evaluated.";
        }
        if (field == L"Asset-list ID" || field == L"3D Sound asset-list ID") {
            return L"VCAssetRefList class 0x36 identity. A unique nonzero same-file definition is "
                   L"a candidate link. Media loading and effects are not evaluated.";
        }
        if (field == L"Timer duration (raw)") {
            return L"Unsigned 32-bit duration passed by the Timer body. Time units and actual "
                   L"elapsed timing are unverified.";
        }
        if (field == L"Timer ID") {
            return L"Unsigned byte identifying runtime timer state. It has no stored database "
                   L"definition link.";
        }
        if (field.starts_with(L"Timer control") || field.starts_with(L"Timer selector") ||
            field.starts_with(L"Timer flag")) {
            return L"Raw timer control split into a low-seven-bit selector and high flag bit. "
                   L"Their detailed behavior is unverified.";
        }
        if (field == L"Enable target ID") {
            return L"Full ID requested as VCEnabled class 0x46 through native database routing. "
                   L"Same-file resolution is not established for this action.";
        }
        if (field == L"Enable control byte (raw)" || field == L"Enable flag (nonzero)") {
            return L"Control byte tested as nonzero by the native Enable reader. Current state and "
                   L"execution effects are not evaluated.";
        }
        if (field.starts_with(L"Set View") && contains(field, L" ID")) {
            return L"Stored candidate for the named scene component. View uses class 0x2b, node "
                   L"0x28, location 0x29 and viewpoint 0x2a. Runtime resolution is not evaluated.";
        }
        if (field == L"Interface layout ID") {
            return L"VCIFaceLayout class 0x4e identity. A unique nonzero same-file definition is a "
                   L"candidate link. Interface effects are unverified.";
        }
        if (field == L"C++ Function name") {
            return L"Function name preview bounded to 15 bytes and the first NUL. Function lookup "
                   L"and invocation are not performed.";
        }
        if (field == L"Function name bytes (raw)") {
            return L"All 15 original name bytes, including bytes after a NUL. They are retained "
                   L"independently of the name preview.";
        }
        if (field == L"Function name NUL within 15 bytes") {
            return L"Whether the fixed name field contains a NUL terminator. A false result "
                   L"remains explicit instead of reading beyond the field.";
        }
        if (field == L"Function argument count") {
            return L"Unsigned number of argument words and kind bytes. The body length is 16 plus "
                   L"five times this count.";
        }
        if (field.starts_with(L"Function argument ")) {
            return contains(field, L" kind")
                       ? L"Raw argument-kind byte from the separate kind array. Argument "
                         L"evaluation and kind meanings are unverified."
                       : L"Complete big-endian argument word. Its interpretation depends on the "
                         L"raw kind byte. Argument evaluation is not performed.";
        }
        if (field.starts_with(L"3D Sound word")) {
            return L"16-bit sound-body word, retained unsigned and projected as native signed "
                   L"16-bit. Axes, units and effects are unknown.";
        }
        if (contains(field, L"reference class")) {
            return L"Class requested by the native action accessor. A stored candidate does not "
                   L"establish the runtime database source or effects.";
        }
        if (contains(field, L"selector") || contains(field, L"control") ||
            contains(field, L"byte at body") || contains(field, L"word at body")) {
            return L"Original action-body field with verified width and byte order. Its detailed "
                   L"purpose is unknown.";
        }
    }
    if (cls == 0x2f && field.starts_with(L"Text +")) {
        return L"Bounded copy of one cached name string. Display accepts printable ASCII. Stored "
               L"counted resources preserve all original bytes.";
    }
    if (contains(field, L"unavailable") || contains(field, L"unsupported") ||
        contains(field, L"unreadable") || contains(field, L"uncached")) {
        return L"Decoding or copying did not produce a supported value. Inspect Raw for available "
               L"bytes. No meaning is inferred from a failed decode.";
    }
    return L"Purpose unknown or not yet verified. The displayed value is retained without "
           L"assigning a gameplay meaning.";
}
}
