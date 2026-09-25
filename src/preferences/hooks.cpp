#include "hooks.h"
#include "platform/imports.h"

#include <cwchar>
#include <cstdlib>

namespace {
ImportHooks hooks;
}

bool install_preferences_hooks(HMODULE executable, const wchar_t* path) {
    if (!path || !*path || wcslen(path) >= _countof(preferences::settings_path)) {
        return false;
    }
    wcscpy_s(preferences::settings_path, path);
    return hooks.install(executable, "advapi32.dll", preferences::registry_hook);
}

void remove_preferences_hooks() {
    hooks.remove();
}
