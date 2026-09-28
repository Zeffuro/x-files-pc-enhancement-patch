#include "movie_preview.h"
#include "resources.h"
#include "devtools/preview.h"
#include "playback/grading.h"
#include <algorithm>
#include <memory>

namespace enhancements {
namespace {
struct Sample {
    const wchar_t* label;
    const wchar_t* filename;
    std::uint64_t end;
};

constexpr Sample samples[]{
    {L"Office portrait", L"19738.XMV", 2000},
    {L"Office doorway", L"19814.XMV", 2000},
    {L"Laboratory portrait", L"20083.XMV", 3000},
};

struct PreviewState {
    std::unique_ptr<devtools::Preview> player;
    std::filesystem::path root, path;
    std::vector<const Sample*> available;
    std::uint64_t end = 0;
    MovieContrast mode = MovieContrast::Off;
    bool playing = false;
    ULONGLONG tick = 0;
    std::wstring status;
};

class PreviewCanvas {
    HDC destination_;
    RECT bounds_;
    HDC dc_;
    HBITMAP bitmap_;
    HGDIOBJ previous_;

public:
    explicit PreviewCanvas(const DRAWITEMSTRUCT& item)
        : destination_(item.hDC), bounds_(item.rcItem), dc_(CreateCompatibleDC(item.hDC)),
          bitmap_(CreateCompatibleBitmap(item.hDC, bounds_.right - bounds_.left,
                                         bounds_.bottom - bounds_.top)),
          previous_(dc_ && bitmap_ ? SelectObject(dc_, bitmap_) : nullptr) {
        if (previous_) {
            SetViewportOrgEx(dc_, -bounds_.left, -bounds_.top, nullptr);
        }
    }

    ~PreviewCanvas() {
        if (previous_) {
            SelectObject(dc_, previous_);
        }
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
        if (dc_) {
            DeleteDC(dc_);
        }
    }

    HDC dc() const {
        return previous_ ? dc_ : nullptr;
    }

