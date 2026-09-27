#include "preview.h"
#include "media/frame_reference.h"
#include <fstream>
#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <windows.h>

namespace saves {
namespace {
std::filesystem::path sidecar(std::filesystem::path save) {
    save.replace_extension(L".preview");
    return save;
}

media::Movie open_movie(const std::filesystem::path& root, const SceneReference& reference) {
    if (!valid_scene_reference(reference)) {
        throw std::runtime_error("Invalid scene preview");
    }
    const auto path = root / reference.movie;
    if (std::filesystem::file_size(path) > 128ull * 1024 * 1024) {
        throw std::runtime_error("Scene movie is too large to preview");
    }
    auto movie = media::Movie::open(path);
    if (!movie.timescale || movie.duration > UINT32_MAX) {
        throw std::runtime_error("Scene movie timeline is too large to preview");
    }
    return movie;
}
}

bool valid_scene_reference(const SceneReference& reference) {
    auto path = reference.movie.generic_wstring();
    std::transform(path.begin(), path.end(), path.begin(), std::towlower);
    if (!reference.track || reference.sample > 1000000 || reference.movie.is_absolute()) {
        return false;
    }
    if (media::navigation_archive(reference.movie) && reference.movie.parent_path().empty()) {
        return !reference.motion;
    }
    if (path.size() != 12 || (!path.starts_with(L"xn/") && !path.starts_with(L"xv/")) ||
        !path.ends_with(L".xmv")) {
        return false;
    }
    return std::all_of(path.begin() + 3, path.begin() + 8,
                       [](wchar_t c) { return c >= L'0' && c <= L'9'; }) &&
           (!reference.motion || path.starts_with(L"xn/"));
}

void write_scene_reference(const std::filesystem::path& save, const SceneReference& reference) {
    if (!valid_scene_reference(reference)) {
        return;
    }
    const auto path = reference.movie.generic_string();
    const auto data = "XFSCENE2\n" + path + '\n' + std::to_string(reference.track) + ' ' +
                      std::to_string(reference.sample) + ' ' + (reference.motion ? "1 " : "0 ") +
                      std::to_string(reference.time) + '\n';
    const auto output = CreateFileW(sidecar(save).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot write the scene preview reference");
    }
    DWORD written = 0;
    const auto ok =
        WriteFile(output, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
        written == data.size() && FlushFileBuffers(output);
    CloseHandle(output);
    if (!ok) {
        throw std::runtime_error("Cannot finish the scene preview reference");
    }
}

SceneReference read_scene_reference(const std::filesystem::path& save) {
    SceneReference result;
    const auto file = sidecar(save);
    const auto flags = GetFileAttributesW(file.c_str());
    std::error_code error;
    if (save.empty() || flags == INVALID_FILE_ATTRIBUTES ||
        (flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        std::filesystem::file_size(file, error) > 256 || error) {
        return {};
    }
    std::ifstream input(file, std::ios::binary);
    std::string signature, movie;
    std::getline(input, signature);
    std::getline(input, movie);
    result.movie = movie;
    input >> result.track >> result.sample >> result.motion;
    if (signature == "XFSCENE2") {
        input >> result.time;
    } else {
        result.motion = false;
    }
    if (!input || (signature != "XFSCENE1" && signature != "XFSCENE2") ||
        !valid_scene_reference(result)) {
        return {};
    }
    input >> std::ws;
    return input.eof() ? result : SceneReference{};
}

ScenePreview::ScenePreview(const std::filesystem::path& root, const SceneReference& reference)
    : movie_(open_movie(root, reference)), reference_(reference) {
    for (const auto& track : movie_.tracks) {
        if (track.id == reference.track && track.handler == "vide" &&
            reference.sample < track.samples.size()) {
            track_ = &track;
            break;
        }
    }
    if (!track_) {
        throw std::runtime_error("Saved scene frame is unavailable");
    }
    if (reference_.motion &&
        (reference_.time >= movie_.duration ||
         track_->sample_at(reference_.time, movie_.timescale) != reference_.sample)) {
        reference_.motion = false;
    }
}

const media::Frame& ScenePreview::update(std::uint64_t milliseconds) {
    auto sample = static_cast<std::size_t>(reference_.sample);
    if (reference_.motion && movie_.duration && movie_.timescale) {
        const auto elapsed = (milliseconds * movie_.timescale / 1000) % movie_.duration;
        const auto time = (reference_.time + elapsed) % movie_.duration;
        sample = track_->sample_at(time, movie_.timescale).value_or(sample);
    }
    if (sample_ != sample) {
        decoder_.decode(movie_, *track_, sample);
        sample_ = sample;
    }
    return decoder_.last_frame();
}
}
