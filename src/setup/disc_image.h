#pragma once

#include "media.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

std::vector<MediaFile> read_disc_image(const std::filesystem::path& path);
std::string media_file_sha256(const MediaFile& file);
void copy_media_file(const MediaFile& file, const std::filesystem::path& output,
                     const std::function<bool()>& keep_going);
