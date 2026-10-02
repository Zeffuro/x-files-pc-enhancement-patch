#pragma once

#include "identity.h"

#include <filesystem>

enum class DisplayMode { Borderless, Windowed };

struct StagedGame {
    std::filesystem::path directory;
    Identity identity;
};

void stage_patch_files(const std::filesystem::path& package,
                       const std::filesystem::path& destination);

StagedGame stage_game(const std::filesystem::path& source, const std::filesystem::path& destination,
                      const std::filesystem::path& media, DisplayMode mode,
                      bool preserve_settings = true);
