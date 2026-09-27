#include "game_context.h"
#include "log_file.h"
#include "devtools/variables.h"
#include "enhancements/game_ui.h"
#include <windows.h>
#include <sstream>
#include <vector>

namespace diagnostics {
namespace {
std::filesystem::path context_path() {
    std::vector<wchar_t> name(32768);
    const auto length = GetModuleFileNameW(nullptr, name.data(), 32768);
    if (!length || length == 32768) {
        return {};
    }
    return log_directory(std::filesystem::path(name.data()).parent_path()) / L"game-context.log";
}

void record(std::wstring_view message) {
    const auto length = WideCharToMultiByte(
        CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
    if (!length) {
        return;
    }
    std::string utf8(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), utf8.data(),
                        length, nullptr, nullptr);
    const auto path = context_path();
    if (!path.empty()) {
        append_log(path, "pid=" + std::to_string(GetCurrentProcessId()) +
                             " tick=" + std::to_string(GetTickCount64()) + " " + utf8 + "\n");
    }
}
}

void record_tool_context(std::wstring_view text) noexcept {
    try {
        static thread_local std::wstring previous;
        if (text != previous) {
            record(text);
            previous = text;
        }
    } catch (...) {
    }
}

void record_game_context() noexcept {
    try {
        static thread_local ULONGLONG sampled = 0;
        static thread_local std::wstring previous;
        const auto now = GetTickCount64();
        if (now - sampled < 500) {
            return;
        }
        sampled = now;
        namespace game = enhancements::game;
        const auto image = game::executable_image();
        if (!image) {
            return;
        }
        const auto& profile = game::edition();
        std::wostringstream message;
        for (const auto& [name, offset] : {std::pair{L"session", profile.session_active},
                                           std::pair{L"scene", profile.scene_active}}) {
            int value = 0;
            SIZE_T count = 0;
            if (ReadProcessMemory(GetCurrentProcess(), image + offset, &value, sizeof(value),
                                  &count) &&
                count == sizeof(value)) {
                message << name << L"=" << value << L" ";
            }
        }
        message << L"\n" << devtools::inspect_variables(image, profile);
        const auto text = message.str();
        if (text != previous) {
            record(text);
            previous = text;
        }
    } catch (...) {
    }
}
}
