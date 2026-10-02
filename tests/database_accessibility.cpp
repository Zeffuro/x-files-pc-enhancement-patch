#include "devtools/database/browser_accessibility.h"
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using Microsoft::WRL::ComPtr;
constexpr int row_count = 66168;
constexpr UINT replace_rows = WM_APP + 31;
constexpr UINT close_list = WM_APP + 32;
constexpr UINT empty_list = WM_APP + 33;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

LRESULT CALLBACK host(HWND window, UINT message, WPARAM value, LPARAM data) {
    if (message == WM_NOTIFY) {
        auto* notification = reinterpret_cast<NMHDR*>(data);
        if (notification->code == LVN_GETDISPINFOW) {
            auto& item = reinterpret_cast<NMLVDISPINFOW*>(data)->item;
            if (item.mask & LVIF_TEXT) {
                const auto name = L"Record " + std::to_wstring(item.iItem) + L" column " +
                                  std::to_wstring(item.iSubItem);
                wcsncpy_s(item.pszText, static_cast<std::size_t>(item.cchTextMax), name.c_str(),
                          _TRUNCATE);
            }
        }
        return 0;
    }
    if (message == replace_rows) {
        const auto list = GetDlgItem(window, 1);
        ListView_SetItemCount(list, 0);
        ListView_SetItemCount(list, row_count);
        return 0;
    }
    if (message == close_list) {
        DestroyWindow(GetDlgItem(window, 1));
        return 0;
    }
    if (message == empty_list) {
        ListView_SetItemCount(GetDlgItem(window, 2), 0);
        return 0;
    }
    return DefWindowProcW(window, message, value, data);
}

void check_tree(IUIAutomation* automation, IUIAutomationElement* root) {
    ComPtr<IUIAutomationCondition> all;
    require(SUCCEEDED(automation->CreateTrueCondition(&all)), "Create tree condition");
    ComPtr<IUIAutomationElementArray> children;
    require(SUCCEEDED(root->FindAll(TreeScope_Descendants, all.Get(), &children)),
            "Enumerate accessible viewport");
    int count = 0;
    require(SUCCEEDED(children->get_Length(&count)) && count > 0 && count < 100,
            "Accessible tree must remain bounded by viewport");
}

