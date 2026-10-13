#include <windows.h>

namespace {
BOOL WINAPI trace_read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size,
                       SIZE_T* copied);
}

#define ReadProcessMemory trace_read
#include "game/access/script.h"
#undef ReadProcessMemory
#include "game/functions/generated.h"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace native_game;

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

struct ReadRequest {
    const void* source;
    void* destination;
    SIZE_T size;
    unsigned calls = 0;
    BOOL returned = FALSE;
    SIZE_T copied = 0;
};

ReadRequest* expected_read = nullptr;
unsigned read_requests = 0;
bool guard_denial = true;

BOOL WINAPI trace_read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size,
                       SIZE_T* copied) {
    require(expected_read != nullptr, "Native read occurred outside its expected sequence");
    require(process == GetCurrentProcess() && source == expected_read->source &&
                destination == expected_read->destination && size == expected_read->size && copied,
            "Native read changed its process, address, destination or exact width");
    ++expected_read->calls;
    ++read_requests;
    const auto result = ReadProcessMemory(process, source, destination, size, copied);
    expected_read->returned = result;
    expected_read->copied = *copied;
    return result;
}

template <class Ref, class T>
bool read(const Ref& ref, T& value, const void* source, SIZE_T width) {
    ReadRequest expected{source, &value, width};
    expected_read = &expected;
    const auto result = ref.read(value);
    expected_read = nullptr;
    require(expected.calls == (source ? 1u : 0u), "Native read duplicated or omitted a request");
    require(result == (expected.returned && expected.copied == width),
            "Native read changed the underlying success or exact-copy result");
    return result;
}

void require_guard_result(bool readable, const char* reason) {
    require(!guard_denial || !readable, reason);
}

void guard_capability(const void* boundary) {
    MEMORY_BASIC_INFORMATION region{};
    require(VirtualQuery(boundary, &region, sizeof(region)) == sizeof(region) &&
                region.State == MEM_RESERVE,
            "The fixture tail must be reserved and uncommitted");
    unsigned value = 0;
    SIZE_T copied = 0;
    const auto result = ReadProcessMemory(GetCurrentProcess(), boundary, &value, 4, &copied);
    guard_denial = !result;
    if (guard_denial) {
        require(copied == 0, "Reserved-page denial must not report a completed read");
        std::cout << "ReadProcessMemory guard enforcement: native denial\n";
    } else {
        require(copied == 4 &&
                    GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr,
                "Reserved pages unexpectedly readable outside the measured Wine runtime");
        std::cout << "Wine ReadProcessMemory returns bytes from reserved pages. "
                     "Exact request and result-forwarding checks remain enforced.\n";
    }
}

class GuardPages {
public:
    GuardPages() {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        page_size = system.dwPageSize;
        allocation = static_cast<std::byte*>(
            VirtualAlloc(nullptr, page_size * 2, MEM_RESERVE, PAGE_NOACCESS));
        require(allocation != nullptr, "Cannot allocate native-read guard pages");
        if (!VirtualAlloc(allocation, page_size, MEM_COMMIT, PAGE_READWRITE)) {
            VirtualFree(allocation, 0, MEM_RELEASE);
            allocation = nullptr;
            throw std::runtime_error("Cannot make the fixture page writable");
        }
    }

    GuardPages(const GuardPages&) = delete;
    GuardPages& operator=(const GuardPages&) = delete;

    ~GuardPages() {
        VirtualFree(allocation, 0, MEM_RELEASE);
    }

    std::byte* tail(std::size_t width) const {
        require(width <= page_size, "Native fixture exceeds one page");
        return allocation + page_size - width;
    }

private:
    std::byte* allocation = nullptr;
    std::size_t page_size = 0;
};

template <class T> void put(std::byte* object, unsigned offset, const T& value) {
    std::memcpy(object + offset, &value, sizeof(value));
}

void application_boundary() {
    GuardPages pages;
    auto* object = pages.tail(0xf8);
    guard_capability(pages.tail(0));
    std::array<std::byte, 8> state_storage{}, view_storage{};
    void* state = state_storage.data();
    void* view = view_storage.data();
    put(object, 0xe4, state);
    put(object, 0xec, view);

    const ApplicationRef application(object);
    void* actual_state = nullptr;
    MainView* actual_view = nullptr;
    require(read(application.state(), actual_state, object + 0xe4, 4) && actual_state == state,
            "Narrow state read must fit the original Application prefix");
    require(read(application.view(), actual_view, object + 0xec, 4) && actual_view == view,
            "Narrow view read must fit the original Application prefix");
    Application prefix{};
    require(read(application, prefix, object, 0xf8),
            "The complete original Application prefix is readable");
    unsigned count = 0;
    require_guard_result(read(application.commands().count(), count, object + 0x26c, 4),
                         "Extended application queues must not widen a readable prefix");
}

