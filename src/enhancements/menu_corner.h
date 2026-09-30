#pragma once

#include <windows.h>
#include <optional>

namespace enhancements::game {

struct MenuCornerIdentity {
    void* application;
    void* state;
    void* view;
    void* object;

    bool operator==(const MenuCornerIdentity&) const = default;
};

std::optional<RECT> menu_corner();
std::optional<MenuCornerIdentity> menu_corner_identity();
bool suppress_menu_corner(bool suppress);

}
