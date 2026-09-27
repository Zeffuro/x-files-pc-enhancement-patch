#include "game/assets/clip_index.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

namespace game_assets {
namespace {

constexpr std::uintmax_t max_hdb_size = 128u * 1024u * 1024u;
constexpr std::size_t max_labels = 30000;
constexpr char field_separator = '\x7f';

struct Asset {
    std::wstring movie;
    std::string directory;
    unsigned id = 0;
};

struct Match {
    Asset asset;
    ClipLabel label;
    std::size_t end = 0;
};

bool decimal(std::string_view value, unsigned& result) {
    if (value.empty()) {
        return false;
    }
    unsigned number = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9' ||
            number > (std::numeric_limits<unsigned>::max() - unsigned(digit - '0')) / 10) {
            return false;
        }
        number = number * 10 + unsigned(digit - '0');
    }
    result = number;
    return true;
}

bool printable(std::string_view value, bool allow_separator) {
    for (const unsigned char ch : value) {
        if (ch < 0x20 || (ch > 0x7e && !(allow_separator && ch == 0x7f))) {
            return false;
        }
    }
    return true;
}

std::wstring label_text(std::string_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(ch == 0x7f ? L'/' : wchar_t(ch));
    }
    const auto first = result.find_first_not_of(L' ');
    if (first == std::wstring::npos) {
        return {};
    }
    const auto last = result.find_last_not_of(L' ');
    return result.substr(first, last - first + 1);
}

std::optional<std::string_view> field(std::string_view raw, std::size_t& cursor,
                                      std::size_t max_length, bool first_alpha) {
    if (cursor >= raw.size()) {
        return std::nullopt;
    }
    const auto end = raw.find(field_separator, cursor);
    if (end == std::string_view::npos || end <= cursor || end - cursor > max_length) {
        return std::nullopt;
    }
    const auto value = raw.substr(cursor, end - cursor);
    if (!printable(value, false) ||
        (first_alpha && !((value.front() >= 'A' && value.front() <= 'Z') ||
                          (value.front() >= 'a' && value.front() <= 'z')))) {
        return std::nullopt;
    }
    cursor = end + 1;
    return value;
}

std::optional<Asset> asset_after_movie(std::string_view raw, std::size_t movie_end,
                                       std::size_t& end) {
    // The short binary bundle between the paths is opaque, but contains no letters.
    for (std::size_t gap = 1; gap <= 12 && movie_end + gap + 7 <= raw.size(); ++gap) {
        const unsigned char ch = static_cast<unsigned char>(raw[movie_end + gap - 1]);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            break;
        }
        const auto start = movie_end + gap;
        if (raw.substr(start, 3) != "XN\x7f" && raw.substr(start, 3) != "XV\x7f") {
            continue;
        }
        const auto digits_start = start + 3;
        auto cursor = digits_start;
        while (cursor < raw.size() && cursor - digits_start < 10 && raw[cursor] >= '0' &&
               raw[cursor] <= '9') {
            ++cursor;
        }
        if (raw.substr(cursor, 4) != ".xmv") {
            return std::nullopt;
        }
        unsigned id = 0;
        if (!decimal(raw.substr(digits_start, cursor - digits_start), id)) {
            return std::nullopt;
        }
        Asset asset;
        asset.directory = std::string(raw.substr(start, 2));
        asset.movie = std::wstring(asset.directory.begin(), asset.directory.end()) + L"/";
        for (const char digit : raw.substr(digits_start, cursor - digits_start)) {
            asset.movie.push_back(wchar_t(digit));
        }
        asset.movie += L".xmv";
        asset.id = id;
        end = cursor + 4;
        return asset;
    }
    return std::nullopt;
}

