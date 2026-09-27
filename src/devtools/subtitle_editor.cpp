#include "subtitle_editor.h"
#include "subtitle_time.h"
#include "diagnostics/game_context.h"
#include "inspector_internal.h"
#include "platform/tool_cursor.h"
#include <algorithm>
#include <cwchar>
#include <stdexcept>

namespace devtools {
namespace {
constexpr int cues_id = 2601, install_id = 2603;
constexpr int seek_id = 2614, start_here_id = 2615, end_here_id = 2616, clock_id = 2617;
constexpr int first_id = 2618, last_id = 2619;
constexpr int begin_id = 2604, end_id = 2605, text_id = 2606;
constexpr int add_id = 2607, remove_id = 2608;
constexpr int previous_id = 2609, next_id = 2610, status_id = 2611, image_id = 2612, play_id = 2613;

struct Editor {
    std::filesystem::path root, relative;
    Preview* preview;
    HWND game;
    std::unique_ptr<Preview>* player;
    std::filesystem::path* selection;
    const std::vector<std::filesystem::path>* movies;
    std::vector<media::subtitles::Cue> cues;
    std::vector<media::subtitles::Cue> installed_cues;
    std::optional<std::size_t> selected;
    bool updating = false;
    bool changed = false;
    bool pending = false;
};

std::wstring value(HWND window, int id) {
    const auto control = GetDlgItem(window, id);
    std::wstring result(GetWindowTextLengthW(control) + 1, L'\0');
    result.resize(GetWindowTextW(control, result.data(), static_cast<int>(result.size())));
    return result;
}

std::wstring timestamp(std::uint64_t ms) {
    wchar_t result[24]{};
    swprintf_s(result, L"%02llu:%02llu:%02llu,%03llu",
               static_cast<unsigned long long>(ms / 3600000),
               static_cast<unsigned long long>(ms / 60000 % 60),
               static_cast<unsigned long long>(ms / 1000 % 60),
               static_cast<unsigned long long>(ms % 1000));
    return result;
}

std::wstring summary(const media::subtitles::Cue& cue, std::size_t index) {
    auto line = cue.text.substr(0, cue.text.find_first_of(L"\r\n"));
    if (line.size() > 55) {
        line.resize(55);
        line += L"...";
    }
    return std::to_wstring(index + 1) + L"    " + timestamp(cue.begin) + L" to " +
           timestamp(cue.end) + L"    " + line;
}

void select(HWND window, Editor& state, std::optional<std::size_t> index) {
    state.updating = true;
    state.selected = index;
    SendDlgItemMessageW(window, cues_id, LB_SETCURSEL,
                        index ? static_cast<WPARAM>(*index) : static_cast<WPARAM>(-1), 0);
    if (index) {
        const auto& cue = state.cues[*index];
        SetDlgItemTextW(window, begin_id, timestamp(cue.begin).c_str());
        SetDlgItemTextW(window, end_id, timestamp(cue.end).c_str());
        std::wstring lines;
        for (const auto c : cue.text) {
            if (c == L'\n') {
                lines += L'\r';
            }
            lines += c;
        }
        SetDlgItemTextW(window, text_id, lines.c_str());
    } else {
        for (const auto id : {begin_id, end_id, text_id}) {
            SetDlgItemTextW(window, id, L"");
        }
    }
    for (const auto id : {begin_id, end_id, text_id, remove_id, start_here_id, end_here_id}) {
        EnableWindow(GetDlgItem(window, id), index.has_value());
    }
    state.updating = false;
}

void refresh(HWND window, Editor& state, std::optional<std::size_t> selected) {
    state.updating = true;
    SendDlgItemMessageW(window, cues_id, LB_RESETCONTENT, 0, 0);
    for (std::size_t index = 0; index < state.cues.size(); ++index) {
        const auto row = summary(state.cues[index], index);
        SendDlgItemMessageW(window, cues_id, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(row.c_str()));
    }
    state.updating = false;
    select(window, state, selected);
}

void commit(HWND window, Editor& state) {
    if (!state.selected) {
        return;
    }
    auto cue = state.cues[*state.selected];
    cue.begin = subtitle_time(value(window, begin_id));
    cue.end = subtitle_time(value(window, end_id));
    if (cue.begin >= cue.end || cue.end > state.preview->duration()) {
        throw std::runtime_error("Cue times must be ordered and inside this movie.");
    }
    cue.text = value(window, text_id);
    cue.text.erase(std::remove(cue.text.begin(), cue.text.end(), L'\r'), cue.text.end());
    while (!cue.text.empty() && cue.text.front() == L'\n') {
        cue.text.erase(cue.text.begin());
    }
    while (!cue.text.empty() && cue.text.back() == L'\n') {
        cue.text.pop_back();
    }
    if (cue.text.empty() || cue.text.find_first_not_of(L" \n\t") == std::wstring::npos) {
        throw std::runtime_error("Enter caption text for this cue.");
    }
    state.cues[*state.selected] = std::move(cue);
}

std::vector<media::subtitles::Cue> checked_cues(HWND window, Editor& state) {
    commit(window, state);
    const auto text = media::subtitles::srt_text(state.cues, 1000);
    auto checked = media::subtitles::srt_cues(text, state.preview->duration());
    if (checked.size() != state.cues.size()) {
        throw std::runtime_error("A cue has invalid timing.");
    }
    return checked;
}

void add(HWND window, Editor& state) {
    commit(window, state);
    const auto duration = state.preview->duration();
    if (!duration) {
        throw std::runtime_error("This movie has no time available for a cue.");
    }
    const auto begin = std::min(state.preview->time(), duration - 1);
    const auto end = std::min(begin + 2000, duration);
    const auto at = std::upper_bound(state.cues.begin(), state.cues.end(), begin,
                                     [](auto time, const auto& cue) { return time < cue.begin; });
    const auto index = static_cast<std::size_t>(at - state.cues.begin());
    state.cues.insert(at, {begin, end, L"Enter subtitle here"});
    state.changed = true;
    refresh(window, state, index);
    SetFocus(GetDlgItem(window, text_id));
    SendDlgItemMessageW(window, text_id, EM_SETSEL, 0, -1);
}

void remove(HWND window, Editor& state) {
    if (!state.selected) {
        return;
    }
    const auto index = *state.selected;
    state.cues.erase(state.cues.begin() + index);
    state.changed = true;
    refresh(window, state,
            state.cues.empty() ? std::nullopt
                               : std::optional(std::min(index, state.cues.size() - 1)));
}

void apply(HWND window, Editor& state, bool install) {
    auto cues = checked_cues(window, state);
    if (install) {
        media::subtitles::install_text(state.root, state.relative,
                                       media::subtitles::srt_text(cues, 1000));
        state.changed = false;
        SetDlgItemTextW(window, status_id, L"Saved");
        if (cues.empty()) {
            cues = media::subtitles::native_cues(state.preview->movie());
            for (auto& cue : cues) {
                cue.begin = cue.begin * 1000 / state.preview->movie().timescale;
                cue.end = (cue.end * 1000 + state.preview->movie().timescale - 1) /
                          state.preview->movie().timescale;
            }
        }
        state.installed_cues = cues;
    } else {
        SetDlgItemTextW(window, status_id, L"Draft preview");
    }
    state.preview->captions(std::move(cues));
    state.pending = false;
    InvalidateRect(GetDlgItem(window, image_id), nullptr, FALSE);
}

void center_editor(HWND window);

INT_PTR CALLBACK confirm_draft(HWND window, UINT message, WPARAM value, LPARAM) {
    if (message == WM_INITDIALOG) {
        center_editor(window);
        return TRUE;
    }
    if (message == WM_COMMAND &&
        (LOWORD(value) == IDYES || LOWORD(value) == IDNO || LOWORD(value) == IDCANCEL)) {
        EndDialog(window, LOWORD(value));
        return TRUE;
    }
    return FALSE;
}

bool leave_draft(HWND window, Editor& state) {
    if (!state.changed) {
        return true;
    }
    const auto answer =
        DialogBoxParamW(inspector::state.module, MAKEINTRESOURCEW(2630), window, confirm_draft, 0);
    if (answer != IDYES && answer != IDNO) {
        return false;
    }
    if (answer == IDYES) {
        apply(window, state, true);
    } else {
        state.cues = state.installed_cues;
        state.preview->captions(state.installed_cues);
        state.changed = false;
        state.pending = false;
        refresh(window, state, state.cues.empty() ? std::nullopt : std::optional<std::size_t>(0));
    }
    return true;
}

void update_navigation(HWND window, const Editor& state) {
    const auto current = std::find(state.movies->begin(), state.movies->end(), state.relative);
    EnableWindow(GetDlgItem(window, previous_id),
                 current != state.movies->end() && current != state.movies->begin());
    EnableWindow(GetDlgItem(window, next_id),
                 current != state.movies->end() && std::next(current) != state.movies->end());
}

void navigate(HWND window, Editor& state, int step) {
    const auto current = std::find(state.movies->begin(), state.movies->end(), state.relative);
    if (current == state.movies->end() || (step < 0 && current == state.movies->begin()) ||
        (step > 0 && std::next(current) == state.movies->end()) || !leave_draft(window, state)) {
        return;
    }
    auto candidate = std::next(current, step);
    std::unique_ptr<Preview> next;
    for (;;) {
        next = std::make_unique<Preview>(state.root, *candidate);
        if (next->has_video()) {
            break;
        }
        if ((step < 0 && candidate == state.movies->begin()) ||
            (step > 0 && std::next(candidate) == state.movies->end())) {
            SetDlgItemTextW(window, status_id, L"No more video clips in this direction.");
            return;
        }
        std::advance(candidate, step);
    }
    const auto path = *candidate;
    state.preview->pause();
    *state.player = std::move(next);
    state.preview = state.player->get();
    state.relative = path;
    *state.selection = path;
    state.cues = state.preview->cues();
    state.installed_cues = state.cues;
    state.changed = false;
    state.pending = false;
    diagnostics::record_tool_context(L"subtitle editor " + path.generic_wstring());
    SetWindowTextW(window, (L"Subtitles - " + path.generic_wstring()).c_str());
    SetDlgItemTextW(window, status_id, L"");
    refresh(window, state, state.cues.empty() ? std::nullopt : std::optional<std::size_t>(0));
    update_navigation(window, state);
    InvalidateRect(GetDlgItem(window, image_id), nullptr, FALSE);
}

void center_editor(HWND window) {
    const auto owner = GetWindow(window, GW_OWNER);
    WINDOWINFO parent{sizeof(WINDOWINFO)}, editor{sizeof(WINDOWINFO)};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    // GetWindowRect can be translated into game coordinates by the display wrapper.
    if (!GetWindowInfo(owner, &parent) || !GetWindowInfo(window, &editor) ||
        !GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return;
    }
    const auto width = editor.rcWindow.right - editor.rcWindow.left;
    const auto height = editor.rcWindow.bottom - editor.rcWindow.top;
    const auto& work = monitor.rcWork;
    const auto x = std::clamp((parent.rcWindow.left + parent.rcWindow.right - width) / 2, work.left,
                              std::max(work.left, work.right - width));
    const auto y = std::clamp((parent.rcWindow.top + parent.rcWindow.bottom - height) / 2, work.top,
                              std::max(work.top, work.bottom - height));
    if (auto placement = BeginDeferWindowPos(1)) {
        placement = DeferWindowPos(placement, window, nullptr, x, y, 0, 0,
                                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (placement) {
            EndDeferWindowPos(placement);
        }
    }
}

INT_PTR CALLBACK dialog(HWND window, UINT message, WPARAM value, LPARAM data) {
    auto* state = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            state = reinterpret_cast<Editor*>(data);
            SetWindowLongPtrW(window, DWLP_USER, data);
            SetWindowTextW(window, (L"Subtitles - " + state->relative.generic_wstring()).c_str());
            center_editor(window);
            diagnostics::record_tool_context(L"subtitle editor " +
                                             state->relative.generic_wstring());
            state->cues = state->preview->cues();
            state->installed_cues = state->cues;
            SendDlgItemMessageW(window, begin_id, EM_SETLIMITTEXT, 24, 0);
            SendDlgItemMessageW(window, end_id, EM_SETLIMITTEXT, 24, 0);
            SendDlgItemMessageW(window, text_id, EM_SETLIMITTEXT, 4096, 0);
            std::optional<std::size_t> initial;
            for (std::size_t index = 0; index < state->cues.size(); ++index) {
                if (state->cues[index].begin <= state->preview->time() &&
                    state->preview->time() < state->cues[index].end) {
                    initial = index;
                    break;
                }
            }
            if (!initial && !state->cues.empty()) {
                initial = 0;
            }
            refresh(window, *state, initial);
            update_navigation(window, *state);
            SendDlgItemMessageW(window, seek_id, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
            SetTimer(window, 1, 33, nullptr);
            return TRUE;
        }
        if (message == WM_TIMER && state) {
            if (state->pending) {
                state->pending = false;
                try {
                    state->preview->captions(checked_cues(window, *state));
                    SetDlgItemTextW(window, status_id, L"Unsaved changes");
                    if (state->selected) {
                        const auto index = *state->selected;
                        const auto row = summary(state->cues[index], index);
                        SendDlgItemMessageW(window, cues_id, LB_DELETESTRING, index, 0);
                        SendDlgItemMessageW(window, cues_id, LB_INSERTSTRING, index,
                                            reinterpret_cast<LPARAM>(row.c_str()));
                        SendDlgItemMessageW(window, cues_id, LB_SETCURSEL, index, 0);
                    }
                } catch (const std::exception& error) {
                    SetDlgItemTextA(window, status_id, error.what());
                }
            }
            state->preview->update();
            const auto time = state->preview->time(), duration = state->preview->duration();
            SendDlgItemMessageW(window, seek_id, TBM_SETPOS, TRUE,
                                duration ? static_cast<LPARAM>(time * 1000 / duration) : 0);
            SetDlgItemTextW(window, clock_id,
                            (timestamp(time) + L" / " + timestamp(duration)).c_str());
            SetDlgItemTextW(window, play_id, state->preview->playing() ? L"Pause" : L"Play");
            InvalidateRect(GetDlgItem(window, image_id), nullptr, FALSE);
            return TRUE;
        }
        if (message == WM_HSCROLL && state &&
            reinterpret_cast<HWND>(data) == GetDlgItem(window, seek_id)) {
            const auto position = SendDlgItemMessageW(window, seek_id, TBM_GETPOS, 0, 0);
            state->preview->seek(state->preview->duration() * position / 1000);
            return TRUE;
        }
        if (message == WM_DRAWITEM && state) {
            const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(data);
            if (item->CtlID == image_id) {
                inspector::paint_frame(item->hDC, item->rcItem, &state->preview->frame(),
                                       state->preview->caption());
                return TRUE;
            }
        }
        if (message == WM_CLOSE && state) {
            if (leave_draft(window, *state)) {
                EndDialog(window, IDCANCEL);
            }
            return TRUE;
        }
        if (message != WM_COMMAND || !state) {
            return FALSE;
        }
        const auto id = LOWORD(value), event = HIWORD(value);
        if (id == IDCANCEL) {
            if (leave_draft(window, *state)) {
                EndDialog(window, IDCANCEL);
            }
            return TRUE;
        }
        if (id == cues_id && event == LBN_SELCHANGE && !state->updating) {
            const auto next = SendDlgItemMessageW(window, cues_id, LB_GETCURSEL, 0, 0);
            if (next == LB_ERR) {
                return TRUE;
            }
            try {
                commit(window, *state);
            } catch (...) {
                SendDlgItemMessageW(window, cues_id, LB_SETCURSEL,
                                    state->selected ? static_cast<WPARAM>(*state->selected)
                                                    : static_cast<WPARAM>(-1),
                                    0);
                throw;
            }
            refresh(window, *state, static_cast<std::size_t>(next));
            state->preview->seek(state->cues[static_cast<std::size_t>(next)].begin);
            return TRUE;
        }
        if ((id == begin_id || id == end_id || id == text_id) && event == EN_CHANGE &&
            !state->updating) {
            state->changed = true;
            state->pending = true;
            return TRUE;
        }
        if (id == start_here_id || id == end_here_id) {
            SetDlgItemTextW(window, id == start_here_id ? begin_id : end_id,
                            timestamp(state->preview->time()).c_str());
            return TRUE;
        }
        if (id == add_id || id == remove_id) {
            if (id == add_id) {
                add(window, *state);
            } else {
                remove(window, *state);
            }
            state->pending = true;
            return TRUE;
        }
        if (id == previous_id || id == next_id) {
            navigate(window, *state, id == previous_id ? -1 : 1);
            return TRUE;
        }
        if (id == first_id || id == last_id) {
            state->preview->pause();
            state->preview->seek(id == first_id ? 0 : state->preview->duration());
            return TRUE;
        }
        if (id == play_id) {
            if (state->preview->playing()) {
                state->preview->pause();
            } else {
                state->preview->play();
            }
            return TRUE;
        }
        if (id == install_id) {
            apply(window, *state, true);
            return TRUE;
        }

    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "Cannot apply subtitles", MB_OK | MB_ICONERROR);
        return TRUE;
    }
    return FALSE;
}
}

void edit_subtitles(HWND owner, HWND game, HMODULE module, const std::filesystem::path& root,
                    std::filesystem::path& relative, std::unique_ptr<Preview>& preview,
                    const std::vector<std::filesystem::path>& movies) {
    if (!preview->has_video()) {
        throw std::runtime_error(
            "Sound captions are not supported yet. Choose a movie with video.");
    }
    preview->pause();
    Editor state{root, relative, preview.get(), game, &preview, &relative, &movies};
    platform::ToolCursor cursor(game);
    if (DialogBoxParamW(module, MAKEINTRESOURCEW(2600), owner, dialog,
                        reinterpret_cast<LPARAM>(&state)) == -1) {
        throw std::runtime_error("Cannot open subtitle editor.");
    }
    preview->pause();
}
}
