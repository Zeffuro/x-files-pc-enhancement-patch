#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
const void* denied_read = nullptr;
const void* short_read = nullptr;

BOOL WINAPI fixture_read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size,
                         SIZE_T* copied) {
    if (source == denied_read) {
        *copied = 0;
        return FALSE;
    }
    const auto result = ReadProcessMemory(process, source, destination, size, copied);
    if (result && source == short_read && *copied) {
        --*copied;
    }
    return result;
}
}

#define ReadProcessMemory fixture_read
#include "game/cheats.cpp"
#undef ReadProcessMemory
#include "devtools/easter_egg_request.h"
#include <utility>

namespace {
using namespace native_game;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Expected {
    std::wstring_view code;
    std::uint32_t trigger, actions;
};

constexpr Expected expected[]{
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

struct Dispatch {
    std::uint32_t trigger = 0, actions = 0;
    void* queue = nullptr;
    std::uintptr_t reserved = 1;
    unsigned calls = 0;
};

Dispatch dispatched;

void __stdcall record_dispatch(std::uint32_t trigger, std::uint32_t actions, void* queue,
                               std::uintptr_t reserved) {
    dispatched = {trigger, actions, queue, reserved, dispatched.calls + 1};
}

struct Fixture {
    std::vector<std::byte> image = std::vector<std::byte>(0x300000);
    ApplicationQueuesPrefix app{};
    std::array<std::array<void*, 10>, 2> storage{};
    std::byte state{}, view{}, replacement{};
    HWND window = nullptr;
    Profile profile;

    explicit Fixture(const Profile& build) : profile(build) {
        window = CreateWindowExW(0, L"STATIC", L"Native cheat fixture", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Cannot create the owner-thread fixture window");
        put(profile.application, &app.application);
        put(profile.session_active, 1);
        put(profile.scene_active, 1);
        put(profile.pending_load, static_cast<void*>(nullptr));
        app.application.state = &state;
        app.application.view = reinterpret_cast<MainView*>(&view);
        for (unsigned i = 0; i < 2; ++i) {
            auto& queue = i ? app.pointer_events : app.commands;
            queue = {{image.data() + profile.command_queue_type, 0,
                      std::numeric_limits<unsigned>::max(), storage[i].data(), 10},
                     std::numeric_limits<unsigned>::max()};
        }
        for (std::size_t i = 0; i < std::size(expected); ++i) {
            put(profile.cheat_table + static_cast<unsigned>(i * 8), expected[i].trigger);
            put(profile.cheat_table + static_cast<unsigned>(i * 8 + 4), expected[i].actions);
        }
    }

    ~Fixture() {
        DestroyWindow(window);
    }

    template <class T> void put(unsigned offset, const T& value) {
        require(offset <= image.size() && sizeof(value) <= image.size() - offset,
                "Fixture image write exceeds its allocation");
        std::memcpy(image.data() + offset, &value, sizeof(value));
    }

    CheatContext context() const {
        const auto result = cheat_context(image.data(), profile, window);
        require(result.has_value(), "An idle owned fixture has no cheat context");
        require(cheat_identity(image.data(), profile, window) == result,
                "An idle fixture has different identity and readiness");
        return *result;
    }

    bool queue(const CheatContext& context, std::size_t index = 1) {
        return queue_cheat(image.data(), profile, context, index, record_dispatch);
    }

    template <class Action> void reject(Action action) {
        const auto before_image = image;
        const auto before_storage = storage;
        std::array<std::byte, sizeof(app)> before_app{};
        std::memcpy(before_app.data(), &app, sizeof(app));
        const auto before_calls = dispatched.calls;
        require(!action(), "An invalid native cheat request was accepted");
        require(dispatched.calls == before_calls, "Rejected request invoked native dispatch");
        require(image == before_image && storage == before_storage &&
                    std::memcmp(&app, before_app.data(), sizeof(app)) == 0,
                "Rejected request changed its native image, application or queues");
    }

    void unready(const CheatContext& expected_context) {
        require(cheat_identity(image.data(), profile, window) == expected_context,
                "Queue readiness changed the captured native identity");
        require(!cheat_context(image.data(), profile, window),
                "A busy or malformed queue retained a ready cheat context");
        reject([&] { return queue(expected_context); });
    }

    void invalid_identity(const CheatContext& expected_context) {
        require(!cheat_identity(image.data(), profile, window),
                "Invalid native state retained a cheat identity");
        require(!cheat_context(image.data(), profile, window),
                "Invalid native state retained a ready cheat context");
        reject([&] { return queue(expected_context); });
    }
};

void verify_pending_request(Fixture& fixture, const CheatContext& context) {
    using devtools::EasterEggRequest;
    using Status = devtools::EasterEggRequestStatus;
    const auto poll = [&](EasterEggRequest& request, bool exploring, std::uint64_t now) {
        return devtools::poll_easter_egg_request(
            request, cheat_identity(fixture.image.data(), fixture.profile, fixture.window),
            exploring,
            cheat_context(fixture.image.data(), fixture.profile, fixture.window).has_value(), now);
    };
    for (auto* queue : {&fixture.app.commands, &fixture.app.pointer_events}) {
        const auto saved = *queue;
        std::optional<EasterEggRequest> pending = EasterEggRequest{context, 2, {}};
        const auto before = dispatched.calls;
        queue->data.count = 1;
        fixture.unready(context);
        require(poll(*pending, true, 100) == Status::waiting && pending->waiting_since == 100 &&
                    poll(*pending, true, 116) == Status::waiting && dispatched.calls == before,
                "A native focus event was rejected or dispatched before its queue drained");
        *queue = saved;
        require(poll(*pending, true, 132) == Status::ready,
                "A drained native queue did not release the deferred request");
        const auto request = std::exchange(pending, std::nullopt);
        require(!pending && fixture.queue(request->context, request->index) &&
                    dispatched.calls == before + 1 && dispatched.trigger == expected[2].trigger &&
                    dispatched.actions == expected[2].actions,
                "Deferred level-selector request did not schedule its original pair once");

        for (const auto now : {2100u, 2101u}) {
            EasterEggRequest expired{context, 2, {100}};
            require(poll(expired, true, now) == Status::timed_out && dispatched.calls == before + 1,
                    "Readiness at or beyond the deadline dispatched an expired request");
        }
        EasterEggRequest busy{context, 2, {100}};
        queue->data.count = 1;
        require(poll(busy, true, 2099) == Status::waiting &&
                    poll(busy, true, 2100) == Status::timed_out,
                "A busy native queue did not respect the bounded retry deadline");
        *queue = saved;
        EasterEggRequest backwards{context, 2, {100}};
        require(poll(backwards, true, 99) == Status::timed_out,
                "A regressed timer extended the deferred request lifetime");
    }
    EasterEggRequest immediate{context, 2, {}};
    require(poll(immediate, true, 100) == Status::ready && !immediate.waiting_since,
            "An already idle request acquired a retry delay");
    EasterEggRequest loading{
        context, std::size(expected), {}, devtools::EasterEggRequestKind::studio_load};
    const auto completed = fixture.app.commands.last;
    const auto before_load = dispatched.calls;
    fixture.app.commands.last = 0;
    require(poll(loading, true, 100) == Status::waiting && dispatched.calls == before_load,
            "Studio load started before native save bookkeeping completed");
    fixture.app.commands.last = completed;
    require(poll(loading, true, 116) == Status::ready &&
                loading.kind == devtools::EasterEggRequestKind::studio_load &&
                dispatched.calls == before_load,
            "Studio load stage was lost while waiting for the native save");
    for (const bool ready : {false, true}) {
        EasterEggRequest unsafe{context, 2, {100}};
        require(devtools::poll_easter_egg_request(unsafe, context, false, ready, 116) ==
                    Status::scene_changed,
                "Unsafe exploration retained a deferred request");
        require(devtools::poll_easter_egg_request(unsafe, std::nullopt, true, ready, 116) ==
                    Status::scene_changed,
                "Absent native identity retained a deferred request");
        for (const auto field :
             {&CheatContext::application, &CheatContext::state, &CheatContext::view}) {
            auto stale = context;
            ++(stale.*field);
            require(devtools::poll_easter_egg_request(unsafe, stale, true, ready, 116) ==
                        Status::scene_changed,
                    "A replaced native scene retained a deferred request");
        }
    }
}

void verify_profile(const Profile& profile, unsigned scheduler_rva, unsigned table_rva) {
    Fixture fixture(profile);
    const auto context = fixture.context();
    require(context.window == fixture.window &&
                context.application == reinterpret_cast<std::uintptr_t>(&fixture.app) &&
                context.state == reinterpret_cast<std::uintptr_t>(&fixture.state) &&
                context.view == reinterpret_cast<std::uintptr_t>(&fixture.view),
            "Captured context differs from the current application, state or view");
    require(profile.queue_action_list == scheduler_rva && profile.cheat_table == table_rva,
            "Profile differs from the original native scheduler or table address");
    const NativeFunctions functions(fixture.image.data(), profile);
    require(reinterpret_cast<std::uintptr_t>(functions.queue_action_list_target()) ==
                reinterpret_cast<std::uintptr_t>(fixture.image.data()) + scheduler_rva,
            "Native scheduling target ignored the relocated image base");
    verify_pending_request(fixture, context);

    for (std::size_t i = 0; i < std::size(expected); ++i) {
        const auto before = dispatched.calls;
        require(fixture.queue(context, i) && dispatched.calls == before + 1 &&
                    dispatched.trigger == expected[i].trigger &&
                    dispatched.actions == expected[i].actions &&
                    dispatched.queue == &fixture.app.commands && dispatched.reserved == 0,
                "Cheat scheduling changed the original pair, primary queue or fourth argument");
    }

    fixture.reject([&] { return fixture.queue(context, std::size(expected)); });
    fixture.reject([&] { return fixture.queue(context, std::numeric_limits<std::size_t>::max()); });
    for (const auto field :
         {&CheatContext::application, &CheatContext::state, &CheatContext::view}) {
        auto stale = context;
        ++(stale.*field);
        fixture.reject([&] { return fixture.queue(stale); });
    }
    auto missing_window = context;
    missing_window.window = nullptr;
    fixture.reject([&] { return fixture.queue(missing_window); });
    require(!cheat_identity(fixture.image.data(), profile, nullptr) &&
                !cheat_identity(nullptr, profile, fixture.window),
            "Missing native image or owner window retained an identity");
    fixture.reject([&] { return queue_cheat(nullptr, profile, context, 1, record_dispatch); });
    fixture.reject([&] { return queue_cheat(fixture.image.data(), profile, context, 1); });

    fixture.reject([&] {
        bool accepted = true;
        bool identity = true;
        std::thread worker([&] {
            identity = cheat_identity(fixture.image.data(), profile, fixture.window).has_value();
            accepted = fixture.queue(context);
        });
        worker.join();
        return accepted || identity;
    });

    for (const auto field :
         {&Profile::application, &Profile::queue_action_list, &Profile::cheat_table,
          &Profile::session_active, &Profile::scene_active, &Profile::pending_load,
          &Profile::command_queue_type}) {
        const auto saved = fixture.profile.*field;
        fixture.profile.*field = 0;
        fixture.invalid_identity(context);
        fixture.profile.*field = saved;
    }
    fixture.put(profile.application, static_cast<Application*>(nullptr));
    fixture.invalid_identity(context);
    auto replacement_app = fixture.app;
    fixture.put(profile.application, &replacement_app.application);
    require(cheat_identity(fixture.image.data(), profile, fixture.window) &&
                cheat_identity(fixture.image.data(), profile, fixture.window) != context,
            "Replacement application did not change the native identity");
    fixture.reject([&] { return fixture.queue(context); });
    fixture.put(profile.application, &fixture.app.application);
    fixture.put(profile.session_active, 0);
    require(fixture.queue(context),
            "A valid loaded session with the startup flag clear was blocked");
    fixture.put(profile.session_active, 1);
    fixture.put(profile.scene_active, 0);
    fixture.invalid_identity(context);
    fixture.put(profile.scene_active, 1);
    fixture.put(profile.pending_load, static_cast<void*>(&fixture.replacement));
    fixture.invalid_identity(context);
    fixture.put(profile.pending_load, static_cast<void*>(nullptr));

    fixture.app.application.state = &fixture.replacement;
    require(cheat_identity(fixture.image.data(), profile, fixture.window) &&
                cheat_identity(fixture.image.data(), profile, fixture.window) != context,
            "Replacement state did not change the native identity");
    fixture.reject([&] { return fixture.queue(context); });
    fixture.app.application.state = nullptr;
    fixture.invalid_identity(context);
    fixture.app.application.state = &fixture.state;
    fixture.app.application.view = reinterpret_cast<MainView*>(&fixture.replacement);
    require(cheat_identity(fixture.image.data(), profile, fixture.window) &&
                cheat_identity(fixture.image.data(), profile, fixture.window) != context,
            "Replacement view did not change the native identity");
    fixture.reject([&] { return fixture.queue(context); });
    fixture.app.application.view = nullptr;
    fixture.invalid_identity(context);
    fixture.app.application.view = reinterpret_cast<MainView*>(&fixture.view);
    for (auto* queue : {&fixture.app.commands, &fixture.app.pointer_events}) {
        const auto saved = *queue;
        queue->data.count = 1;
        fixture.unready(context);
        *queue = saved;
        queue->data.current = 0;
        fixture.unready(context);
        *queue = saved;
        queue->last = 0;
        fixture.unready(context);
        *queue = saved;
        for (const auto capacity : {0u, 9u, 65537u}) {
            queue->data.capacity = capacity;
            fixture.unready(context);
        }
        *queue = saved;
        queue->data.vtable = fixture.image.data();
        fixture.unready(context);
        *queue = saved;
        queue->data.storage = nullptr;
        fixture.unready(context);
        *queue = saved;
        require(fixture.context() == context,
                "Native readiness did not recover after the pending queue cleared");
    }

    for (const auto index : {0u, 1u, 25u}) {
        const auto offset = profile.cheat_table + index * 8;
        fixture.put(offset, expected[index].trigger ^ 1u);
        fixture.reject([&] { return fixture.queue(context, 1); });
        fixture.put(offset, expected[index].trigger);
        fixture.put(offset + 4, expected[index].actions ^ 1u);
        fixture.reject([&] { return fixture.queue(context, 1); });
        fixture.put(offset + 4, expected[index].actions);
    }

    const std::array<const void*, 10> read_sources{fixture.image.data() + profile.application,
                                                   &fixture.app.application,
                                                   fixture.image.data() + profile.session_active,
                                                   fixture.image.data() + profile.scene_active,
                                                   fixture.image.data() + profile.pending_load,
                                                   &fixture.app.commands,
                                                   &fixture.app.pointer_events,
                                                   fixture.storage[0].data(),
                                                   fixture.storage[1].data(),
                                                   fixture.image.data() + profile.cheat_table};
    for (std::size_t i = 0; i < read_sources.size(); ++i) {
        const auto source = read_sources[i];
        denied_read = source;
        require(cheat_identity(fixture.image.data(), profile, fixture.window).has_value() ==
                    (i >= 5),
                "Identity reads do not distinguish native state from queue and table reads");
        require(cheat_context(fixture.image.data(), profile, fixture.window).has_value() ==
                    (i == 9),
                "Readiness reads do not distinguish native state and queues from the table");
        fixture.reject([&] { return fixture.queue(context); });
        denied_read = nullptr;
        short_read = source;
        require(cheat_identity(fixture.image.data(), profile, fixture.window).has_value() ==
                    (i >= 5),
                "Identity accepted a partial native-state read or depended on queue readiness");
        require(cheat_context(fixture.image.data(), profile, fixture.window).has_value() ==
                    (i == 9),
                "Readiness accepted a partial state or queue read");
        fixture.reject([&] { return fixture.queue(context); });
        short_read = nullptr;
    }
    for (const bool partial : {false, true}) {
        const auto source = fixture.image.data() + profile.queue_action_list;
        denied_read = partial ? nullptr : source;
        short_read = partial ? source : nullptr;
        fixture.reject([&] { return queue_cheat(fixture.image.data(), profile, context, 1); });
        denied_read = short_read = nullptr;
    }
    require(fixture.queue(context, 25),
            "A repaired fixture did not recover after rejected requests");
}
}

int main() {
    try {
        require(cheats().size() == std::size(expected), "Original cheat coverage is incomplete");
        for (std::size_t i = 0; i < std::size(expected); ++i) {
            require(cheats()[i].code == expected[i].code &&
                        cheats()[i].trigger == expected[i].trigger &&
                        cheats()[i].actions == expected[i].actions &&
                        cheat_index(expected[i].code) == i,
                    "A cheat code or its original table index changed");
        }
        require(!cheat_index(L"") && !cheat_index(L"superagent ") && !cheat_index(L"unknown code"),
                "An unrecognized tool code resolved to a native table row");
        verify_profile(profile_cd_10012, 0x7c680, 0x2548d0);
        verify_profile(profile_cd_10019, 0x7bcc0, 0x256918);
        verify_profile(profile_cd_10020, 0x7be20, 0x257910);
        verify_profile(profile_dvd_20000, 0x7bd40, 0x25bdb8);
        std::cout
            << "All 26 original cheats and all four relocated native dispatch guards passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
