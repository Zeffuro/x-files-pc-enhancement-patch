#include "browser_stored_asset_ref.h"
#include "game/database/stored_asset_ref.h"

namespace devtools::database_browser {
namespace {
std::wstring escaped(std::span<const std::uint8_t> bytes) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring text;
    for (const auto byte : bytes) {
        if (byte >= 0x20 && byte <= 0x7e && byte != '\\') {
            text += wchar_t(byte);
        } else {
            text += L"\\x";
            text += digits[byte >> 4];
            text += digits[byte & 15];
        }
    }
    return text;
}
}

void stored_asset_ref_row(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                          Row& row) {
    if (!state.database) {
        return;
    }
    const auto ref = game_assets::parse_stored_asset_reference(
        state.database->bytes(), state.stored, record.class_id, record.id);
    if (!ref) {
        return;
    }
    row.columns[3] += L" | Asset name data: " + std::to_wstring(ref->name_size) + L" bytes";
    row.search += L" " + escaped(ref->name);
    row.search +=
        L" Flags (raw): " + std::to_wstring(ref->flags[0]) + L", " + std::to_wstring(ref->flags[1]);
    for (const auto value :
         {ref->name_mark, ref->name_size, ref->raw_words[0], ref->raw_words[1]}) {
        row.search += L" " + std::to_wstring(value) + L" " + hex(value);
    }
    for (unsigned i = 0; i < 2; ++i) {
        row.search +=
            L" Raw byte +" + std::to_wstring(22 + i) + L": " + std::to_wstring(ref->raw_bytes[i]);
    }
}

void stored_asset_ref_properties(Browser& state, const game_assets::StoredDatabaseRecord& record,
                                 std::wstring& raw) {
    if (!state.database || record.class_id != 0x35) {
        return;
    }
    const auto property = [&](HTREEITEM parent, const std::wstring& text) {
        TVINSERTSTRUCTW node{};
        node.hParent = parent;
        node.hInsertAfter = TVI_LAST;
        node.item.mask = TVIF_TEXT;
        node.item.pszText = const_cast<LPWSTR>(text.c_str());
        return TreeView_InsertItem(state.properties, &node);
    };
    const auto ref = game_assets::parse_stored_asset_reference(
        state.database->bytes(), state.stored, record.class_id, record.id);
    if (!ref) {
        property(TVI_ROOT, L"Stored asset-reference decoding unavailable or unsupported.");
        return;
    }
    const auto group = property(TVI_ROOT, L"Stored asset reference");
    property(group, L"Flags (raw): " + std::to_wstring(ref->flags[0]) + L", " +
                        std::to_wstring(ref->flags[1]));
    property(group, L"Name-data mark: " + hex(ref->name_mark));
    property(group, L"Name-data size: " + std::to_wstring(ref->name_size));
    property(group, L"Name data (escaped): " + escaped(std::span(ref->name).first(
                                                   std::min(ref->name.size(), std::size_t{256}))));
    if (ref->name.size() > 256) {
        property(group, L"Name preview shows first 256 bytes. Search and Raw retain all bytes.");
    }
    for (unsigned i = 0; i < 2; ++i) {
        property(group, L"Word +" + std::to_wstring(14 + 4 * i) + L" (raw): " +
                            std::to_wstring(ref->raw_words[i]) + L" (" + hex(ref->raw_words[i]) +
                            L")");
        property(group, L"Byte +" + std::to_wstring(22 + i) + L" (raw): " +
                            std::to_wstring(ref->raw_bytes[i]));
    }
    property(group, L"Decoded descriptor: 24 bytes, stored version 1");
    raw += L"\r\nStored asset-reference descriptor at " + hex(ref->offset) + L"\r\n" +
           state.database->hex(ref->offset, 24);
    for (std::size_t at = 0; at < ref->name.size(); at += 4096) {
        raw += L"\r\nStored asset name data at " + hex(ref->name_mark + at) + L"\r\n" +
               state.database->hex(ref->name_mark + at,
                                   std::min(ref->name.size() - at, std::size_t{4096}));
    }
    TreeView_Expand(state.properties, group, TVE_EXPAND);
}
}
