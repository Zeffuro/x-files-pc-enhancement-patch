#include "transcript/page.h"
#include "platform/game_fonts.h"
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

void screenshot(HDC dc, const std::filesystem::path& path) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 640;
    info.bmiHeader.biHeight = -480;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<std::uint8_t> pixels(640 * 480 * 4);
    require(GetDIBits(dc, static_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP)), 0, 480,
                      pixels.data(), &info, DIB_RGB_COLORS) != 0,
            "Cannot capture transcript page");
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1) {
            platform::register_private_font(std::filesystem::path(argv[1]) / L"HCD.TTR");
        }
        transcript::Page page;
        const auto note = transcript::edition_note(argc > 1 ? argv[1] : L"missing-game");
        require(note.empty() || note == L"Dialogue captions may be unavailable in this edition.",
                "Edition note is not concise");
        require(transcript::edition_note(L"missing-game").empty(),
                "Unknown editions have a wordy header note");
        page.draw(nullptr, {}, note, 0);
        require(page.pages() == 1, "Empty transcript cannot be viewed");
        std::vector<std::wstring> entries{L"Selected: What can you tell me about the case?",
                                          L"A.D. Skinner, you've got a call on line one.",
                                          L"All right. Tell him I'll call him back.",
                                          L"Selected: Where were Mulder and Scully last seen?",
                                          L"Their last known location was Everett, Washington."};
        page.draw(nullptr, entries, note, 0);
        if (argc > 2) {
            screenshot(page.dc(), argv[2]);
            page.draw(nullptr, entries, L"Dialogue captions may be unavailable in this edition.",
                      0);
            auto warning_path = std::filesystem::path(argv[2]);
            warning_path.replace_filename(warning_path.stem().wstring() + L"-edition-note.bmp");
            screenshot(page.dc(), warning_path);
        }
        entries.assign(15, L"A short dialogue line.");
        page.draw(nullptr, entries, L"", 0);
        require(page.pages() == 1, "Short entries still use blank rows");
        entries = {std::wstring(2000, L'W'), L"Final entry"};
        page.draw(nullptr, entries, note, 0);
        require(page.pages() > 1, "Long dialogue was truncated instead of paginated");
        page.draw(nullptr, entries, note, page.pages() - 1);
        page.draw(nullptr, entries, note, 99999);
        require(page.pages() > 1, "Last-page navigation lost dialogue");
        std::cout << "Transcript layout passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
