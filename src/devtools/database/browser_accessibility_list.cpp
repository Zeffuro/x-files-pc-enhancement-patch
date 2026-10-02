#include "browser_accessibility_internal.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <wrl/client.h>

namespace devtools::database_browser::accessibility_detail {
HRESULT host_provider(Target& target, IRawElementProviderSimple** result) {
    const auto window = target.window.load();
    if (!window) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto hr = UiaHostProviderFromHwnd(window, result);
    if (window != target.window.load()) {
        if (*result) {
            (*result)->Release();
            *result = nullptr;
        }
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return hr;
}

HRESULT start_index(IRawElementProviderSimple* after, IRawElementProviderFragmentRoot* root,
                    int& index, unsigned& revision) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> parent;
    auto hr = after->QueryInterface(IID_PPV_ARGS(&fragment));
    if (FAILED(hr)) {
        return E_INVALIDARG;
    }
    hr = fragment->get_FragmentRoot(&parent);
    if (FAILED(hr)) {
        return hr;
    }
    if (parent.Get() != root) {
        return E_INVALIDARG;
    }
    SAFEARRAY* id = nullptr;
    hr = fragment->GetRuntimeId(&id);
    if (FAILED(hr) || !id) {
        return FAILED(hr) ? hr : E_INVALIDARG;
    }
    LONG at = 1;
    int version = 0;
    hr = SafeArrayGetElement(id, &at, &version);
    if (SUCCEEDED(hr)) {
        at = 2;
        hr = SafeArrayGetElement(id, &at, &index);
    }
    revision = static_cast<unsigned>(version);
    SafeArrayDestroy(id);
    return hr;
}

void notify(IRawElementProviderSimple* provider) {
    if (UiaClientsAreListening()) {
        UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr,
                                      0);
        UiaRaiseAutomationEvent(provider, UIA_Selection_InvalidatedEventId);
    }
}

std::wstring row_name(HWND window, int index) {
    std::wstring result;
    for (int column = 0; column < 4; ++column) {
        std::wstring buffer(8192, L'\0');
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.iSubItem = column;
        item.pszText = buffer.data();
        item.cchTextMax = static_cast<int>(std::size(buffer));
        ListView_GetItem(window, &item);
        if (column) {
            result += L" | ";
        }
        result += item.pszText ? item.pszText : L"";
    }
    return result;
}

void execute(HWND window, Target& target, Request& request) {
    if (request.revision && request.revision != target.revision.load()) {
        return;
    }
    request.revision = target.revision.load();
    request.count = ListView_GetItemCount(window);
    if (request.index >= request.count) {
        return;
    }
    request.result = S_OK;
    if (request.action == Action::find) {
        int start = request.index + 1;
        request.index = -1;
        for (int at = start; at < request.count; ++at) {
            const bool matches = request.property == 0 ||
                                 (request.property == UIA_NamePropertyId &&
                                  row_name(window, at) == request.value.bstrVal) ||
                                 (request.property == UIA_AutomationIdPropertyId &&
                                  std::to_wstring(at) == request.value.bstrVal) ||
                                 (request.property == UIA_SelectionItemIsSelectedPropertyId &&
                                  bool(ListView_GetItemState(window, at, LVIS_SELECTED)) ==
                                      (request.value.boolVal != VARIANT_FALSE));
            if (matches) {
                request.index = at;
                break;
            }
        }
        return;
    }
    if (request.action == Action::name) {
        request.name = request.index < 0 ? L"Database records" : row_name(window, request.index);
        return;
    }
    if (request.action == Action::select || request.action == Action::add ||
        request.action == Action::remove) {
        if (request.action == Action::add) {
            const auto selected = ListView_GetNextItem(window, -1, LVNI_SELECTED);
            if (selected >= 0 && selected != request.index) {
                request.result = UIA_E_INVALIDOPERATION;
                return;
            }
        }
        if (request.action != Action::remove) {
            ListView_SetItemState(window, -1, 0, LVIS_SELECTED);
        }
        ListView_SetItemState(window, request.index,
                              request.action != Action::remove ? LVIS_SELECTED | LVIS_FOCUSED : 0,
                              LVIS_SELECTED | LVIS_FOCUSED);
    } else if (request.action == Action::reveal || request.action == Action::focus) {
        if (request.index >= 0) {
            ListView_EnsureVisible(window, request.index, FALSE);
        }
        if (request.action == Action::focus) {
            SetFocus(window);
            if (request.index >= 0) {
                ListView_SetItemState(window, request.index, LVIS_FOCUSED, LVIS_FOCUSED);
            }
        }
    }
    request.horizontal.fMask = request.vertical.fMask = SIF_ALL;
    if (!GetScrollInfo(window, SB_HORZ, &request.horizontal)) {
        request.horizontal.nPage = 1;
    }
    if (!GetScrollInfo(window, SB_VERT, &request.vertical)) {
        request.vertical.nPage = 1;
    }
    if (request.action == Action::scroll || request.action == Action::percent) {
        const auto valid = [&](SCROLLINFO info, double value) {
            const auto unchanged = request.action == Action::percent
                                       ? UIA_ScrollPatternNoScroll
                                       : double(ScrollAmount_NoAmount);
            return value == unchanged || info.nMax - info.nMin + 1 > int(info.nPage);
        };
        if (!valid(request.horizontal, request.x) || !valid(request.vertical, request.y)) {
            request.result = UIA_E_INVALIDOPERATION;
            return;
        }
        const auto move = [&](int axis, SCROLLINFO info, double value) {
            if (request.action == Action::percent && value == UIA_ScrollPatternNoScroll) {
                return;
            }
            const int maximum = std::max(0, info.nMax - info.nMin - int(info.nPage) + 1);
            int position = info.nPos;
            if (request.action == Action::percent) {
                position = info.nMin + static_cast<int>(std::lround(maximum * value / 100));
            } else {
                const auto amount = static_cast<ScrollAmount>(static_cast<int>(value));
                const int step =
                    amount == ScrollAmount_LargeDecrement || amount == ScrollAmount_LargeIncrement
                        ? int(info.nPage)
                        : 1;
                position +=
                    amount == ScrollAmount_LargeDecrement || amount == ScrollAmount_SmallDecrement
                        ? -step
                    : amount == ScrollAmount_NoAmount ? 0
                                                      : step;
            }
            position = std::clamp(position, info.nMin, info.nMin + maximum);
            if (axis == SB_HORZ) {
                ListView_Scroll(window, position - info.nPos, 0);
            } else {
                RECT row{};
                if (ListView_GetItemRect(window, 0, &row, LVIR_BOUNDS)) {
                    ListView_Scroll(window, 0, (position - info.nPos) * (row.bottom - row.top));
                }
            }
        };
        move(SB_HORZ, request.horizontal, request.x);
        move(SB_VERT, request.vertical, request.y);
    }
    request.first = std::max(0, ListView_GetTopIndex(window));
    request.last = std::min(request.count - 1, request.first + ListView_GetCountPerPage(window));
    request.selected = ListView_GetNextItem(window, -1, LVNI_SELECTED);
    request.focused = ListView_GetNextItem(window, -1, LVNI_FOCUSED);
    request.has_focus = GetFocus() == window;
    request.enabled = IsWindowEnabled(window) != FALSE;
    RECT client{};
    GetClientRect(window, &client);
    request.bounds = client;
    if (request.index >= 0) {
        ListView_GetItemRect(window, request.index, &request.bounds, LVIR_BOUNDS);
    }
    request.visible = IsWindowVisible(window) && request.bounds.bottom > client.top &&
                      request.bounds.top < client.bottom;
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&request.bounds), 2);
}

}
