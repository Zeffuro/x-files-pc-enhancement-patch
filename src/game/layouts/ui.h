#pragma once

#include "game/profiles/generated.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace native_game {

static_assert(sizeof(void*) == 4);

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

    const List<ChildView>& children_for(const Profile& profile) const {
        return *reinterpret_cast<const List<ChildView>*>(reinterpret_cast<const std::byte*>(this) +
                                                         profile.children);
    }
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

    const Rectangle& viewport_for(const Profile& profile) const {
        return *reinterpret_cast<const Rectangle*>(reinterpret_cast<const std::byte*>(this) +
                                                   profile.choice_viewport);
    }

    const RECT& tab_for(const Profile& profile) const {
        return (&viewport_for(profile) + 1)->bounds;
    }

    RECT close_for(const Profile& profile) const {
        const auto origin = *reinterpret_cast<const POINT*>(
            reinterpret_cast<const std::byte*>(this) + profile.choice_viewport - 12);
        return {origin.x + 234, origin.y + 17, origin.x + 242, origin.y + 25};
    }
};

static_assert(offsetof(Container, count) == 8);
static_assert(offsetof(Application, view) == 0xec);
static_assert(offsetof(Application, preferences) == 0xf4);
static_assert(offsetof(ChoiceList, viewport) == profile_dvd_20000.choice_viewport);
static_assert(offsetof(ChoiceList, tab) == 0xb0);
static_assert(offsetof(MainView, inventory) == 0x39c);
static_assert(sizeof(ConversationIcons) == 0x38);
static_assert(offsetof(MainView, conversation_icons) == 0x2f4);
static_assert(offsetof(MainView, children) == profile_dvd_20000.children);
static_assert(offsetof(InventoryIcon, rectangle) == 0x80);
static_assert(offsetof(InventoryIcon, graphic) == 0x98);

}
