#include "enhancements/inventory.h"
#include "game/layouts/asset_reference.h"

#include <iostream>
#include <cstring>
#include <stdexcept>

namespace enhancements::game {

const Edition* profile = &dvd;
std::byte* image = nullptr;

const Edition& edition() {
    return *profile;
}

std::byte* executable_image() {
    return image;
}

MainView* current_view() {
    return nullptr;
}

bool world_navigation_available() {
    return false;
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
        for (const auto profile : {&game::dvd, &game::cd, &native_game::profile_cd_10019,
                                   &native_game::profile_cd_10020}) {
            game::profile = profile;
            std::vector<std::byte> image(0x300000);
            game::image = image.data();
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
            const auto panel = choices.panel_for(*profile);
            const RECT expected_panel{230, 296, 473, 405};
            require(EqualRect(&panel, &expected_panel),
                    "Conversation hints must follow the frame across edition layouts.");
            game::MainView view{};
            auto& children = *reinterpret_cast<game::List<game::ChildView>*>(
                reinterpret_cast<std::byte*>(&view) + profile->children);
            game::InventoryIcon gun{}, phone{};
            gun.rectangle.bounds = {336, 420, 366, 460};
            constexpr char descriptor[] = "\x02"
                                          "50Graphics\x7f"
                                          "inventory items\x7f"
                                          "FBI Issue\x7f"
                                          "wilmoresgun.pic\x01"
                                          "0";
            native_game::AssetReference gun_asset{image.data() + profile->asset_reference, 0x1be8};
            gun_asset.descriptor = {nullptr, descriptor, sizeof(descriptor)};
            const auto gun_resource =
                reinterpret_cast<game::InventoryIcon::Graphic::Resource*>(&gun_asset);
            game::InventoryIcon::Graphic gun_graphic{};
            gun_graphic.resource = gun_resource;
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
            const auto named = inventory_items(&view);
            require(named.front().asset_path ==
                            L"Graphics/inventory items/FBI Issue/wilmoresgun.pic" &&
                        named.back().asset_path.empty(),
                    "Inventory did not use its actual cached asset path.");
            gun_asset.vtable = nullptr;
            require(inventory_items(&view).front().asset_path.empty(),
                    "A different native resource class was read as an asset reference.");
            gun_asset.vtable = image.data() + profile->asset_reference;
            gun_asset.descriptor.data = reinterpret_cast<const char*>(1);
            require(inventory_items(&view).front().asset_path.empty() &&
                        inventory_items(&view).front().resource == 0x1be8u,
                    "Unreadable asset text changed resource identity or was dereferenced.");
            gun_asset.descriptor.data = descriptor;
            gun_asset.descriptor.capacity = 4;
            require(inventory_items(&view).front().asset_path.empty(),
                    "An asset path exceeded its native string allocation.");
            gun_asset.descriptor.capacity = sizeof(descriptor);
            require(native_game::asset_path("\x02"
                                            "x0path")
                            .empty() &&
                        native_game::asset_path("\x03"
                                                "999short")
                            .empty() &&
                        native_game::asset_path("\x01"
                                                "3a\nb")
                            .empty() &&
                        native_game::asset_path("\x04"
                                                "0001a")
                            .empty(),
                    "Malformed native asset descriptors were accepted.");
            const auto unreadable = reinterpret_cast<void*>(1);
            gun_graphic.resource = static_cast<game::InventoryIcon::Graphic::Resource*>(unreadable);
            const auto unknown = inventory_items(&view);
            require(unknown.size() == 2 && !unknown.front().resource,
                    "Unreadable native resource did not remain an unnamed inventory item.");
            require(!inventory_item_bounds(&view, 0x1be8),
                    "Unreadable inventory resource matched the gun shortcut.");
            require(inventory_bounds(&view).size() == 2,
                    "Unreadable resource removed otherwise valid navigation bounds.");
            gun_graphic.resource = gun_resource;
            gun.graphic = static_cast<game::InventoryIcon::Graphic*>(unreadable);
            require(!inventory_items(&view).front().resource,
                    "Unreadable graphic pointer was dereferenced.");
            gun.graphic = &gun_graphic;
            gun_item.icon = static_cast<game::InventoryIcon*>(unreadable);
            require(inventory_items(&view).size() == 1, "Unreadable icon was dereferenced.");
            gun_item.icon = &gun;
            first.value = static_cast<game::InventoryItem*>(unreadable);
            require(inventory_items(&view).size() == 1, "Unreadable item was dereferenced.");
            first.value = &gun_item;
            first.next = static_cast<game::List<game::InventoryItem>::Node*>(unreadable);
            require(inventory_items(&view).size() == 1, "Unreadable list node was dereferenced.");
            first.next = &second;
            root.value = static_cast<game::ChildView*>(unreadable);
            require(inventory_bounds(&view).empty(), "Unreadable child view remained visible.");
            root.value = &child;
            require(inventory_items(static_cast<game::MainView*>(unreadable)).empty(),
                    "Unreadable game view was dereferenced.");
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
