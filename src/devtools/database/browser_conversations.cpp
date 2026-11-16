#include "browser_state.h"
#include "game/database/stored_conversation.h"
#include "game/database/stored_asset_list.h"
#include "game/database/stored_asset_ref.h"
#include "game/database/stored_fields.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace devtools::database_browser {
namespace {
constexpr std::size_t row_limit = 4096;

std::wstring display_name(std::span<const std::uint8_t> bytes) {
    std::wstring result;
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (const auto byte : bytes) {
        if (result.size() >= 256) {
            result += L"... (name preview)";
            break;
        }
        if (!byte) {
            break;
        }
        if (byte >= 32 && byte < 127) {
            result += wchar_t(byte);
        } else {
            result += L"\\x";
            result += digits[byte >> 4];
            result += digits[byte & 15];
        }
    }
    return result.empty() ? L"(empty topic name)" : result;
}

std::wstring title(const game_assets::StoredConversation& value) {
    return value.name_available ? display_name(value.name)
                                : L"Topic " + std::to_wstring(value.id) + L" (name unavailable)";
}

std::vector<std::filesystem::path> movie_paths(std::span<const std::uint8_t> bytes) {
    std::set<std::filesystem::path> paths;
    for (std::size_t at = 0; at < bytes.size();) {
        if (bytes[at] < 32) {
            ++at;
            continue;
        }
        std::wstring token;
        while (at < bytes.size() && bytes[at] >= 32) {
            token += bytes[at] == 127 ? L'/' : wchar_t(bytes[at]);
            ++at;
        }
        std::size_t prefix = 0, length = 0;
        while (prefix < token.size() && prefix < 6 && token[prefix] >= L'0' &&
               token[prefix] <= L'9') {
            length = length * 10 + static_cast<std::size_t>(token[prefix++] - L'0');
        }
        if (prefix) {
            if (length != token.size() - prefix) {
                continue;
            }
            token.erase(0, prefix);
        }
        if (!token.starts_with(L"XV/") && !token.starts_with(L"XN/")) {
            continue;
        }
        if (const auto path = database_asset_path(token);
            path && lower(path->extension().wstring()) == L".xmv") {
            paths.insert(std::filesystem::path(lower(path->generic_wstring())));
        }
    }
    return {paths.begin(), paths.end()};
}

const ConversationRow* selected(const Browser& state) {
    const auto index = ListView_GetNextItem(state.conversations_list, -1, LVNI_SELECTED);
    return index >= 0 && std::size_t(index) < state.conversation_rows.size()
               ? &state.conversation_rows[static_cast<std::size_t>(index)]
               : nullptr;
}

void add(Browser& state, ConversationRow row) {
    if (state.conversation_rows.size() < row_limit) {
        state.conversation_rows.push_back(std::move(row));
    } else {
        state.conversations_limited = true;
    }
}

void media_rows(Browser& state, std::uint32_t list_id, const std::wstring& role,
                const std::wstring& topic, std::uint32_t topic_id, bool slots) {
    const auto list =
        game_assets::parse_stored_asset_list(state.database->bytes(), state.stored, 0x36, list_id);
    const auto origin =
        L"Database: " + state.path.wstring() + L"\r\nList ID: " + std::to_wstring(list_id) +
        L"\r\nStored candidates. Current eligibility and trigger outcomes are not evaluated.";
    if (!list_id || !list || list->ids.empty()) {
        add(state, {{role, topic, L"List " + std::to_wstring(list_id),
                     !list_id ? L"None"
                     : !list  ? L"Missing / unsupported"
                              : L"Empty"},
                    origin,
                    {},
                    {},
                    topic_id,
                    list_id,
                    {},
                    {}});
        return;
    }
    for (std::size_t index = 0; index < list->ids.size(); ++index) {
        if (state.conversation_rows.size() >= row_limit) {
            state.conversations_limited = true;
            break;
        }
        const auto id = list->ids[index];
        const auto ref = game_assets::parse_stored_asset_reference(state.database->bytes(),
                                                                   state.stored, 0x35, id);
        const auto paths = ref ? movie_paths(ref->name) : std::vector<std::filesystem::path>{};
        auto label = role + L" [" + std::to_wstring(index) + L"]";
        if (slots && index >= 7) {
            label = L"Extra stored reply [" + std::to_wstring(index) + L"]";
        } else if (slots && index == 2) {
            label += L" / select flashlight or binoculars";
        } else if (slots && index == 4) {
            label += L" / badge click fallback";
        }
        const auto detail = topic + L"\r\n" + origin + L"\r\nStored position: " +
                            std::to_wstring(index) + L"\r\nAsset reference ID: " +
                            std::to_wstring(id);
        const Destination destination{
            ref ? std::optional<DatabaseObjectKey>{{0x35, id, false}} : std::nullopt, {}, true};
        if (paths.empty()) {
            add(state, {{label, topic, L"Asset " + std::to_wstring(id),
                         !id   ? L"Null entry"
                         : ref ? L"No movie path"
                               : L"Missing / unsupported"},
                        detail,
                        destination,
                        {},
                        topic_id,
                        list_id,
                        id,
                        index});
        }
        for (const auto& path : paths) {
            const auto asset = database_asset_find(state.assets, path);
            const bool installed = asset && state.assets.assets[*asset].present;
            add(state,
                {{label, topic, path.generic_wstring(), installed ? L"Installed" : L"Missing file"},
                 detail + (paths.size() > 1
                               ? L"\r\nMultiple stored paths. No variant is selected here."
                               : L""),
                 destination,
                 path,
                 topic_id,
                 list_id,
                 id,
                 index});
        }
    }
}

void topic_rows(Browser& state, std::uint32_t id, bool first) {
    const auto value =
        game_assets::parse_stored_conversation(state.database->bytes(), state.stored, 0x31, id);
    if (!value) {
        add(state,
            {{L"Topic", L"Conversation " + std::to_wstring(id), L"", L"Missing / unsupported"},
             L"The exact conversation definition could not be decoded.",
             {},
             {},
             id,
             {},
             {},
             {}});
        return;
    }
    const auto name = title(*value);
    add(state,
        {{L"Topic", name, L"Conversation " + std::to_wstring(id), L"Stored definition"},
         L"Database: " + state.path.wstring() + L"\r\nName ID: " + std::to_wstring(value->name_id) +
             L"\r\nTrigger-list ID: " + std::to_wstring(value->triggers_id) + L"\r\nRaw flag: " +
             std::to_wstring(value->raw_flag) +
             L"\r\nThis topic's presence does not establish current availability.",
         {{{0x31, id, false}}, {}, true},
         {},
         id,
         {},
         {},
         {}});
    const bool stock = first && !value->icons_id && value->atoms_id;
    if (stock) {
        media_rows(state, value->atoms_id, L"Stock slot", name, id, true);
    } else {
        media_rows(state, value->question_id, L"Question", name, id, false);
        media_rows(state, value->responses_id, L"Response", name, id, false);
        if (value->atoms_id) {
            media_rows(state, value->atoms_id, L"Topic reply", name, id, false);
        }
    }
}
}

