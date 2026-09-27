#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace ui {

enum class Language { English, German, French, Spanish, Italian, Japanese };

Language system_language();
Language language();
Language read_language(const std::filesystem::path& settings_file);
void save_language(const std::filesystem::path& settings_file, Language value);
const wchar_t* language_name(Language value);
const wchar_t* translate(const wchar_t* english);
const wchar_t* translate(const wchar_t* english, Language value);
void translate_dialog(HWND window);
void translate_dialog(HWND window, Language value);

class DialogTranslator {
public:
    explicit DialogTranslator(HWND window);
    void apply(Language value) const;

private:
    HWND window_;
    std::wstring title_;
    std::vector<std::pair<HWND, std::wstring>> labels_;
};

}
