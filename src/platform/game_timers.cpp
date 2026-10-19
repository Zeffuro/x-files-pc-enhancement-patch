#include "game_timers.h"
#include "long_timers.h"
#include "imports.h"
#include "identity.h"
#include "runtime.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <memory>
#include <vector>

namespace platform {
namespace {

ImportHooks hooks;
std::once_flag installed;
std::unique_ptr<LongTimers> storage;
std::atomic<LongTimers*> timers{nullptr};

void CALLBACK dispatch(UINT id, UINT message, DWORD_PTR token, DWORD_PTR first, DWORD_PTR second) {
    if (const auto instance = timers.load(std::memory_order_acquire)) {
        instance->tick(id, message, token, first, second);
    }
}

bool pin() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&dispatch), &module) != FALSE;
}

MMRESULT WINAPI set(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user,
                    UINT flags) {
    const auto instance = timers.load(std::memory_order_acquire);
    return instance ? instance->set(delay, resolution, callback, user, flags)
                    : timeSetEvent(delay, resolution, callback, user, flags);
}

MMRESULT WINAPI kill(UINT id) {
    const auto instance = timers.load(std::memory_order_acquire);
    return instance ? instance->kill(id) : timeKillEvent(id);
}

FARPROC resolve(const char* name) {
    if (std::strcmp(name, "timeSetEvent") == 0) {
        return reinterpret_cast<FARPROC>(&set);
    }
    if (std::strcmp(name, "timeKillEvent") == 0) {
        return reinterpret_cast<FARPROC>(&kill);
    }
    return nullptr;
}

void trace_hooks(unsigned value) noexcept {
    try {
        trace_value("long_timer_hooks", value);
    } catch (...) {
    }
}

}

void install_game_timers() {
    std::call_once(installed, [] {
        try {
            std::vector<wchar_t> path(32768);
            const auto length =
                GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length == path.size()) {
                return;
            }
            const auto identity = identify(path.data());
            if (!identity.build || !identity.build->profile ||
                !hooks.install(GetModuleHandleW(nullptr), "WINMM.dll", resolve)) {
                return;
            }
            const auto previous_set = reinterpret_cast<decltype(&timeSetEvent)>(
                hooks.previous(reinterpret_cast<FARPROC>(&set)));
            const auto previous_kill = reinterpret_cast<decltype(&timeKillEvent)>(
                hooks.previous(reinterpret_cast<FARPROC>(&kill)));
            if (!previous_set || !previous_kill) {
                hooks.remove();
                return;
            }
            storage = std::make_unique<LongTimers>(LongTimers::Functions{
                previous_set, previous_kill, &timeGetDevCaps, &timeGetTime, &dispatch, &pin});
            timers.store(storage.get(), std::memory_order_release);
            trace_hooks(1);
        } catch (...) {
            hooks.remove();
            trace_hooks(0);
        }
    });
}

void remove_game_timers(bool process_exit) {
    timers.store(nullptr, std::memory_order_release);
    if (process_exit) {
        // ExitProcess may terminate a callback while it owns the critical section.
        storage.release();
    } else {
        hooks.remove();
    }
}

}
