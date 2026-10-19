#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <array>

namespace platform {

class LongTimers {
public:
    struct Functions {
        decltype(&timeSetEvent) set;
        decltype(&timeKillEvent) kill;
        decltype(&timeGetDevCaps) caps;
        decltype(&timeGetTime) clock;
        LPTIMECALLBACK dispatch;
        bool (*pin)();
    };

    explicit LongTimers(Functions functions) noexcept;
    // The owner must keep this adapter alive until backing callbacks can no longer enter.
    ~LongTimers();
    LongTimers(const LongTimers&) = delete;
    LongTimers& operator=(const LongTimers&) = delete;

    MMRESULT set(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user,
                 UINT flags) noexcept;
    MMRESULT kill(UINT id) noexcept;
    void tick(UINT id, UINT message, DWORD_PTR token, DWORD_PTR first, DWORD_PTR second) noexcept;

private:
    struct Record {
        DWORD_PTR token = 0;
        UINT id = 0;
        LPTIMECALLBACK callback = nullptr;
        DWORD_PTR user = 0;
        DWORD sampled = 0;
        DWORD remaining = 0;
    };

    Functions functions_;
    CRITICAL_SECTION section_{};
    bool ready_ = false;
    bool pinned_ = false;
    std::array<Record, 16> records_{};
};

}
