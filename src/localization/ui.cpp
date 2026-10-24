#include "ui.h"
#include "english.h"
#include "german.h"
#include "french.h"
#include "spanish.h"
#include "italian.h"
#include "japanese.h"
#include "russian.h"

#include <array>
#include <algorithm>
#include <span>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

namespace ui {
namespace {

constexpr std::array<const wchar_t*, language_count> codes{L"en", L"de", L"fr", L"es",
                                                           L"it", L"ja", L"ru"};
constexpr std::array<const wchar_t*, language_count> names{
    L"English", L"Deutsch", L"Français", L"Español", L"Italiano", L"日本語", L"Русский"};

std::filesystem::path settings_path() {
    std::vector<wchar_t> executable(32768);
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) {
        return {};
    }
    return std::filesystem::path(executable.data()).parent_path() / L"patch.ini";
}

BOOL CALLBACK translate_child(HWND window, LPARAM parameter) {
    wchar_t class_name[32]{};
    GetClassNameW(window, class_name, _countof(class_name));
    if (std::wcscmp(class_name, L"Static") != 0 && std::wcscmp(class_name, L"Button") != 0) {
        return TRUE;
    }
    const auto selected = static_cast<Language>(parameter);
    std::wstring source(GetWindowTextLengthW(window) + 1, L'\0');
    source.resize(GetWindowTextW(window, source.data(), static_cast<int>(source.size())));
    if (!source.empty()) {
        const auto* translated = translate(source.c_str(), selected);
        if (translated != source.c_str()) {
            SetWindowTextW(window, translated);
        }
    }
    return TRUE;
}

}

Language system_language() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_GERMAN:
            return Language::German;
        case LANG_FRENCH:
            return Language::French;
        case LANG_SPANISH:
            return Language::Spanish;
        case LANG_ITALIAN:
            return Language::Italian;
        case LANG_JAPANESE:
            return Language::Japanese;
        case LANG_RUSSIAN:
            return Language::Russian;
        default:
            return Language::English;
    }
}

Language language() {
    const auto path = settings_path();
    if (path.empty()) {
        return system_language();
    }
    return read_language(path);
}

Language read_language(const std::filesystem::path& path) {
    wchar_t code[16]{};
    GetPrivateProfileStringW(L"Interface", L"Language", L"", code, _countof(code), path.c_str());
    for (std::size_t index = 0; index < codes.size(); ++index) {
        if (_wcsicmp(code, codes[index]) == 0) {
            return static_cast<Language>(index);
        }
    }
    return system_language();
}

void save_language(const std::filesystem::path& settings_file, Language value) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= codes.size() || !WritePrivateProfileStringW(L"Interface", L"Language",
                                                             codes[index], settings_file.c_str())) {
        throw std::runtime_error("Cannot save the interface language");
    }
}

const wchar_t* language_name(Language value) {
    const auto index = static_cast<std::size_t>(value);
    return index < names.size() ? names[index] : names[0];
}

const wchar_t* translate(const wchar_t* english, Language value) {
    if (!english || value == Language::English) {
        return english;
    }
    const auto source =
        std::find_if(std::begin(detail::english_entries), std::end(detail::english_entries),
                     [&](const auto& entry) { return std::wcscmp(english, entry.text) == 0; });
    if (source == std::end(detail::english_entries)) {
        return english;
    }
    std::span<const detail::Translation> entries;
    switch (value) {
        case Language::German:
            entries = detail::german_entries;
            break;
        case Language::French:
            entries = detail::french_entries;
            break;
        case Language::Spanish:
            entries = detail::spanish_entries;
            break;
        case Language::Italian:
            entries = detail::italian_entries;
            break;
        case Language::Japanese:
            entries = detail::japanese_entries;
            break;
        case Language::Russian:
            entries = detail::russian_entries;
            break;
        default:
            return english;
    }
    for (const auto& entry : entries) {
        if (entry.id == source->id) {
            return entry.text;
        }
    }
    return english;
}

const wchar_t* translate(const wchar_t* english) {
    return translate(english, language());
}

void translate_dialog(HWND window, Language value) {
    std::wstring title(GetWindowTextLengthW(window) + 1, L'\0');
    title.resize(GetWindowTextW(window, title.data(), static_cast<int>(title.size())));
    if (!title.empty()) {
        SetWindowTextW(window, translate(title.c_str(), value));
    }
    EnumChildWindows(window, translate_child, static_cast<LPARAM>(value));
}

void translate_dialog(HWND window) {
    translate_dialog(window, language());
}

DialogTranslator::DialogTranslator(HWND window) : window_(window) {
    title_.resize(GetWindowTextLengthW(window) + 1, L'\0');
    title_.resize(GetWindowTextW(window, title_.data(), static_cast<int>(title_.size())));

    struct Capture {
        static BOOL CALLBACK run(HWND child, LPARAM context) {
            wchar_t class_name[32]{};
            GetClassNameW(child, class_name, _countof(class_name));
            if (std::wcscmp(class_name, L"Static") != 0 &&
                std::wcscmp(class_name, L"Button") != 0) {
                return TRUE;
            }
            std::wstring label(GetWindowTextLengthW(child) + 1, L'\0');
            label.resize(GetWindowTextW(child, label.data(), static_cast<int>(label.size())));
            if (!label.empty()) {
                auto& labels =
                    *reinterpret_cast<std::vector<std::pair<HWND, std::wstring>>*>(context);
                labels.emplace_back(child, std::move(label));
            }
            return TRUE;
        }
    };

    EnumChildWindows(window, Capture::run, reinterpret_cast<LPARAM>(&labels_));
}

void DialogTranslator::apply(Language value) const {
    SetWindowTextW(window_, translate(title_.c_str(), value));
    for (const auto& [child, english] : labels_) {
        SetWindowTextW(child, translate(english.c_str(), value));
    }
}

}
