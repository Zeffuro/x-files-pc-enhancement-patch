#include "settings.h"

#include <windows.h>
#include <vector>
#include <stdexcept>
#include <limits>

namespace {

constexpr std::array quick_menu_keys{L"Save", L"Load", L"Transcript", L"Tweaks", L"Menu"};

unsigned read_controller(const wchar_t* key, unsigned fallback, const std::filesystem::path& path) {
    wchar_t text[32]{};
    const auto size =
        GetPrivateProfileStringW(L"Controller", key, L"", text, _countof(text), path.c_str());
    if (!size || size >= _countof(text) - 1) {
        return fallback;
    }
    unsigned value = 0;
    for (const auto* digit = text; *digit; ++digit) {
        if (*digit < L'0' || *digit > L'9' ||
            value > (std::numeric_limits<unsigned>::max() - (*digit - L'0')) / 10) {
            return fallback;
        }
        value = value * 10 + (*digit - L'0');
    }
    return value;
}

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
    result.dialogue_transcript =
        GetPrivateProfileIntW(L"Enhancements", L"DialogueTranscript", 1, path.c_str()) != 0;
    result.quick_menu = GetPrivateProfileIntW(L"Enhancements", L"QuickMenu", 1, path.c_str()) != 0;
    for (std::size_t index = 0; index < quick_menu_keys.size(); ++index) {
        result.quick_menu_items[index] =
            GetPrivateProfileIntW(L"QuickMenu", quick_menu_keys[index], 1, path.c_str()) != 0;
    }
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
    auto& profile = result.controller_profile;
    for (std::size_t index = 0; index < controller::action_count; ++index) {
        profile.bindings[index] = static_cast<controller::Binding>(read_controller(
            controller::action_keys[index], static_cast<unsigned>(profile.bindings[index]), path));
    }
    profile.deadzone = read_controller(L"Deadzone", 7849, path);
    profile.sensitivity = read_controller(L"Sensitivity", 100, path);
    profile.curve = static_cast<controller::Curve>(read_controller(L"Curve", 1, path));
    profile.trigger_threshold = read_controller(L"TriggerThreshold", 30, path);
    profile.invert_x = read_controller(L"InvertX", 0, path) == 1;
    profile.invert_y = read_controller(L"InvertY", 0, path) == 1;
    profile = controller::normalize(profile);
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
        !WritePrivateProfileStringW(L"Enhancements", L"DialogueTranscript",
                                    value.dialogue_transcript ? L"1" : L"0", path.c_str()) ||
        !WritePrivateProfileStringW(L"Enhancements", L"QuickMenu", value.quick_menu ? L"1" : L"0",
                                    path.c_str()) ||
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
    for (std::size_t index = 0; index < quick_menu_keys.size(); ++index) {
        if (!WritePrivateProfileStringW(L"QuickMenu", quick_menu_keys[index],
                                        value.quick_menu_items[index] ? L"1" : L"0",
                                        path.c_str())) {
            throw std::runtime_error("Cannot save quick menu settings");
        }
    }
    const auto profile = controller::normalize(value.controller_profile);
    const auto write_controller = [&](const wchar_t* key, unsigned setting) {
        const auto text = std::to_wstring(setting);
        if (!WritePrivateProfileStringW(L"Controller", key, text.c_str(), path.c_str())) {
            throw std::runtime_error("Cannot save controller settings");
        }
    };
    for (std::size_t index = 0; index < controller::action_count; ++index) {
        write_controller(controller::action_keys[index],
                         static_cast<unsigned>(profile.bindings[index]));
    }
    write_controller(L"Deadzone", profile.deadzone);
    write_controller(L"Sensitivity", profile.sensitivity);
    write_controller(L"Curve", static_cast<unsigned>(profile.curve));
    write_controller(L"TriggerThreshold", profile.trigger_threshold);
    write_controller(L"InvertX", profile.invert_x);
    write_controller(L"InvertY", profile.invert_y);
    current_settings() = value;
    current_settings().controller_profile = profile;
}
