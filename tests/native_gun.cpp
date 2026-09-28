#include "enhancements/gun.h"
#include "enhancements/game_ui.h"
#include "enhancements/dialogue.h"
#include "enhancements/game_resources.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class T> void write(void* address, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(address) + offset, &value, sizeof(value));
}

std::byte* image = nullptr;
const enhancements::game::Edition* profile = nullptr;
bool world = true, aiming = false, modal = false, dialogue = false;
unsigned selections = 0, modes = 0;
unsigned cursor_updates = 0;
void* expected_resource = nullptr;
void* expected_queue = nullptr;
std::byte* variable = nullptr;
std::byte* application = nullptr;
bool reenter = false, change_state = false;

void __stdcall queue_mouse_move(void* app, const std::byte* previous, const std::byte* current) {
    require(app == application, "Cursor update must use the current application.");
    POINT before{}, after{};
    std::memcpy(&before, previous + 4, sizeof(before));
    std::memcpy(&after, current + 4, sizeof(after));
    require(before.x == 320 && before.y == 210 && before.x == after.x && before.y == after.y,
            "Cursor refresh must use equal native points without nudging the pointer.");
    require(modes == 1, "Cursor update must follow native gun selection.");
    ++cursor_updates;
}

void __stdcall select_inventory(void* resource, void* queue) {
    require(resource == expected_resource && queue == expected_queue,
            "Native selector received the wrong owned resource or application queue.");
    require(enhancements::controller_gun_busy(), "Native transaction must guard nested polling.");
    ++selections;
    if (reenter) {
        require(!enhancements::equip_controller_gun(),
                "Recursive equip must not invoke native code.");
    }
    if (change_state) {
        write(application, 0xe4, static_cast<void*>(nullptr));
    }
    write(image, profile->selected_inventory, resource);
}

void __stdcall set_action(int mode, void* queue) {
    require(selections == modes + 1 && mode == 9 && queue == expected_queue,
            "Native action must follow resource selection with the fixed gun mode.");
    ++modes;
    write(variable, 0x38, mode);
}

template <class T> void trampoline(std::uint32_t offset, T function) {
    image[offset] = std::byte{0x68};
    write(image, offset + 1, function);
    image[offset + 5] = std::byte{0xc3};
}

struct Fixture {
    std::byte* allocation = nullptr;
    std::array<std::byte, 0x300> app{}, state{};
    std::array<std::byte, 0x48> action_variable{};
    std::array<std::array<std::byte, 0x200>, 3> items{};
    std::array<std::array<std::byte, 0x60>, 3> resources{};
    std::array<std::array<std::byte, 0x20>, 3> graphics{}, assets{};
    std::array<native_game::List<std::byte>::Node, 3> nodes{};
    native_game::List<std::byte>::Node event_node{};
    void* event_vtable = reinterpret_cast<void*>(0x1234);
    std::byte view{};

