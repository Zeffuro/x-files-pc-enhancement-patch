#pragma once

#include <array>
#include <filesystem>
#include <fstream>

namespace saves {
inline bool supported_header(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 24> header{};
    return input.read(reinterpret_cast<char*>(header.data()), header.size()) && header[0] == 0 &&
           header[1] == 0 && header[2] == 0 && header[3] == 5 && header[20] == 0 &&
           header[21] == 0 && header[22] == 5 && header[23] == 1;
}
}
