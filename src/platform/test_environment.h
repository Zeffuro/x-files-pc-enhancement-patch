#pragma once
#include <windows.h>

namespace platform {
inline bool test_audio_muted() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"XFILES_TEST_MUTE_AUDIO", value, 2) == 1 && value[0] == L'1';
}
}
