#include "browser_state.h"
#include <algorithm>

namespace devtools::database_browser {
void text_candidate_content(Browser& state, const Row& row) {
    if (!state.database) {
        return;
    }
    const auto& candidates = state.database->strings();
    const auto found = std::lower_bound(
        candidates.begin(), candidates.end(), row.key,
        [](const auto& candidate, auto offset) { return candidate.offset < offset; });
    if (found == candidates.end() || found->offset != row.key) {
        return;
    }
    std::wstring content;
    for (std::size_t i = 0; i < found->text.size(); ++i) {
        const auto ch = found->text[i];
        if (ch == '\r') {
            if (i + 1 < found->text.size() && found->text[i + 1] == '\n') {
                ++i;
            }
            content += L"\r\n";
        } else if (ch == '\n') {
            content += L"\r\n";
        } else {
            content += wchar_t(ch);
        }
    }
    SetWindowTextW(state.content, content.c_str());
    const auto property = [&](const std::wstring& text) {
        TVINSERTSTRUCTW node{};
        node.hParent = TVI_ROOT;
        node.hInsertAfter = TVI_LAST;
        node.item.mask = TVIF_TEXT;
        node.item.pszText = const_cast<LPWSTR>(text.c_str());
        TreeView_InsertItem(state.properties, &node);
    };
    property(L"ASCII scan fragment. May contain partial strings or unused data.");
    property(L"HTML tags are shown as source text. A complete document is not established.");
    property(L"Text bytes: " + std::to_wstring(found->text.size()));
    const auto end = std::size_t(found->offset) + found->text.size();
    const auto bytes = state.database->bytes();
    property(end == bytes.size() ? L"Scan boundary: end of file"
             : bytes[end] == 0   ? L"Scan boundary: zero byte"
                                 : L"Scan boundary: non-text byte " + hex(bytes[end]));
}
}