void check(HWND window, HWND list) {
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Initialize client MTA");
    ComPtr<IUIAutomation> automation;
    require(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&automation))),
            "Create UI Automation");
    ComPtr<IUIAutomationElement> root;
    require(SUCCEEDED(automation->ElementFromHandle(list, &root)), "Retrieve list provider");
    check_tree(automation.Get(), root.Get());
    BSTR root_name = nullptr;
    require(SUCCEEDED(root->get_CurrentName(&root_name)) &&
                std::wstring(root_name) == L"Database records",
            "Native provider overrides proxy");
    SysFreeString(root_name);

    ComPtr<IUIAutomationItemContainerPattern> container;
    require(
        SUCCEEDED(root->GetCurrentPatternAs(UIA_ItemContainerPatternId, IID_PPV_ARGS(&container))),
        "Item lookup pattern");
    ComPtr<IUIAutomationElement> other_root, other_item;
    require(SUCCEEDED(automation->ElementFromHandle(GetDlgItem(window, 2), &other_root)),
            "Retrieve independent list");
    ComPtr<IUIAutomationItemContainerPattern> other_container;
    require(SUCCEEDED(other_root->GetCurrentPatternAs(UIA_ItemContainerPatternId,
                                                      IID_PPV_ARGS(&other_container))),
            "Independent list lookup");
    VARIANT next{};
    require(SUCCEEDED(other_container->FindItemByProperty(nullptr, 0, next, &other_item)) &&
                other_item,
            "Retrieve foreign startAfter item");
    ComPtr<IUIAutomationElement> invalid;
    require(container->FindItemByProperty(other_item.Get(), 0, next, &invalid) == E_INVALIDARG,
            "Foreign startAfter must be rejected");
    require(container->FindItemByProperty(nullptr, UIA_HelpTextPropertyId, next, &invalid) ==
                E_INVALIDARG,
            "Unsupported lookup property must be rejected");
    ComPtr<IUIAutomationElement> first, second;
    require(SUCCEEDED(container->FindItemByProperty(nullptr, 0, next, &first)) && first,
            "Retrieve first item");
    require(SUCCEEDED(container->FindItemByProperty(first.Get(), 0, next, &second)) && second,
            "Same-container startAfter lookup");
    BSTR second_name = nullptr;
    require(SUCCEEDED(second->get_CurrentName(&second_name)) &&
                std::wstring(second_name).starts_with(L"Record 1 column 0"),
            "startAfter returns following item");
    SysFreeString(second_name);
    SendMessageW(window, empty_list, 0, 0);
    ComPtr<IUIAutomationScrollPattern> empty_scroll;
    require(SUCCEEDED(
                other_root->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&empty_scroll))),
            "Empty list scroll pattern");
    BOOL empty_scrollable = TRUE;
    require(SUCCEEDED(empty_scroll->get_CurrentVerticallyScrollable(&empty_scrollable)) &&
                !empty_scrollable,
            "Empty list is not scrollable");
    VARIANT id{};
    id.vt = VT_BSTR;
    id.bstrVal = SysAllocString(L"66167");
    ComPtr<IUIAutomationElement> last;
    require(
        SUCCEEDED(container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, id, &last)) &&
            last,
        "Find final offscreen record");
    VariantClear(&id);
    require(SUCCEEDED(container->FindItemByProperty(last.Get(), 0, next, &invalid)) && !invalid,
            "Final item has no following item");
    ComPtr<IUIAutomationVirtualizedItemPattern> virtual_item;
    require(SUCCEEDED(last->GetCurrentPatternAs(UIA_VirtualizedItemPatternId,
                                                IID_PPV_ARGS(&virtual_item))),
            "Realization pattern");
    require(SUCCEEDED(virtual_item->Realize()), "Realize final record");
    ComPtr<IUIAutomationSelectionItemPattern> selection;
    require(
        SUCCEEDED(last->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))),
        "Selection pattern");
    require(SUCCEEDED(selection->Select()), "Select final record");
    require(SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) ==
                row_count - 1,
            "Full list selection identity");
    BSTR name = nullptr;
    require(SUCCEEDED(last->get_CurrentName(&name)) &&
                std::wstring(name).find(L"Record 66167 column 3") != std::wstring::npos,
            "Owner-data provider name includes all columns");
    SysFreeString(name);
    check_tree(automation.Get(), root.Get());

    ComPtr<IUIAutomationScrollPattern> scroll;
    require(SUCCEEDED(root->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scroll))),
            "Scroll pattern");
    BOOL horizontally_scrollable = TRUE;
    double horizontal_percent = 0, horizontal_size = 0;
    require(SUCCEEDED(scroll->get_CurrentHorizontallyScrollable(&horizontally_scrollable)) &&
                !horizontally_scrollable,
            "Absent horizontal scrollbar is not scrollable");
    require(SUCCEEDED(scroll->get_CurrentHorizontalScrollPercent(&horizontal_percent)) &&
                horizontal_percent == UIA_ScrollPatternNoScroll,
            "Absent scrollbar percent");
    require(SUCCEEDED(scroll->get_CurrentHorizontalViewSize(&horizontal_size)) &&
                horizontal_size == 100,
            "Absent scrollbar view covers entire content");
    require(scroll->Scroll(ScrollAmount_SmallIncrement, ScrollAmount_NoAmount) ==
                UIA_E_INVALIDOPERATION,
            "Non-scrollable horizontal axis rejects Scroll");
    require(scroll->SetScrollPercent(50, 50) == UIA_E_INVALIDOPERATION,
            "Unsupported percent axis rejects entire operation");
    require(SUCCEEDED(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 50)),
            "Scroll to middle of full list");
    const auto middle = SendMessageW(list, LVM_GETTOPINDEX, 0, 0);
    require(middle > 30000 && middle < 35000, "Middle scrollbar position spans full corpus");
    check_tree(automation.Get(), root.Get());
    require(SUCCEEDED(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 0)),
            "Scroll to first record");
    require(SendMessageW(list, LVM_GETTOPINDEX, 0, 0) == 0, "First record reachable");
    SendMessageW(window, replace_rows, 0, 0);
    require(container->FindItemByProperty(last.Get(), 0, next, &invalid) ==
                UIA_E_ELEMENTNOTAVAILABLE,
            "Stale startAfter lookup must be rejected");
    name = nullptr;
    require(FAILED(last->get_CurrentName(&name)), "Replaced rows invalidate retained elements");
    SysFreeString(name);
    SendMessageW(window, close_list, 0, 0);
    name = nullptr;
    require(FAILED(root->get_CurrentName(&name)), "Destroyed list invalidates retained root");
    SysFreeString(name);
    virtual_item.Reset();
    selection.Reset();
    scroll.Reset();
    container.Reset();
    last.Reset();
    root.Reset();
    automation.Reset();
    CoUninitialize();
}
}

int main() {
    try {
        const auto instance = GetModuleHandleW(nullptr);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        require(InitCommonControlsEx(&controls), "Initialize list controls");
        WNDCLASSW type{};
        type.lpfnWndProc = host;
        type.hInstance = instance;
        type.lpszClassName = L"DatabaseAccessibilityTest";
        require(RegisterClassW(&type), "Register test host");
        const auto window = CreateWindowW(type.lpszClassName, L"", WS_POPUP, 0, 0, 800, 600,
                                          nullptr, nullptr, instance, nullptr);
        const auto list = CreateWindowW(
            WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL,
            0, 0, 750, 500, window, reinterpret_cast<HMENU>(1), instance, nullptr);
        require(window && list, "Create owner-data list");
        for (int index = 0; index < 4; ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t*>(L"Column");
            column.cx = 170;
            ListView_InsertColumn(list, index, &column);
        }
        ListView_SetItemCount(list, row_count);
        devtools::database_browser::install_list_accessibility(list);
        const auto other =
            CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL,
                          0, 0, 750, 500, window, reinterpret_cast<HMENU>(2), instance, nullptr);
        require(other != nullptr, "Create second list");
        ListView_SetItemCount(other, 2);
        devtools::database_browser::install_list_accessibility(other);
        std::atomic<bool> finished = false;
        std::string error;
        std::thread client([&] {
            try {
                check(window, list);
            } catch (const std::exception& failure) {
                error = failure.what();
            }
            finished = true;
        });
        const auto deadline = GetTickCount64() + 30000;
        while (!finished && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (!finished) {
            std::cerr << "Accessibility client exceeded 30 seconds\n";
            ExitProcess(1);
        }
        client.join();
        DestroyWindow(window);
        require(error.empty(), error.c_str());
        std::cout << "Database accessibility viewport, scroll, selection and lifetime pass\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
