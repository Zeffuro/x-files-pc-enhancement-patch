#include "enhancements/game_ui.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
POINT pointer{};
ULONGLONG tick = 1000;
unsigned moves = 0, clicks = 0, recorded = 0;
std::wstring recorded_text;
const enhancements::game::Edition* profile = &enhancements::game::dvd;
std::vector<RECT> test_evidence;
void* native_input = nullptr;

BOOL WINAPI cursor_position(POINT* point) {
    *point = pointer;
    return TRUE;
}

BOOL WINAPI move_pointer(int x, int y) {
    pointer = {x, y};
    ++moves;
    return TRUE;
}

BOOL WINAPI coordinates(HWND, POINT*) {
    return TRUE;
}

UINT WINAPI submit_input(UINT count, INPUT* events, int) {
    for (UINT i = 0; i < count; ++i) {
        clicks += events[i].type == INPUT_MOUSE && (events[i].mi.dwFlags & MOUSEEVENTF_LEFTDOWN);
    }
    return count;
}

SHORT WINAPI key_state(int) {
    return 0;
}

ULONGLONG WINAPI time_now() {
    return tick;
}

int WINAPI native_text(HDC, LPCSTR, int, LPRECT bounds, UINT) {
    return bounds ? bounds->bottom - bounds->top : 0;
}

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}
}

#define GetCursorPos cursor_position
#define SetCursorPos move_pointer
#define ScreenToClient coordinates
#define ClientToScreen coordinates
#define SendInput submit_input
#define GetAsyncKeyState key_state
#define GetTickCount64 time_now
#define DrawTextA native_text
#include "enhancements/dialogue.cpp"
#undef GetCursorPos
#undef SetCursorPos
#undef ScreenToClient
#undef ClientToScreen
#undef SendInput
#undef GetAsyncKeyState
#undef GetTickCount64
#undef DrawTextA

bool ImportHooks::install(HMODULE, const char*, Resolver) {
    return false;
}

void ImportHooks::remove() {}

Identity identify(const std::filesystem::path&) {
    return {};
}

std::string sha256(const std::filesystem::path&) {
    return {};
}

void trace_value(const char*, std::uint32_t) {}

namespace transcript {
void record_choice(std::wstring text) noexcept {
    ++recorded;
    recorded_text = std::move(text);
}
}

namespace enhancements {
void set_game_string_code_page(UINT) noexcept {}

UINT game_string_code_page() noexcept {
    return 1252;
}

UINT game_resource_code_page(std::string_view, bool japanese) noexcept {
    return japanese ? 932 : 1252;
}

int WINAPI draw_game_text(HDC dc, LPCSTR text, int length, LPRECT bounds, UINT format) noexcept {
    return native_text(dc, text, length, bounds, format);
}

BOOL WINAPI output_game_text(HDC, int, int, LPCSTR, int) noexcept {
    return FALSE;
}

BOOL WINAPI measure_game_text(HDC, LPCSTR, int, LPSIZE) noexcept {
    return FALSE;
}

void set_game_string_dvd_fallback(bool) noexcept {}

int WINAPI load_game_string(HINSTANCE, UINT, LPSTR, int) noexcept {
    return 0;
}

std::vector<RECT> evidence_bounds(const game::MainView*) {
    return test_evidence;
}

int inventory_target(std::span<const RECT> items, POINT cursor, int horizontal, int vertical) {
    if (horizontal || !vertical) {
        return -1;
    }
    int selected = -1;
    LONG distance = 10000;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto center = (items[i].top + items[i].bottom) / 2;
        const auto delta = (center - cursor.y) * vertical;
        if (delta > 0 && delta < distance) {
            distance = delta;
            selected = static_cast<int>(i);
        }
    }
    return selected;
}

namespace game {
const Edition& edition() {
    return *profile;
}

void* current_input() {
    return native_input;
}
}
}

namespace {
using namespace enhancements;

struct Row {
    std::string text;
    int height;
};

struct Fixture;
Fixture* active_fixture = nullptr;
void* selected_list = nullptr;

void** __stdcall current_list(void*) {
    return &selected_list;
}

struct Fixture {
    game::MainView view{};
    game::Application app{};
    game::Application* app_pointer = &app;
    alignas(game::ChoiceList) std::array<std::byte, 0x200> talk_storage{}, history_storage{};
    std::array<void*, 36> methods{};
    DrawList slot = nullptr;
    std::vector<Row> rows{{"First response", 16},
                          {"That blood worries me", 32},
                          {"A long wrapped response partly visible at the bottom", 32},
                          {"Fourth response", 16},
                          {"Fifth response", 32},
                          {"Sixth response", 16},
                          {"Last response", 32}};
    bool redraw_due = false;
    bool immediate_redraw = false, perpetual_scroll = false, fixed_step = false;
    unsigned up_calls = 0, down_calls = 0;

