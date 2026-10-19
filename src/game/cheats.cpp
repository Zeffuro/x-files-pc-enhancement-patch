#include "cheats.h"
#include "access/ui.h"
#include "functions/runtime.h"
#include "layouts/commands.h"
#include <array>
#include <limits>

namespace native_game {
namespace {
constexpr Cheat entries[]{
    {L"Mary Astadourian", 0x11918, 0x11919},
    {L"superagent", 0x123d0, 0x123d2},
    {L"warp speed x", 0xffdd, 0xffdf},
    {L"Corey Hart", 0x140bc, 0x140bd},
    {L"the nature of infinity is paradox", 0x14471, 0x14886},
    {L"good idea oLord", 0xffe2, 0xffe4},
    {L"paranoia me up", 0x14a54, 0x14a56},
    {L"paranoia me down", 0x14a59, 0x14a5a},
    {L"loss me up", 0x14a5c, 0x14a5d},
    {L"loss me down", 0x14a61, 0x14a62},
    {L"x me up", 0x14a64, 0x14a65},
    {L"x me down", 0x14a67, 0x14a68},
    {L"asta me up", 0x14a6a, 0x14a6b},
    {L"asta me down", 0x14a6d, 0x14a6e},
    {L"Dana Scully", 0x14e13, 0x14e14},
    {L"Walter Skinner", 0x14e22, 0x14e23},
    {L"Armistead Shanks", 0x14e56, 0x14e57},
    {L"Mark Cook", 0x14e59, 0x14e5a},
    {L"John Amis", 0x14e5c, 0x14e5d},
    {L"Arley", 0x14e5f, 0x14e60},
    {L"Joan Truit", 0x14e62, 0x14e63},
    {L"Smolnikoff", 0x14e65, 0x14e66},
    {L"Stearns", 0x14e6b, 0x14e6c},
    {L"Von", 0xe550, 0xe552},
    {L"action movie", 0x15151, 0x15152},
    {L"big booty", 0x15272, 0x15273},
};

struct Pair {
    std::uint32_t trigger, actions;
};

bool idle(const MemoryRef<CommandQueue>& queue, const NativeRuntime& native) {
    CommandQueue value{};
    if (!queue.read(value) || value.data.vtable != native.command_queue_type() ||
        value.data.capacity < 10 || value.data.capacity > 65536 || !value.data.storage ||
        value.data.count || value.data.current != std::numeric_limits<unsigned>::max() ||
        value.last != std::numeric_limits<unsigned>::max()) {
        return false;
    }
    std::array<void*, 10> storage{};
    return MemoryRef<decltype(storage)>(value.data.storage).read(storage);
}
}

std::span<const Cheat> cheats() {
    return entries;
}

std::optional<std::size_t> cheat_index(std::wstring_view code) {
    for (std::size_t i = 0; i < std::size(entries); ++i) {
        if (entries[i].code == code) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<CheatContext> cheat_identity(const std::byte* image, const Profile& profile,
                                           HWND window) {
    if (!image || !profile.application || !profile.queue_action_list || !profile.cheat_table ||
        !profile.session_active || !profile.scene_active || !profile.pending_load ||
        !profile.command_queue_type || !window ||
        GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
        return std::nullopt;
    }
    const NativeRuntime native(image, profile);
    Application* app = nullptr;
    Application value{};
    int session = 0, scene = 0;
    void* load = nullptr;
    if (!native.application().read(app) || !app || !MemoryRef<Application>(app).read(value) ||
        !value.state || !value.view || !native.session_active().read(session) ||
        !native.scene_active().read(scene) || !scene || !native.pending_load().read(load) || load) {
        return std::nullopt;
    }
    return CheatContext{window, reinterpret_cast<std::uintptr_t>(app),
                        reinterpret_cast<std::uintptr_t>(value.state),
                        reinterpret_cast<std::uintptr_t>(value.view)};
}

std::optional<CheatContext> cheat_context(const std::byte* image, const Profile& profile,
                                          HWND window) {
    const auto identity = cheat_identity(image, profile, window);
    if (!identity) {
        return std::nullopt;
    }
    const NativeRuntime native(image, profile);
    const ApplicationRef app(identity->application);
    return idle(app.commands(), native) && idle(app.pointer_events(), native) ? identity
                                                                              : std::nullopt;
}

bool queue_cheat(const std::byte* image, const Profile& profile, const CheatContext& expected,
                 std::size_t index, NativeFunctions::QueueActionList callback) {
    const auto current = cheat_context(image, profile, expected.window);
    if (index >= std::size(entries) || !current || *current != expected) {
        return false;
    }
    std::array<Pair, std::size(entries)> table{};
    if (!MemoryRef<decltype(table)>(image + profile.cheat_table).read(table)) {
        return false;
    }
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i].trigger != entries[i].trigger || table[i].actions != entries[i].actions) {
            return false;
        }
    }
    const NativeFunctions native(image, profile);
    if (!callback) {
        constexpr std::array<std::uint8_t, 12> prefix{0x64, 0xa1, 0,    0,    0,    0,
                                                      0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
        std::array<std::uint8_t, 12> actual{};
        if (!MemoryRef<decltype(actual)>(
                 reinterpret_cast<const void*>(native.queue_action_list_target()))
                 .read(actual) ||
            actual != prefix) {
            return false;
        }
        callback = native.queue_action_list_target();
    }
    // Original commands retain their resources and are consumed by the normal game update.
    callback(table[index].trigger, table[index].actions,
             ApplicationRef(expected.application).commands().data(), 0);
    return true;
}
}
