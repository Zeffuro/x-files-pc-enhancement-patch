#pragma once

#include <filesystem>
#include <string>
#include "game/profiles/generated.h"

struct Identity {
    std::string sha256;
    const char* edition;
    const native_game::Build* build;
};

Identity identify(const std::filesystem::path& executable);
std::string sha256(const std::filesystem::path& path);
std::string sha256(const std::filesystem::path& path, std::uintmax_t offset, std::uintmax_t size);
