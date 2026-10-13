#include "input_source.h"
#include "dialogue.h"
#include "game/layouts/script.h"
#include "game/functions/runtime.h"
#include "game/access/script.h"
#include "game_strings.h"
#include "game_ui.h"
#include "identity.h"
#include "platform/imports.h"
#include "runtime.h"
#include "focus.h"
#include "inventory.h"
#include "transcript/capture.h"
#include "transcript/selection.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace enhancements {
namespace {

using DrawList = native_game::NativeFunctions::DrawList;
using ScrollList = native_game::NativeFunctions::MainScrollUp;

ImportHooks imports;
DrawList original_draw = nullptr;
DrawList* draw_slot = nullptr;
game::ChoiceList* talk_list = nullptr;
game::ChoiceList* history_list = nullptr;
game::Application** application = nullptr;
ScrollList scroll_up = nullptr;
ScrollList scroll_down = nullptr;
RECT close_button{};
thread_local game::Container* visible_container = nullptr;
thread_local Dialogue* collecting = nullptr;
thread_local RECT viewport{};
thread_local Dialogue visible;
thread_local HWND dialogue_window = nullptr;
thread_local POINT return_cursor{};
thread_local POINT close_cursor{};
thread_local bool return_valid = false;
thread_local ULONGLONG closing_until = 0;
thread_local bool evidence_focus = false;
UINT text_code_page = 1252;
thread_local transcript::Selection pending_choice;
thread_local void* pending_input = nullptr;
thread_local unsigned draw_generation = 0;

struct ScrollNavigation {
    HWND window;
    game::Container* container;
    game::ChoiceList* list;
    void* input;
    std::uintptr_t anchor;
    std::wstring text;
    POINT cursor;
    int direction;
    unsigned generation;
    unsigned remaining;
    ULONGLONG deadline;
};

thread_local std::optional<ScrollNavigation> scroll_navigation;

struct Capture {
    Dialogue* previous;
    RECT previous_viewport;

    Capture(Dialogue& frame, const RECT& bounds)
        : previous(collecting), previous_viewport(viewport) {
        collecting = &frame;
        viewport = bounds;
    }

    ~Capture() {
        collecting = previous;
        viewport = previous_viewport;
    }
};

bool replace_draw(DrawList replacement) {
    DWORD protection = 0;
    if (!VirtualProtect(draw_slot, sizeof(*draw_slot), PAGE_READWRITE, &protection)) {
        return false;
    }
    *draw_slot = replacement;
    DWORD unused = 0;
    VirtualProtect(draw_slot, sizeof(*draw_slot), protection, &unused);
    return true;
}

int WINAPI draw_text(HDC dc, LPCSTR text, int length, LPRECT bounds, UINT format) {
    const auto result = DrawTextA(dc, text, length, bounds, format);
    if (collecting && result && !(format & DT_CALCRECT) && text && length != 0 && bounds) {
        RECT clipped{};
        if (IntersectRect(&clipped, bounds, &viewport) &&
            collecting->count < collecting->choices.size()) {
            const auto index = collecting->count++;
            collecting->choices[index] = clipped;
            // Native rows own distinct text buffers that survive scroll redraws.
            collecting->rows[index] = reinterpret_cast<std::uintptr_t>(text);
            const auto size = length < 0 ? static_cast<int>(std::strlen(text)) : length;
            const auto count = MultiByteToWideChar(text_code_page, 0, text, size, nullptr, 0);
            if (count > 0) {
                collecting->text[index].resize(count);
                MultiByteToWideChar(text_code_page, 0, text, size, collecting->text[index].data(),
                                    count);
            }
        }
    }
    return result;
}

FARPROC resolve(const char* name) {
    if (std::strcmp(name, "DrawTextA") == 0) {
        return reinterpret_cast<FARPROC>(draw_text);
    }
    return std::strcmp(name, "LoadStringA") == 0 ? reinterpret_cast<FARPROC>(load_game_string)
                                                 : nullptr;
}

int __stdcall draw_list(game::Container* object, void* context, void* clip) {
    if (!object->count) {
        return original_draw(object, context, clip);
    }
    const auto list = static_cast<game::ChoiceList*>(object->current());
    if (list != talk_list && list != history_list) {
        return original_draw(object, context, clip);
    }

    if (!visible_container) {
        return_valid =
            GetCursorPos(&return_cursor) && ScreenToClient(dialogue_window, &return_cursor);
    }

    Dialogue frame;
    frame.talk = talk_list->tab_for(game::edition());
    frame.history = history_list->tab_for(game::edition());
    frame.is_history = list == history_list;
    frame.panel = list->panel_for(game::edition());
    frame.viewport = list->viewport_for(game::edition()).bounds;
    int result;
    {
        Capture capture(frame, frame.viewport);
        result = original_draw(object, context, clip);
    }

    struct Row {
        RECT bounds;
        std::wstring text;
        std::uintptr_t identity;
    };

    std::vector<Row> rows;
    for (std::size_t i = 0; i < frame.count; ++i) {
        rows.push_back({frame.choices[i], std::move(frame.text[i]), frame.rows[i]});
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.bounds.top < b.bounds.top; });
    rows.erase(std::unique(rows.begin(), rows.end(),
                           [](const auto& a, const auto& b) {
                               return a.bounds.top == b.bounds.top &&
                                      a.bounds.bottom == b.bounds.bottom;
                           }),
               rows.end());
    frame.count = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        frame.choices[i] = rows[i].bounds;
        frame.text[i] = std::move(rows[i].text);
        frame.rows[i] = rows[i].identity;
    }
    visible = frame;
    ++draw_generation;
    visible_container = object;
    close_button = list->close_for(game::edition());
    return result;
}

