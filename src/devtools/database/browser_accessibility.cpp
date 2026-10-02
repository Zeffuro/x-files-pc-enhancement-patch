#include "browser_accessibility.h"
#include "browser_accessibility_internal.h"
#include <UIAutomation.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <new>
#include <string>
#include <stdexcept>

namespace devtools::database_browser {
namespace {
using namespace accessibility_detail;
const UINT request_message = RegisterWindowMessageW(L"XFiles.Database.Accessibility.Request");

class Provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot,
                       public ISelectionProvider,
                       public IScrollProvider,
                       public ISelectionItemProvider,
                       public IScrollItemProvider,
                       public IItemContainerProvider,
                       public IVirtualizedItemProvider {
    std::atomic<ULONG> references{1};
    std::shared_ptr<Target> target;
    Provider* root;
    int index;
    unsigned revision;

    HRESULT read(Request& request) const {
        const auto window = target->window.load();
        if (!window) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        request.index = index;
        request.revision = index < 0 ? 0 : revision;
        SendMessageW(window, request_message, reinterpret_cast<WPARAM>(target.get()),
                     reinterpret_cast<LPARAM>(&request));
        return request.result;
    }

    HRESULT action(Action value) {
        Request request;
        request.action = value;
        return read(request);
    }

    template <class T> HRESULT item(int row, unsigned version, T** result) {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (row < 0) {
            return S_OK;
        }
        auto* value = new (std::nothrow) Provider(target, root, row, version);
        if (!value) {
            return E_OUTOFMEMORY;
        }
        const auto hr = value->QueryInterface(__uuidof(T), reinterpret_cast<void**>(result));
        value->Release();
        return hr;
    }

    template <class T> HRESULT fixed(T* result, T value) {
        if (!result) {
            return E_POINTER;
        }
        Request request;
        const auto hr = read(request);
        if (SUCCEEDED(hr)) {
            *result = value;
        }
        return hr;
    }

    HRESULT scroll_value(int axis, bool size, double* result) {
        if (!result) {
            return E_POINTER;
        }
        Request request;
        const auto hr = read(request);
        if (FAILED(hr)) {
            return hr;
        }
        const auto info = axis == SB_VERT ? request.vertical : request.horizontal;
        const auto range = std::max(1, info.nMax - info.nMin + 1);
        const auto maximum = std::max(0, range - int(info.nPage));
        *result = size      ? (maximum ? std::min(100.0, 100.0 * info.nPage / range) : 100.0)
                  : maximum ? 100.0 * (info.nPos - info.nMin) / maximum
                            : UIA_ScrollPatternNoScroll;
        return S_OK;
    }

public:
    Provider(std::shared_ptr<Target> value, Provider* parent = nullptr, int row = -1,
             unsigned version = 0)
        : target(std::move(value)), root(parent ? parent : this), index(row), revision(version) {
        if (root != this) {
            root->AddRef();
        }
    }

    ~Provider() {
        if (root != this) {
            root->Release();
        }
    }

    Target& state() {
        return *target;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) {
            *result = static_cast<IRawElementProviderSimple*>(this);
        } else if (id == __uuidof(IRawElementProviderFragment)) {
            *result = static_cast<IRawElementProviderFragment*>(this);
        } else if (index < 0 && id == __uuidof(IRawElementProviderFragmentRoot)) {
            *result = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (index < 0 && id == __uuidof(ISelectionProvider)) {
            *result = static_cast<ISelectionProvider*>(this);
        } else if (index < 0 && id == __uuidof(IScrollProvider)) {
            *result = static_cast<IScrollProvider*>(this);
        } else if (index < 0 && id == __uuidof(IItemContainerProvider)) {
            *result = static_cast<IItemContainerProvider*>(this);
        } else if (index >= 0 && id == __uuidof(ISelectionItemProvider)) {
            *result = static_cast<ISelectionItemProvider*>(this);
        } else if (index >= 0 && id == __uuidof(IScrollItemProvider)) {
            *result = static_cast<IScrollItemProvider*>(this);
        } else if (index >= 0 && id == __uuidof(IVirtualizedItemProvider)) {
            *result = static_cast<IVirtualizedItemProvider*>(this);
        }
        if (!*result) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override {
        return fixed(result, ProviderOptions_ServerSideProvider);
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        if (FAILED(hr)) {
            return hr;
        }
        const IID* iid =
            id == UIA_SelectionPatternId && index < 0          ? &__uuidof(ISelectionProvider)
            : id == UIA_ScrollPatternId && index < 0           ? &__uuidof(IScrollProvider)
            : id == UIA_ItemContainerPatternId && index < 0    ? &__uuidof(IItemContainerProvider)
            : id == UIA_SelectionItemPatternId && index >= 0   ? &__uuidof(ISelectionItemProvider)
            : id == UIA_ScrollItemPatternId && index >= 0      ? &__uuidof(IScrollItemProvider)
            : id == UIA_VirtualizedItemPatternId && index >= 0 ? &__uuidof(IVirtualizedItemProvider)
                                                               : nullptr;
        return iid ? QueryInterface(*iid, reinterpret_cast<void**>(result)) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* result) override {
        if (!result) {
            return E_POINTER;
        }
        VariantInit(result);
        Request request;
        request.action = id == UIA_NamePropertyId ? Action::name : Action::inspect;
        const auto hr = read(request);
        if (FAILED(hr)) {
            return hr;
        }
        if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId) {
            const auto value = id == UIA_NamePropertyId ? request.name
                               : index < 0              ? L"DatabaseRecords"
                                                        : std::to_wstring(index);
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(value.c_str());
            return result->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (id == UIA_ControlTypePropertyId) {
            result->vt = VT_I4;
            result->lVal = index < 0 ? UIA_ListControlTypeId : UIA_ListItemControlTypeId;
        } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                   id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsEnabledPropertyId ||
                   id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsOffscreenPropertyId ||
                   id == UIA_SelectionItemIsSelectedPropertyId) {
            result->vt = VT_BOOL;
            const bool value = id == UIA_IsEnabledPropertyId ? request.enabled
                               : id == UIA_HasKeyboardFocusPropertyId
                                   ? request.has_focus && (index < 0 || request.focused == index)
                               : id == UIA_SelectionItemIsSelectedPropertyId
                                   ? request.selected == index
                               : id == UIA_IsOffscreenPropertyId ? !request.visible
                                                                 : true;
            result->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE
    get_HostRawElementProvider(IRawElementProviderSimple** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        return FAILED(hr) || index >= 0 ? hr : host_provider(*target, result);
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        if (FAILED(hr)) {
            return hr;
        }
        if (index >= 0 && direction == NavigateDirection_Parent) {
            return root->QueryInterface(__uuidof(IRawElementProviderFragment),
                                        reinterpret_cast<void**>(result));
        }
        int row = -1;
        if (index < 0) {
            row = direction == NavigateDirection_FirstChild  ? request.first
                  : direction == NavigateDirection_LastChild ? request.last
                                                             : -1;
        } else if (direction == NavigateDirection_NextSibling && index < request.last) {
            row = std::max(request.first, index + 1);
        } else if (direction == NavigateDirection_PreviousSibling && index > request.first) {
            row = std::min(request.last, index - 1);
        }
        return item(row < request.count ? row : -1, request.revision, result);
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        if (FAILED(hr) || index < 0) {
            return hr;
        }
        *result = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!*result) {
            return E_OUTOFMEMORY;
        }
        int values[] = {UiaAppendRuntimeId, static_cast<int>(revision), index};
        for (LONG at = 0; at < 3; ++at) {
            const auto stored = SafeArrayPutElement(*result, &at, &values[at]);
            if (FAILED(stored)) {
                SafeArrayDestroy(*result);
                *result = nullptr;
                return stored;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override {
        if (!result) {
            return E_POINTER;
        }
        Request request;
        const auto hr = read(request);
        if (SUCCEEDED(hr)) {
            *result = {double(request.bounds.left), double(request.bounds.top),
                       double(request.bounds.right - request.bounds.left),
                       double(request.bounds.bottom - request.bounds.top)};
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override {
        return fixed(result, static_cast<SAFEARRAY*>(nullptr));
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return action(Action::focus);
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        return FAILED(hr) ? hr
                          : root->QueryInterface(__uuidof(IRawElementProviderFragmentRoot),
                                                 reinterpret_cast<void**>(result));
    }

    HRESULT STDMETHODCALLTYPE
    ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        if (FAILED(hr)) {
            return hr;
        }
        if (x < request.bounds.left || x >= request.bounds.right || y < request.bounds.top ||
            y >= request.bounds.bottom) {
            return S_OK;
        }
        for (int at = request.first; at <= request.last; ++at) {
            Provider value(target, root, at, request.revision);
            UiaRect bounds{};
            if (SUCCEEDED(value.get_BoundingRectangle(&bounds)) && x >= bounds.left &&
                x < bounds.left + bounds.width && y >= bounds.top &&
                y < bounds.top + bounds.height) {
                return item(at, request.revision, result);
            }
        }
        return QueryInterface(__uuidof(IRawElementProviderFragment),
                              reinterpret_cast<void**>(result));
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override {
        Request request;
        const auto hr = read(request);
        return FAILED(hr)
                   ? hr
                   : item(request.has_focus ? request.focused : -1, request.revision, result);
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        if (FAILED(hr)) {
            return hr;
        }
        *result = SafeArrayCreateVector(VT_UNKNOWN, 0, request.selected >= 0 ? 1 : 0);
        if (!*result) {
            return E_OUTOFMEMORY;
        }
        IRawElementProviderSimple* selected = nullptr;
        auto created = item(request.selected, request.revision, &selected);
        if (selected) {
            LONG at = 0;
            created = SafeArrayPutElement(*result, &at, selected);
            selected->Release();
        }
        if (FAILED(created)) {
            SafeArrayDestroy(*result);
            *result = nullptr;
        }
        return created;
    }

    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* result) override {
        return fixed(result, FALSE);
    }

    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* result) override {
        return fixed(result, FALSE);
    }

    HRESULT STDMETHODCALLTYPE Select() override {
        return action(Action::select);
    }

    HRESULT STDMETHODCALLTYPE AddToSelection() override {
        return action(Action::add);
    }

    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
        return action(Action::remove);
    }

    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* result) override {
        if (!result) {
            return E_POINTER;
        }
        Request request;
        const auto hr = read(request);
        if (SUCCEEDED(hr)) {
            *result = request.selected == index;
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        Request request;
        const auto hr = read(request);
        return FAILED(hr) ? hr
                          : root->QueryInterface(__uuidof(IRawElementProviderSimple),
                                                 reinterpret_cast<void**>(result));
    }

    HRESULT STDMETHODCALLTYPE ScrollIntoView() override {
        return action(Action::reveal);
    }

    HRESULT STDMETHODCALLTYPE Realize() override {
        return ScrollIntoView();
    }

    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        if (horizontal < ScrollAmount_LargeDecrement || horizontal > ScrollAmount_SmallIncrement ||
            vertical < ScrollAmount_LargeDecrement || vertical > ScrollAmount_SmallIncrement) {
            return E_INVALIDARG;
        }
        Request request;
        request.action = Action::scroll;
        request.x = horizontal;
        request.y = vertical;
        return read(request);
    }

    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        const auto valid = [](double value) {
            return value == UIA_ScrollPatternNoScroll ||
                   (std::isfinite(value) && value >= 0 && value <= 100);
        };
        if (!valid(horizontal) || !valid(vertical)) {
            return E_INVALIDARG;
        }
        Request request;
        request.action = Action::percent;
        request.x = horizontal;
        request.y = vertical;
        return read(request);
    }

    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* result) override {
        return scroll_value(SB_HORZ, false, result);
    }

    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* result) override {
        return scroll_value(SB_VERT, false, result);
    }

    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* result) override {
        return scroll_value(SB_HORZ, true, result);
    }

    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* result) override {
        return scroll_value(SB_VERT, true, result);
    }

    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* result) override {
        double value = 0;
        const auto hr = get_HorizontalScrollPercent(&value);
        if (!result) {
            return E_POINTER;
        }
        *result = value != UIA_ScrollPatternNoScroll;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* result) override {
        double value = 0;
        const auto hr = get_VerticalScrollPercent(&value);
        if (!result) {
            return E_POINTER;
        }
        *result = value != UIA_ScrollPatternNoScroll;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE FindItemByProperty(IRawElementProviderSimple* after,
                                                 PROPERTYID property, VARIANT value,
                                                 IRawElementProviderSimple** result) override {
        if (!result) {
            return E_POINTER;
        }
        *result = nullptr;
        if (property != 0 && property != UIA_NamePropertyId &&
            property != UIA_AutomationIdPropertyId &&
            property != UIA_SelectionItemIsSelectedPropertyId) {
            return E_INVALIDARG;
        }
        if ((property == UIA_NamePropertyId || property == UIA_AutomationIdPropertyId) &&
            (value.vt != VT_BSTR || !value.bstrVal)) {
            return E_INVALIDARG;
        }
        if (property == UIA_SelectionItemIsSelectedPropertyId && value.vt != VT_BOOL) {
            return E_INVALIDARG;
        }
        Request request;
        request.action = Action::find;
        request.property = property;
        request.value = value;
        if (after) {
            const auto hr = start_index(after, root, request.index, request.revision);
            if (FAILED(hr)) {
                return hr;
            }
        }
        const auto window = target->window.load();
        if (!window) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        SendMessageW(window, request_message, reinterpret_cast<WPARAM>(target.get()),
                     reinterpret_cast<LPARAM>(&request));
        return FAILED(request.result) ? request.result
                                      : item(request.index, request.revision, result);
    }
};

