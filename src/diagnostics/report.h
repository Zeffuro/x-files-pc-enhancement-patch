#pragma once

#include <filesystem>
#include <string_view>

namespace diagnostics {
void create_report(const std::filesystem::path& directory, const std::filesystem::path& output,
                   std::string_view details, const std::filesystem::path& save = {});
}
