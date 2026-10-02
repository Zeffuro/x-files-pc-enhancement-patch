#pragma once
#include <ole2.h>
#include <UIAutomation.h>
#include <atomic>
#include <string>

namespace devtools::database_browser::accessibility_detail {
struct Target {
    std::atomic<HWND> window;
    std::atomic<unsigned> revision{1};

    explicit Target(HWND value) : window(value) {}
};
enum class Action { inspect, name, select, add, remove, reveal, focus, scroll, percent, find };

struct Request {
    Action action = Action::inspect;
    unsigned revision = 0;
    int index = -1, count = 0, first = 0, last = -1, selected = -1, focused = -1;
    bool enabled = false, has_focus = false, visible = false;
    RECT bounds{};
    SCROLLINFO horizontal{sizeof(SCROLLINFO)}, vertical{sizeof(SCROLLINFO)};
    double x = 0, y = 0;
    PROPERTYID property = 0;
    VARIANT value{};
    std::wstring name;
    HRESULT result = UIA_E_ELEMENTNOTAVAILABLE;
};

void execute(HWND window, Target& target, Request& request);
void notify(IRawElementProviderSimple* provider);
HRESULT host_provider(Target& target, IRawElementProviderSimple** result);
HRESULT start_index(IRawElementProviderSimple* after, IRawElementProviderFragmentRoot* root,
                    int& index, unsigned& revision);
}
