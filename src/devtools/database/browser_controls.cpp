#include "browser_state.h"
#include "browser_accessibility.h"
#include <algorithm>
#include <stdexcept>
#include <set>

namespace devtools::database_browser {
LRESULT CALLBACK control_keys(HWND window, UINT message, WPARAM key, LPARAM data, UINT_PTR,
                              DWORD_PTR owner) {
    const auto root = reinterpret_cast<HWND>(owner);
    const bool search_key = key == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) &&
                            !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000);
    if (search_key && message == WM_GETDLGCODE) {
        return DefSubclassProc(window, message, key, data) | DLGC_WANTMESSAGE;
    }
    if (search_key && message == WM_KEYDOWN) {
        const auto search = GetDlgItem(GetParent(window), search_id);
        SetFocus(search);
        SendMessageW(search, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_GETDLGCODE && key == VK_RETURN &&
        (GetDlgCtrlID(window) == list_id || GetDlgCtrlID(window) == relations_id ||
         GetDlgCtrlID(window) == database_classes_id)) {
        return DefSubclassProc(window, message, key, data) | DLGC_WANTMESSAGE;
    }

    if (message == WM_KEYDOWN && key == VK_ESCAPE) {
        SendMessageW(root, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && key == VK_TAB) {
        SetFocus(GetNextDlgTabItem(root, window, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return 0;
    }
    return DefSubclassProc(window, message, key, data);
}

void create_controls(Browser& state, HMODULE module, HFONT font, HWND parent) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        throw std::runtime_error("Cannot initialize database controls");
    }
    const auto child = [&](const wchar_t* name, const wchar_t* caption, DWORD style, int id) {
        const auto control = CreateWindowExW(
            0, name, caption, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 1, 1,
            state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module, nullptr);
        if (!control) {
            throw std::runtime_error("Cannot create database browser control");
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (style & WS_TABSTOP) {
            SetWindowSubclass(control, control_keys, 1, reinterpret_cast<DWORD_PTR>(parent));
        }
        return control;
    };
    state.mode = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, mode_id);
    unsigned mode = 0;
    for (const auto* name :
         {L"Loaded objects", L"Media labels", L"Text candidates", L"Native reads", L"Variables",
          L"Changed variables", L"Asset files", L"Database tables"}) {
        if (state.snapshot_provider || mode == 1 || mode == 2 || mode == 6 || mode == 7) {
            const auto index =
                SendMessageW(state.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            SendMessageW(state.mode, CB_SETITEMDATA, index, mode);
        }
        ++mode;
    }
    select_mode(state);
    state.filter = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, filter_id);
    configure_filter(state);
    state.search = child(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, search_id);
    SendMessageW(state.search, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search ID, name, path or field"));
    state.refresh = child(L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP, refresh_id);
    state.auto_refresh =
        child(L"BUTTON", L"Auto-refresh", BS_AUTOCHECKBOX | WS_TABSTOP, auto_refresh_id);
    ShowWindow(state.auto_refresh, state.snapshot_provider ? SW_SHOW : SW_HIDE);
    EnableWindow(state.auto_refresh, bool(state.snapshot_provider));
    state.back = child(L"BUTTON", L"Back", BS_PUSHBUTTON | WS_TABSTOP, back_id);
    state.list = child(WC_LISTVIEWW, L"",
                       LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER |
                           WS_TABSTOP,
                       list_id);
    install_list_accessibility(state.list);
    state.title = child(L"STATIC", L"Select a record", SS_LEFT, title_id);
    state.tabs = child(WC_TABCONTROLW, L"", WS_TABSTOP, tabs_id);
    state.properties = child(WC_TREEVIEWW, L"",
                             TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
                                 WS_BORDER | WS_TABSTOP,
                             properties_id);
    state.relations = child(WC_LISTVIEWW, L"",
                            LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                WS_BORDER | WS_TABSTOP,
                            relations_id);
    state.follow = child(L"BUTTON", L"Follow link", BS_PUSHBUTTON | WS_TABSTOP, follow_id);
    state.preview = child(L"BUTTON", L"Open preview", BS_PUSHBUTTON | WS_TABSTOP, preview_id);
    for (const auto list : {state.list, state.relations}) {
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        int column = 0;
        for (const auto* name : {L"Direction", L"Field", L"Target", L"Availability"}) {
            LVCOLUMNW heading{};
            heading.mask = LVCF_TEXT;
            heading.pszText = const_cast<LPWSTR>(name);
            ListView_InsertColumn(list, column++, &heading);
        }
    }
    state.detail = child(L"EDIT", L"",
                         ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL |
                             WS_BORDER | WS_TABSTOP,
                         detail_id);
    state.fixed_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                   0, CLEARTYPE_QUALITY, 0, L"Consolas");
    SendMessageW(state.detail, WM_SETFONT, reinterpret_cast<WPARAM>(state.fixed_font), FALSE);
    state.content =
        child(L"EDIT", L"",
              ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
              content_id);
    SendMessageW(state.content, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
    state.font_view = child(L"STATIC", L"", SS_OWNERDRAW | WS_BORDER, font_id);
    state.font_preview = std::make_unique<FontPreview>();
    create_string_controls(state, module, font);
    create_table_controls(state, module, font);
    state.offset = child(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, offset_id);
    SendMessageW(state.offset, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Hex file offset"));
    state.jump = child(L"BUTTON", L"Go to offset", BS_PUSHBUTTON | WS_TABSTOP, jump_id);
    state.status = child(L"STATIC", L"Read-only game database", 0, status_id);
    state.database_view = child(L"BUTTON", L"Database tables",
                                BS_PUSHLIKE | BS_CHECKBOX | WS_TABSTOP, database_view_id);
    state.asset_browse =
        child(L"BUTTON", L"Asset files", BS_PUSHLIKE | BS_CHECKBOX | WS_TABSTOP, asset_browse_id);
    state.database_heading = child(L"STATIC", L"Database tables", SS_LEFT, database_heading_id);
    state.list_context = child(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, list_context_id);
    // The tab background must paint behind its sibling page controls.
    SetWindowPos(state.tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void configure_filter(Browser& state) {
    SendMessageW(state.filter, CB_RESETCONTENT, 0, 0);
    const wchar_t* records[] = {L"All record types", L"Titles",          L"Names",
                                L"Asset references", L"Hotspots",        L"Actions",
                                L"Action lists",     L"Triggers",        L"Trigger lists",
                                L"Variables",        L"Standard actions"};
    const wchar_t* assets[] = {
        L"All assets",     L"Installed", L"Missing",      L"Movies",    L"Navigation archives",
        L"Image archives", L"Audio",     L"Images",       L"Databases", L"Fonts",
        L"Palettes",       L"Hotspots",  L"Other assets", L"Text (XT)", L"Localization"};
    const auto names = state.mode_index == 6 ? std::span<const wchar_t* const>(assets)
                                             : std::span<const wchar_t* const>(records);
    for (const auto* name : names) {
        SendMessageW(state.filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    }
    if (state.mode_index != 6) {
        constexpr std::uint32_t legacy[] = {0,    0x27, 0x2f, 0x35, 0x39, 0x41,
                                            0x42, 0x51, 0x52, 0x53, 0x54};
        std::set<std::uint32_t> classes;
        for (std::size_t index = 0; index < std::size(legacy); ++index) {
            SendMessageW(state.filter, CB_SETITEMDATA, index, legacy[index]);
            classes.insert(legacy[index]);
        }
        const auto add_class = [&](std::uint32_t cls) {
            if (classes.insert(cls).second) {
                const auto name = std::wstring(database_class_name(cls)) + L" (" + hex(cls) + L")";
                const auto index = SendMessageW(state.filter, CB_ADDSTRING, 0,
                                                reinterpret_cast<LPARAM>(name.c_str()));
                SendMessageW(state.filter, CB_SETITEMDATA, index, cls);
            }
        };
        for (const auto& record : state.stored.records) {
            add_class(record.class_id);
        }
        for (const auto& object : state.native.objects) {
            add_class(object.class_id);
        }
    }
    state.filter_index = 0;
    SendMessageW(state.filter, CB_SETCURSEL, 0, 0);
}

void layout(Browser& state) {
    RECT bounds{};
    GetClientRect(state.window, &bounds);
    const int width = std::max(600L, bounds.right), height = std::max(320L, bounds.bottom);
    const int sidebar = std::clamp(width / 6, 180, 240);
    const int list_x = sidebar + 12;
    const int split = std::max(180, (width - list_x) * 40 / 100);
    const int right = list_x + split + 12, detail_width = width - right;
    const bool compact = width < 1000 && state.snapshot_provider;
    const int mode_width = compact ? 155 : 175, filter_width = compact ? 135 : 170;
    MoveWindow(state.mode, list_x, 0, mode_width, 250, TRUE);
    MoveWindow(state.filter, list_x + mode_width + 8, 0, filter_width, 340, TRUE);
    const bool asset_filter = state.mode_index == 6;
    const int search_x = list_x + mode_width + 8 + (asset_filter ? filter_width + 8 : 0);
    ShowWindow(state.filter, asset_filter ? SW_SHOW : SW_HIDE);
    MoveWindow(state.search, search_x, 0,
               std::max(80, width - search_x - (state.snapshot_provider ? 330 : 192)), 28, TRUE);
    MoveWindow(state.auto_refresh, width - 318, 0, 130, 28, TRUE);
    MoveWindow(state.refresh, width - 180, 0, 90, 28, TRUE);
    MoveWindow(state.back, width - 82, 0, 82, 28, TRUE);
    MoveWindow(state.list_context, list_x, 40, split, 20, TRUE);
    MoveWindow(state.list, list_x, 64, split, height - 122, TRUE);
    MoveWindow(state.database_view, 0, 0, sidebar, 28, TRUE);
    MoveWindow(state.asset_browse, 0, 36, sidebar, 28, TRUE);
    MoveWindow(state.database_heading, 0, 78, sidebar, 20, TRUE);
    MoveWindow(state.database_classes, 0, 104, sidebar, std::max(80, height - 342), TRUE);
    MoveWindow(state.database_info, 0, height - 232, sidebar, 136, TRUE);
    MoveWindow(state.database_browse, 0, height - 88, sidebar, 28, TRUE);
    for (const auto control : {state.database_classes, state.database_info, state.database_browse,
                               state.database_heading}) {
        ShowWindow(control, state.database ? SW_SHOW : SW_HIDE);
    }
    ListView_SetColumnWidth(state.database_classes, 0, 0);
    ListView_SetColumnWidth(state.database_classes, 1, std::max(90, sidebar - 64));
    ListView_SetColumnWidth(state.database_classes, 2, 60);
    ListView_SetColumnWidth(state.database_classes, 3, 0);
    MoveWindow(state.title, right, 40, detail_width - 135, 52, TRUE);
    MoveWindow(state.tabs, right, 98, detail_width, height - 156, TRUE);
    RECT pane{0, 0, detail_width, height - 156};
    TabCtrl_AdjustRect(state.tabs, FALSE, &pane);
    const int x = right + pane.left + 4, y = 98 + pane.top + 4;
    const int w = std::max(40L, pane.right - pane.left - 8),
              h = std::max(40L, pane.bottom - pane.top - 8);
    MoveWindow(state.properties, x, y, w, h, TRUE);
    MoveWindow(state.content, x, y, w, h, TRUE);
    MoveWindow(state.font_view, x, y, w, h, TRUE);
    MoveWindow(state.relations, x, y, w, std::max(10, h - 38), TRUE);
    MoveWindow(state.follow, x, y + h - 30, 112, 28, TRUE);
    MoveWindow(state.preview, width - 125, 40, 125, 28, TRUE);
    MoveWindow(state.detail, x, y, w, std::max(10, h - 38), TRUE);
    MoveWindow(state.offset, x, y + h - 30, 160, 28, TRUE);
    MoveWindow(state.jump, x + 168, y + h - 30, 120, 28, TRUE);
    MoveWindow(state.status, 0, height - 48, width, 48, TRUE);
    const auto tab = selected_pane(state);
    ShowWindow(state.properties, tab == Pane::overview ? SW_SHOW : SW_HIDE);
    ShowWindow(state.content, tab == Pane::text ? SW_SHOW : SW_HIDE);
    ShowWindow(state.font_view, tab == Pane::font ? SW_SHOW : SW_HIDE);
    layout_strings(state, x, y, w, h, tab == Pane::strings);
    layout_tables(state, x, y, w, h, tab);
    for (const auto control : {state.relations, state.follow}) {
        ShowWindow(control, tab == Pane::links ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(state.detail, tab == Pane::raw ? SW_SHOW : SW_HIDE);
    const auto* row = selected_row(state);
    for (const auto control : {state.offset, state.jump}) {
        ShowWindow(control,
                   tab == Pane::raw && state.database && !(row && row->asset) ? SW_SHOW : SW_HIDE);
    }
    const bool assets = state.mode_index == 6;
    ListView_SetColumnWidth(state.list, 0, assets ? 160 : 80);
    ListView_SetColumnWidth(state.list, 1, assets ? 70 : 100);
    ListView_SetColumnWidth(state.list, 2, assets ? 80 : 72);
    ListView_SetColumnWidth(state.list, 3, std::max(120, split - (assets ? 315 : 257)));
    ListView_SetColumnWidth(state.relations, 0, 65);
    ListView_SetColumnWidth(state.relations, 1, 100);
    ListView_SetColumnWidth(state.relations, 2, std::max(140, w - 280));
    ListView_SetColumnWidth(state.relations, 3, 105);
}
}