void conversation_row_name(const Browser& state, const game_assets::StoredDatabaseRecord& record,
                           Row& row) {
    if (state.database && record.class_id == 0x31) {
        if (const auto value = game_assets::parse_stored_conversation(
                state.database->bytes(), state.stored, record.class_id, record.id)) {
            row.columns[3] = title(*value);
        }
    } else if (state.database && record.class_id == 0x2d) {
        const auto picture = game_assets::parse_stored_fields(state.database->bytes(), state.stored,
                                                              0x2d, record.id);
        const auto character =
            picture ? game_assets::parse_stored_fields(state.database->bytes(), state.stored, 0x2c,
                                                       picture->scalars.front().value)
                    : std::nullopt;
        const auto name =
            character ? game_assets::parse_stored_fields(state.database->bytes(), state.stored,
                                                         0x2f, character->scalars.front().value)
                      : std::nullopt;
        if (name && !name->blobs.empty()) {
            row.columns[3] = display_name(name->blobs.front().bytes);
        }
    }
}

void update_conversations(Browser& state, std::uint32_t cls, std::uint32_t id) {
    const auto previous = ListView_GetNextItem(state.conversations_list, -1, LVNI_SELECTED);
    ListView_SetItemState(state.conversations_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    state.conversation_rows.clear();
    state.conversations_limited = false;
    state.conversation_source.reset();
    state.has_conversations = state.database && (cls == 0x31 || cls == 0x32 || cls == 0x2d);
    if (state.has_conversations) {
        state.conversation_source = DatabaseObjectKey{cls, id, false};
        if (cls == 0x31) {
            topic_rows(state, id, false);
        } else if (const auto list = game_assets::parse_stored_conversations(
                       state.database->bytes(), state.stored, cls, id)) {
            if (list->conversations.empty()) {
                add(state, {{L"Conversations", L"", L"List " + std::to_wstring(list->id), L"Empty"},
                            L"No conversation members are stored in this association.",
                            {},
                            {}});
            }
            for (std::size_t at = 0; at < list->conversations.size(); ++at) {
                if (state.conversation_rows.size() >= row_limit) {
                    state.conversations_limited = true;
                    break;
                }
                topic_rows(state, list->conversations[at], at == 0);
            }
        } else {
            add(state, {{L"Conversations", L"", L"", L"Missing / unsupported"},
                        L"The stored conversation association could not be decoded.",
                        {},
                        {}});
        }
        if (state.conversations_limited) {
            state.conversation_rows.push_back(
                {{L"Display limit", L"First 4096 rows shown", L"", L"Incomplete"},
                 L"Follow individual topic records to inspect their complete candidate lists.",
                 {},
                 {}});
        }
    }
    ListView_SetItemCount(state.conversations_list,
                          static_cast<int>(state.conversation_rows.size()));
    if (previous >= 0 && std::size_t(previous) < state.conversation_rows.size()) {
        ListView_SetItemState(state.conversations_list, previous, LVIS_SELECTED, LVIS_SELECTED);
    }
    select_conversation(state);
}

void select_conversation(Browser& state) {
    const auto* row = selected(state);
    SetWindowTextW(
        state.conversations_text,
        row ? row->detail.c_str()
            : L"Select a topic or candidate. Stock slots are conditional fallback replies. "
              L"Stored order is preserved. Current game state is not evaluated.");
    const auto asset =
        row && !row->media.empty() ? database_asset_find(state.assets, row->media) : std::nullopt;
    EnableWindow(state.conversations_open, asset && state.assets.assets[*asset].present &&
                                               state.assets.assets[*asset].previewable &&
                                               bool(state.open_asset));
    EnableWindow(state.conversations_follow, row && row->destination.object.has_value());
    EnableWindow(state.conversations_export, state.conversation_source.has_value());
}

void open_conversation_media(Browser& state) {
    const auto* row = selected(state);
    if (row && state.open_asset && !row->media.empty()) {
        const auto asset = database_asset_find(state.assets, row->media);
        if (asset && state.assets.assets[*asset].present &&
            state.assets.assets[*asset].previewable &&
            database_asset_file(state.root, row->media)) {
            const auto path = row->media;
            state.open_asset(path);
        }
    }
}

void follow_conversation(Browser& state) {
    if (const auto* row = selected(state); row && row->destination.object) {
        const auto target = row->destination;
        navigate(state, target, true);
    }
}

void browse_conversations(Browser& state, bool reactions) {
    switch_view(state, 7);
    for (LRESULT at = 0; at < SendMessageW(state.filter, CB_GETCOUNT, 0, 0); ++at) {
        if (SendMessageW(state.filter, CB_GETITEMDATA, at, 0) == (reactions ? 0x2d : 0x31)) {
            state.filter_index = static_cast<unsigned>(at);
            SendMessageW(state.filter, CB_SETCURSEL, at, 0);
            break;
        }
    }
    state.rebuilding = true;
    SetWindowTextW(state.search, L"");
    state.rebuilding = false;
    populate(state);
    if (!state.rows.empty()) {
        ListView_SetItemState(state.list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        describe(state);
    }
    select_pane(state, Pane::conversations);
    layout(state);
    SetFocus(state.search);
}

void create_conversation_controls(Browser& state, HMODULE module, HFONT font) {
    const auto child = [&](const wchar_t* type, const wchar_t* caption, DWORD style, int id) {
        const auto window = CreateWindowExW(
            0, type, caption, WS_CHILD | WS_CLIPSIBLINGS | style, 0, 0, 1, 1, state.window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
        if (!window) {
            throw std::runtime_error("Cannot create conversation explorer control");
        }
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SetWindowSubclass(window, control_keys, 1,
                          reinterpret_cast<DWORD_PTR>(GetParent(state.window)));
        return window;
    };
    state.conversations_browse =
        child(L"BUTTON", L"Conversations", BS_PUSHBUTTON | WS_TABSTOP, conversations_browse_id);
    state.reactions_browse =
        child(L"BUTTON", L"Character reactions", BS_PUSHBUTTON | WS_TABSTOP, reactions_browse_id);
    state.conversations_list = child(WC_LISTVIEWW, L"",
                                     LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL |
                                         LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP,
                                     conversations_list_id);
    state.conversations_text =
        child(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
              conversations_text_id);
    state.conversations_open =
        child(L"BUTTON", L"Open in Library", BS_PUSHBUTTON | WS_TABSTOP, conversations_open_id);
    state.conversations_follow =
        child(L"BUTTON", L"Follow record", BS_PUSHBUTTON | WS_TABSTOP, conversations_follow_id);
    state.conversations_export = child(L"BUTTON", L"Export reference...",
                                       BS_PUSHBUTTON | WS_TABSTOP, conversations_export_id);
    ListView_SetExtendedListViewStyle(state.conversations_list,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    int column = 0;
    for (const auto* name :
         {L"Role / position", L"Topic or reaction", L"Media / record", L"Availability"}) {
        LVCOLUMNW heading{};
        heading.mask = LVCF_TEXT;
        heading.pszText = const_cast<LPWSTR>(name);
        ListView_InsertColumn(state.conversations_list, column++, &heading);
    }
}

void layout_conversations(Browser& state, int x, int y, int width, int height, Pane pane) {
    const int description = std::min(140, std::max(72, height / 3));
    const int button_rows = width >= 401 ? 1 : width >= 248 ? 2 : 3;
    const int button_height = button_rows * 34;
    const int buttons_y = y + height - button_height + 6;
    MoveWindow(state.conversations_list, x, y, width,
               std::max(10, height - description - button_height - 6), TRUE);
    MoveWindow(state.conversations_text, x, y + height - description - button_height, width,
               description, TRUE);
    MoveWindow(state.conversations_open, x, buttons_y, 130, 28, TRUE);
    MoveWindow(state.conversations_follow, x + (width >= 248 ? 138 : 0),
               buttons_y + (width >= 248 ? 0 : 34), 110, 28, TRUE);
    MoveWindow(state.conversations_export, x + (button_rows == 1 ? 256 : 0),
               buttons_y + (button_rows - 1) * 34, 145, 28, TRUE);
    for (const auto control :
         {state.conversations_list, state.conversations_text, state.conversations_open,
          state.conversations_follow, state.conversations_export}) {
        ShowWindow(control, pane == Pane::conversations ? SW_SHOW : SW_HIDE);
    }
    ListView_SetColumnWidth(state.conversations_list, 0, 125);
    ListView_SetColumnWidth(state.conversations_list, 1, std::max(120, width - 375));
    ListView_SetColumnWidth(state.conversations_list, 2, 140);
    ListView_SetColumnWidth(state.conversations_list, 3, 110);
}

bool conversation_notification(Browser& state, NMHDR* notification) {
    if (notification->idFrom != conversations_list_id) {
        return false;
    }
    if (notification->code == LVN_GETDISPINFOW) {
        const auto info = reinterpret_cast<NMLVDISPINFOW*>(notification);
        if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 && info->item.iSubItem >= 0 &&
            info->item.iSubItem < 4 &&
            std::size_t(info->item.iItem) < state.conversation_rows.size()) {
            const auto& value = state.conversation_rows[std::size_t(info->item.iItem)]
                                    .columns[std::size_t(info->item.iSubItem)];
            wcsncpy_s(info->item.pszText, static_cast<std::size_t>(info->item.cchTextMax),
                      value.c_str(), _TRUNCATE);
        }
    } else if (notification->code == LVN_ITEMCHANGED) {
        select_conversation(state);
    } else if (notification->code == NM_DBLCLK ||
               (notification->code == LVN_KEYDOWN &&
                reinterpret_cast<NMLVKEYDOWN*>(notification)->wVKey == VK_RETURN)) {
        open_conversation_media(state);
    }
    return true;
}
}
