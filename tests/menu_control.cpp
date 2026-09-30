#include "enhancements/menu_corner.h"
#include "enhancements/game_ui.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
std::byte* image = nullptr;
const enhancements::game::Edition* profile = nullptr;

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

struct Object {
    void* vtable;
    unsigned visible, enabled, disabled;
};

struct Fixture {
    enhancements::game::Application app{};
    std::array<std::byte, 0x300> state{};
    enhancements::game::MainView view{};
    Object object{};
    enhancements::game::List<std::byte>::Node node{};
    enhancements::game::Rectangle* rectangle = nullptr;
    const RECT original{501, 0, 640, 34};

    enhancements::game::List<std::byte>& list() {
        return *reinterpret_cast<enhancements::game::List<std::byte>*>(state.data() + 0x20c);
    }

    Fixture(const enhancements::game::Edition& edition) {
        profile = &edition;
        image = static_cast<std::byte*>(
            VirtualAlloc(nullptr, 0x300000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        require(image != nullptr, "Cannot allocate native corner fixture");
        *reinterpret_cast<enhancements::game::Application**>(image + profile->application) = &app;
        app.state = state.data();
        app.view = &view;
        object = {image + profile->menu_corner, 0, 1, 0};
        node = {reinterpret_cast<std::byte*>(&object), nullptr, nullptr};
        list() = {nullptr, 1, &node, &node, nullptr};
        rectangle = reinterpret_cast<enhancements::game::Rectangle*>(
            image + profile->menu_corner_rectangle);
        *rectangle = {image + 0x100, original};
    }

    ~Fixture() {
        enhancements::game::suppress_menu_corner(false);
        VirtualFree(image, 0, MEM_RELEASE);
        image = nullptr;
    }

    void restored() {
        require(EqualRect(&rectangle->bounds, &original),
                "Original global bounds were not restored");
        require(rectangle->vtable == image + 0x100, "Suppression modified the Rectangle vtable");
    }
};

void exercise_spanning_object(Fixture& f) {
    using namespace enhancements::game;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    auto* pages = static_cast<std::byte*>(
        VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    require(pages != nullptr, "Cannot allocate cross-page native object");
    auto* second = pages + system.dwPageSize;
    auto* object = reinterpret_cast<Object*>(second - sizeof(void*));
    *object = f.object;
    f.node.value = reinterpret_cast<std::byte*>(object);
    require(menu_corner().has_value(), "Readable cross-page native object was rejected");
    require(VirtualFree(second, system.dwPageSize, MEM_DECOMMIT),
            "Cannot decommit native object page");
    require(!menu_corner() && !suppress_menu_corner(true),
            "Partly uncommitted native object was accepted");
    require(VirtualAlloc(second, system.dwPageSize, MEM_COMMIT, PAGE_READWRITE) == second,
            "Cannot recommit native object page");
    *object = f.object;
    for (const DWORD protection : {PAGE_NOACCESS, PAGE_READWRITE | PAGE_GUARD}) {
        DWORD previous = 0;
        require(VirtualProtect(second, system.dwPageSize, protection, &previous),
                "Cannot protect native object page");
        require(!menu_corner() && !suppress_menu_corner(true),
                "Partly unreadable native object was accepted");
        MEMORY_BASIC_INFORMATION region{};
        require(VirtualQuery(second, &region, sizeof(region)) && region.Protect == protection,
                "Native object inspection consumed page protection");
        require(VirtualProtect(second, system.dwPageSize, previous, &previous),
                "Cannot restore native object page protection");
    }
    require(menu_corner().has_value(), "Restored cross-page native object was rejected");
    f.node.value = reinterpret_cast<std::byte*>(&f.object);
    require(VirtualFree(pages, 0, MEM_RELEASE), "Cannot release native object pages");
}

void exercise(const enhancements::game::Edition& edition) {
    using namespace enhancements::game;
    Fixture f(edition);
    require(menu_corner().has_value(), "Real type26 native menu was rejected");
    const auto identity = menu_corner_identity();
    require(identity && identity->application == &f.app && identity->state == f.state.data() &&
                identity->view == &f.view && identity->object == &f.object,
            "Native corner identity does not describe the validated current context");
    Application replacement_app = f.app;
    *reinterpret_cast<Application**>(image + profile->application) = &replacement_app;
    require(menu_corner_identity() && menu_corner_identity() != identity,
            "Replacement Application retained stale native identity");
    *reinterpret_cast<Application**>(image + profile->application) = &f.app;
    auto replacement_state = f.state;
    f.app.state = replacement_state.data();
    require(menu_corner_identity() && menu_corner_identity() != identity,
            "Replacement State retained stale native identity");
    f.app.state = f.state.data();
    MainView replacement_view{};
    f.app.view = &replacement_view;
    require(menu_corner_identity() && menu_corner_identity() != identity,
            "Replacement view retained stale native identity");
    f.app.view = &f.view;
    Object replacement_object = f.object;
    f.node.value = reinterpret_cast<std::byte*>(&replacement_object);
    require(menu_corner_identity() && menu_corner_identity() != identity,
            "Replacement corner object retained stale native identity");
    f.node.value = reinterpret_cast<std::byte*>(&f.object);
    require(menu_corner_identity() == identity, "Unchanged context lost native identity");
    require(suppress_menu_corner(true), "Native rectangle could not be suppressed");
    require(menu_corner_identity() == identity, "Suppression changed native corner identity");
    require(IsRectEmpty(&f.rectangle->bounds), "Native hit bounds were not cleared");
    const auto suppressed = menu_corner();
    require(suppressed && EqualRect(&*suppressed, &f.original),
            "Suppressed snapshot lost original bounds");
    require(suppress_menu_corner(true), "Repeated suppression lost ownership");
    require(suppress_menu_corner(false), "Native bounds could not be released");
    f.restored();
    require(f.object.visible == 0 && f.object.enabled == 1 && f.object.disabled == 0,
            "Suppression changed native object flags");
    f.object.vtable = image + profile->script_control;
    require(!menu_corner() && !suppress_menu_corner(true),
            "ScriptControl was accepted as the native corner");
    f.object.vtable = image + profile->menu_corner;
    for (auto* flag : {&f.object.enabled, &f.object.disabled}) {
        require(suppress_menu_corner(true), "Eligibility transition setup failed");
        const auto before = *flag;
        *flag = flag == &f.object.enabled ? 0 : 1;
        require(!menu_corner(), "Ineligible native menu remained available");
        f.restored();
        *flag = before;
    }
    require(suppress_menu_corner(true), "Context transition setup failed");
    f.app.state = nullptr;
    require(!menu_corner_identity(), "Missing State retained stale native identity");
    require(IsRectEmpty(&f.rectangle->bounds), "Identity snapshot mutated native suppression");
    require(!menu_corner(), "Missing State kept corner eligible");
    f.restored();
    f.app.state = f.state.data();
    require(suppress_menu_corner(true), "Membership transition setup failed");
    f.list() = {};
    require(!menu_corner(), "Removed type26 object kept corner eligible");
    f.restored();
    f.list() = {nullptr, 1, &f.node, &f.node, nullptr};
    List<std::byte>::Node duplicate{reinterpret_cast<std::byte*>(&f.object), nullptr, &f.node};
    f.node.next = &duplicate;
    f.list().count = 2;
    f.list().last = &duplicate;
    require(!menu_corner(), "Duplicate native menu identity was accepted");
    f.node.next = nullptr;
    f.list().last = &f.node;
    f.list().count = 257;
    require(!menu_corner(), "Unbounded native list was accepted");
    f.list().count = 1;
    f.node.next = &f.node;
    require(!menu_corner(), "Cyclic native list was accepted");
    f.node.next = nullptr;
    f.list().last = nullptr;
    require(!menu_corner(), "Broken list membership was accepted");
    f.list().last = &f.node;
    f.node.value = reinterpret_cast<std::byte*>(1);
    require(!menu_corner(), "Unreadable native object was accepted");
    f.node.value = reinterpret_cast<std::byte*>(&f.object);
    *reinterpret_cast<Application**>(image + profile->application) =
        reinterpret_cast<Application*>(1);
    require(!menu_corner(), "Unreadable Application was accepted");
    *reinterpret_cast<Application**>(image + profile->application) = &f.app;
    f.app.state = reinterpret_cast<void*>(1);
    require(!menu_corner(), "Unreadable State was accepted");
    f.app.state = f.state.data();
    exercise_spanning_object(f);
    DWORD protection = 0;
    require(VirtualProtect(f.rectangle, sizeof(*f.rectangle), PAGE_NOACCESS, &protection),
            "Cannot protect native rectangle fixture");
    require(!menu_corner() && !suppress_menu_corner(true), "Unreadable Rectangle was accepted");
    require(VirtualProtect(f.rectangle, sizeof(*f.rectangle), protection, &protection),
            "Cannot restore native rectangle fixture protection");
    for (const RECT bounds : {RECT{640, 0, 501, 34}, RECT{501, 0, 640, 0}, RECT{-1, 0, 640, 34},
                              RECT{501, 0, 641, 34}}) {
        f.rectangle->bounds = bounds;
        require(!menu_corner() && !suppress_menu_corner(true),
                "Invalid native rectangle was accepted");
    }
    f.rectangle->bounds = f.original;
    require(suppress_menu_corner(true), "External ownership setup failed");
    const RECT external{500, 1, 639, 35};
    f.rectangle->bounds = external;
    require(!suppress_menu_corner(false), "Changed bounds retained suppression ownership");
    require(EqualRect(&f.rectangle->bounds, &external),
            "Release overwrote externally changed bounds");
    f.rectangle->bounds = f.original;
    require(suppress_menu_corner(true), "External vtable ownership setup failed");
    f.rectangle->vtable = image + 0x200;
    require(!suppress_menu_corner(false), "Changed vtable retained suppression ownership");
    require(IsRectEmpty(&f.rectangle->bounds) && f.rectangle->vtable == image + 0x200,
            "Release overwrote externally changed Rectangle ownership");
}
}

namespace enhancements::game {
std::byte* executable_image() {
    return image;
}

const Edition& edition() {
    return *profile;
}
}

int main() {
    try {
        for (const auto* edition :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_dvd_20000, &native_game::profile_cd_10020}) {
            exercise(*edition);
        }
        std::cout << "Four-profile native corner identity, suppression and ownership passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
