#pragma once

#include "movie.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace media::subtitles {

struct Cue {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    std::wstring text;
};

struct Result {
    std::size_t movies = 0;
    std::size_t cues = 0;
    std::size_t skipped = 0;
};

using Progress = std::function<bool(std::size_t completed, std::size_t total)>;

// Native cue times use the movie's timescale. Loaded overrides use milliseconds.
std::vector<Cue> native_cues(const Movie& movie);
// The input cues use milliseconds, while movie_time uses movie_scale.
std::wstring caption_at(const std::vector<Cue>& cues, std::uint64_t movie_time,
                        std::uint32_t movie_scale);
Result export_pack(const std::filesystem::path& game_root,
                   const std::filesystem::path& output_folder, const Progress& progress = {});
Result install_pack(const std::filesystem::path& game_root,
                    const std::filesystem::path& input_folder, const Progress& progress = {});
Result export_zip(const std::filesystem::path& game_root, const std::filesystem::path& output_zip,
                  const Progress& progress = {});
Result install_zip(const std::filesystem::path& game_root, const std::filesystem::path& input_zip,
                   const Progress& progress = {});
std::optional<std::vector<Cue>> load_override(const std::filesystem::path& game_root,
                                              const std::filesystem::path& relative_movie,
                                              const std::filesystem::path& actual_movie) noexcept;
std::string srt_text(const std::vector<Cue>& cues, std::uint32_t scale);
std::vector<Cue> srt_cues(const std::string& text, std::uint64_t duration_ms);
void install_text(const std::filesystem::path& root, const std::filesystem::path& relative,
                  const std::string& text);
std::uint64_t install_generation() noexcept;

}
