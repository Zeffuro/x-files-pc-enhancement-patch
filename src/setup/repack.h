#pragma once

#include "media.h"

void expand_russian_repack(std::vector<MediaFile>& files, const std::filesystem::path& workspace);
std::vector<MediaFile> russian_repack_folder(const std::filesystem::path& folder);
