#include "long_timers.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace platform {
namespace {

class Guard {
public:
    explicit Guard(CRITICAL_SECTION& section) noexcept : section_(section) {
        EnterCriticalSection(&section_);
    }

    ~Guard() {
        LeaveCriticalSection(&section_);
    }

private:
    CRITICAL_SECTION& section_;
};

std::atomic<DWORD_PTR> last_token{0};

DWORD_PTR next_token() noexcept {
    auto previous = last_token.load(std::memory_order_relaxed);
    while (previous != std::numeric_limits<DWORD_PTR>::max()) {
        if (last_token.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) {
            return previous + 1;
        }
    }
    return 0;
}

}

LongTimers::LongTimers(Functions functions) noexcept : functions_(functions) {
    ready_ = InitializeCriticalSectionEx(&section_, 0, 0) != FALSE;
}

LongTimers::~LongTimers() {
    if (ready_) {
        DeleteCriticalSection(&section_);
    }
}

MMRESULT LongTimers::set(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user,
                         UINT flags) noexcept {
    const auto original = functions_.set(delay, resolution, callback, user, flags);
    if (original || !ready_ || flags != 0 || !callback) {
        return original;
    }
    TIMECAPS caps{};
    if (functions_.caps(&caps, sizeof(caps)) != TIMERR_NOERROR || !caps.wPeriodMin ||
        caps.wPeriodMin > caps.wPeriodMax || delay <= caps.wPeriodMax) {
        return original;
    }

    const Guard guard(section_);
    const auto available = std::find_if(records_.begin(), records_.end(),
                                        [](const auto& record) { return !record.token; });
    if (available == records_.end()) {
        return 0;
    }
    if (!pinned_) {
        pinned_ = functions_.pin();
        if (!pinned_) {
            return 0;
        }
    }
    const auto token = next_token();
    if (!token) {
        return 0;
    }
    const auto interval =
        std::clamp(resolution ? std::min(resolution, 100u) : 1u, caps.wPeriodMin, caps.wPeriodMax);
    *available = {token, 0, callback, user, functions_.clock(), delay};
    const auto id = functions_.set(interval, resolution, functions_.dispatch, token, TIME_PERIODIC);
    if (!id) {
        *available = {};
        return 0;
    }
    available->id = id;
    return id;
}

MMRESULT LongTimers::kill(UINT id) noexcept {
    if (ready_) {
        const Guard guard(section_);
        for (auto& record : records_) {
            if (record.token && record.id == id) {
                record = {};
                return functions_.kill(id);
            }
        }
    }
    return functions_.kill(id);
}

void LongTimers::tick(UINT id, UINT message, DWORD_PTR token, DWORD_PTR first,
                      DWORD_PTR second) noexcept {
    if (!ready_ || !token) {
        return;
    }
    const Guard guard(section_);
    for (auto& record : records_) {
        if (record.token != token || record.id != id) {
            continue;
        }
        const auto now = functions_.clock();
        const DWORD elapsed = now - record.sampled;
        record.sampled = now;
        if (elapsed < record.remaining) {
            record.remaining -= elapsed;
            return;
        }
        const auto callback = record.callback;
        const auto user = record.user;
        record = {};
        functions_.kill(id);
        // Cancellation must wait until the native callback has finished using its object.
        callback(id, message, user, first, second);
        return;
    }
}

}
