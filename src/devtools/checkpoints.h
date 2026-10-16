#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>

namespace devtools {
struct CheckpointActions {
    std::function<bool()> available;
    std::function<void(const std::filesystem::path&)> load;
};

namespace checkpoint_controls {
inline constexpr int executable = 5101, baseline = 5102, source = 5103, bundled = 5104,
                     extract = 5105, reconstruct = 5106, load = 5107, status = 5108;
}

bool checkpoint_can_load(const std::filesystem::path& source, const CheckpointActions& actions);
void show_checkpoints(HWND owner, HMODULE module, HFONT font,
                      const std::filesystem::path& initial_root, CheckpointActions actions = {},
                      const std::filesystem::path& initial_source = {});
}
