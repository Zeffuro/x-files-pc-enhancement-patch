#pragma once

#include <filesystem>
#include <string>

struct Identity {
    std::string sha256;
    const char* edition;
};

Identity identify(const std::filesystem::path& executable);
std::string sha256(const std::filesystem::path& path);
std::string sha256(const std::filesystem::path& path, std::uintmax_t offset, std::uintmax_t size);