bool point_at(HWND window, const RECT& bounds, bool activate) {
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return false;
    }
    POINT point{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    if (!ClientToScreen(window, &point) || !move_controller_pointer(point.x, point.y)) {
        return false;
    }
    if (activate) {
        return input::injected_input().click(false, controller_event).started();
    }
    return true;
}

bool advance_scroll(ScrollNavigation& request) {
    if (!scroll_up || !scroll_down || !request.remaining) {
        return false;
    }
    const auto position =
        native_game::ChoiceListRef(request.list, &game::edition()).scroll_position().data();
    std::uint32_t before = 0, after = 0;
    std::memcpy(&before, position, sizeof(before));
    request.generation = draw_generation;
    --request.remaining;
    pending_choice.cancel();
    (request.direction < 0 ? scroll_up : scroll_down)(request.list);
    std::memcpy(&after, position, sizeof(after));
    return before != after;
}

void resume_scroll() {
    if (!scroll_navigation) {
        return;
    }
    const auto frame = current_dialogue();
    POINT cursor{};
    if (!frame || visible_container != scroll_navigation->container ||
        visible_container->current() != scroll_navigation->list ||
        game::current_input() != scroll_navigation->input ||
        GetTickCount64() >= scroll_navigation->deadline || !GetCursorPos(&cursor) ||
        !ScreenToClient(scroll_navigation->window, &cursor) ||
        cursor.x != scroll_navigation->cursor.x || cursor.y != scroll_navigation->cursor.y) {
        scroll_navigation.reset();
        return;
    }
    if (draw_generation == scroll_navigation->generation || !frame->count) {
        return;
    }
    auto request = *scroll_navigation;
    const auto begin = frame->rows.begin();
    const auto end = begin + frame->count;
    const auto anchor = std::find(begin, end, request.anchor);
    int selected = request.direction > 0 ? 0 : static_cast<int>(frame->count) - 1;
    if (anchor != end) {
        if (frame->text[anchor - begin] != request.text) {
            scroll_navigation.reset();
            return;
        }
        selected = static_cast<int>(anchor - begin) + request.direction;
        if (selected < 0 || selected >= static_cast<int>(frame->count)) {
            if (advance_scroll(request)) {
                scroll_navigation = std::move(request);
                return;
            }
            selected = static_cast<int>(anchor - begin);
        }
    }
    scroll_navigation.reset();
    point_at(request.window, visible.choices[selected], false);
}

}

