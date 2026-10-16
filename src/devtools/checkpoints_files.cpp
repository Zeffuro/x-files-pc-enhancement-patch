#include "checkpoints_files.h"
#include "recipes/recipe.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace devtools {
std::filesystem::path checkpoint_module_file(HMODULE module) {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) {
        throw std::runtime_error("Cannot locate the game files");
    }
    path.resize(size);
    return path;
}

std::filesystem::path checkpoint_recipe_folder(HMODULE module) {
    for (const auto instance : {module, static_cast<HMODULE>(nullptr)}) {
        auto root = checkpoint_module_file(instance).parent_path();
        for (int depth = 0; depth < 3; ++depth) {
            const auto folder = root / L"tools" / L"save-recipes";
            if (std::filesystem::is_directory(folder)) {
                return folder;
            }
            root = root.parent_path();
        }
    }
    return {};
}

CheckpointFiles find_checkpoint_files(HMODULE module, const std::filesystem::path& root) {
    const auto inspect = [](const std::filesystem::path& exe) -> CheckpointFiles {
        try {
            const auto gam = exe.parent_path() / L"XFILES.GAM";
            if (std::filesystem::is_regular_file(gam)) {
                return {exe, gam, recipes::identify_executable(exe)};
            }
        } catch (const std::exception&) {
        }
        return {};
    };
    if (auto active = inspect(checkpoint_module_file(nullptr)); !active.executable.empty()) {
        return active;
    }
    std::vector<std::filesystem::path> folders;
    for (auto folder : {root, checkpoint_module_file(module).parent_path()}) {
        for (int depth = 0; depth < 3 && !folder.empty(); ++depth) {
            if (std::find(folders.begin(), folders.end(), folder) == folders.end()) {
                folders.push_back(folder);
            }
            folder = folder.parent_path();
        }
    }
    for (const auto& folder : folders) {
        if (auto standard = inspect(folder / L"XFiles.exe"); !standard.executable.empty()) {
            return standard;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(folder / L"XFILES.GAM", error)) {
            continue;
        }
        std::vector<std::filesystem::path> candidates;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            if (entry.is_regular_file(error) &&
                _wcsicmp(entry.path().extension().c_str(), L".exe") == 0 &&
                candidates.size() < 64) {
                candidates.push_back(entry.path());
            }
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& candidate : candidates) {
            if (auto found = inspect(candidate); !found.executable.empty()) {
                return found;
            }
        }
    }
    return {};
}
}
