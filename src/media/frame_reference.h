#pragma once
#include "movie.h"
#include <algorithm>
#include <cwctype>

namespace media {
struct FrameReference {
    std::uint32_t track;
    std::size_t sample;
    std::size_t count;
};

inline std::optional<FrameReference> frame_reference(const Track& track, std::size_t sample) {
    if (sample >= track.samples.size()) {
        return std::nullopt;
    }
    return FrameReference{track.id, sample, track.samples.size()};
}

inline bool navigation_archive(const std::filesystem::path& path) {
    auto name = path.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), std::towlower);
    return name.size() == 8 && name.starts_with(L"nav") && name.ends_with(L".nmv") &&
           ((name[3] >= L'1' && name[3] <= L'7') || name[3] == L'm');
}

inline bool navigation_loop(const std::filesystem::path& path) {
    auto parent = path.parent_path().filename().wstring();
    auto extension = path.extension().wstring();
    std::transform(parent.begin(), parent.end(), parent.begin(), std::towlower);
    std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);
    const auto name = path.stem().wstring();
    return parent == L"xn" && extension == L".xmv" && !name.empty() &&
           std::all_of(name.begin(), name.end(),
                       [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
}

inline std::wstring frame_key(const std::filesystem::path& path, const FrameReference& frame) {
    auto name = path.generic_wstring();
    std::transform(name.begin(), name.end(), name.begin(), std::towlower);
    return name + L"#track=" + std::to_wstring(frame.track) + L"&sample=" +
           std::to_wstring(frame.sample);
}
}
