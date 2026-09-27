#pragma once

#include "catalog.h"
#include "disc_image.h"

std::vector<MediaFile> read_iso(const std::filesystem::path& path);
std::vector<MediaFile> select_iso_files(const std::vector<MediaFile>& files,
                                        const std::map<std::wstring, MediaRecord>& catalog);
MediaSource inspect_dvd_iso(const std::filesystem::path& image);
MediaSource inspect_iso_folder(const std::filesystem::path& folder);
