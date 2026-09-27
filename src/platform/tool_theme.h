#pragma once
#include <windows.h>

namespace platform {
class ToolTheme {
public:
    explicit ToolTheme(HMODULE module) {
        ACTCTXW context{sizeof(ACTCTXW)};
        context.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
        context.hModule = module;
        context.lpResourceName = MAKEINTRESOURCEW(3);
        handle_ = CreateActCtxW(&context);
        if (handle_ != INVALID_HANDLE_VALUE) {
            ActivateActCtx(handle_, &cookie_);
        }
    }

    ~ToolTheme() {
        if (cookie_) {
            DeactivateActCtx(0, cookie_);
        }
        if (handle_ != INVALID_HANDLE_VALUE) {
            ReleaseActCtx(handle_);
        }
    }

    ToolTheme(const ToolTheme&) = delete;
    ToolTheme& operator=(const ToolTheme&) = delete;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    ULONG_PTR cookie_ = 0;
};
}
