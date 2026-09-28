#include "settings.h"

#include <windows.h>
#include <vector>
#include <stdexcept>

namespace {

std::filesystem::path settings_path() {
    std::vector<wchar_t> buffer(32768);
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) {
        throw std::runtime_error("Cannot locate enhancement settings");
    }
    return std::filesystem::path(buffer.data()).parent_path() / L"patch.ini";
}

Settings& current_settings() {
    static auto value = read_settings(settings_path());
    return value;
}

}

Settings read_settings(const std::filesystem::path& path) {
    Settings result;
    result.dvd_movies = GetPrivateProfileIntW(L"Video", L"DVDMovies", 1, path.c_str()) != 0;
    const auto speed_key = GetPrivateProfileIntW(L"Input", L"MovieSpeedKey", 192, path.c_str());
    if (speed_key <= 254) {
        result.movie_speed_key = speed_key;
    }
    const auto speed = GetPrivateProfileIntW(L"Video", L"MovieSpeed", 2, path.c_str());
    if (speed >= 2 && speed <= 4) {
        result.movie_speed = speed;
    }
    result.movie_speed_mute =
        GetPrivateProfileIntW(L"Audio", L"MovieSpeedMute", 1, path.c_str()) != 0;
    const auto contrast = GetPrivateProfileIntW(L"Video", L"MovieContrast", 0, path.c_str());
    if (contrast <= static_cast<UINT>(MovieContrast::Medium)) {
        result.movie_contrast = static_cast<MovieContrast>(contrast);
    }
    result.dvd_deinterlace =
        GetPrivateProfileIntW(L"Video", L"DVDDeinterlace", 1, path.c_str()) != 0;
    result.save_browser =
        GetPrivateProfileIntW(L"Enhancements", L"SaveBrowser", 0, path.c_str()) != 0;
    result.menu_black_background =
        GetPrivateProfileIntW(L"Enhancements", L"MenuBlackBackground", 1, path.c_str()) != 0;
    result.skip_workstation_login =
        GetPrivateProfileIntW(L"Enhancements", L"SkipWorkstationLogin", 0, path.c_str()) != 0;
    result.skip_menu_animation =
        GetPrivateProfileIntW(L"Enhancements", L"SkipMenuAnimation", 0, path.c_str()) != 0;
    wchar_t device[1024]{};
    GetPrivateProfileStringW(L"Audio", L"Device", L"", device, _countof(device), path.c_str());
    result.audio_device = device;
    result.gamepad = GetPrivateProfileIntW(L"Input", L"Gamepad", 1, path.c_str()) != 0;
    result.vibration = GetPrivateProfileIntW(L"Input", L"Vibration", 1, path.c_str()) != 0;
    result.controller_hints =
        GetPrivateProfileIntW(L"Input", L"ControllerHints", 0, path.c_str()) != 0;
    result.analog_cursor = GetPrivateProfileIntW(L"Input", L"AnalogCursor", 1, path.c_str()) != 0;
    result.spring_cursor = GetPrivateProfileIntW(L"Input", L"SpringCursor", 0, path.c_str()) != 0;
    const auto highlight = GetPrivateProfileIntW(L"Input", L"FocusHighlight", 0, path.c_str());
    if (highlight <= static_cast<UINT>(FocusHighlight::Off)) {
        result.focus_highlight = static_cast<FocusHighlight>(highlight);
    }
    const auto captions = GetPrivateProfileIntW(L"Accessibility", L"Captions", 0, path.c_str());
    if (captions <= static_cast<UINT>(CaptionMode::Off)) {
        result.captions = static_cast<CaptionMode>(captions);
    }
    const auto font = GetPrivateProfileIntW(L"Accessibility", L"CaptionFont",
                                            static_cast<UINT>(CaptionFont::Typist), path.c_str());
    if (font < caption_fonts.size()) {
        result.caption_style.font = static_cast<CaptionFont>(font);
    }
    const auto scale = GetPrivateProfileIntW(L"Accessibility", L"CaptionScale", 100, path.c_str());
    if (scale >= 75 && scale <= 200) {
        result.caption_style.scale = scale;
    }
    result.caption_style.background =
        GetPrivateProfileIntW(L"Accessibility", L"CaptionBackground", 0, path.c_str()) != 0;
    const auto opacity =
        GetPrivateProfileIntW(L"Accessibility", L"CaptionOpacity", 75, path.c_str());
    if (opacity <= 100) {
        result.caption_style.opacity = opacity;
    }
    result.caption_style.background_color =
        GetPrivateProfileIntW(L"Accessibility", L"CaptionBackgroundColor", 0, path.c_str()) &
        0xffffff;
    return result;
}

const Settings& settings() {
    return current_settings();
}

Settings load_settings() {
    return read_settings(settings_path());
}

void save_settings(const Settings& value) {
    const auto path = settings_path();
    const auto highlight = std::to_wstring(static_cast<int>(value.focus_highlight));
    const auto captions = std::to_wstring(static_cast<int>(value.captions));
    const auto font = std::to_wstring(static_cast<int>(value.caption_style.font));
    const auto scale = std::to_wstring(value.caption_style.scale);
    const auto opacity = std::to_wstring(value.caption_style.opacity);
    const auto color = std::to_wstring(value.caption_style.background_color);
    const auto placement = std::to_wstring(static_cast<int>(value.caption_style.placement));
    const auto contrast = std::to_wstring(static_cast<int>(value.movie_contrast));
    const auto speed_key = std::to_wstring(value.movie_speed_key);
    const auto speed = std::to_wstring(value.movie_speed);
    if (!WritePrivateProfileStringW(L"Video", L"DVDMovies", value.dvd_movies ? L"1" : L"0",
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"MovieSpeedKey", speed_key.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"Video", L"MovieSpeed", speed.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"Audio", L"MovieSpeedMute",
                                    value.movie_speed_mute ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"ControllerHints",
                                    value.controller_hints ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Video", L"MovieContrast", contrast.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"Video", L"DVDDeinterlace",
                                    value.dvd_deinterlace ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Enhancements", L"SaveBrowser",
                                    value.save_browser ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"FocusHighlight", highlight.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"Audio", L"Device", value.audio_device.c_str(),
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"Gamepad", value.gamepad ? L"1" : L"0",
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"Vibration", value.vibration ? L"1" : L"0",
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"AnalogCursor", value.analog_cursor ? L"1" : L"0",
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Input", L"SpringCursor", value.spring_cursor ? L"1" : L"0",
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"Captions", captions.c_str(),
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"CaptionFont", font.c_str(), path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"CaptionScale", scale.c_str(),
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"CaptionBackground",
                                    value.caption_style.background ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"CaptionOpacity", opacity.c_str(),
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"CaptionBackgroundColor", color.c_str(),
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Accessibility", L"CaptionPlacement", placement.c_str(),
                                    path.c_str()) ||
        !WritePrivateProfileStringW(L"Enhancements", L"MenuBlackBackground",
                                    value.menu_black_background ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Enhancements", L"SkipWorkstationLogin",
                                    value.skip_workstation_login ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Enhancements", L"SkipMenuAnimation",
                                    value.skip_menu_animation ? L"1" : L"0", path.c_str())) {
        throw std::runtime_error("Cannot save enhancement settings");
    }
    current_settings() = value;
}
