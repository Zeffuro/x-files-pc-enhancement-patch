#include "platform/long_timers.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Event {
public:
    Event() : handle_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        require(handle_ != nullptr, "Cannot create test event");
    }

    ~Event() {
        CloseHandle(handle_);
    }

    void signal() const noexcept {
        SetEvent(handle_);
    }

    bool wait(DWORD timeout = 5000) const noexcept {
        return WaitForSingleObject(handle_, timeout) == WAIT_OBJECT_0;
    }

private:
    HANDLE handle_;
};

struct Call {
    UINT delay, resolution;
    LPTIMECALLBACK callback;
    DWORD_PTR user;
    UINT flags;
};

struct Delivery {
    UINT id, message;
    DWORD_PTR user, first, second;
    DWORD thread;
};

class Fixture {
public:
    inline static Fixture* current = nullptr;
    TIMECAPS limits{1, 65535};
    MMRESULT caps_result = TIMERR_NOERROR;
    MMRESULT original_result = 0;
    MMRESULT backing_result = 41;
    MMRESULT kill_result = TIMERR_NOERROR;
    bool pin_result = true;
    std::atomic<DWORD> now{0};
    unsigned caps_calls = 0, pin_calls = 0;
    std::vector<Call> calls;
    std::vector<UINT> kills;
    std::vector<Delivery> deliveries;
    std::function<void(const Call&)> during_backing;
    std::function<void(UINT)> during_kill;
    std::function<void()> during_delivery;
    Event* dispatch_started = nullptr;
    platform::LongTimers adapter;

    Fixture() : adapter({set, kill, caps, clock, dispatch, pin}) {
        current = this;
    }

    ~Fixture() {
        current = nullptr;
    }

    static MMRESULT WINAPI set(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user,
                               UINT flags) {
        auto& fixture = *current;
        const Call call{delay, resolution, callback, user, flags};
        fixture.calls.push_back(call);
        if (callback != dispatch) {
            return fixture.original_result;
        }
        if (fixture.during_backing) {
            fixture.during_backing(call);
        }
        return fixture.backing_result;
    }

    static MMRESULT WINAPI kill(UINT id) {
        auto& fixture = *current;
        fixture.kills.push_back(id);
        if (fixture.during_kill) {
            fixture.during_kill(id);
        }
        return fixture.kill_result;
    }

    static MMRESULT WINAPI caps(LPTIMECAPS caps, UINT size) {
        auto& fixture = *current;
        ++fixture.caps_calls;
        if (size != sizeof(TIMECAPS)) {
            return TIMERR_NOCANDO;
        }
        *caps = fixture.limits;
        return fixture.caps_result;
    }

    static DWORD WINAPI clock() {
        return current->now.load();
    }

    static bool pin() {
        ++current->pin_calls;
        return current->pin_result;
    }

    static void CALLBACK dispatch(UINT id, UINT message, DWORD_PTR token, DWORD_PTR first,
                                  DWORD_PTR second) {
        if (current->dispatch_started) {
            current->dispatch_started->signal();
        }
        current->adapter.tick(id, message, token, first, second);
    }

    static void CALLBACK native(UINT id, UINT message, DWORD_PTR user, DWORD_PTR first,
                                DWORD_PTR second) {
        auto& fixture = *current;
        fixture.deliveries.push_back({id, message, user, first, second, GetCurrentThreadId()});
        if (fixture.during_delivery) {
            fixture.during_delivery();
        }
    }

    UINT arm(UINT delay = 120000, UINT resolution = 100, DWORD_PTR user = 0x12345678) {
        return adapter.set(delay, resolution, native, user, 0);
    }

    void fire(const Call& call, UINT id = 41) {
        call.callback(id, 0x22, call.user, 0x33, 0x44);
    }
};

void original_success_and_arguments() {
    Fixture fixture;
    fixture.original_result = 0x4321;
    require(fixture.arm() == 0x4321, "Accepted native long timer changed ID");
    const auto call = fixture.calls.at(0);
    require(fixture.calls.size() == 1 && call.delay == 120000 && call.resolution == 100 &&
                call.callback == Fixture::native && call.user == 0x12345678 && call.flags == 0,
            "Original SDK arguments were changed");
    require(fixture.caps_calls == 0 && fixture.pin_calls == 0, "Accepted timer entered fallback");
    fixture.kill_result = 17;
    require(fixture.adapter.kill(0x4321) == 17 && fixture.kills.back() == 0x4321,
            "Ordinary cancellation result changed");
}