    void present() const {
        BitBlt(destination_, bounds_.left, bounds_.top, bounds_.right - bounds_.left,
               bounds_.bottom - bounds_.top, dc_, bounds_.left, bounds_.top, SRCCOPY);
    }
};

void load(HWND window, PreviewState& state, const Sample& sample) {
    const auto path = state.root / L"XV" / sample.filename;
    auto player = std::make_unique<devtools::Preview>(path.parent_path(), path.filename());
    if (!player->has_video()) {
        throw std::runtime_error("Select a movie with video.");
    }
    state.player = std::move(player);
    state.path = path;
    state.end = std::min(sample.end, state.player->duration());
    state.tick = GetTickCount64();
    InvalidateRect(GetDlgItem(window, IDC_PREVIEW_IMAGE), nullptr, FALSE);
}

void paint(HWND window, PreviewState& state, const DRAWITEMSTRUCT& item) {
    PreviewCanvas canvas(item);
    const auto dc = canvas.dc();
    if (!dc) {
        return;
    }
    FillRect(dc, &item.rcItem, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if (!state.player) {
        canvas.present();
        return;
    }
    const auto& frame = state.player->frame();
    if (frame.pixels.empty() || !frame.width || !frame.height) {
        canvas.present();
        return;
    }
    const auto relative =
        (state.path.parent_path().filename() / state.path.filename()).generic_string();
    playback::Grade grade;
    if (const auto image = state.player->image()) {
        for (const auto& track : state.player->movie().tracks) {
            if (track.id == image->track) {
                const auto& description =
                    track.descriptions.at(track.samples.at(image->sample).description);
                grade = playback::movie_grade(state.mode, relative, description.codec,
                                              track.samples.size());
                break;
            }
        }
    }
    auto corrected = frame.pixels;
    playback::apply_grade(corrected, grade);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = frame.width;
    info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    const int half = (item.rcItem.right - item.rcItem.left) / 2;
    const int height = item.rcItem.bottom - item.rcItem.top;
    const double scale =
        std::min((half - 4.0) / frame.width, static_cast<double>(height) / frame.height);
    const int width = static_cast<int>(frame.width * scale);
    const int image_height = static_cast<int>(frame.height * scale);
    const auto saved = SaveDC(dc);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    for (int side = 0; side < 2; ++side) {
        StretchDIBits(dc, item.rcItem.left + side * half + (half - width) / 2,
                      item.rcItem.top + (height - image_height) / 2, width, image_height, 0, 0,
                      frame.width, frame.height, side ? corrected.data() : frame.pixels.data(),
                      &info, DIB_RGB_COLORS, SRCCOPY);
    }
    if (saved) {
        RestoreDC(dc, saved);
    }
    canvas.present();
    const auto status =
        state.mode == MovieContrast::Off ? L"Original image. No color adjustment."
        : grade == playback::Grade{}     ? L"Reviewed colors retain the original for this scene."
        : state.mode == MovieContrast::Scene
            ? L"Reviewed settings applied for this movie."
            : L"Fixed contrast applied. Brightness, levels and color balance stay unchanged.";
    if (state.status != status) {
        state.status = status;
        SetDlgItemTextW(window, IDC_PREVIEW_STATUS, status);
    }
}

INT_PTR CALLBACK procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* state = reinterpret_cast<PreviewState*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            state = reinterpret_cast<PreviewState*>(data);
            SetWindowLongPtrW(window, DWLP_USER, data);
            for (const auto label : {L"Original (off)", L"Reviewed scene grades",
                                     L"Contrast +15% (Cinepak)", L"Contrast +25% (Cinepak)"}) {
                SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(label));
            }
            SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_SETCURSEL,
                                static_cast<WPARAM>(state->mode), 0);
            SetTimer(window, 1, 33, nullptr);
            for (const auto& sample : samples) {
                const auto relative = std::filesystem::path(L"XV") / sample.filename;
                if (playback::scene_grade(relative.generic_string()) != playback::Grade{} &&
                    std::filesystem::is_regular_file(state->root / relative)) {
                    state->available.push_back(&sample);
                    SendDlgItemMessageW(window, IDC_PREVIEW_SAMPLE, CB_ADDSTRING, 0,
                                        reinterpret_cast<LPARAM>(sample.label));
                }
            }
            if (!state->available.empty()) {
                SendDlgItemMessageW(window, IDC_PREVIEW_SAMPLE, CB_SETCURSEL, 0, 0);
                load(window, *state, *state->available.front());
            } else {
                EnableWindow(GetDlgItem(window, IDC_PREVIEW_PLAY), FALSE);
                SetDlgItemTextW(window, IDC_PREVIEW_STATUS,
                                L"Preview scenes are unavailable in this installation.");
            }
            return TRUE;
        }
        if (!state) {
            return FALSE;
        }
        if (message == WM_DRAWITEM && parameter == IDC_PREVIEW_IMAGE) {
            paint(window, *state, *reinterpret_cast<DRAWITEMSTRUCT*>(data));
            return TRUE;
        }
        if (message == WM_TIMER && state->player && state->playing) {
            const auto now = GetTickCount64();
            const auto position =
                state->player->time() + std::min<ULONGLONG>(now - state->tick, 250);
            state->tick = now;
            state->player->seek(position >= state->end ? 0 : position);
            InvalidateRect(GetDlgItem(window, IDC_PREVIEW_IMAGE), nullptr, FALSE);
            return TRUE;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(parameter)) {
                case IDC_MOVIE_CONTRAST:
                    if (HIWORD(parameter) == CBN_SELCHANGE) {
                        state->mode = static_cast<MovieContrast>(
                            SendDlgItemMessageW(window, IDC_MOVIE_CONTRAST, CB_GETCURSEL, 0, 0));
                        InvalidateRect(GetDlgItem(window, IDC_PREVIEW_IMAGE), nullptr, FALSE);
                    }
                    return TRUE;
                case IDC_PREVIEW_PLAY:
                    state->playing = !state->playing;
                    state->tick = GetTickCount64();
                    SetDlgItemTextW(window, IDC_PREVIEW_PLAY,
                                    state->playing ? L"Pause preview" : L"Play preview");
                    return TRUE;
                case IDC_PREVIEW_SAMPLE: {
                    if (HIWORD(parameter) == CBN_SELCHANGE) {
                        const auto index =
                            SendDlgItemMessageW(window, IDC_PREVIEW_SAMPLE, CB_GETCURSEL, 0, 0);
                        if (index >= 0 &&
                            static_cast<std::size_t>(index) < state->available.size()) {
                            load(window, *state, *state->available[index]);
                        }
                    }
                    return TRUE;
                }
                case IDOK:
                case IDCANCEL:
                    EndDialog(window, LOWORD(parameter));
                    return TRUE;
            }
        }
        if (message == WM_DESTROY) {
            KillTimer(window, 1);
        }
    } catch (const std::exception& error) {
        state->playing = false;
        SetDlgItemTextW(window, IDC_PREVIEW_PLAY, L"Play preview");
        MessageBoxA(window, error.what(), "Movie preview", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}
}

MovieContrast show_movie_preview(HWND owner, HMODULE module, MovieContrast mode) {
    std::vector<wchar_t> executable(32768);
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) {
        throw std::runtime_error("Cannot locate game movies.");
    }
    PreviewState state;
    state.mode = mode;
    state.root = std::filesystem::path(executable.data()).parent_path();
    const auto result = DialogBoxParamW(module, MAKEINTRESOURCEW(IDD_MOVIE_PREVIEW), owner,
                                        procedure, reinterpret_cast<LPARAM>(&state));
    if (result == -1) {
        throw std::runtime_error("Cannot open movie preview.");
    }
    return result == IDOK ? state.mode : mode;
}
}
