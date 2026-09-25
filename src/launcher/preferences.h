#pragma once

#include <filesystem>

void stage_preferences(const std::filesystem::path& source,
                       const std::filesystem::path& destination, bool import_existing = true);