void scalar_boundaries() {
    GuardPages pages;
    auto* enabled_object = pages.tail(0x0c);
    put(enabled_object, 8, 0x71825364u);
    unsigned value = 0;
    const ScriptInputRef enabled_input(enabled_object);
    require(read(enabled_input.enabled(), value, enabled_object + 8, 4) && value == 0x71825364u,
            "Enabled scalar must read only four accessible bytes");
    require_guard_result(read(enabled_input.disabled(), value, enabled_object + 0x0c, 4),
                         "The adjacent disabled scalar is inaccessible");
    ScriptInputPrefix full_input{};
    require_guard_result(read(enabled_input, full_input, enabled_object, 0x30),
                         "The enabled scalar fixture has no complete input");

    auto* resource_object = pages.tail(0x1c);
    void* expected_resource = enabled_object;
    put(resource_object, 0x18, expected_resource);
    void* resource = nullptr;
    const ScriptInputRef resource_input(resource_object);
    require(read(resource_input.resource(), resource, resource_object + 0x18, 4) &&
                resource == expected_resource,
            "Resource pointer must remain a narrow pointer read");
    List<void> actions{};
    require_guard_result(read(resource_input.actions(), actions, resource_object + 0x1c, 20),
                         "Resource tail must not imply readable actions");

    auto* control_object = pages.tail(0x148);
    put(control_object, 0x144, 0x92837164u);
    require(read(ScriptControlRef(control_object).id(), value, control_object + 0x144, 4) &&
                value == 0x92837164u,
            "Control ID must read only its accessible scalar");
    require_guard_result(read(MemoryRef<unsigned>(pages.tail(0)), value, pages.tail(0), 4),
                         "The guard page is unreadable");
}

void invalid_addresses() {
    unsigned value = 0;
    const ScriptInputRef null_input(nullptr);
    require(null_input.enabled().address() == 0 && !read(null_input.enabled(), value, nullptr, 4),
            "Null object fields must stay null and fail reads");
    const ScriptInputRef overflow(std::numeric_limits<std::uintptr_t>::max() - 0x10);
    void* resource = nullptr;
    require(overflow.resource().address() == 0 && !read(overflow.resource(), resource, nullptr, 4),
            "Overflowing field addresses must fail without wrapping");
    const ScriptControlRef control_overflow(std::numeric_limits<std::uintptr_t>::max() - 0x100);
    require(control_overflow.id().address() == 0 && !read(control_overflow.id(), value, nullptr, 4),
            "Distant scalar offsets must reject address overflow");
    GuardPages pages;
    native_game::Rectangle rectangle{};
    require(!read(ScriptControlRef(pages.tail(0x200)).rectangle(), rectangle, nullptr, 20),
            "Build-sensitive fields require an explicit profile");
}

struct GoldenLayout {
    const Profile* profile;
    unsigned children, control_rectangle, choice_viewport, text_rectangle;
};

void build_sensitive_fields() {
    constexpr std::array layouts{GoldenLayout{&profile_cd_10012, 0x800, 0x198, 0x98, 0x98},
                                 GoldenLayout{&profile_cd_10019, 0x804, 0x19c, 0x9c, 0x9c},
                                 GoldenLayout{&profile_dvd_20000, 0x804, 0x19c, 0x9c, 0x9c},
                                 GoldenLayout{&profile_cd_10020, 0x804, 0x19c, 0x9c, 0x9c}};
    for (const auto& layout : layouts) {
        GuardPages pages;
        auto* view = pages.tail(layout.children + 20);
        ChildView child{view};
        List<ChildView>::Node node{&child, nullptr, nullptr};
        List<ChildView> children{nullptr, 1, &node, &node, nullptr};
        put(view, layout.children, children);
        List<ChildView> actual{};
        require(read(MainViewRef(view, layout.profile).children(), actual, view + layout.children,
                     20) &&
                    actual.count == 1 && actual.first == &node && actual.last == &node &&
                    actual.current == nullptr,
                "Children must use the edition's golden offset and original list width");

        const RECT bounds{71, 29, 317, 193};
        const native_game::Rectangle expected{nullptr, bounds};
        RECT result{};
        auto* control = pages.tail(layout.control_rectangle + 20);
        put(control, layout.control_rectangle, expected);
        require(read(ScriptControlRef(control, layout.profile).rectangle().bounds(), result,
                     control + layout.control_rectangle + 4, 16) &&
                    EqualRect(&result, &bounds),
                "Control bounds must use the edition's golden rectangle offset");
        auto* choice = pages.tail(layout.choice_viewport + 20);
        put(choice, layout.choice_viewport, expected);
        require(read(ChoiceListRef(choice, layout.profile).viewport().bounds(), result,
                     choice + layout.choice_viewport + 4, 16) &&
                    EqualRect(&result, &bounds),
                "Choice viewport must use the edition's golden rectangle offset");
        auto* text = pages.tail(layout.text_rectangle + 20);
        put(text, layout.text_rectangle, expected);
        require(read(MainTextGroupRef(text, layout.profile).viewport().bounds(), result,
                     text + layout.text_rectangle + 4, 16) &&
                    EqualRect(&result, &bounds),
                "Text viewport must use the edition's golden rectangle offset");
    }
}