    Fixture() {
        clear_dialogue();
        pending_choice.cancel();
        pointer = {340, 100};
        test_evidence = {{300, 245, 340, 280}};
        native_input = nullptr;
        moves = clicks = recorded = 0;
        recorded_text.clear();
        tick = 1000;
        active_fixture = this;
        app.view = &view;
        application = &app_pointer;
        draw_slot = &slot;
        original_draw = render_native;
        talk_list = reinterpret_cast<game::ChoiceList*>(talk_storage.data());
        history_list = reinterpret_cast<game::ChoiceList*>(history_storage.data());
        selected_list = talk_list;
        methods[0x8c / sizeof(void*)] = reinterpret_cast<void*>(current_list);
        view.dialogue.elements_vtable = methods.data();
        view.dialogue.count = 1;
        scroll_up = native_up;
        scroll_down = native_down;
        for (auto* list : {talk_list, history_list}) {
            auto* bytes = reinterpret_cast<std::byte*>(list);
            const RECT viewport_bounds{235, 300, 450, 365};
            const RECT tab_bounds{235, 280, 320, 294};
            const POINT origin{230, 280};
            std::memcpy(bytes + profile->choice_viewport + 4, &viewport_bounds,
                        sizeof(viewport_bounds));
            std::memcpy(bytes + profile->choice_viewport + 24, &tab_bounds, sizeof(tab_bounds));
            std::memcpy(bytes + profile->choice_viewport - 12, &origin, sizeof(origin));
        }
        repaint();
    }

    ~Fixture() {
        clear_dialogue();
        active_fixture = nullptr;
        application = nullptr;
        draw_slot = nullptr;
        scroll_up = scroll_down = nullptr;
        selected_list = nullptr;
    }

    unsigned position() const {
        unsigned value = 0;
        const auto* bytes = reinterpret_cast<const std::byte*>(selected_list);
        std::memcpy(&value, bytes + profile->choice_viewport + 0x9c, sizeof(value));
        return value;
    }

    void position(unsigned value) {
        auto* bytes = reinterpret_cast<std::byte*>(selected_list);
        std::memcpy(bytes + profile->choice_viewport + 0x9c, &value, sizeof(value));
    }

    static int __stdcall render_native(game::Container*, void*, void*) {
        auto& fixture = *active_fixture;
        int top = 300;
        if (fixture.fixed_step) {
            top -= static_cast<int>(fixture.position()) * 22;
        } else {
            for (unsigned i = 0; i < (fixture.perpetual_scroll ? 0 : fixture.position()); ++i) {
                top -= fixture.rows[i].height + 6;
            }
        }
        for (const auto& row : fixture.rows) {
            RECT bounds{235, top, 450, top + row.height};
            draw_text(nullptr, row.text.c_str(), -1, &bounds, DT_WORDBREAK);
            top += row.height + 6;
        }
        return 1;
    }

    static void __stdcall native_up(game::ChoiceList* list) {
        auto& fixture = *active_fixture;
        require(list == selected_list, "Up used the wrong native list");
        ++fixture.up_calls;
        const auto value = fixture.position();
        if (value) {
            fixture.position(value - 1);
            fixture.redraw_due = true;
            if (fixture.immediate_redraw) {
                fixture.repaint();
            }
        }
    }

    static void __stdcall native_down(game::ChoiceList* list) {
        auto& fixture = *active_fixture;
        require(list == selected_list, "Down used the wrong native list");
        ++fixture.down_calls;
        const auto value = fixture.position();
        int bottom = 294;
        for (const auto& row : fixture.rows) {
            bottom += row.height + 6;
        }
        const bool more = fixture.fixed_step
                              ? bottom - static_cast<int>(value) * 22 > visible.viewport.bottom
                              : value + 1 < fixture.rows.size();
        if (fixture.perpetual_scroll || more) {
            fixture.position(value + 1);
            fixture.redraw_due = true;
            if (fixture.immediate_redraw) {
                fixture.repaint();
            }
        }
    }

    void repaint() {
        redraw_due = false;
        draw_list(&view.dialogue, nullptr, nullptr);
    }

    void settle() {
        for (unsigned i = 0; i < 40; ++i) {
            if (redraw_due) {
                repaint();
            }
            ++tick;
            update_dialogue(true);
            if (!redraw_due) {
                return;
            }
        }
        require(false, "Navigation failed to bound native scrolling");
    }

    void focus(unsigned index) {
        const auto& target = visible.choices[index];
        pointer = {(target.left + target.right) / 2, (target.top + target.bottom) / 2};
    }

