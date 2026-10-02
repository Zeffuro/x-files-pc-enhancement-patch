#include "devtools/database/asset_io.h"
#include "devtools/database/pff_image.h"
#include "devtools/database/pff_browser.h"
#include "game/assets/pff.h"
#include <commctrl.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <typename Function> void rejected(Function function) {
    bool failed = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "Invalid operation was accepted");
}

void word(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

COLORREF pixel(HWND window, int x, int y) {
    const auto picture = GetDlgItem(window, 4306);
    RECT bounds{};
    GetClientRect(picture, &bounds);
    const auto screen = GetDC(nullptr);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, bounds.right, bounds.bottom);
    require(dc && bitmap, "Cannot capture archive preview");
    const auto previous = SelectObject(dc, bitmap);
    DRAWITEMSTRUCT item{};
    item.hDC = dc;
    item.hwndItem = picture;
    item.rcItem = bounds;
    SendMessageW(window, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&item));
    const auto result = GetPixel(dc, x, y);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return result;
}

void actual_size(HWND window) {
    const auto size = GetDlgItem(window, 4308);
    require(size && SendMessageW(size, CB_GETCOUNT, 0, 0) == 2, "Archive size modes missing");
    SendMessageW(size, CB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(4308, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(size));
}

std::wstring text(HWND window) {
    std::wstring result(GetWindowTextLengthW(window) + 1, L'\0');
    result.resize(GetWindowTextW(window, result.data(), static_cast<int>(result.size())));
    return result;
}
}

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::path(temp) /
                      (L"xfiles-asset-io-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                       std::to_wstring(stamp));
    HWND parent = nullptr;
    try {
        require(std::filesystem::create_directory(root), "Cannot create unique asset fixture");
        const media::Frame image{
            2, 2, {0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 32, 64, 128, 255}};
        const auto png = devtools::asset_png_bytes(image);
        const auto png_path = root / L"export.png";
        devtools::write_new_asset(png_path, png);
        const auto imported = devtools::read_asset_png(png_path);
        require(imported.width == image.width && imported.height == image.height &&
                    imported.pixels == image.pixels,
                "PNG export/import changed image pixels");
        rejected([&] { devtools::write_new_asset(png_path, std::vector<std::uint8_t>{1, 2, 3}); });
        require(devtools::read_asset_bytes(png_path) == png, "Export overwrote existing file");
        rejected([&] { devtools::read_asset_bytes(png_path, 1); });
        rejected([&] { devtools::asset_png_bytes({2, 2, {1}}); });
        rejected([&] { devtools::read_asset_png(root / L"absent.png"); });
        const auto pict = devtools::encode_pff_image(imported);
        std::vector<std::uint8_t> archive{'P', 'F', 'F', ' '};
        word(archive, 1);
        word(archive, 20);
        word(archive, static_cast<std::uint32_t>(20 + pict.size()));
        word(archive, 42);
        archive.insert(archive.end(), pict.begin(), pict.end());
        const auto original_path = root / L"original.pff";
        devtools::write_new_asset(original_path, archive);
        const auto parsed = game_assets::PffArchive::load(original_path);
        require(parsed.has_value(), "Exported PFF did not parse");
        auto edited = imported;
        edited.pixels[2] = 23;
        const auto replacement = parsed->replace(0, devtools::encode_pff_image(edited));
        require(replacement.has_value(), "Imported PICT did not replace archive entry");
        const auto copy_path = root / L"modified.pff";
        devtools::write_new_asset(copy_path, *replacement);
        const auto copy = game_assets::PffArchive::load(copy_path);
        require(copy && copy->entries()[0].header_value == 42 &&
                    devtools::decode_pff_image(copy->entry(0)).pixels == edited.pixels,
                "Saved archive copy lost metadata or imported pixels");
        require(devtools::read_asset_bytes(original_path) == archive,
                "Import changed the original archive");
        rejected([&] { devtools::write_new_asset(original_path, *replacement); });
        devtools::write_new_asset(root / L"invalid.png", std::vector<std::uint8_t>{1, 2, 3});
        rejected([&] { devtools::read_asset_png(root / L"invalid.png"); });
        const auto module = GetModuleHandleW(nullptr);
        parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr,
                                 nullptr, module, nullptr);
        require(parent != nullptr, "Cannot create test host");
        const auto font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const auto window = devtools::open_pff_browser(parent, module, font, original_path, false);
        require(window && *window && ListView_GetItemCount(GetDlgItem(*window, 4300)) == 1,
                "Archive browser entries unavailable");
        require(text(GetDlgItem(*window, 4307)).find(L"Image: 2 x 2") != std::wstring::npos,
                "Archive browser preview metadata missing");
        require(text(GetDlgItem(*window, 4305)).find(L"read-only") != std::wstring::npos,
                "Archive browser did not identify original source");
        actual_size(*window);
        RECT picture_bounds{};
        GetClientRect(GetDlgItem(*window, 4306), &picture_bounds);
        const auto center_x = (picture_bounds.right - 2) / 2;
        const auto center_y = (picture_bounds.bottom - 2) / 2;
        require(pixel(*window, center_x, center_y) == RGB(255, 0, 0) &&
                    pixel(*window, center_x + 1, center_y) == RGB(0, 255, 0) &&
                    pixel(*window, center_x, center_y + 1) == RGB(0, 0, 255) &&
                    pixel(*window, center_x - 1, center_y) == RGB(0, 0, 0) &&
                    pixel(*window, center_x + 2, center_y) == RGB(0, 0, 0),
                "Archive actual size did not preserve centered 1:1 pixels");
        SendMessageW(*window, WM_CLOSE, 0, 0);
        require(!*window, "Archive close retained stale window lifetime");
        const auto reopened =
            devtools::open_pff_browser(parent, module, font, original_path, false);
        require(reopened && *reopened, "Cannot reopen closed archive browser");
        SendMessageW(*reopened, WM_CLOSE, 0, 0);
        media::Frame oversized{1024, 768, std::vector<std::uint8_t>(1024 * 768 * 4, 255)};
        oversized.pixels[oversized.pixels.size() - 4] = 50;
        oversized.pixels[oversized.pixels.size() - 3] = 120;
        oversized.pixels[oversized.pixels.size() - 2] = 230;
        const auto large_pict = devtools::encode_pff_image(oversized);
        std::vector<std::uint8_t> large_archive{'P', 'F', 'F', ' '};
        word(large_archive, 1);
        word(large_archive, 20);
        word(large_archive, static_cast<std::uint32_t>(20 + large_pict.size()));
        word(large_archive, 99);
        large_archive.insert(large_archive.end(), large_pict.begin(), large_pict.end());
        devtools::write_new_asset(root / L"oversized.pff", large_archive);
        const auto large =
            devtools::open_pff_browser(parent, module, font, root / L"oversized.pff", false);
        actual_size(*large);
        const auto picture = GetDlgItem(*large, 4306);
        SCROLLINFO horizontal{sizeof(horizontal)}, vertical{sizeof(vertical)};
        horizontal.fMask = vertical.fMask = SIF_ALL;
        GetScrollInfo(picture, SB_HORZ, &horizontal);
        GetScrollInfo(picture, SB_VERT, &vertical);
        require(horizontal.nMax == 1023 && vertical.nMax == 767 && horizontal.nPage < 1024 &&
                    vertical.nPage < 768,
                "Oversized archive image did not expose both scrollbars");
        SendMessageW(picture, WM_HSCROLL, SB_BOTTOM, 0);
        SendMessageW(picture, WM_VSCROLL, SB_BOTTOM, 0);
        GetClientRect(picture, &picture_bounds);
        require(pixel(*large, picture_bounds.right - 1, picture_bounds.bottom - 1) ==
                    RGB(230, 120, 50),
                "Actual size scrolling did not reach bottom-right image pixel");
        const auto size = GetDlgItem(*large, 4308);
        SendMessageW(size, CB_SETCURSEL, 0, 0);
        SendMessageW(*large, WM_COMMAND, MAKEWPARAM(4308, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(size));
        require((GetWindowLongW(picture, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)) == 0,
                "Fit retained actual-size scrollbars");
        DestroyWindow(parent);
        parent = nullptr;
        require(!*large, "Owner destruction retained active archive browser");
        require(devtools::read_asset_bytes(original_path) == archive,
                "Archive browser changed original file");
        std::filesystem::remove_all(root);
        std::cout << "Asset PNG/PICT import/export and archive copy passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (parent) {
            DestroyWindow(parent);
        }
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