void unsupported_and_invalid() {
    for (const UINT flags : {TIME_PERIODIC, TIME_CALLBACK_EVENT_SET, TIME_CALLBACK_EVENT_PULSE,
                             TIME_KILL_SYNCHRONOUS, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS}) {
        Fixture fixture;
        require(fixture.adapter.set(120000, 100, Fixture::native, 99, flags) == 0,
                "Unsupported flags gained fallback");
        require(fixture.calls.size() == 1 && fixture.calls[0].flags == flags &&
                    fixture.caps_calls == 0 && fixture.pin_calls == 0,
                "Unsupported call changed or queried fallback");
    }
    {
        Fixture fixture;
        require(fixture.adapter.set(120000, 100, nullptr, 99, 0) == 0 &&
                    fixture.calls.size() == 1 && fixture.caps_calls == 0,
                "Null callback gained fallback");
    }
    for (const UINT delay : {0u, 1u, 60000u, 65535u}) {
        Fixture fixture;
        require(fixture.arm(delay) == 0 && fixture.calls.size() == 1 && fixture.pin_calls == 0,
                "In-range allocation failure gained fallback");
    }
    for (const TIMECAPS limits : {TIMECAPS{0, 65535}, TIMECAPS{1, 0}, TIMECAPS{20, 10}}) {
        Fixture fixture;
        fixture.limits = limits;
        require(fixture.arm() == 0 && fixture.calls.size() == 1 && fixture.pin_calls == 0,
                "Invalid provider capability gained fallback");
    }
    Fixture fixture;
    fixture.caps_result = TIMERR_NOCANDO;
    require(fixture.arm() == 0 && fixture.calls.size() == 1 && fixture.pin_calls == 0,
            "Failed capability query gained fallback");
}

void allocation_failures_and_capacity() {
    {
        Fixture fixture;
        fixture.pin_result = false;
        require(fixture.arm() == 0 && fixture.calls.size() == 1, "Pin failure armed backing timer");
        fixture.pin_result = true;
        require(fixture.arm() == 41 && fixture.pin_calls == 2, "Pin failure leaked a record");
    }
    {
        Fixture fixture;
        fixture.backing_result = 0;
        for (unsigned index = 0; index != 20; ++index) {
            require(fixture.arm() == 0, "Backing failure returned a live ID");
        }
        fixture.backing_result = 41;
        require(fixture.arm() == 41 && fixture.pin_calls == 1 && fixture.kills.empty(),
                "Backing failure leaked capacity or repinned module");
    }
    Fixture fixture;
    std::vector<Call> backing;
    for (UINT index = 0; index != 16; ++index) {
        fixture.backing_result = 100 + index;
        require(fixture.arm() == 100 + index, "Available record was rejected");
        backing.push_back(fixture.calls.back());
    }
    const auto before = fixture.calls.size();
    require(fixture.arm() == 0 && fixture.calls.size() == before + 1,
            "Record exhaustion allocated another backing timer");
    require(fixture.adapter.kill(107) == TIMERR_NOERROR, "Capacity cancellation failed");
    fixture.backing_result = 207;
    require(fixture.arm() == 207 && fixture.calls.back().user != backing[7].user,
            "Freed record was unavailable or reused its token");
    fixture.now = 120000;
    for (UINT index = 0; index != 16; ++index) {
        fixture.fire(backing[index], 100 + index);
    }
    require(fixture.deliveries.size() == 15, "Capacity overflow overwrote a live record");
}

void backing_interval_and_arguments() {
    struct Example {
        TIMECAPS limits;
        UINT resolution, interval;
    };

    for (const auto example :
         {Example{{1, 65535}, 100, 100}, Example{{1, 65535}, 0, 1}, Example{{1, 65535}, 500, 100},
          Example{{20, 65535}, 3, 20}, Example{{1, 10}, 100, 10}, Example{{200, 200}, 0, 200}}) {
        Fixture fixture;
        fixture.limits = example.limits;
        require(fixture.arm(120000, example.resolution) == 41, "Fallback did not return real ID");
        const auto call = fixture.calls.back();
        require(fixture.calls.size() == 2 && call.delay == example.interval &&
                    call.resolution == example.resolution && call.callback == Fixture::dispatch &&
                    call.user != 0 && call.flags == TIME_PERIODIC,
                "Backing SDK arguments violate interval or callback contract");
    }
}