    std::wstring selected() const {
        for (std::size_t i = 0; i < visible.count; ++i) {
            if (PtInRect(&visible.choices[i], pointer)) {
                return visible.text[i];
            }
        }
        return {};
    }

    void step(int direction) {
        require(navigate_dialogue(nullptr, direction, 0), "Visible dialogue rejected navigation");
        settle();
    }

    void arm() {
        focus(static_cast<unsigned>(visible.count - 1));
        const auto before = pointer;
        const auto generation = draw_generation;
        navigate_dialogue(nullptr, 1, 0);
        require(redraw_due && draw_generation == generation && pointer.x == before.x &&
                    pointer.y == before.y,
                "Navigation moved before the native deferred redraw");
    }
};

void capture_clipping() {
    Fixture fixture;
    require(visible.count == 3 && visible.choices[2].bottom == visible.viewport.bottom &&
                visible.text[2] == L"A long wrapped response partly visible at the bottom",
            "Production capture omitted or truncated the bottom-clipped multiline choice");
    fixture.position(1);
    fixture.repaint();
    require(visible.text.front() == L"That blood worries me",
            "Wholly offscreen response remained captured");
    Dialogue frame;
    const RECT clip{100, 100, 300, 150};
    Capture capture(frame, clip);
    RECT top{100, 80, 300, 115}, bottom{100, 140, 300, 180};
    RECT hidden{100, 150, 300, 180}, measured{100, 110, 300, 130};
    draw_text(nullptr, "Top clipped complete text", -1, &top, DT_WORDBREAK);
    draw_text(nullptr, "Bottom clipped complete text", -1, &bottom, DT_WORDBREAK);
    draw_text(nullptr, "Hidden", -1, &hidden, DT_WORDBREAK);
    draw_text(nullptr, "Measured", -1, &measured, DT_CALCRECT);
    require(frame.count == 2 && frame.choices[0].top == clip.top &&
                frame.choices[1].bottom == clip.bottom &&
                frame.text[0] == L"Top clipped complete text" &&
                frame.text[1] == L"Bottom clipped complete text",
            "Clipping accepted hidden/calculated text or lost complete row text");
}

void full_traversal(bool fixed_step) {
    Fixture fixture;
    fixture.fixed_step = fixed_step;
    fixture.focus(0);
    for (std::size_t i = 1; i < fixture.rows.size(); ++i) {
        const auto generation = draw_generation;
        const auto& text = fixture.rows[i].text;
        const std::wstring wanted(text.begin(), text.end());
        const bool hidden = std::find(visible.text.begin(), visible.text.begin() + visible.count,
                                      wanted) == visible.text.begin() + visible.count;
        fixture.step(1);
        require(fixture.selected() == wanted,
                "Down skipped or could not expose an overflowing response");
        if (hidden) {
            require(draw_generation > generation, "Hidden response selected without redrawing");
        }
    }
    fixture.step(1);
    require(fixture.selected() == L"Last response", "Native bottom clamp wrapped the list");
    for (std::size_t i = fixture.rows.size() - 1; i > 0; --i) {
        fixture.step(-1);
        const auto& text = fixture.rows[i - 1].text;
        require(fixture.selected() == std::wstring(text.begin(), text.end()),
                "Up skipped an earlier response or entered evidence before scrolling to top");
    }
    require(fixture.position() == 0 && fixture.up_calls && fixture.down_calls,
            "Traversal did not exercise both native scroll directions");
    fixture.step(-1);
    require(PtInRect(&test_evidence.front(), pointer),
            "True list-top Up did not bridge to evidence");
    fixture.step(1);
    require(fixture.selected() == L"First response", "Evidence Down did not return to choices");
    require(clicks == 0 && recorded == 0 && !pending_choice.pending(),
            "Response navigation activated a target or recorded a transcript choice");
}

void cancellation() {
    for (unsigned mode = 0; mode < 10; ++mode) {
        Fixture fixture;
        fixture.arm();
        const auto before_moves = moves;
        switch (mode) {
            case 0:
                update_dialogue(false);
                break;
            case 1:
                pointer.x += 5;
                break;
            case 2:
                tick += 2000;
                break;
            case 3:
                fixture.view.dialogue.count = 0;
                break;
            case 4:
                selected_list = history_list;
                break;
            case 5:
                begin_dialogue_click(nullptr, WM_LBUTTONDOWN);
                cancel_dialogue_click();
                break;
            case 6:
                require(scroll_dialogue(-1), "Wheel cancellation did not use native scroll");
                break;
            case 7:
                cancel_dialogue_click();
                break;
            case 8:
                native_input = reinterpret_cast<void*>(1);
                break;
            case 9:
                fixture.rows[2].text[0] = 'Z';
                break;
        }
        fixture.settle();
        require(!scroll_navigation && moves == before_moves,
                "Cancelled or stale scrolling moved the pointer after redraw");
    }
}

void fallback_history_transcript() {
    Fixture fixture;
    scroll_up = scroll_down = nullptr;
    fixture.focus(static_cast<unsigned>(visible.count - 1));
    fixture.step(1);
    require(fixture.selected() == L"First response", "Unavailable callbacks broke legacy wrap");
    fixture.focus(2);
    begin_dialogue_click(nullptr, WM_LBUTTONDOWN);
    native_input = reinterpret_cast<void*>(1);
    finish_dialogue_click(WM_LBUTTONUP);
    require(recorded == 1 &&
                recorded_text == L"A long wrapped response partly visible at the bottom",
            "Accepted clipped response lost its complete transcript text");
    native_input = nullptr;
    selected_list = history_list;
    fixture.repaint();
    require(visible.is_history, "History draw did not update dialogue identity");
    fixture.focus(0);
    fixture.step(1);
    require(fixture.selected() == L"That blood worries me", "History navigation lost row order");
    const auto before = clicks;
    require(navigate_dialogue(nullptr, 0, -1) && clicks == before + 1,
            "Talk tab navigation lost its native activation");
}

void redraw_edges() {
    {
        Fixture fixture;
        for (auto& row : fixture.rows) {
            row.height = 16;
        }
        const RECT bounds{235, 300, 450, 315};
        std::memcpy(fixture.talk_storage.data() + profile->choice_viewport + 4, &bounds,
                    sizeof(bounds));
        fixture.repaint();
        fixture.focus(0);
        fixture.step(1);
        require(fixture.selected() == L"That blood worries me",
                "Anchor scrolled out of view and lost the next response");
        fixture.step(-1);
        require(fixture.selected() == L"First response",
                "Anchor scrolled out of view and lost the preceding response");
    }
    {
        Fixture fixture;
        fixture.immediate_redraw = true;
        fixture.focus(2);
        fixture.step(1);
        require(fixture.selected() == L"Fourth response" && fixture.down_calls >= 2,
                "Synchronous redraw skipped a choice or failed to reveal the next row");
    }
    {
        Fixture fixture;
        fixture.rows[2].text = fixture.rows[1].text;
        fixture.repaint();
        fixture.focus(2);
        fixture.step(1);
        require(fixture.selected() == L"Fourth response",
                "Equal response labels confused the scrolling anchor");
    }
    {
        Fixture fixture;
        fixture.perpetual_scroll = true;
        fixture.arm();
        fixture.settle();
        require(fixture.down_calls == 32 && !scroll_navigation && clicks == 0,
                "Unchanging native row content did not terminate within the scroll bound");
    }
    {
        Fixture fixture;
        fixture.rows.clear();
        fixture.repaint();
        require(visible.count == 0 && navigate_dialogue(nullptr, 1, 0) && clicks == 0,
                "Empty response list activated a target or lost input ownership");
    }
    {
        Fixture fixture;
        fixture.arm();
        const auto before = clicks;
        require(navigate_dialogue(nullptr, 0, 1) && !scroll_navigation && clicks == before + 1,
                "Tab activation did not cancel deferred response navigation");
        const auto after = moves;
        fixture.settle();
        require(moves == after, "Cancelled tab navigation moved after a deferred redraw");
        fixture.position(0);
        fixture.repaint();
        fixture.arm();
        require(focus_conversation_evidence(nullptr) && !scroll_navigation,
                "Group focus did not cancel deferred scrolling");
        const auto group_moves = moves;
        fixture.settle();
        require(moves == group_moves && PtInRect(&test_evidence.front(), pointer),
                "Deferred redraw stole evidence group focus");
    }
    {
        Dialogue frame;
        const RECT clip{0, 0, 640, 480};
        Capture capture(frame, clip);
        for (unsigned i = 0; i < 40; ++i) {
            RECT bounds{20, 20, 40, 40};
            draw_text(nullptr, "Bounded", 7, &bounds, DT_WORDBREAK);
        }
        require(frame.count == frame.choices.size(), "Capture exceeded its fixed row capacity");
    }
}
}

int main() {
    try {
        for (const auto* edition : {&game::cd, &native_game::profile_cd_10019,
                                    &native_game::profile_cd_10020, &game::dvd}) {
            profile = edition;
            capture_clipping();
            full_traversal(false);
            full_traversal(true);
            cancellation();
            fallback_history_transcript();
            redraw_edges();
        }
        std::cout
            << "Production dialogue clipping, deferred scrolling and complete traversal passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
