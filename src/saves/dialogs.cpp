#include "dialogs.h"
#include "paths.h"
#include "platform/imports.h"

#include <commdlg.h>
#include <vector>
#include <cstring>
#include <string>

namespace saves {
namespace {
ImportHooks hooks;

BOOL choose(OPENFILENAMEA* dialog, bool writing) {
    if (!dialog) {
        return writing ? GetSaveFileNameA(dialog) : GetOpenFileNameA(dialog);
    }
    try {
        std::vector<char> directory(32768);
        const auto length = GetEnvironmentVariableA("XFILES_PATCH_SAVES", directory.data(),
                                                    static_cast<DWORD>(directory.size()));
        if (!length || length >= directory.size()) {
            return writing ? GetSaveFileNameA(dialog) : GetOpenFileNameA(dialog);
        }
        const auto previous = dialog->lpstrInitialDir;
        const auto previous_flags = dialog->Flags;
        const auto mapped = redirected_path(dialog->lpstrFile);
        if (!mapped.empty()) {
            const auto filename = mapped.string();
            if (filename.size() < dialog->nMaxFile) {
                std::memcpy(dialog->lpstrFile, filename.c_str(), filename.size() + 1);
            }
        }
        dialog->lpstrInitialDir = directory.data();
        dialog->Flags |= OFN_NOCHANGEDIR;
        const auto result = writing ? GetSaveFileNameA(dialog) : GetOpenFileNameA(dialog);
        dialog->lpstrInitialDir = previous;
        dialog->Flags = (dialog->Flags & ~OFN_NOCHANGEDIR) | (previous_flags & OFN_NOCHANGEDIR);
        return result;
    } catch (...) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
}

BOOL WINAPI open_dialog(OPENFILENAMEA* dialog) {
    return choose(dialog, false);
}

BOOL WINAPI save_dialog(OPENFILENAMEA* dialog) {
    return choose(dialog, true);
}

FARPROC resolve(const char* name) {
    if (!std::strcmp(name, "GetOpenFileNameA")) {
        return reinterpret_cast<FARPROC>(open_dialog);
    }
    if (!std::strcmp(name, "GetSaveFileNameA")) {
        return reinterpret_cast<FARPROC>(save_dialog);
    }
    return nullptr;
}
}

bool install_dialog_hooks(HMODULE executable) {
    return hooks.install(executable, "comdlg32.dll", resolve);
}

void remove_dialog_hooks() {
    hooks.remove();
}
}
