#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include "edition.h"

namespace enhancements::game {

struct Container {
    void* vtable;
    void** elements_vtable;
    unsigned count;

    void* current() {
        constexpr auto current_method = 0x8c / sizeof(void*);
        using GetCurrent = void**(__stdcall*)(void*);
        const auto reference =
            reinterpret_cast<GetCurrent>(elements_vtable[current_method])(&elements_vtable);
        return reference ? *reference : nullptr;
    }
};

template <typename T> struct List {
    struct Node {
        T* value;
        Node* next;
        Node* previous;
    };

    void* vtable;
    unsigned count;
    Node* first;
    Node* last;
    Node* current;
};

struct Rectangle {
    void* vtable;
    RECT bounds;
};

struct InventoryIcon {
    std::byte reserved[0x80];
    Rectangle rectangle;
    void* unknown;

    struct Graphic {
        std::byte reserved[0x18];

        struct Resource {
            void* vtable;
            unsigned id;
        }* resource;
    }* graphic;
};

struct InventoryItem {
    InventoryIcon* icon;
    void* point_vtable;
    POINT position;
};

struct Inventory {
    void* vtable;
    List<InventoryItem> items;
};

struct ChildView {
    void* object;
};

struct ConversationIcons {
    Inventory inventory;
    std::byte reserved[0x20];
};

struct MainView {
    std::byte reserved[0x290];
    Container dialogue;
    std::byte before_conversation_icons[0x58];
    ConversationIcons conversation_icons[3];
    Inventory inventory;
    std::byte before_children[0x450];
    List<ChildView> children;
};

struct Application {
    std::byte reserved[0xe4];
    void* state;
    void* unknown;
    MainView* view;
    void* before_preferences;
    void* preferences;
};

struct ChoiceList {
    std::byte reserved[0x9c];
    Rectangle viewport;
    Rectangle tab;

    const Rectangle& viewport_for(const Edition& profile) const {
        return *reinterpret_cast<const Rectangle*>(reinterpret_cast<const std::byte*>(this) +
                                                   profile.choice_viewport);
    }

    const RECT& tab_for(const Edition& profile) const {
        return (&viewport_for(profile) + 1)->bounds;
    }

    RECT close_for(const Edition& profile) const {
        const auto origin = *reinterpret_cast<const POINT*>(
            reinterpret_cast<const std::byte*>(this) + profile.choice_viewport - 12);
        return {origin.x + 234, origin.y + 17, origin.x + 242, origin.y + 25};
    }
};

static_assert(offsetof(Container, count) == 8);
static_assert(offsetof(Application, view) == 0xec);
static_assert(offsetof(Application, preferences) == 0xf4);
static_assert(offsetof(ChoiceList, tab) == 0xb0);
static_assert(offsetof(MainView, inventory) == 0x39c);
static_assert(sizeof(ConversationIcons) == 0x38);
static_assert(offsetof(MainView, conversation_icons) == 0x2f4);
static_assert(offsetof(MainView, children) == 0x804);
static_assert(offsetof(InventoryIcon, rectangle) == 0x80);
static_assert(offsetof(InventoryIcon, graphic) == 0x98);

MainView* current_view();
std::byte* executable_image();
void* current_input();
std::uintptr_t input_vtable();
bool menu_confirmation_active();
bool saving_available();
std::vector<RECT> modal_buttons();

struct ScriptControls {
    struct Text {
        RECT bounds;
        std::string value;
    };

    std::vector<unsigned> resources;
    std::vector<RECT> buttons;
    std::vector<RECT> hover_buttons;
    std::vector<RECT> acknowledgement_buttons;
    std::vector<RECT> dialog_buttons;
    std::vector<RECT> dialog_fields;
    bool script_dialog = false;
    bool text_input = false;
    std::vector<RECT> text;
    std::vector<Text> fields;
};

ScriptControls script_controls();
bool world_navigation_available();
RECT scene_bounds();
std::vector<RECT> world_hotspots(bool navigation_only);
std::vector<RECT> emotion_targets();
std::vector<RECT> aiming_targets();
bool movie_skippable();

}
