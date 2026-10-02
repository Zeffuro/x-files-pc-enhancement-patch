#include "pff_browser_state.h"
#include "pff_image.h"
#include <commctrl.h>
#include <algorithm>
#include <stdexcept>

namespace devtools::pff_browser {
namespace {
LRESULT CALLBACK keys(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                      DWORD_PTR owner) {
    const auto parent = reinterpret_cast<HWND>(owner);
    if (message == WM_KEYDOWN && value == VK_ESCAPE) {
        SendMessageW(parent, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && value == VK_TAB) {
        SetFocus(GetNextDlgTabItem(parent, window, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return 0;
    }
    return DefSubclassProc(window, message, value, data);
}

LRESULT CALLBACK picture_proc(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR,
                              DWORD_PTR owner) {
    auto& state = *reinterpret_cast<State*>(owner);
    if (state.view.scroll(window, message, value)) {
        return 0;
    }
    return message == WM_ERASEBKGND ? 1 : DefSubclassProc(window, message, value, data);
}

HWND child(State& state, const wchar_t* type, const wchar_t* caption, DWORD style, unsigned id) {
    const auto window =
        CreateWindowExW(0, type, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, state.window,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.module, nullptr);
    if (!window) {
        throw std::runtime_error("Cannot create archive browser control");
    }
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), FALSE);
    if (style & WS_TABSTOP) {
        SetWindowSubclass(window, keys, 1, reinterpret_cast<DWORD_PTR>(state.window));
    }
    return window;
}

void layout(State& state) {
    RECT bounds{};
    GetClientRect(state.window, &bounds);
    const int width = std::max(700L, bounds.right), height = std::max(500L, bounds.bottom);
    const int split = width * 40 / 100, right = split + 24;
    MoveWindow(state.list, 12, 12, split, height - 110, TRUE);
    MoveWindow(state.picture, right, 12, width - right - 12, height - 266, TRUE);
    MoveWindow(state.size, right, height - 242, 145, 160, TRUE);
    MoveWindow(state.details, right, height - 208, width - right - 12, 100, TRUE);
    MoveWindow(state.export_button, 12, height - 86, 125, 28, TRUE);
    MoveWindow(state.import_button, 145, height - 86, 125, 28, TRUE);
    MoveWindow(state.save, 278, height - 86, 150, 28, TRUE);
    MoveWindow(state.reset, 436, height - 86, 80, 28, TRUE);
    MoveWindow(state.status, 12, height - 48, width - 24, 40, TRUE);
    ListView_SetColumnWidth(state.list, 0, 70);
    ListView_SetColumnWidth(state.list, 1, 115);
    ListView_SetColumnWidth(state.list, 2, std::max(90, split - 190));
    state.view.refresh(state.picture, state.image.width, state.image.height);
}

void paint(const State& state, const DRAWITEMSTRUCT& item) {
    const auto width = item.rcItem.right - item.rcItem.left;
    const auto height = item.rcItem.bottom - item.rcItem.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    const auto dc = CreateCompatibleDC(item.hDC);
    const auto bitmap = CreateCompatibleBitmap(item.hDC, width, height);
    if (dc && bitmap) {
        const auto previous = SelectObject(dc, bitmap);
        RECT bounds{0, 0, width, height};
        FillRect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const auto& frame = state.image;
        if (frame.width && frame.height && !frame.pixels.empty()) {
            const auto destination =
                state.view.destination(state.picture, bounds, frame.width, frame.height);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = static_cast<LONG>(frame.width);
            info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            SetStretchBltMode(dc, state.view.actual ? COLORONCOLOR : HALFTONE);
            StretchDIBits(dc, destination.left, destination.top,
                          destination.right - destination.left,
                          destination.bottom - destination.top, 0, 0, frame.width, frame.height,
                          frame.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        } else {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(240, 240, 240));
            SelectObject(dc, state.font);
            DrawTextW(dc, L"No decoded image", -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, previous);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (dc) {
        DeleteDC(dc);
    }
}

void controls(State& state) {
    state.list = child(state, WC_LISTVIEWW, L"",
                       LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER |
                           WS_TABSTOP,
                       list_id);
    ListView_SetExtendedListViewStyle(state.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    int column = 0;
    for (const auto* name : {L"Entry", L"Header value (raw)", L"Bytes"}) {
        LVCOLUMNW heading{};
        heading.mask = LVCF_TEXT;
        heading.pszText = const_cast<LPWSTR>(name);
        ListView_InsertColumn(state.list, column++, &heading);
    }
    state.picture = child(state, L"STATIC", L"", SS_OWNERDRAW, picture_id);
    SetWindowSubclass(state.picture, picture_proc, 1, reinterpret_cast<DWORD_PTR>(&state));
    state.size = child(state, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, size_id);
    SendMessageW(state.size, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fit"));
    SendMessageW(state.size, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Actual size"));
    SendMessageW(state.size, CB_SETCURSEL, 0, 0);
    state.details =
        child(state, L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
              details_id);
    state.export_button = child(state, L"BUTTON", L"Export asset...", WS_TABSTOP, export_id);
    state.import_button = child(state, L"BUTTON", L"Import asset...", WS_TABSTOP, import_id);
    state.save = child(state, L"BUTTON", L"Save archive copy...", WS_TABSTOP, save_id);
    state.reset = child(state, L"BUTTON", L"Revert", WS_TABSTOP, reset_id);
    state.status = child(state, L"STATIC", L"", SS_LEFT, status_id);
    ListView_SetItemCount(state.list, static_cast<int>(state.archive->entries().size()));
    layout(state);
    if (!state.archive->entries().empty()) {
        ListView_SetItemState(state.list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    select(state);
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) {
        return DefWindowProcW(window, message, value, data);
    }
    try {
        if (message == WM_SIZE) {
            layout(*state);
        } else if (message == WM_GETMINMAXINFO) {
            reinterpret_cast<MINMAXINFO*>(data)->ptMinTrackSize = {750, 560};
            return 0;
        } else if (message == WM_DRAWITEM) {
            paint(*state, *reinterpret_cast<DRAWITEMSTRUCT*>(data));
            return TRUE;
        } else if (message == WM_COMMAND) {
            switch (LOWORD(value)) {
                case size_id:
                    if (HIWORD(value) == CBN_SELCHANGE) {
                        state->view.actual = SendMessageW(state->size, CB_GETCURSEL, 0, 0) == 1;
                        state->view.reset(state->picture);
                        state->view.refresh(state->picture, state->image.width,
                                            state->image.height);
                        InvalidateRect(state->picture, nullptr, FALSE);
                    }
                    break;
                case export_id:
                    export_asset(*state);
                    break;
                case import_id:
                    import_asset(*state);
                    break;
                case save_id:
                    save_archive(*state);
                    break;
                case reset_id:
                    reset_archive(*state);
                    break;
            }
        } else if (message == WM_NOTIFY) {
            const auto* notice = reinterpret_cast<NMHDR*>(data);
            if (notice->hwndFrom == state->list && notice->code == LVN_ITEMCHANGED) {
                select(*state);
            } else if (notice->hwndFrom == state->list && notice->code == LVN_GETDISPINFOW) {
                auto* item = &reinterpret_cast<NMLVDISPINFOW*>(data)->item;
                if ((item->mask & LVIF_TEXT) && item->iItem >= 0 && item->iSubItem >= 0 &&
                    std::size_t(item->iItem) < state->archive->entries().size()) {
                    const auto& entry = state->archive->entries()[item->iItem];
                    const auto label = std::to_wstring(item->iSubItem == 0   ? item->iItem
                                                       : item->iSubItem == 1 ? entry.header_value
                                                                             : entry.size);
                    if (item->pszText && item->cchTextMax > 0) {
                        wcsncpy_s(item->pszText, static_cast<std::size_t>(item->cchTextMax),
                                  label.c_str(), _TRUNCATE);
                    }
                }
            }
        } else if (message == WM_NCDESTROY) {
            *state->lifetime = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (state->owned) {
                delete state;
            }
        }
    } catch (const std::exception& error) {
        SetWindowTextA(state->status, error.what());
    }
    return DefWindowProcW(window, message, value, data);
}
}

void select(State& state) {
    state.selected.reset();
    state.image = {};
    const auto selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    std::wstring description;
    if (selected >= 0 && std::size_t(selected) < state.archive->entries().size()) {
        state.selected = static_cast<std::size_t>(selected);
        const auto& entry = state.archive->entries()[*state.selected];
        description = L"Entry " + std::to_wstring(selected) + L" | Offset " +
                      std::to_wstring(entry.offset) + L" | " + std::to_wstring(entry.size) +
                      L" bytes\r\nHeader value (raw): " + std::to_wstring(entry.header_value);
        try {
            state.image = decode_pff_image(state.archive->entry(*state.selected));
            description += L"\r\nImage: " + std::to_wstring(state.image.width) + L" x " +
                           std::to_wstring(state.image.height);
        } catch (const std::exception& error) {
            const std::string reason = error.what();
            description +=
                L"\r\nPreview unavailable: " + std::wstring(reason.begin(), reason.end());
        }
    }
    SetWindowTextW(state.details, description.c_str());
    const auto status =
        std::to_wstring(state.archive->entries().size()) + L" entries | " +
        (state.modified ? L"Modified copy, ready to save" : L"Original archive, read-only") +
        L"\r\nExport PNG or original PICT bytes. Import changes the copy in memory.";
    SetWindowTextW(state.status, status.c_str());
    EnableWindow(state.export_button, state.selected.has_value());
    EnableWindow(state.import_button, state.selected.has_value());
    EnableWindow(state.reset, state.modified);
    EnableWindow(state.size, !state.image.pixels.empty());
    state.view.reset(state.picture);
    state.view.refresh(state.picture, state.image.width, state.image.height);
    InvalidateRect(state.picture, nullptr, FALSE);
}
}

namespace devtools {
std::shared_ptr<HWND> open_pff_browser(HWND owner, HMODULE module, HFONT font,
                                       const std::filesystem::path& path, bool visible) {
    using namespace pff_browser;
    auto state = std::make_unique<State>();
    std::wstring error;
    state->archive = game_assets::PffArchive::load(path, &error);
    if (!state->archive) {
        throw failure(error);
    }
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&common)) {
        throw std::runtime_error("Cannot initialize archive controls");
    }
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = procedure;
    type.hInstance = module;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = pff_browser_class;
    type.hIcon = LoadIconW(module, MAKEINTRESOURCEW(101));
    type.hIconSm = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXSMICON),
                                                 GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register archive browser");
    }
    state->module = module;
    state->font = font;
    state->path = path;
    const auto title = path.filename().wstring() + L" - PFF archive";
    state->lifetime = std::make_shared<HWND>(nullptr);
    state->window = CreateWindowExW(WS_EX_CONTROLPARENT, pff_browser_class, title.c_str(),
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                    CW_USEDEFAULT, 1050, 760, owner, nullptr, module, nullptr);
    if (!state->window) {
        throw std::runtime_error("Cannot create archive browser");
    }
    *state->lifetime = state->window;
    SetWindowLongPtrW(state->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
    try {
        controls(*state);
    } catch (...) {
        DestroyWindow(state->window);
        throw;
    }
    const auto result = state->lifetime;
    state->owned = true;
    state.release();
    if (visible) {
        ShowWindow(*result, SW_SHOW);
        SetFocus(GetDlgItem(*result, list_id));
    }
    return result;
}
}
