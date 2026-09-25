#pragma once

#include <filesystem>

struct Session {
    std::filesystem::path media;
    bool portable = true;
};

void save_session(const std::filesystem::path& directory, const Session& session);
Session read_session(const std::filesystem::path& directory);