void attach_dialogue(HWND window) {
    dialogue_window = window;
    if (draw_slot) {
        return;
    }
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) {
        return;
    }
    try {
        const auto identity = identify(path.data());
        if (!identity.build || !identity.build->profile) {
            return;
        }
        text_code_page = identity.build->id == native_game::BuildId::cd_10020 ? 932 : 1252;
        set_game_string_code_page(text_code_page);
        set_game_string_dvd_fallback(identity.build->id == native_game::BuildId::dvd_20000);
        auto* base = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        const auto& addresses = *identity.build->profile;
        const native_game::NativeRuntime functions(base, addresses);
        draw_slot = functions.draw_slot().get();
        original_draw = functions.draw_list_target();
        talk_list = functions.talk_list().get();
        history_list = functions.history_list().get();
        application = functions.application().get();
        scroll_up = functions.main_scroll_up_target();
        scroll_down = functions.main_scroll_down_target();
        if (*draw_slot != original_draw ||
            !imports.install(GetModuleHandleW(nullptr), "USER32.dll", resolve) ||
            !replace_draw(draw_list)) {
            imports.remove();
            draw_slot = nullptr;
        }
    } catch (...) {
        trace_value("dialogue_navigation_unavailable", 1);
    }
}

void detach_dialogue() {
    set_game_string_dvd_fallback(false);
    if (draw_slot) {
        replace_draw(original_draw);
        draw_slot = nullptr;
    }
    imports.remove();
    scroll_up = scroll_down = nullptr;
    clear_dialogue();
}

game::MainView* game::current_view() {
    return draw_slot && application && *application ? (*application)->view : nullptr;
}

const Dialogue* current_dialogue() {
    if (!visible_container || !application || !*application) {
        return nullptr;
    }
    const auto ui = (*application)->view;
    // Lists redraw only when changed, so their native container determines visibility.
    if (!ui || &ui->dialogue != visible_container || !ui->dialogue.count) {
        clear_dialogue();
        return nullptr;
    }
    return &visible;
}

void clear_dialogue() {
    scroll_navigation.reset();
    visible_container = nullptr;
    visible = {};
    return_valid = false;
    closing_until = 0;
    evidence_focus = false;
}

void update_dialogue(bool focused) {
    if (!focused) {
        pending_choice.cancel();
        scroll_navigation.reset();
    }
    if (closing_until) {
        POINT cursor{};
        if (!focused || GetTickCount64() >= closing_until || !GetCursorPos(&cursor) ||
            !ScreenToClient(dialogue_window, &cursor) || cursor.x != close_cursor.x ||
            cursor.y != close_cursor.y) {
            closing_until = 0;
        } else {
            const auto view = game::current_view();
            if (view && &view->dialogue == visible_container && !view->dialogue.count) {
                auto point = return_cursor;
                if (return_valid && ClientToScreen(dialogue_window, &point)) {
                    move_controller_pointer(point.x, point.y);
                }
            }
        }
    }
    current_dialogue();
    if (focused) {
        resume_scroll();
    }
    finish_dialogue_click(0);
}

void begin_dialogue_click(HWND window, UINT message) {
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONDBLCLK) {
        return;
    }
    pending_choice.cancel();
    scroll_navigation.reset();
    const auto frame = current_dialogue();
    POINT point{};
    if (!frame || !GetCursorPos(&point) || !ScreenToClient(window, &point)) {
        return;
    }
    for (std::size_t i = 0; i < frame->count; ++i) {
        if (PtInRect(&frame->choices[i], point)) {
            pending_choice.begin(frame->text[i], GetTickCount64());
            pending_input = game::current_input();
            trace_value("transcript_choice_pending",
                        static_cast<std::uint32_t>(frame->text[i].size()));
            break;
        }
    }
}

void finish_dialogue_click(UINT message) {
    if (!pending_choice.pending()) {
        return;
    }
    if (message == WM_LBUTTONUP) {
        pending_choice.release(GetTickCount64());
    }
    // Native acceptance removes the choice panel or changes its input state.
    const auto frame = current_dialogue();
    auto selected =
        pending_choice.update(!frame || game::current_input() != pending_input, GetTickCount64());
    if (!selected.empty()) {
        transcript::record_choice(std::move(selected));
        trace_value("transcript_choice_accepted", 1);
    }
}

void cancel_dialogue_click() {
    scroll_navigation.reset();
    pending_choice.cancel();
}

