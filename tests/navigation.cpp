#include "enhancements/inventory.h"

#include <iostream>
#include <cstring>
#include <stdexcept>

namespace enhancements::game {

const Edition* profile = &dvd;

const Edition& edition() {
    return *profile;
}

MainView* current_view() {
    return nullptr;
}

RECT scene_bounds() {
    return {20, 90, 620, 330};
}

}

int main() {
    using namespace enhancements;
    try {
        const auto require = [](bool condition, const char* message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        };
        for (const auto profile : {&game::dvd, &game::cd}) {
            game::profile = profile;
            game::ChoiceList choices{};
            const std::size_t viewport_offset = profile == &game::cd ? 0x98 : 0x9c;
            const RECT viewport{239, 305, 451, 395}, tab{238, 285, 330, 297};
            const POINT origin{228, 285};
            auto bytes = reinterpret_cast<std::byte*>(&choices);
            std::memcpy(bytes + viewport_offset + 4, &viewport, sizeof(viewport));
            std::memcpy(bytes + viewport_offset + 24, &tab, sizeof(tab));
            std::memcpy(bytes + viewport_offset - 12, &origin, sizeof(origin));
            require(EqualRect(&choices.viewport_for(*profile).bounds, &viewport),
                    "Conversation viewport used the wrong edition layout.");
            require(EqualRect(&choices.tab_for(*profile), &tab),
                    "Conversation tabs used the wrong edition layout.");
            const auto close = choices.close_for(*profile);
            require(close.left == 462 && close.top == 302,
                    "Conversation close box must follow the panel origin.");
            game::MainView view{};
            auto& children = *reinterpret_cast<game::List<game::ChildView>*>(
                reinterpret_cast<std::byte*>(&view) + profile->children);
            game::InventoryIcon gun{}, phone{};
            gun.rectangle.bounds = {336, 420, 366, 460};
            game::InventoryIcon::Graphic::Resource gun_resource{nullptr, 0x1be8};
            game::InventoryIcon::Graphic gun_graphic{};
            gun_graphic.resource = &gun_resource;
            gun.graphic = &gun_graphic;
            phone.rectangle.bounds = {37, 420, 50, 460};
            game::InventoryItem gun_item{&gun}, phone_item{&phone};
            game::List<game::InventoryItem>::Node second{&phone_item};
            game::List<game::InventoryItem>::Node first{&gun_item, &second};
            view.inventory.items.count = 2;
            view.inventory.items.first = &first;
            game::ChildView child{&view.inventory};
            game::List<game::ChildView>::Node root{&child};
            children.count = 1;
            children.first = &root;

            auto bounds = inventory_bounds(&view);
            require(bounds.size() == 2 && bounds[0].left == 37 && bounds[1].left == 336,
                    "Inventory navigation must follow screen order, not ownership list order.");
            require(inventory_item_bounds(&view, 0x1be8)->left == 336,
                    "Gun shortcut used inventory order instead of resource identity.");
            require(!inventory_item_bounds(&view, 123), "Unowned item was selectable.");
            children.count = 0;
            require(inventory_bounds(&view).empty(),
                    "Hidden inventory remained navigable in menus.");
            require(!inventory_item_bounds(&view, 0x1be8),
                    "Gun shortcut bypassed hidden inventory.");
            children.count = 1;
            view.inventory.items.count = 0;
            require(inventory_bounds(&view).empty(), "Empty inventory retained stale items.");
            view.inventory.items.count = 2;
            gun.rectangle.bounds.right = 700;
            require(inventory_bounds(&view).size() == 1,
                    "Offscreen inventory bounds were accepted.");
            view.inventory.items.count = 1000;
            require(inventory_bounds(&view).empty(), "Unexpected inventory layout was accepted.");
            require(inventory_bounds(nullptr).empty(), "Missing game view was not handled.");
        }
        std::cout << "Inventory visibility, ownership and layout checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
