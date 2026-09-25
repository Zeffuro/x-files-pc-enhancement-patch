#pragma once

#include <windows.h>

class ImportHooks {
public:
    using Resolver = FARPROC (*)(const char* name);

    bool install(HMODULE executable, const char* library, Resolver resolve);
    FARPROC previous(FARPROC hook) const;
    bool active() const;
    void remove();

private:
    struct Import {
        ULONG_PTR* slot;
        ULONG_PTR original;
        ULONG_PTR replacement;
    };

    Import imports_[16]{};
    unsigned count_ = 0;
};