LRESULT CALLBACK accessibility(HWND window, UINT message, WPARAM value, LPARAM data, UINT_PTR id,
                               DWORD_PTR owner) {
    auto* provider = reinterpret_cast<Provider*>(owner);
    provider->AddRef();
    const auto release = [](Provider* value) { value->Release(); };
    const std::unique_ptr<Provider, decltype(release)> retained(provider, release);
    auto& target = provider->state();
    if (message == request_message && value == reinterpret_cast<WPARAM>(&target)) {
        try {
            execute(window, target, *reinterpret_cast<Request*>(data));
        } catch (const std::bad_alloc&) {
            reinterpret_cast<Request*>(data)->result = E_OUTOFMEMORY;
        }
        return 0;
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(data) == UiaRootObjectId) {
        return UiaReturnRawElementProvider(window, value, data, provider);
    }
    if (message == LVM_SETITEMCOUNT || message == LVM_SORTITEMS || message == LVM_SORTITEMSEX) {
        ++target.revision;
    }
    if (message == WM_DESTROY) {
        UiaReturnRawElementProvider(window, 0, 0, nullptr);
    }
    if (message == WM_NCDESTROY) {
        target.window = nullptr;
        RemoveWindowSubclass(window, accessibility, id);
        provider->Release();
    }
    const auto result = DefSubclassProc(window, message, value, data);
    if (message == LVM_SETITEMCOUNT || message == LVM_SCROLL || message == LVM_SETITEMSTATE ||
        message == WM_VSCROLL || message == WM_HSCROLL || message == WM_MOUSEWHEEL ||
        message == WM_KEYDOWN || message == WM_LBUTTONUP || message == WM_SIZE) {
        notify(provider);
    }
    return result;
}
}

void install_list_accessibility(HWND list) {
    if (!request_message) {
        throw std::runtime_error("Cannot register database accessibility message");
    }
    auto* provider = new Provider(std::make_shared<Target>(list));
    if (!SetWindowSubclass(list, accessibility, 1, reinterpret_cast<DWORD_PTR>(provider))) {
        provider->Release();
        throw std::runtime_error("Cannot install database accessibility provider");
    }
}
}
