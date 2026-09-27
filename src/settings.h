#pragma once

#include <filesystem>
#include <array>

enum class FocusHighlight { Automatic, Always, Off };

enum class CaptionMode { Game, On, Off };
enum class CaptionFont { Modern, Game, Typist, ZonkersHand, Bubbledot };
enum class CaptionPlacement { Inside, Below };

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
    bool save_browser = false;
    bool menu_black_background = true;
    bool skip_menu_animation = false;
    bool skip_workstation_login = false;
    std::wstring audio_device;
    bool gamepad = true;
    bool analog_cursor = true;
    bool spring_cursor = false;
    FocusHighlight focus_highlight = FocusHighlight::Automatic;
    CaptionMode captions = CaptionMode::Game;
    CaptionStyle caption_style;
};

Settings read_settings(const std::filesystem::path& path);
const Settings& settings();
void save_settings(const Settings& value);
