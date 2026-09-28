#include "captions.h"
#include "identity.h"

#include <algorithm>
#include <stdexcept>

namespace dvd {
namespace {
struct Mapping {
    const wchar_t* name;
    std::uintmax_t vob_size;
    std::string_view vob_hash;
    std::uintmax_t movie_size;
    std::string_view movie_hash;
    std::uint64_t duration;
    std::size_t cues;
    std::int64_t offset;
};

// Offsets align the exact English MOV audio with the shared DVD presentation clock.
// Interactive binocular composition still requires QuickTime's drawing surfaces.
constexpr Mapping mappings[]{
    {L"19650", 13099008, "ac03534c3e76641666a08e7164da8ebf499248c3858e8702d7887352425dd620",
     8783518, "607a75c4952e0507938ba6fcc6e3436993c6583e262ed271027a792bc1779244", 10220, 2, -3609},
};

const Mapping* mapping(const std::filesystem::path& path) {
    if (!_wcsicmp(path.extension().c_str(), L".vob")) {
        for (const auto& entry : mappings) {
            if (!_wcsicmp(path.stem().c_str(), entry.name)) {
                return &entry;
            }
        }
    }
    return nullptr;
}

std::uint64_t mapped_time(std::uint64_t time, std::uint32_t scale, std::int64_t offset) {
    if (!scale || time > UINT64_MAX / 90000) {
        throw std::runtime_error("Invalid DVD caption timescale");
    }
    const auto ticks = time * 90000 / scale;
    if (offset < 0) {
        const auto trim = static_cast<std::uint64_t>(-offset);
        return ticks > trim ? ticks - trim : 0;
    }
    if (ticks > UINT64_MAX - static_cast<std::uint64_t>(offset)) {
        throw std::runtime_error("DVD caption offset overflow");
    }
    return ticks + offset;
}

void identity(const std::filesystem::path& path, std::uintmax_t size, std::string_view expected) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != size ||
        sha256(path) != expected) {
        throw std::runtime_error("DVD caption source does not match the supported English pair");
    }
}
}

Captions::Captions(std::vector<media::subtitles::Cue> cues, std::uint32_t scale,
                   std::int64_t offset) {
    if (!scale || cues.empty() || offset < -90000 || offset > 90000) {
        throw std::runtime_error("DVD movie has no mapped captions");
    }
    std::uint64_t previous = 0;
    for (auto& cue : cues) {
        if (cue.begin < previous || cue.end <= cue.begin || cue.text.empty()) {
            throw std::runtime_error("Invalid DVD caption cues");
        }
        previous = cue.begin;
        cue.begin = mapped_time(cue.begin, scale, offset);
        cue.end = mapped_time(cue.end, scale, offset);
        if (cue.end > cue.begin) {
            cues_.push_back(std::move(cue));
        }
    }
    if (cues_.empty()) {
        throw std::runtime_error("DVD movie has no visible captions");
    }
}

bool Captions::supported(const std::filesystem::path& vob) {
    return mapping(vob) != nullptr;
}

Captions Captions::load(const std::filesystem::path& vob) {
    const auto* pair = mapping(vob);
    if (!pair) {
        throw std::runtime_error("DVD clip has no caption mapping");
    }
    const auto root = vob.parent_path().parent_path();
    const auto relative = std::filesystem::path(L"XV") / (std::wstring(pair->name) + L".XMV");
    const auto source = root / relative;
    identity(vob, pair->vob_size, pair->vob_hash);
    identity(source, pair->movie_size, pair->movie_hash);
    const auto movie = media::Movie::open(source);
    auto cues = media::subtitles::native_cues(movie);
    if (movie.timescale != 600 || movie.duration != pair->duration || cues.size() != pair->cues) {
        throw std::runtime_error("DVD caption mapping has unexpected text data");
    }
    Captions mapped;
    if (!cues.empty()) {
        mapped = Captions(std::move(cues), movie.timescale, pair->offset);
    }
    if (auto override = media::subtitles::load_override(root, relative, source)) {
        if (override->empty()) {
            mapped.cues_.clear();
        } else {
            mapped = Captions(std::move(*override), 1000, pair->offset);
        }
    }
    return mapped;
}

std::wstring Captions::at(std::int64_t time) const {
    if (time < 0) {
        return {};
    }
    std::wstring text;
    for (const auto& cue : cues_) {
        if (cue.begin > static_cast<std::uint64_t>(time)) {
            break;
        }
        if (static_cast<std::uint64_t>(time) < cue.end) {
            if (!text.empty()) {
                text += L'\n';
            }
            text += cue.text;
        }
    }
    return text;
}

}
