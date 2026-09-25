#include "preferences/hooks.h"
#include "media/files.h"

#include <cstdlib>

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        static wchar_t path[32768];
        const DWORD length =
            GetEnvironmentVariableW(L"XFILES_PATCH_PREFERENCES", path, _countof(path));
        if (length >= _countof(path)) {
            return FALSE;
        }
        if (length && !install_preferences_hooks(GetModuleHandleW(nullptr), path)) {
            return FALSE;
        }
        if (GetEnvironmentVariableW(L"XFILES_PATCH_MEDIA", nullptr, 0) &&
            !media::install_file_hooks(GetModuleHandleW(nullptr))) {
            remove_preferences_hooks();
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        remove_preferences_hooks();
        media::remove_file_hooks();
    }
    return TRUE;
}
