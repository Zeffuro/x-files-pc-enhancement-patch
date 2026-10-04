#include "enhancements/world_cursors.h"
#include "enhancements/world_interactions.h"

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace enhancements::game;

template <class T> void put(void* target, unsigned offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(target) + offset, &value, sizeof(value));
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

using MemoryRead = BOOL(WINAPI*)(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T*);
MemoryRead original_read = nullptr;
const void* watched_source = nullptr;
unsigned watched_reads = 0;
std::function<void()> change_on_recheck;

BOOL WINAPI changing_read(HANDLE process, LPCVOID source, LPVOID target, SIZE_T size,
                          SIZE_T* copied) {
    if (source == watched_source && ++watched_reads == 2 && change_on_recheck) {
        change_on_recheck();
    }
    return original_read(process, source, target, size, copied);
}

struct MemoryReadHook {
    IMAGE_THUNK_DATA* slot = nullptr;

    MemoryReadHook() {
        auto base = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto pe = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            base + pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for (; imports->Name && !slot; ++imports) {
            if (!imports->OriginalFirstThunk) {
                continue;
            }
            auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
            auto functions = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++functions) {
                if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                    auto name =
                        reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                    if (std::strcmp(name->Name, "ReadProcessMemory") == 0) {
                        slot = functions;
                        break;
                    }
                }
            }
        }
        require(slot != nullptr, "ReadProcessMemory fixture import was not found");
        original_read = reinterpret_cast<MemoryRead>(slot->u1.Function);
        replace(reinterpret_cast<ULONG_PTR>(&changing_read));
    }

    void replace(ULONG_PTR function) const {
        DWORD protection = 0, ignored = 0;
        require(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection) != 0,
                "Could not protect fixture import");
        slot->u1.Function = function;
        require(VirtualProtect(slot, sizeof(*slot), protection, &ignored) != 0,
                "Could not restore fixture import protection");
    }

    ~MemoryReadHook() {
        change_on_recheck = {};
        watched_source = nullptr;
        replace(reinterpret_cast<ULONG_PTR>(original_read));
    }
};

struct Fixture {
    const Edition& profile;
    std::vector<std::byte> image = std::vector<std::byte>(3 * 1024 * 1024);
    alignas(void*) std::array<std::byte, 0x300> app{};
    alignas(void*) std::array<std::byte, 0x180> object{};
    alignas(void*) std::array<std::byte, 0x80> resource{}, variable{}, linked{};
    List<std::byte>::Node trigger_node{};
    void* trigger_type = nullptr;

    explicit Fixture(const Edition& profile) : profile(profile) {
        put(object.data(), 0, image.data() + profile.world_picture);
        put(object.data(), 8, 1u);
        put(object.data(), 0x18, resource.data());
        put(resource.data(), 0, image.data() + profile.world_picture_resource);
        put(resource.data(), 4, 123u);
        put(resource.data(), 0x44, 0x122u);
        put(object.data(), 0x1c, List<std::byte>{image.data(), 0, nullptr, nullptr, nullptr});
        trigger_type = image.data();
        trigger_node = {reinterpret_cast<std::byte*>(&trigger_type), nullptr, nullptr};
    }

    Interaction refine(Interaction fallback = Interaction::click) {
        return world_cursor_interaction(object.data(), resource.data(),
                                        reinterpret_cast<Application*>(app.data()), image.data(),
                                        profile, fallback);
    }

    Interaction classify() {
        return world_interaction(object.data(), resource.data(),
                                 reinterpret_cast<Application*>(app.data()), image.data(), profile);
    }

    void click() {
        put(object.data(), 0x1c,
            List<std::byte>{image.data(), 1, &trigger_node, &trigger_node, nullptr});
    }

    void shape(unsigned id) {
        put(object.data(), 0, image.data() + profile.world_hotspot);
        put(resource.data(), 0, image.data() + profile.world_hotspot_resource);
        put(resource.data(), 0x40, id);
        click();
    }

    void navigation(unsigned id) {
        put(object.data(), 0, image.data() + profile.world_navigation);
        put(resource.data(), 0, image.data() + profile.world_navigation_resource);
        put(resource.data(), 0x2c, 456u);
        put(linked.data(), 0, image.data() + profile.world_navigation_shape_resource);
        put(linked.data(), 4, 456u);
        put(linked.data(), 0x40, id);
    }

    Interaction navigation_refine() {
        return navigation_cursor_interaction(object.data(), resource.data(), linked.data(),
                                             image.data(), profile);
    }
};

template <class Mutation>
void navigation_rejected(const Edition& profile, Mutation mutation, const char* message,
                         bool during_read = false) {
    Fixture fixture(profile);
    fixture.navigation(0x4454);
    if (during_read) {
        watched_source = fixture.object.data();
        watched_reads = 0;
        change_on_recheck = [&] { mutation(fixture); };
    } else {
        mutation(fixture);
    }
    const auto actual = fixture.navigation_refine();
    change_on_recheck = {};
    watched_source = nullptr;
    require(actual == Interaction::click, message);
}

