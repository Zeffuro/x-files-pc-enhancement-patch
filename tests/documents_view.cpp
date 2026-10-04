#include "enhancements/documents.h"
#include "enhancements/game_ui.h"
#include "enhancements/game_resources.h"
#include "enhancements/scene_overlay.h"
#include "game/render/native_render.h"
#include "playback/fast_forward.h"
#include "settings.h"
#include <cstdint>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace {
POINT fixture_cursor{};
bool fixture_alt = false;

BOOL WINAPI fixture_get_cursor(LPPOINT point) {
    *point = fixture_cursor;
    return TRUE;
}

BOOL WINAPI fixture_coordinates(HWND, LPPOINT) {
    return TRUE;
}

SHORT WINAPI fixture_key_state(int key) {
    return key == VK_MENU && fixture_alt ? SHORT(-32768) : SHORT(0);
}
}

#define GetCursorPos fixture_get_cursor
#define ScreenToClient fixture_coordinates
#define GetKeyState fixture_key_state
#include "enhancements/documents.cpp"
#undef GetCursorPos
#undef ScreenToClient
#undef GetKeyState

namespace {
using namespace enhancements;
game::ScriptControls controls;
Settings options;
native_game::CanvasSource reader = nullptr;
bool native_input = false, native_valid = true, paused = false, browser = false;
bool transcript_open = false, entry = false, confirmation = false, quick = false;
unsigned pauses = 0, resumes = 0, suspended = 0;

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

std::uint64_t canvas_hash(HDC dc) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 640;
    info.bmiHeader.biHeight = -480;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<unsigned char> pixels(640 * 480 * 4);
    require(GetDIBits(dc, static_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP)), 0, 480,
                      pixels.data(), &info, DIB_RGB_COLORS) != 0,
            "Cannot compare reader frames");
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto pixel : pixels) {
        hash = (hash ^ pixel) * 1099511628211ULL;
    }
    return hash;
}
}

const Settings& settings() {
    return options;
}

namespace native_game {
bool native_render_available() {
    return true;
}

void set_canvas_reader(CanvasSource callback) {
    reader = callback;
}

void invalidate_canvas() {}
}

namespace enhancements::game {
const Edition& edition() {
    return dvd;
}

std::uintptr_t input_vtable() {
    return native_input ? dvd.main_menu : 0;
}

bool menu_confirmation_active() {
    return confirmation;
}

ScriptControls script_controls() {
    return controls;
}
}

namespace enhancements {
bool text_entry_busy() {
    return entry;
}

void suspend_controller() {
    ++suspended;
}

SceneOverlay::~SceneOverlay() {}

bool scene_overlay_available(bool) {
    return !paused && native_valid;
}

bool SceneOverlay::begin(bool) {
    if (paused || !native_valid) {
        return false;
    }
    paused = true;
    ++pauses;
    return true;
}

bool SceneOverlay::valid() const {
    return paused && native_valid;
}

void SceneOverlay::end(bool abandon) {
    if (paused && native_valid && !abandon) {
        ++resumes;
    }
    paused = false;
}
}

namespace enhancements::quick_menu {
void dismiss() {
    quick = false;
}

bool expanded() {
    return quick;
}
}

namespace saves {
bool browser_active() {
    return browser;
}
}

namespace transcript {
bool active() {
    return transcript_open;
}
}

