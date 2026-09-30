#pragma once

#include "history.h"
#include <filesystem>

namespace playback {
struct Movie;
}

namespace transcript {

const History& history();
void record_choice(std::wstring text) noexcept;
void record_marker(std::wstring text) noexcept;
void observe_movie(playback::Movie& movie) noexcept;

}
