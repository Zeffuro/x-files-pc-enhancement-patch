#pragma once
#include "pff_browser.h"
#include "game/assets/pff.h"
#include "media/video.h"
#include "image_view.h"
#include <optional>
#include <stdexcept>

namespace devtools::pff_browser {
inline std::runtime_error failure(const std::wstring& message) {
    std::string value;
    for (const auto ch : message) {
        value += static_cast<char>(ch);
    }
    return std::runtime_error(value);
}

enum {
    list_id = 4300,
    export_id,
    import_id,
    save_id,
    reset_id,
    status_id,
    picture_id,
    details_id,
    size_id
};

struct State {
    HWND window = nullptr, list = nullptr, picture = nullptr, details = nullptr, status = nullptr;
    HWND export_button = nullptr, import_button = nullptr, save = nullptr, reset = nullptr;
    HWND size = nullptr;
    ImageView view;
    HMODULE module = nullptr;
    HFONT font = nullptr;
    std::filesystem::path path;
    std::optional<game_assets::PffArchive> archive;
    std::shared_ptr<HWND> lifetime;
    std::optional<std::size_t> selected;
    media::Frame image;
    bool modified = false, owned = false;
};

void select(State& state);
void export_asset(State& state);
void import_asset(State& state);
void save_archive(State& state);
void reset_archive(State& state);
}