    Fixture(const enhancements::game::Edition& edition, unsigned gun_index) {
        profile = &edition;
        image = static_cast<std::byte*>(
            VirtualAlloc(nullptr, 0x300000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        require(image != nullptr, "Cannot allocate native ABI fixture.");
        allocation = image;
        world = true;
        aiming = modal = dialogue = reenter = change_state = false;
        selections = modes = cursor_updates = 0;
        application = app.data();
        variable = action_variable.data();
        expected_resource = resources[gun_index].data();
        expected_queue = app.data() + 0x268;
        write(image, profile->application, app.data());
        write(image, profile->scene_active, 1u);
        write(image, profile->registered_gun_action, 0x1bf6u);
        write(image, profile->inventory_action_variable, variable);
        write(app.data(), 0xe4, state.data());
        write(app.data(), 0xec, &view);
        write(app.data(), 0x280, reinterpret_cast<void*>(0x1234));
        write(app.data(), 0x28c, reinterpret_cast<void*>(0x5678));
        write(app.data(), 0x290, 100u);
        write(variable, 0x41, std::uint8_t{1});
        event_node.value = reinterpret_cast<std::byte*>(&event_vtable);
        const native_game::List<std::byte> events{nullptr, 1, &event_node, &event_node};
        for (unsigned i = 0; i < items.size(); ++i) {
            nodes[i] = {items[i].data(), i == 2 ? nullptr : &nodes[i + 1],
                        i == 0 ? nullptr : &nodes[i - 1]};
            write(items[i].data(), 0, image + profile->owned_inventory_item);
            write(items[i].data(), 8, 1u);
            write(items[i].data(), 0x18, resources[i].data());
            write(items[i].data(), 0x1c, events);
            write(items[i].data(), 0x1e4, graphics[i].data());
            write(graphics[i].data(), 0x18, assets[i].data());
            write(assets[i].data(), 4,
                  i == gun_index ? enhancements::resource::inventory_gun : 0x2000u + i);
            write(resources[i].data(), 0x38, 0x1bf6u);
        }
        const native_game::List<std::byte> owned{nullptr, 3, &nodes.front(), &nodes.back()};
        write(state.data(), 0x144, owned);
        trampoline(profile->select_inventory, select_inventory);
        trampoline(profile->set_inventory_action, set_action);
        trampoline(profile->queue_mouse_move, queue_mouse_move);
        FlushInstructionCache(GetCurrentProcess(), image, 0x300000);
    }

    ~Fixture() {
        VirtualFree(allocation, 0, MEM_RELEASE);
        image = nullptr;
    }
};

}

namespace enhancements::game {

const Edition& edition() {
    return *profile;
}

std::byte* executable_image() {
    return image;
}

bool world_navigation_available() {
    return world;
}

bool menu_confirmation_active() {
    return modal;
}

std::vector<RECT> aiming_targets() {
    return aiming ? std::vector<RECT>{{0, 0, 10, 10}} : std::vector<RECT>{};
}

}

namespace enhancements {

const Dialogue* current_dialogue() {
    static Dialogue value;
    return dialogue ? &value : nullptr;
}

}

int main() {
    try {
        for (const auto edition :
             {&native_game::profile_cd_10012, &native_game::profile_cd_10019,
              &native_game::profile_cd_10020, &native_game::profile_dvd_20000}) {
            for (unsigned position = 0; position < 3; ++position) {
                Fixture f(*edition, position);
                reenter = true;
                require(enhancements::equip_controller_gun(),
                        "Owned available gun must equip at every inventory position.");
                require(
                    selections == 1 && modes == 1 && !enhancements::controller_gun_busy(),
                    "Equip must invoke each native function exactly once and release its guard.");
                require(enhancements::equip_controller_gun() && selections == 1 && modes == 1,
                        "Already equipped gun must not invoke native functions again.");
                enhancements::refresh_controller_gun_cursor({320, 210});
                require(cursor_updates == 1,
                        "Successful equip must allow a stationary cursor update.");
                write(f.app.data(), 0x284, 100u);
                enhancements::refresh_controller_gun_cursor({320, 210});
                require(cursor_updates == 1,
                        "A full native input queue must reject cursor updates.");
                write(f.app.data(), 0x284, 0u);
                write(f.app.data(), 0x290, 0u);
                enhancements::refresh_controller_gun_cursor({320, 210});
                require(cursor_updates == 1,
                        "An unavailable input queue must reject cursor updates.");
                write(f.app.data(), 0x290, 100u);
                write(f.app.data(), 0x28c, static_cast<void*>(nullptr));
                enhancements::refresh_controller_gun_cursor({320, 210});
                require(cursor_updates == 1,
                        "Missing native input storage must reject cursor updates.");
            }
            for (bool other_resource : {false, true}) {
                Fixture f(*edition, 1);
                write(variable, 0x38, 9);
                write(image, profile->selected_inventory,
                      other_resource ? f.resources[0].data() : nullptr);
                require(enhancements::equip_controller_gun() && selections == 1 && modes == 1,
                        "Gun mode with a missing or different resource must repair selection.");
            }
            const auto reject = [&](auto invalidate, const char* reason) {
                Fixture f(*edition, 1);
                invalidate(f);
                require(!enhancements::equip_controller_gun() && !selections && !modes, reason);
            };
            reject([](Fixture&) { image = nullptr; }, "Unknown executable must fail closed.");
            reject([](Fixture&) { world = false; }, "Unavailable gameplay must fail closed.");
            reject([](Fixture&) { modal = true; }, "Modal context must fail closed.");
            reject([](Fixture&) { dialogue = true; }, "Conversation context must fail closed.");
            reject([](Fixture& f) { write(f.app.data(), 0x26c, 1u); },
                   "Pending native events must defer equip.");
            reject([](Fixture&) { write(image, profile->scene_active, 0u); },
                   "Inactive scene must fail closed.");
            reject([](Fixture&) { write(variable, 0x41, std::uint8_t{2}); },
                   "Unregistered action state must fail closed.");
            reject([](Fixture& f) { write(f.assets[1].data(), 4, 123u); },
                   "Unowned gun must fail closed.");
            reject(
                [](Fixture& f) {
                    write(f.assets[0].data(), 4, enhancements::resource::inventory_gun);
                },
                "Duplicate gun identity must fail closed.");
            reject([](Fixture& f) { write(f.items[1].data(), 8, 0u); },
                   "Disabled gun must fail closed.");
            reject([](Fixture& f) { write(f.items[1].data(), 12, 1u); },
                   "Blocked gun must fail closed.");
            reject([](Fixture& f) { write(f.resources[1].data(), 0x38, 123u); },
                   "Unavailable gun action must fail closed.");
            reject(
                [](Fixture& f) { write(f.items[1].data(), 0x1c, native_game::List<std::byte>{}); },
                "Gun without native click action must fail closed.");
            reject([](Fixture& f) { f.nodes[1].next = &f.nodes[1]; },
                   "Cyclic ownership must fail closed.");
            reject([](Fixture& f) { f.nodes[1].value = reinterpret_cast<std::byte*>(1); },
                   "Unreadable inventory must fail closed.");
            reject([](Fixture& f) { write(f.state.data(), 0x148, 257u); },
                   "Oversized ownership must fail closed.");
            {
                Fixture f(*edition, 1);
                world = false;
                aiming = true;
                require(enhancements::equip_controller_gun(),
                        "Active action targets must allow native equip.");
            }
            {
                Fixture f(*edition, 1);
                change_state = true;
                require(!enhancements::equip_controller_gun() && selections == 1 && modes == 0,
                        "Scene replacement during native resource loading must cancel the second "
                        "call.");
            }
        }
        std::cout << "Native gun selection checks passed for all four profiles.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
