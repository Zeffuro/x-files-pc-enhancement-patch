#pragma once
#include <filesystem>

namespace devtools {
struct BrowserSource {
    std::filesystem::path root, database, asset;
};

BrowserSource browser_source(const std::filesystem::path& requested);
}
