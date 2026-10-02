#include "pff_browser_state.h"
#include "pff_image.h"
#include "asset_io.h"
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace devtools::pff_browser {
namespace {
struct Choice {
    std::filesystem::path path;
    DWORD format = 1;
};

std::optional<Choice> choose(State& state, bool save, const wchar_t* title, const wchar_t* filters,
                             const std::wstring& suggested, const wchar_t* extension) {
    std::wstring filename(32768, L'\0');
    std::copy(suggested.begin(), suggested.end(), filename.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.window;
    dialog.lpstrFilter = filters;
    dialog.nFilterIndex = 1;
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = extension;
    dialog.Flags =
        OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_NOREADONLYRETURN : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
        return std::nullopt;
    }
    filename.resize(wcslen(filename.c_str()));
    return Choice{filename, dialog.nFilterIndex};
}

std::wstring extension(const std::filesystem::path& path) {
    auto result = path.extension().wstring();
    std::transform(result.begin(), result.end(), result.begin(), std::towlower);
    return result;
}

void saved(State& state, const std::filesystem::path& path) {
    const auto status = L"Saved: " + path.wstring() + L"\r\nOriginal archive kept unchanged.";
    SetWindowTextW(state.status, status.c_str());
}
}

void export_asset(State& state) {
    if (!state.selected) {
        return;
    }
    const auto name =
        state.path.stem().wstring() + L"-entry-" + std::to_wstring(*state.selected) + L".bin";
    const auto choice = choose(state, true, L"Export PFF asset",
                               L"Original PICT bytes (*.bin)\0*.bin\0PNG image (*.png)\0*.png\0"
                               L"PICT file (*.pict)\0*.pict\0",
                               name, L"bin");
    if (!choice) {
        return;
    }
    const auto raw = state.archive->entry(*state.selected);
    auto output = choice->path;
    output.replace_extension(choice->format == 2   ? L".png"
                             : choice->format == 3 ? L".pict"
                                                   : L".bin");
    if (choice->format == 2) {
        const auto bytes = asset_png_bytes(decode_pff_image(raw));
        write_new_asset(output, bytes);
    } else if (choice->format == 3) {
        std::vector<std::uint8_t> bytes(512);
        bytes.insert(bytes.end(), raw.begin(), raw.end());
        write_new_asset(output, bytes);
    } else {
        write_new_asset(output, raw);
    }
    saved(state, output);
}

void import_asset(State& state) {
    if (!state.selected) {
        return;
    }
    const auto choice =
        choose(state, false, L"Import replacement for selected PFF entry",
               L"Asset files (*.png, *.bin, *.pict)\0*.png;*.bin;*.pict\0", L"", nullptr);
    if (!choice) {
        return;
    }
    const auto suffix = extension(choice->path);
    std::vector<std::uint8_t> bytes;
    if (suffix == L".png") {
        bytes = encode_pff_image(read_asset_png(choice->path));
    } else if (suffix == L".bin" || suffix == L".pict") {
        bytes = read_asset_bytes(choice->path);
        if (suffix == L".pict" && bytes.size() >= 512 &&
            std::all_of(bytes.begin(), bytes.begin() + 512,
                        [](auto value) { return value == 0; })) {
            bytes.erase(bytes.begin(), bytes.begin() + 512);
        }
        if (bytes.size() < 14 || bytes[10] != 0 || bytes[11] != 0x11 || bytes[12] != 2 ||
            bytes[13] != 0xff) {
            throw std::runtime_error("Raw import must contain a PICT version 2 image");
        }
    } else {
        throw std::runtime_error("Import a PNG, raw .bin asset or .pict file");
    }
    std::wstring error;
    const auto replacement = state.archive->replace(*state.selected, bytes, &error);
    if (!replacement) {
        throw failure(error);
    }
    auto next = game_assets::PffArchive::parse(*replacement, &error);
    if (!next) {
        throw failure(error);
    }
    state.archive = std::move(next);
    state.modified = true;
    select(state);
    InvalidateRect(state.list, nullptr, FALSE);
}

void save_archive(State& state) {
    const auto choice =
        choose(state, true, L"Save a new PFF archive copy", L"PFF archive (*.pff)\0*.pff\0",
               state.path.stem().wstring() + L"-modified.pff", L"pff");
    if (choice) {
        auto output = choice->path;
        output.replace_extension(L".pff");
        write_new_asset(output, state.archive->bytes());
        saved(state, output);
    }
}

void reset_archive(State& state) {
    if (!state.modified) {
        return;
    }
    std::wstring error;
    auto original = game_assets::PffArchive::load(state.path, &error);
    if (!original) {
        throw failure(error);
    }
    const auto selection = state.selected;
    state.archive = std::move(original);
    state.modified = false;
    ListView_SetItemCount(state.list, static_cast<int>(state.archive->entries().size()));
    if (selection && *selection < state.archive->entries().size()) {
        ListView_SetItemState(state.list, static_cast<int>(*selection),
                              LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    select(state);
    InvalidateRect(state.list, nullptr, FALSE);
}
}