void deadline_and_delivery() {
    Fixture fixture;
    fixture.now = 1234;
    require(fixture.arm(120017) == 41, "Cannot arm uneven deadline");
    const auto call = fixture.calls.back();
    for (const DWORD elapsed : {0u, 31u, 999u, 1000u, 119999u, 120016u}) {
        fixture.now = 1234 + elapsed;
        fixture.fire(call);
        require(fixture.deliveries.empty() && fixture.kills.empty(), "Timer delivered early");
    }
    fixture.now = 1234 + 120017;
    DWORD provider_thread = 0;
    std::thread provider([&] {
        provider_thread = GetCurrentThreadId();
        fixture.fire(call);
    });
    provider.join();
    require(fixture.deliveries.size() == 1 && fixture.kills == std::vector<UINT>{41},
            "Deadline did not deliver and retire exactly once");
    const auto delivery = fixture.deliveries[0];
    require(delivery.id == 41 && delivery.message == 0x22 && delivery.user == 0x12345678 &&
                delivery.first == 0x33 && delivery.second == 0x44 &&
                delivery.thread == provider_thread && provider_thread != GetCurrentThreadId(),
            "Native callback arguments or provider thread changed");
    for (unsigned index = 0; index != 5; ++index) {
        fixture.now += 50000;
        fixture.fire(call);
    }
    require(fixture.deliveries.size() == 1 && fixture.kills.size() == 1,
            "Retired periodic callback delivered again");
}

void delayed_wrap_and_failed_kill() {
    for (const DWORD start : {DWORD{0}, std::numeric_limits<DWORD>::max() - 60000u}) {
        Fixture fixture;
        fixture.now = start;
        fixture.arm();
        const auto call = fixture.calls.back();
        fixture.now = start + 70000;
        fixture.fire(call);
        require(fixture.deliveries.empty(), "Wrap or delayed tick delivered early");
        fixture.now = start + 190000;
        fixture.kill_result = TIMERR_NOCANDO;
        fixture.fire(call);
        fixture.fire(call);
        require(fixture.deliveries.size() == 1 && fixture.kills.size() == 1,
                "Delayed delivery or failed backing kill broke one-shot behavior");
    }
    Fixture fixture;
    fixture.arm(std::numeric_limits<UINT>::max());
    const auto call = fixture.calls.back();
    for (const DWORD now : {DWORD{0x80000000}, std::numeric_limits<DWORD>::max() - 1}) {
        fixture.now = now;
        fixture.fire(call);
        require(fixture.deliveries.empty(), "Large duration used a signed deadline");
    }
    fixture.now = std::numeric_limits<DWORD>::max();
    fixture.fire(call);
    require(fixture.deliveries.size() == 1, "Maximum duration never completed");
}

void stale_callbacks_and_id_reuse() {
    for (const bool complete : {false, true}) {
        Fixture fixture;
        fixture.arm();
        const auto old = fixture.calls.back();
        fixture.now = complete ? 120000 : 50000;
        if (complete) {
            fixture.fire(old);
        } else {
            fixture.adapter.kill(41);
        }
        const auto delivered = fixture.deliveries.size();
        require(fixture.arm() == 41, "Real provider ID could not be reused");
        const auto fresh = fixture.calls.back();
        require(fresh.user != old.user, "Record reuse recycled callback token");
        fixture.now = 240000;
        fixture.fire(old);
        fixture.fire(fresh, 42);
        fixture.adapter.tick(41, 0, 0, 0, 0);
        require(fixture.deliveries.size() == delivered,
                "Stale or mismatched callback reached user");
        fixture.fire(fresh);
        require(fixture.deliveries.size() == delivered + 1, "Fresh ID reuse lost its callback");
    }
    DWORD_PTR first_token = 0;
    {
        Fixture fixture;
        fixture.arm();
        first_token = fixture.calls.back().user;
        fixture.adapter.kill(41);
    }
    Fixture fixture;
    fixture.arm();
    require(fixture.calls.back().user != first_token, "Adapter replacement recycled process token");
}