std::optional<Match> parse(std::string_view raw, std::size_t start) {
    std::size_t cursor = start;
    ClipLabel label;
    if (raw.substr(start, 6) == "Video\x7f") {
        cursor += 6;
        const auto type = field(raw, cursor, 24, true);
        if (!type) {
            return std::nullopt;
        }
        if (*type == "Game") {
            label.kind = L"game";
            label.location = L"Game";
        } else if (type->substr(0, 5) == "Node ") {
            unsigned node = 0;
            if (!decimal(type->substr(5), node)) {
                return std::nullopt;
            }
            const auto location = field(raw, cursor, 41, true);
            if (!location) {
                return std::nullopt;
            }
            label.kind = L"scene";
            label.node = node;
            label.location = label_text(*location);
        } else {
            return std::nullopt;
        }
    } else if (raw.substr(start, 5) == "Navs\x7f") {
        cursor += 5;
        const auto category = field(raw, cursor, 31, true);
        const auto location = category ? field(raw, cursor, 41, true) : std::nullopt;
        if (!location) {
            return std::nullopt;
        }
        label.kind = L"nav";
        label.category = label_text(*category);
        label.location = label_text(*location);
    } else {
        return std::nullopt;
    }

    const std::size_t max_scene = label.kind == L"game" ? 60 : 80;
    for (auto movie_end = cursor + 5;
         movie_end <= raw.size() && movie_end <= cursor + max_scene + 4; ++movie_end) {
        if (raw.substr(movie_end - 4, 4) != ".mov") {
            continue;
        }
        const auto scene = raw.substr(cursor, movie_end - cursor - 4);
        if (scene.empty() || scene.size() > max_scene || !printable(scene, true)) {
            return std::nullopt;
        }
        std::size_t end = 0;
        const auto asset = asset_after_movie(raw, movie_end, end);
        if (!asset) {
            continue;
        }
        label.scene = label_text(scene);
        label.offset = start;
        return Match{*asset, std::move(label), end};
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, unsigned>> movie_key(const std::filesystem::path& movie) {
    auto directory = movie.parent_path().filename().wstring();
    auto stem = movie.stem().wstring();
    auto extension = movie.extension().wstring();
    if (directory.size() != 2 || stem.empty() || extension.size() != 4) {
        return std::nullopt;
    }
    for (auto& ch : directory) {
        if (ch >= L'a' && ch <= L'z') {
            ch -= L'a' - L'A';
        }
    }
    for (auto& ch : extension) {
        if (ch >= L'A' && ch <= L'Z') {
            ch += L'a' - L'A';
        }
    }
    if ((directory != L"XN" && directory != L"XV") || extension != L".xmv") {
        return std::nullopt;
    }
    std::string ascii_stem;
    ascii_stem.reserve(stem.size());
    for (const wchar_t ch : stem) {
        if (ch < L'0' || ch > L'9') {
            return std::nullopt;
        }
        ascii_stem.push_back(char(ch));
    }
    unsigned id = 0;
    if (!decimal(ascii_stem, id)) {
        return std::nullopt;
    }
    return std::pair{std::string{char(directory[0]), char(directory[1])}, id};
}

} // namespace

std::optional<ClipIndex> ClipIndex::load(const std::filesystem::path& hdb) {
    std::error_code error;
    const auto size = std::filesystem::file_size(hdb, error);
    if (error || size > max_hdb_size || size > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    std::ifstream input(hdb, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::string raw(static_cast<std::size_t>(size), '\0');
    if (!input.read(raw.data(), static_cast<std::streamsize>(raw.size()))) {
        return std::nullopt;
    }

    ClipIndex index;
    std::size_t label_count = 0;
    const std::string_view data(raw);
    for (std::size_t cursor = 0; cursor < data.size();) {
        const auto found = data.find_first_of("VN", cursor);
        if (found == std::string_view::npos) {
            break;
        }
        const auto match = parse(data, found);
        if (!match) {
            cursor = found + 1;
            continue;
        }
        const auto key = std::pair{match->asset.directory, match->asset.id};
        auto [at, inserted] = index.positions_.try_emplace(key, index.entries_.size());
        if (inserted) {
            index.entries_.push_back(ClipEntry{match->asset.movie, {}});
        }
        index.entries_[at->second].labels.push_back(match->label);
        if (++label_count > max_labels) {
            return std::nullopt;
        }
        cursor = match->end;
    }
    return index;
}

std::vector<ClipLabel> ClipIndex::labels(const std::filesystem::path& movie) const {
    const auto key = movie_key(movie);
    if (!key) {
        return {};
    }
    const auto found = positions_.find(*key);
    if (found != positions_.end()) {
        return entries_[found->second].labels;
    }
    return {};
}

}
