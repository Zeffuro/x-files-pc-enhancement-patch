#pragma once

#include <filesystem>
#include <functional>
#include <vector>
#include <cstdint>
#include <string>
#include <optional>
#include <memory>

struct MediaFile {
    std::filesystem::path source, relative;
    std::uintmax_t size;
    std::string checksum;
    std::optional<std::uint64_t> offset;
};

struct MediaSource {
    std::filesystem::path root, game;
    std::vector<MediaFile> files;
    std::uintmax_t bytes = 0;
    std::shared_ptr<void> workspace;
};

MediaSource inspect_media(const std::filesystem::path& selected);
void validate_destination(const MediaSource& source, const std::filesystem::path& destination);
void install_media(const MediaSource& source, const std::filesystem::path& destination,
                   bool windowed, const std::function<bool(unsigned)>& progress);