bool close_dialogue(HWND window) {
    scroll_navigation.reset();
    pending_choice.cancel();
    if (!current_dialogue()) {
        return false;
    }
    // The close-box action also updates conversation state outside the panel.
    if (closing_until) {
        return true;
    }
    if (point_at(window, close_button, true)) {
        close_cursor = {(close_button.left + close_button.right) / 2,
                        (close_button.top + close_button.bottom) / 2};
        closing_until = GetTickCount64() + 1000;
    }
    return true;
}

bool navigate_dialogue(HWND window, int direction, int tab) {
    if (tab) {
        scroll_navigation.reset();
    } else {
        resume_scroll();
        if (scroll_navigation) {
            return true;
        }
    }
    const auto* dialogue = current_dialogue();
    if (!dialogue) {
        return false;
    }
    const auto items = conversation_evidence();
    POINT cursor{};
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
        return true;
    }
    const auto evidence = std::find_if(items.begin(), items.end(), [&](const RECT& item) {
        return PtInRect(&item, cursor) != FALSE;
    });
    if (evidence != items.end()) {
        evidence_focus = true;
        auto targets = items;
        if (direction && !tab) {
            targets.insert(targets.end(), dialogue->choices.begin(),
                           dialogue->choices.begin() + dialogue->count);
        }
        const auto next = inventory_target(targets, cursor, tab, direction);
        if (next >= 0) {
            evidence_focus = static_cast<std::size_t>(next) < items.size();
            point_at(window, targets[next], false);
        }
        return true;
    }
    evidence_focus = false;
    if (tab) {
        point_at(window, tab < 0 ? dialogue->talk : dialogue->history, true);
        return true;
    }
    if (!direction || !dialogue->count) {
        return true;
    }
    const auto count = static_cast<int>(dialogue->count);
    int selected = direction > 0 ? 0 : count - 1;
    for (int index = 0; index < count; ++index) {
        if (PtInRect(&dialogue->choices[index], cursor)) {
            const auto boundary = index + direction < 0 || index + direction >= count;
            if (boundary && scroll_up && scroll_down) {
                const auto list = static_cast<game::ChoiceList*>(visible_container->current());
                if (list == (dialogue->is_history ? history_list : talk_list)) {
                    ScrollNavigation request{window,
                                             visible_container,
                                             list,
                                             game::current_input(),
                                             dialogue->rows[index],
                                             dialogue->text[index],
                                             cursor,
                                             direction,
                                             draw_generation,
                                             32,
                                             GetTickCount64() + 1000};
                    if (advance_scroll(request)) {
                        scroll_navigation = std::move(request);
                        resume_scroll();
                        return true;
                    }
                    if (direction > 0) {
                        return true;
                    }
                }
            }
            if (index == 0 && direction < 0) {
                const auto next = inventory_target(items, cursor, 0, -1);
                if (next >= 0) {
                    evidence_focus = point_at(window, items[next], false);
                    return true;
                }
            }
            selected = (index + direction + count) % count;
            break;
        }
    }
    point_at(window, dialogue->choices[selected], false);
    return true;
}

bool scroll_dialogue(int direction) {
    scroll_navigation.reset();
    const auto frame = current_dialogue();
    if (!frame || !direction || !scroll_up || !scroll_down) {
        return false;
    }
    const auto list = static_cast<game::ChoiceList*>(visible_container->current());
    if (list != (frame->is_history ? history_list : talk_list)) {
        return false;
    }
    pending_choice.cancel();
    // Native arrow callbacks clamp and redraw without activating a response.
    (direction < 0 ? scroll_up : scroll_down)(list);
    return true;
}

std::vector<RECT> conversation_evidence() {
    const auto view = game::current_view();
    if (!current_dialogue() || !view) {
        return {};
    }
    return evidence_bounds(view);
}

bool focus_conversation_evidence(HWND window) {
    scroll_navigation.reset();
    const auto dialogue = current_dialogue();
    if (!dialogue) {
        return false;
    }
    const auto items = conversation_evidence();
    if (evidence_focus || items.empty()) {
        evidence_focus = false;
        if (dialogue->count) {
            point_at(window, dialogue->choices.front(), false);
        } else {
            point_at(window, dialogue->talk, true);
        }
    } else {
        evidence_focus = point_at(window, items.front(), false);
    }
    return true;
}

}