int main() {
    HWND window = nullptr;
    try {
        window = CreateWindowExW(0, L"STATIC", L"Document input test", WS_POPUP, 0, 0, 640, 480,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Cannot create a hidden input fixture");
        const auto send = [&](UINT id, WPARAM value = 0, LPARAM data = 0) {
            return documents::message(window, id, value, data);
        };
        const auto click_text = [&] {
            fixture_cursor = {300, 200};
            return send(WM_LBUTTONDOWN) && send(WM_LBUTTONUP) && documents::active();
        };
        controls.resources = {resource::pda_message};
        controls.document_text = {{{200, 90, 410, 340}, "Current note\n" + std::string(3000, 'W')}};
        documents::update(window);
        require(reader && documents::available(), "Reader failed to attach to a current note");
        require(!send(WM_KEYDOWN, VK_F7) && !send(WM_KEYDOWN, VK_F7, LPARAM{1} << 30) &&
                    !send(WM_KEYUP, VK_F7) && !documents::active() && pauses == 0,
                "F7 opened the reader or consumed native input");
        const auto background = reinterpret_cast<HDC>(0x1234);
        require(reader(background) == background,
                "An unopened reader added a canvas overlay over native message text");
        fixture_cursor = {100, 200};
        require(!send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP) && !documents::active(),
                "A click outside the message body was consumed by the reader");
        controls.buttons = {{300, 180, 320, 220}};
        fixture_cursor = {310, 200};
        require(!send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP) && !documents::active(),
                "A native arrow inside the body opened the reader or lost its click");
        require(!send(WM_LBUTTONDOWN), "The reader took a native control press");
        fixture_cursor = {350, 200};
        require(!send(WM_LBUTTONUP) && !documents::active(),
                "The reader took a native control release after dragging into text");
        require(send(WM_LBUTTONDOWN), "A text press did not belong to the reader");
        fixture_cursor = {100, 200};
        require(send(WM_LBUTTONUP) && !documents::active(),
                "A reader-owned release leaked after dragging outside the body");
        require(!send(WM_LBUTTONUP), "The reader retained a completed text press");
        fixture_cursor = {350, 200};
        require(send(WM_LBUTTONDOWN), "A second text press did not belong to the reader");
        fixture_cursor = {310, 200};
        require(send(WM_LBUTTONUP) && !documents::active(),
                "Dragging a text press onto a native arrow opened the reader");
        controls.buttons.clear();
        fixture_cursor = {300, 200};
        require(send(WM_LBUTTONDOWN), "Cannot begin a text press before focus loss");
        send(WM_KILLFOCUS);
        send(WM_SETFOCUS);
        require(send(WM_LBUTTONUP) && !documents::active(),
                "A text release after focus loss activated the reader or leaked input");
        controls.buttons = {{300, 180, 320, 220}};
        for (const POINT target : {POINT{100, 200}, POINT{310, 200}}) {
            fixture_cursor = {350, 200};
            require(send(WM_LBUTTONDOWN), "Cannot begin a reader gesture before focus loss");
            send(WM_KILLFOCUS);
            send(WM_SETFOCUS);
            fixture_cursor = target;
            require(!send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP) && !documents::active(),
                    "A stale reader gesture consumed a new native click after focus loss");
        }
        controls.buttons.clear();
        fixture_cursor = {300, 200};
        const auto epoch = playback::movie_speed_input.context_epoch;
        require(send(WM_LBUTTONDOWN) && send(WM_LBUTTONUP) && documents::active(),
                "Clicking message text did not open the document");
        require(pauses == 1 && suspended == 1 && playback::movie_speed_input.context_epoch > epoch,
                "Read did not pause the scene and suspend held input");
        require(reader(nullptr) != nullptr, "Reader supplied no native canvas");
        for (const auto key : {VK_RETURN, VK_TAB, VK_MENU}) {
            require(!send(WM_SYSKEYDOWN, key, LPARAM{1} << 29) &&
                        !send(WM_SYSKEYUP, key, LPARAM{1} << 29) && documents::active(),
                    "Reader swallowed Alt+Enter, Alt+Tab or AltGr");
        }
        require(!send(WM_KEYDOWN, 'E', LPARAM{1} << 29) && !send(WM_KEYUP, 'E', LPARAM{1} << 29) &&
                    documents::active(),
                "Reader swallowed an AltGr key carrying the native Alt context");
        fixture_alt = true;
        require(!send(WM_KEYDOWN, VK_RETURN) && documents::active(),
                "Reader swallowed a system shortcut indicated by the native Alt state");
        fixture_alt = false;
        const auto original = canvas_hash(reader(nullptr));
        send(WM_MOUSEWHEEL, MAKEWPARAM(0, -60));
        send(WM_KILLFOCUS);
        send(WM_SETFOCUS);
        send(WM_MOUSEWHEEL, MAKEWPARAM(0, -60));
        require(canvas_hash(reader(nullptr)) == original,
                "A half-detent survived reader focus loss and scrolled on return");
        send(WM_ACTIVATEAPP, FALSE);
        send(WM_ACTIVATEAPP, TRUE);
        send(WM_MOUSEWHEEL, MAKEWPARAM(0, -60));
        send(WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -60));
        require(canvas_hash(reader(nullptr)) == original,
                "Normal scrolling remainder changed type size after entering Ctrl zoom");
        send(WM_MOUSEWHEEL, MAKEWPARAM(0, -60));
        require(canvas_hash(reader(nullptr)) == original,
                "Ctrl zoom remainder scrolled the document after changing modes");
        send(WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA));
        require(canvas_hash(reader(nullptr)) != original,
                "A complete Ctrl wheel detent failed to change reader type size");
        require(send(WM_KEYDOWN, VK_F7) && send(WM_KEYDOWN, VK_F7, LPARAM{1} << 30) &&
                    documents::active(),
                "F7 closed the reader instead of remaining inactive");
        require(send(WM_KEYDOWN, VK_END) && send(WM_KEYDOWN, VK_OEM_PLUS) &&
                    send(WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA)),
                "Reader did not own its size or scroll controls");
        reader(nullptr);
        require(send(WM_KEYDOWN, VK_ESCAPE) && !documents::active() && resumes == 1,
                "Closing Read did not resume its paused scene");
        require(click_text(), "Cannot reopen Read by clicking text");
        send(WM_LBUTTONDOWN);
        send(WM_KEYDOWN, VK_ESCAPE);
        require(send(WM_LBUTTONUP), "A held mouse release leaked after closing Read");
        require(click_text(), "Cannot reopen for document replacement");
        controls.document_text[0].value = "Different note";
        documents::update(window);
        require(!documents::active() && resumes == 3,
                "Reader retained a stale document after its source changed");
        require(click_text(), "Cannot reopen for invalidation");
        native_valid = false;
        documents::update(window);
        require(!documents::active() && resumes == 3,
                "Reader resumed an invalid replacement scene");
        native_valid = true;
        native_input = true;
        require(!documents::available() && !send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP),
                "Reader opened over a native input context");
        native_input = false;
        for (auto* blocker : {&browser, &transcript_open, &entry, &confirmation}) {
            *blocker = true;
            require(!documents::available(), "Reader bypassed an active overlay or confirmation");
            *blocker = false;
        }
        options.readable_documents = false;
        require(!documents::available(), "Disabled Read remained available");
        options.readable_documents = true;
        documents::update(window, false);
        require(reader(nullptr) == nullptr,
                "An unopened reader added an overlay outside the foreground");
        documents::update(window);
        require(click_text(), "Cannot open before settings dismissal");
        documents::release();
        require(!reader && !documents::active() && !paused && resumes == 4,
                "Settings dismissal left the original scene paused");
        documents::update(window);
        require(click_text(), "Cannot reopen after releasing the surface");
        documents::release(true);
        require(resumes == 4 && !paused && !reader,
                "Detach cleanup resumed a scene owned by the native lifecycle");
        controls.document_text = {{{200, 90, 410, 340}, "First message"}};
        documents::update(window);
        fixture_cursor = {300, 200};
        require(send(WM_LBUTTONDOWN), "Cannot begin a press before native text replacement");
        controls.document_text[0] = {{200, 90, 410, 150}, "Replacement message"};
        require(send(WM_LBUTTONUP) && !documents::active(),
                "A text release opened a replacement document outside its current bounds");
        fixture_cursor = {300, 200};
        require(!send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP) && !documents::active(),
                "A click used stale document bounds before the next timer update");
        fixture_cursor = {300, 110};
        require(send(WM_LBUTTONDOWN) && send(WM_LBUTTONUP) && documents::active(),
                "A click in freshly replaced message text failed to open the reader");
        documents::update(window);
        require(documents::active(), "A text click opened the stale previous document source");
        controls.document_text[0].bounds.bottom = 155;
        documents::update(window);
        require(!documents::active() && !paused && reader(background) == background,
                "Changed native text geometry retained an outdated open reader");
        documents::update(window, false);
        require(!documents::available() && !send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP) &&
                    !send(WM_KEYDOWN, VK_F7) && reader(background) == background,
                "Background text accepted input or added an unopened reader overlay");
        documents::update(window);
        controls.resources.clear();
        require(!send(WM_LBUTTONDOWN) && !send(WM_LBUTTONUP) && reader(background) == background,
                "A removed message source retained its click target or canvas overlay");
        documents::release();
        DestroyWindow(window);
        window = nullptr;
        std::cout << "Readable document input passed\n";
    } catch (const std::exception& error) {
        documents::release(true);
        if (window) {
            DestroyWindow(window);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