void failed_cancel_still_suppresses_delivery() {
    Fixture fixture;
    fixture.arm();
    const auto call = fixture.calls.back();
    fixture.kill_result = TIMERR_NOCANDO;
    require(fixture.adapter.kill(41) == TIMERR_NOCANDO, "Cancellation provider result changed");
    fixture.now = 200000;
    fixture.fire(call);
    require(fixture.deliveries.empty(),
            "Failed provider cancellation left native callback reachable");
}

void callback_before_set_returns() {
    Fixture fixture;
    Event started, returned;
    std::thread provider;
    bool early_return = false;
    fixture.dispatch_started = &started;
    fixture.during_backing = [&](const Call& call) {
        fixture.now = 120000;
        provider = std::thread([&, call] {
            fixture.fire(call);
            returned.signal();
        });
        started.wait();
        early_return = returned.wait(100);
    };
    const auto id = fixture.arm();
    provider.join();
    require(!early_return && id == 41 && fixture.deliveries.size() == 1 &&
                fixture.deliveries[0].id == 41,
            "Callback observed unpublished backing ID");
}

void reentrant_callback() {
    Fixture fixture;
    fixture.arm();
    const auto old = fixture.calls.back();
    UINT rearmed = 0;
    MMRESULT self_kill = TIMERR_NOCANDO;
    fixture.during_delivery = [&] {
        self_kill = fixture.adapter.kill(41);
        rearmed = fixture.arm();
    };
    fixture.now = 120000;
    fixture.fire(old);
    fixture.during_delivery = {};
    require(self_kill == TIMERR_NOERROR && rearmed == 41, "Native callback cannot kill and rearm");
    const auto fresh = fixture.calls.back();
    fixture.now = 240000;
    fixture.fire(old);
    require(fixture.deliveries.size() == 1, "Reentrant slot reuse accepted stale callback");
    fixture.fire(fresh);
    require(fixture.deliveries.size() == 2, "Reentrant slot reuse lost new callback");
}

void in_flight_callback_blocks_kill() {
    Fixture fixture;
    Event entered, release, kill_started, kill_returned;
    std::atomic<bool> active{false}, unsafe_return{false};
    bool callback_wait_ok = false;
    fixture.during_delivery = [&] {
        active = true;
        entered.signal();
        callback_wait_ok = release.wait();
        active = false;
    };
    fixture.arm();
    const auto call = fixture.calls.back();
    fixture.now = 120000;
    std::thread provider([&] { fixture.fire(call); });
    const bool entered_ok = entered.wait();
    std::thread killer([&] {
        kill_started.signal();
        fixture.adapter.kill(41);
        unsafe_return = active.load();
        kill_returned.signal();
    });
    const bool started_ok = kill_started.wait();
    const bool premature_return = kill_returned.wait(100);
    release.signal();
    provider.join();
    killer.join();
    require(entered_ok && started_ok && callback_wait_ok && !premature_return && !unsafe_return &&
                kill_returned.wait(0),
            "Kill returned while native callback could still use its object");
}

void ordinary_kill_releases_adapter_lock() {
    Fixture fixture;
    Event inside_provider, release, armed;
    bool provider_wait_ok = false;
    fixture.during_kill = [&](UINT id) {
        if (id == 900) {
            inside_provider.signal();
            provider_wait_ok = release.wait();
        }
    };
    std::thread killer([&] { fixture.adapter.kill(900); });
    const bool entered = inside_provider.wait();
    UINT result = 0;
    std::thread setter([&] {
        result = fixture.arm();
        armed.signal();
    });
    const bool progressed = armed.wait(1000);
    release.signal();
    killer.join();
    setter.join();
    require(entered && progressed && provider_wait_ok && result == 41,
            "Ordinary provider kill held adapter lock");
}

}

int main() {
    try {
        original_success_and_arguments();
        unsupported_and_invalid();
        allocation_failures_and_capacity();
        backing_interval_and_arguments();
        deadline_and_delivery();
        delayed_wrap_and_failed_kill();
        stale_callbacks_and_id_reuse();
        failed_cancel_still_suppresses_delivery();
        callback_before_set_returns();
        reentrant_callback();
        in_flight_callback_blocks_kill();
        ordinary_kill_releases_adapter_lock();
        std::cout << "Long timer adapter tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