template <class Mutation>
void rejected(const Edition& profile, Mutation mutation, const char* message) {
    Fixture fixture(profile);
    mutation(fixture);
    require(fixture.refine() == Interaction::click, message);
    require(fixture.refine(Interaction::unknown) == Interaction::unknown,
            "Descriptor invented ordinary registration");
}

void check(const Edition& profile) {
    Fixture fixture(profile);
    require(fixture.classify() == Interaction::unknown,
            "Empty event0 plus a mouth descriptor invented Talk");
    fixture.click();
    require(fixture.classify() == Interaction::talk,
            "Unhovered registered picture did not use its configured mouth");
    put(fixture.resource.data(), 0x44, 0x1bfcu);
    require(fixture.classify() == Interaction::view,
            "Unhovered registered picture did not use its configured eye");
    put(fixture.image.data(), profile.selected_inventory, fixture.resource.data());
    put(fixture.image.data(), profile.inventory_action_variable, fixture.variable.data());
    put(fixture.variable.data(), 0x38, 9u);
    put(fixture.variable.data(), 0x41, std::uint8_t{2});
    put(fixture.image.data(), profile.world_hover, reinterpret_cast<void*>(1));
    put(fixture.app.data(), 0x26c, 1u);
    put(fixture.app.data(), 0x284, 1u);
    require(fixture.classify() == Interaction::view,
            "Configured base cursor depended on native hover or selected inventory");
    put(fixture.resource.data(), 0x44, 0x4454u);
    require(fixture.classify() == Interaction::use,
            "Configured action hands did not acquire the ordinary Use category");
    for (const auto id : {0x11cu, 0x11du, 0x11eu, 0x11fu, 0x120u, 0x117au, 0xea2eu}) {
        put(fixture.resource.data(), 0x44, id);
        require(fixture.classify() == Interaction::click,
                "Configured picture movement asset invented a movement target");
    }
    put(fixture.resource.data(), 0x44, 0xdeadbeefu);
    require(fixture.classify() == Interaction::click,
            "Unknown configured picture descriptor acquired a category");
    put(fixture.resource.data(), 0x44, 0x122u);
    put(fixture.object.data(), 0x1c,
        List<std::byte>{fixture.image.data(), 0, nullptr, nullptr, nullptr});
    put(fixture.object.data(), 0x148, &fixture.trigger_type);
    put(fixture.object.data(), 0x160, 1u);
    require(fixture.classify() == Interaction::item,
            "Configured mouth overrode the proved item-only contract");
    require(fixture.refine(Interaction::item) == Interaction::item,
            "Cursor helper overrode an item-only classification");
    rejected(
        profile, [](Fixture& f) { put(f.object.data(), 0x18, f.variable.data()); },
        "Stale target resource identity was accepted");
    rejected(
        profile, [](Fixture& f) { put(f.object.data(), 8, 0u); }, "Disabled picture was refined");
    rejected(
        profile, [](Fixture& f) { put(f.object.data(), 12, 1u); }, "Blocked picture was refined");
    rejected(
        profile, [](Fixture& f) { put(f.object.data(), 0, f.image.data()); },
        "Unsupported picture object was refined");
    rejected(
        profile, [](Fixture& f) { put(f.resource.data(), 0, f.image.data()); },
        "Unsupported picture resource was refined");
    rejected(
        profile, [](Fixture& f) { put(f.resource.data(), 4, 0u); },
        "Zero picture resource identity was accepted");
    require(world_cursor_interaction(fixture.object.data(), reinterpret_cast<void*>(1), nullptr,
                                     fixture.image.data(), profile,
                                     Interaction::click) == Interaction::click,
            "Unreadable picture resource lost the generic registered action");

    Fixture shape(profile);
    for (const auto [id, verb] :
         {std::pair{0x1bfcu, Interaction::view}, std::pair{0x122u, Interaction::talk},
          std::pair{0x4454u, Interaction::use}, std::pair{0x11cu, Interaction::move_left},
          std::pair{0x11du, Interaction::move_right}, std::pair{0x11eu, Interaction::move_forward},
          std::pair{0x120u, Interaction::move_forward},
          std::pair{0xea2eu, Interaction::move_forward}, std::pair{0x11fu, Interaction::move_back},
          std::pair{0x117au, Interaction::move_back}}) {
        shape.shape(id);
        require(shape.classify() == verb, "Verified configured shape cursor was not refined");
    }
    for (const auto id : {0u, 0x2efu, 0xdeadbeefu}) {
        shape.shape(id);
        require(shape.classify() == Interaction::click, "Unproved shape ID acquired a category");
    }
    shape.shape(0x1bfc);
    put(shape.resource.data(), 0, shape.image.data());
    require(shape.classify() == Interaction::click, "Unsupported shape resource was refined");
    put(shape.object.data(), 0x18, reinterpret_cast<void*>(1));
    require(shape.classify() == Interaction::click, "Stale shape resource lost the generic action");
    shape.shape(0x1bfc);
    put(shape.object.data(), 0x18, shape.resource.data());
    put(shape.object.data(), 0x1c,
        List<std::byte>{shape.image.data(), 0, nullptr, nullptr, nullptr});
    require(shape.classify() == Interaction::unknown,
            "Shape cursor descriptor invented direct registration");

    Fixture navigation(profile);
    for (const auto [id, verb] :
         {std::pair{0x1bfcu, Interaction::view}, std::pair{0x122u, Interaction::talk},
          std::pair{0x4454u, Interaction::use}, std::pair{0x11cu, Interaction::move_left},
          std::pair{0x11du, Interaction::move_right}, std::pair{0x11eu, Interaction::move_forward},
          std::pair{0x120u, Interaction::move_forward},
          std::pair{0xea2eu, Interaction::move_forward}, std::pair{0x11fu, Interaction::move_back},
          std::pair{0x117au, Interaction::move_back}}) {
        navigation.navigation(id);
        require(navigation.navigation_refine() == verb,
                "Exact navigation wrapper and owned linked shape did not refine its cursor");
    }
    for (const auto id : {0u, 0x2efu, 0xdeadbeefu}) {
        navigation.navigation(id);
        require(navigation.navigation_refine() == Interaction::click,
                "Unknown navigation cursor acquired an unsupported category");
    }
    for (const auto during_read : {false, true}) {
        navigation_rejected(
            profile, [](Fixture& f) { put(f.object.data(), 0, f.image.data()); },
            "Foreign or changed navigation object type was accepted", during_read);
        navigation_rejected(
            profile, [](Fixture& f) { put(f.object.data(), 0x18, f.linked.data()); },
            "Foreign or changed navigation wrapper pointer was accepted", during_read);
        navigation_rejected(
            profile, [](Fixture& f) { put(f.resource.data(), 0, f.image.data()); },
            "Foreign or changed navigation wrapper type was accepted", during_read);
        navigation_rejected(
            profile, [](Fixture& f) { put(f.resource.data(), 4, 0u); },
            "Zero or changed navigation wrapper identity was accepted", during_read);
        navigation_rejected(
            profile, [](Fixture& f) { put(f.resource.data(), 0x2c, 457u); },
            "Mismatched or changed navigation shape link was accepted", during_read);
        navigation_rejected(
            profile,
            [](Fixture& f) {
                put(f.linked.data(), 0, f.image.data() + f.profile.world_hotspot_resource);
            },
            "Class37 or changed navigation linked type was accepted", during_read);
        navigation_rejected(
            profile, [](Fixture& f) { put(f.linked.data(), 4, 0u); },
            "Zero or changed linked navigation identity was accepted", during_read);
        if (during_read) {
            navigation_rejected(
                profile, [](Fixture& f) { put(f.linked.data(), 0x40, 0x122u); },
                "Changing configured navigation cursor was accepted", true);
        }
    }
    navigation_rejected(
        profile, [](Fixture& f) { put(f.object.data(), 8, 0u); },
        "Disabled navigation was refined");
    navigation_rejected(
        profile, [](Fixture& f) { put(f.object.data(), 12, 1u); },
        "Blocked navigation was refined");
    require(navigation_cursor_interaction(navigation.object.data(), navigation.resource.data(),
                                          reinterpret_cast<void*>(1), navigation.image.data(),
                                          profile) == Interaction::click,
            "Unreadable linked navigation resource was refined");
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto storage = static_cast<std::byte*>(
        VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS));
    require(storage && VirtualAlloc(storage, system.dwPageSize, MEM_COMMIT, PAGE_READWRITE),
            "Could not allocate navigation descriptor read boundary");
    const auto boundary = storage + system.dwPageSize - 0x40;
    put(boundary, 0, navigation.image.data() + profile.world_navigation_shape_resource);
    put(boundary, 4, 456u);
    const auto boundary_result =
        navigation_cursor_interaction(navigation.object.data(), navigation.resource.data(),
                                      boundary, navigation.image.data(), profile);
    VirtualFree(storage, 0, MEM_RELEASE);
    require(boundary_result == Interaction::click,
            "Unreadable cursor field on a valid linked navigation header was refined");
}

}

int main() {
    try {
        MemoryReadHook hook;
        for (const auto* profile :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_dvd_20000, &native_game::profile_cd_10020}) {
            check(*profile);
        }
        std::cout << "All four configured native cursor profiles passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
