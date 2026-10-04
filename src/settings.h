#pragma once

#include "controller_profile.h"

#include <filesystem>
#include <array>

enum class FocusHighlight { Automatic, Always, Off };

enum class CaptionMode { Game, On, Off };
enum class CaptionFont { Modern, Game, Typist, ZonkersHand, Bubbledot };
enum class CaptionPlacement { Inside, Below };
enum class MovieContrast { Off, Scene, Mild, Medium };

struct CaptionTypeface {
    const wchar_t* name;
    const wchar_t* file;
};

inline constexpr std::array<CaptionTypeface, 5> caption_fonts{{
    {L"Segoe UI", nullptr},
    {L"Schmutz ICG Cleaned", L"DLG.TTR"},
    {L"Typist", L"HCD.TTR"},
    {L"ZonkersHand", L"JRN.TTR"},
    {L"Bubbledot Coarse Positive", L"PHN.TTR"},
}};

struct CaptionStyle {
    CaptionFont font = CaptionFont::Typist;
    unsigned scale = 100;
    bool background = false;
    CaptionPlacement placement = CaptionPlacement::Inside;

    unsigned opacity = 75;
    unsigned background_color = 0;

    bool operator==(const CaptionStyle&) const = default;
};

struct Settings {
    bool dvd_movies = true;
    bool dvd_deinterlace = true;
    unsigned movie_speed_key = 192;
    unsigned movie_speed = 2;
    bool movie_speed_mute = true;
    MovieContrast movie_contrast = MovieContrast::Off;
    bool save_browser = false;
    bool autosaves = true;
    bool continue_latest = true;
    bool dialogue_transcript = true;
    bool hotspot_reveal = true;
    bool hotspot_exits_only = false;
    bool hotspot_labels = true;
    unsigned hotspot_reveal_key = 164;
    bool readable_documents = true;
    bool quick_menu = true;
    std::array<bool, 5> quick_menu_items{true, true, true, true, true};
    bool menu_black_background = true;
    bool skip_menu_animation = false;
    bool skip_workstation_login = false;
    std::wstring audio_device;
    bool gamepad = true;
    bool vibration = true;
    bool controller_hints = false;
    bool analog_cursor = true;
    bool spring_cursor = false;
    controller::Profile controller_profile;
    FocusHighlight focus_highlight = FocusHighlight::Automatic;
    CaptionMode captions = CaptionMode::Game;
    CaptionStyle caption_style;
};

Settings read_settings(const std::filesystem::path& path);
Settings load_settings();
const Settings& settings();
void save_settings(const Settings& value);
