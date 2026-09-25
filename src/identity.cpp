#include "identity.h"

#include <windows.h>
#include <array>
#include <algorithm>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <stdexcept>

std::string sha256(const std::filesystem::path& path) {
    return sha256(path, 0, std::filesystem::file_size(path));
}

std::string sha256(const std::filesystem::path& path, std::uintmax_t offset, std::uintmax_t size) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot read file for SHA-256.");
    }
    const auto length = std::filesystem::file_size(path);
    if (offset > length || size > length - offset) {
        throw std::runtime_error("SHA-256 extent is outside the file.");
    }
    stream.seekg(static_cast<std::streamoff>(offset));
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        throw std::runtime_error("Cannot initialize SHA-256.");
    }

    struct Guard {
        BCRYPT_HASH_HANDLE value;

        ~Guard() {
            BCryptDestroyHash(value);
        }
    } guard{hash};

    std::vector<unsigned char> buffer(65536);
    while (size) {
        const auto count =
            static_cast<std::streamsize>(std::min<std::uintmax_t>(size, buffer.size()));
        if (!stream.read(reinterpret_cast<char*>(buffer.data()), count)) {
            throw std::runtime_error("Reading file for SHA-256 failed.");
        }
        if (BCryptHashData(hash, buffer.data(), static_cast<ULONG>(stream.gcount()), 0) < 0) {
            throw std::runtime_error("SHA-256 update failed.");
        }
        size -= count;
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        throw std::runtime_error("SHA-256 finish failed.");
    }
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

Identity identify(const std::filesystem::path& executable) {
    auto result = sha256(executable);
    const char* edition = nullptr;
    if (result == "eae65c27f0c026530e363ebc3483a150c002e831e88ae6e04d3c2f3d4da859ce") {
        edition = "CD";
    }
    if (result == "1c7385b15bc11f6ff46b5a30ca383441df96be5be1c1dec31eae650f2243a2ad") {
        edition = "DVD";
    }
    return {result, edition};
}
