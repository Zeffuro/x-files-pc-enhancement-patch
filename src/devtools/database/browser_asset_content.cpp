#include "browser_state.h"
#include "game/assets/xt_text.h"
#include <fstream>

namespace devtools::database_browser {
namespace {
void property(Browser& state, HTREEITEM parent, const std::wstring& text) {
    TVINSERTSTRUCTW node{};
    node.hParent = parent;
    node.hInsertAfter = TVI_LAST;
    node.item.mask = TVIF_TEXT;
    node.item.pszText = const_cast<LPWSTR>(text.c_str());
    TreeView_InsertItem(state.properties, &node);
}

std::wstring display_lines(const std::wstring& text) {
    std::wstring result;
    for (std::size_t at = 0; at < text.size(); ++at) {
        const auto ch = text[at];
        if (ch == L'\r') {
            if (at + 1 < text.size() && text[at + 1] == L'\n') {
                ++at;
            }
            result += L"\r\n";
        } else if (ch == L'\n') {
            result += L"\r\n";
        } else {
            result += ch;
        }
    }
    return result;
}
}

void asset_content(Browser& state, const DatabaseAsset& asset, std::wstring& raw) {
    const auto file = database_asset_file(state.root, asset.path);
    if (asset.type == L"Localization") {
        load_strings(state, file.value_or(std::filesystem::path{}));
        property(state, TVI_ROOT, state.resource_strings.status);
        property(state, TVI_ROOT,
                 L"Strings: " + std::to_wstring(state.resource_strings.strings.size()));
        property(state, TVI_ROOT,
                 L"Strings preserve resource IDs and language IDs. DLL code is not executed.");
        EnableWindow(state.preview, asset.present);
        SetWindowTextW(state.preview, L"Show strings");
    }
    if (!file) {
        raw = L"Asset file is missing or its path is unsafe.";
        SetWindowTextW(state.content, raw.c_str());
        return;
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(*file, error);
    std::ifstream input(*file, std::ios::binary);
    if (error || !input) {
        raw = L"Cannot read asset file.";
        SetWindowTextW(state.content, raw.c_str());
        return;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::min(size, std::uintmax_t(4096))));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (input.bad() || static_cast<std::size_t>(input.gcount()) != bytes.size()) {
            raw = L"Asset file changed or could not be read completely.";
            SetWindowTextW(state.content, raw.c_str());
            return;
        }
    }
    raw = L"Asset bytes: " + asset.path.generic_wstring() + L"\r\nFile size: " +
          std::to_wstring(size) + L" bytes\r\n" + database_native_hex(bytes);
    if (bytes.size() < size) {
        raw += L"\r\nFirst " + std::to_wstring(bytes.size()) + L" bytes shown.";
    }
    if (asset.type == L"Text") {
        const auto decoded = game_assets::load_xt_text(*file);
        property(state, TVI_ROOT,
                 std::wstring(L"Text encoding: ") +
                     game_assets::xt_encoding_name(decoded.encoding));
        property(state, TVI_ROOT, decoded.status);
        const auto content = decoded.valid ? display_lines(decoded.text) : decoded.status;
        SetWindowTextW(state.content, content.c_str());
    }
}
}