std::array<unsigned, 6> order{};
unsigned call_count = 0;
std::array<std::byte, 8> input_storage{};
void* state_seen = nullptr;
void* name_seen = nullptr;
void* queue_seen = nullptr;
void* release_seen = nullptr;
ChoiceList* scroll_seen = nullptr;
int mode_seen = 0;

void record(unsigned event) {
    require(call_count < order.size(), "Unexpected native call count");
    order[call_count++] = event;
}

void* __stdcall mock_current_input(void* state) {
    record(1);
    state_seen = state;
    return input_storage.data();
}

int __stdcall mock_load_file(void* name, void* queue) {
    record(2);
    name_seen = name;
    queue_seen = queue;
    return -1973;
}

void __stdcall mock_pause(void* state, int mode, void* queue) {
    record(3);
    state_seen = state;
    mode_seen = mode;
    queue_seen = queue;
}

void __stdcall mock_scroll_up(ChoiceList* list) {
    record(4);
    scroll_seen = list;
}

void __stdcall mock_scroll_down(ChoiceList* list) {
    record(5);
    scroll_seen = list;
}

void __fastcall mock_release(void* receiver, void*) {
    record(6);
    release_seen = receiver;
}

template <class Function> std::uint32_t mock_rva(const std::byte* image, Function function) {
    const auto address = reinterpret_cast<std::uintptr_t>(function);
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    require(address >= base && address - base <= std::numeric_limits<std::uint32_t>::max(),
            "Mock native function must reside in the current test module");
    return static_cast<std::uint32_t>(address - base);
}

void native_dispatch() {
    const auto* image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    require(image != nullptr, "Cannot locate native dispatch test module");
    auto addresses = profile_dvd_20000;
    addresses.current_input = mock_rva(image, &mock_current_input);
    addresses.load_file = mock_rva(image, &mock_load_file);
    addresses.pause = mock_rva(image, &mock_pause);
    addresses.main_scroll_up = mock_rva(image, &mock_scroll_up);
    addresses.main_scroll_down = mock_rva(image, &mock_scroll_down);
    addresses.release = mock_rva(image, &mock_release);
    const NativeFunctions native(image, addresses);
    std::array<std::byte, 4> state{}, name{}, queue{}, resource{};
    ChoiceList list{};

    require(native.current_input(state.data()) == input_storage.data() &&
                state_seen == state.data(),
            "Named input dispatch must preserve its pointer argument and return");
    require(native.load_file(name.data(), queue.data()) == -1973 && name_seen == name.data() &&
                queue_seen == queue.data(),
            "Named load dispatch must preserve argument order and signed return");
    native.pause(state.data(), -317, queue.data());
    require(state_seen == state.data() && mode_seen == -317 && queue_seen == queue.data(),
            "Named pause dispatch must preserve all stdcall arguments");
    require(native.main_scroll_up_target() == &mock_scroll_up &&
                native.main_scroll_down_target() == &mock_scroll_down,
            "Native scrollbar target getters must preserve callback identity");
    native.main_scroll_up_target()(&list);
    require(scroll_seen == &list, "Typed up callback must preserve its list receiver");
    native.main_scroll_down(&list);
    require(scroll_seen == &list, "Named down method must preserve its list receiver");
    native.release(resource.data());
    require(release_seen == resource.data(), "Thiscall release must preserve its ECX receiver");
    require(call_count == order.size() && order == std::array<unsigned, 6>{1, 2, 3, 4, 5, 6},
            "Named native dispatch must preserve call order without extra calls");
}
}

int main() {
    try {
        application_boundary();
        scalar_boundaries();
        invalid_addresses();
        build_sensitive_fields();
        require(read_requests == 27, "Native fields changed their expected read request sequence");
        native_dispatch();
        std::cout << "27 exact native read requests and named native dispatch passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
