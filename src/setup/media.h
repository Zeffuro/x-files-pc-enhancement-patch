#pragma once

#include <filesystem>
#include <functional>
#include <vector>
#include <cstdint>
#include <string>
#include <optional>
#include <memory>

enum class MediaSetId {
    unknown,
    cd_en,
    dvd_en,
    cd_de,
    cd_fr,
    cd_es,
    cd_it,
    cd_jp,
    ru_fan,
    cd_en_us,
    dvd_fr
};

struct ImageGeometry {
    std::uint32_t sector_size = 2048;
    std::uint32_t payload_offset = 0;
};

struct MediaFile {
    std::filesystem::path source, relative;
    std::uintmax_t size;
    std::string checksum;
    std::optional<std::uint64_t> offset;
    ImageGeometry geometry;
};

struct MediaSource {
    std::filesystem::path root, game;
    std::vector<MediaFile> files;
    std::uintmax_t bytes = 0;
    std::shared_ptr<void> workspace;
    MediaSetId set = MediaSetId::unknown;
    std::vector<std::filesystem::path> additional_roots;
};

struct MediaImportOptions {
    bool french_dvd = false;
    std::filesystem::path french_cd;
};

MediaSource inspect_media(const std::filesystem::path& selected,
                          const MediaImportOptions& options = {});
std::vector<MediaFile> read_media_folder(const std::filesystem::path& root);
void validate_destination(const MediaSource& source, const std::filesystem::path& destination);
void install_media(const MediaSource& source, const std::filesystem::path& destination,
                   bool windowed, const std::function<bool(unsigned)>& progress);
