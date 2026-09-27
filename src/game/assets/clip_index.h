#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace game_assets {

struct ClipLabel {
    std::wstring location;
    std::wstring scene;
    std::wstring kind;
    std::wstring category;
    std::optional<unsigned> node;
    std::uint64_t offset = 0;
};

struct ClipEntry {
    std::wstring movie;
    std::vector<ClipLabel> labels;
};

class ClipIndex {
public:
    static std::optional<ClipIndex> load(const std::filesystem::path& hdb);

    std::vector<ClipLabel> labels(const std::filesystem::path& movie) const;

    const std::vector<ClipEntry>& entries() const {
        return entries_;
    }

private:
    std::vector<ClipEntry> entries_;
    std::map<std::pair<std::string, unsigned>, std::size_t> positions_;
};

}
