#include "enhancements/script_scroll.h"
#include "enhancements/game_resources.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace enhancements;

void require(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

template <class T> void put(std::byte* object, unsigned offset, const T& value) {
    std::memcpy(object + offset, &value, sizeof(value));
}

game::ChoiceList* called = nullptr;
int direction = 0, calls = 0;

void __stdcall up(game::ChoiceList* object) {
    called = object;
    direction = -1;
    ++calls;
}

void __stdcall down(game::ChoiceList* object) {
    called = object;
    direction = 1;
    ++calls;
}

struct Types {
    unsigned group, rows, rectangle, point;
};

void verify(const game::Edition& profile, Types types, unsigned resource, POINT upper,
            POINT lower) {
    std::vector<std::byte> image(0x300000), state(0x300), view(0x900);
    std::array<std::byte, 0x30> root{}, resource_data{};
    std::array<std::array<std::byte, 0x1c0>, 2> controls{};
    std::array<game::List<std::byte>::Node, 2> inputs{};
    std::array<std::byte, 0xc0> text{};
    std::array<std::byte, 20> row{};
    auto* group = image.data() + profile.main_text;
    put(root.data(), 0, image.data() + profile.script_root);
    put(root.data(), 0x18, resource_data.data());
    put(resource_data.data(), 4, resource);
    game::List<std::byte>::Node root_node{root.data(), nullptr, nullptr};
    put(state.data(), 0x25c, game::List<std::byte>{nullptr, 1, &root_node, &root_node, nullptr});
    const RECT upper_bounds{upper.x - 7, upper.y - 7, upper.x + 7, upper.y + 7};
    const RECT lower_bounds{lower.x - 7, lower.y - 7, lower.x + 7, lower.y + 7};
    for (unsigned i = 0; i < controls.size(); ++i) {
        auto* control = controls[i].data();
        put(control, 0, image.data() + profile.script_control);
        put(control, 8, 1u);
        put(control, 0x20, 1u);
        put(control, 0x144, script_control::input_first);
        put(control, profile.control_rectangle + 4, i ? lower_bounds : upper_bounds);
        inputs[i] = {control, i ? nullptr : &inputs[1], i ? &inputs[0] : nullptr};
    }
    put(state.data(), 0x270, game::List<std::byte>{nullptr, 2, &inputs[0], &inputs[1], nullptr});
    game::ChildView child{view.data() + 0x4c8};
    game::List<game::ChildView>::Node child_node{&child, nullptr, nullptr};
    put(view.data(), profile.children,
        game::List<game::ChildView>{nullptr, 1, &child_node, &child_node, nullptr});
    std::array<std::byte*, 2> entries{group, nullptr};
    put(view.data(), 0x4d0, 1u);
    put(view.data(), 0x4d4, 0u);
    put(view.data(), 0x4d8, entries.data());
    put(group, 0, image.data() + types.group);
    put(group, profile.main_text_rectangle,
        game::Rectangle{image.data() + types.rectangle, {200, 60, 418, 353}});
    put(text.data(), 0, image.data() + profile.text);
    put(row.data(), 0, text.data());
    put(row.data(), 4, image.data() + types.point);
    game::List<std::byte>::Node row_node{row.data(), nullptr, nullptr};
    put(group, 4,
        game::List<std::byte>{image.data() + types.rows, 1, &row_node, &row_node, nullptr});
    const auto scroll = [&](const RECT& bounds = RECT{}) {
        return game::scroll_script_button(state.data(), view.data(), image.data(), profile,
                                          resource, IsRectEmpty(&bounds) ? upper_bounds : bounds,
                                          up, down);
    };
    require(scroll() && called == reinterpret_cast<game::ChoiceList*>(group) && direction == -1,
            "Up did not call the owned native main-text group");
    require(scroll(lower_bounds) && direction == 1, "Down selected the wrong native callback");
    const auto baseline = calls;
    child.object = nullptr;
    require(!scroll(), "A hidden main-text owner activated");
    child.object = view.data() + 0x4c8;
    entries[0] = view.data();
    require(!scroll(), "A retained unselected singleton activated");
    entries[0] = group;
    put(view.data(), 0x4d4, 1u);
    require(!scroll(), "An invalid owner selection activated");
    put(view.data(), 0x4d4, 0u);
    put(view.data(), 0x4d0, 257u);
    require(!scroll(), "An oversized native owner activated");
    put(view.data(), 0x4d0, 1u);
    put(controls[0].data(), 8, 0u);
    require(!scroll(), "A hidden arrow activated");
    put(controls[0].data(), 8, 1u);
    put(controls[1].data(), 12, 1u);
    require(!scroll(), "A missing enabled opposite arrow activated");
    put(controls[1].data(), 12, 0u);
    put(controls[0].data(), 0x20, 0u);
    require(!scroll(), "A control without click actions activated");
    put(controls[0].data(), 0x20, 1u);
    put(controls[0].data(), 0x144, script_control::dialog_text);
    require(!scroll(), "A native dialog activated underlying text");
    put(controls[0].data(), 0x144, script_control::input_first);
    put(resource_data.data(), 4, resource::pda_inbox);
    require(!scroll(), "A stale document resource activated");
    put(resource_data.data(), 4, resource);
    root_node.next = &root_node;
    require(!scroll(), "A malformed root list activated");
    root_node.next = nullptr;
    inputs[1].previous = nullptr;
    require(!scroll(), "A malformed control list activated");
    inputs[1].previous = &inputs[0];
    put(group, 0, image.data());
    require(!scroll(), "A different native group class activated");
    put(group, 0, image.data() + types.group);
    put(group, 4, image.data());
    require(!scroll(), "A different native row-list class activated");
    put(group, 4, image.data() + types.rows);
    put(group, profile.main_text_rectangle, image.data());
    require(!scroll(), "A different native rectangle class activated");
    put(group, profile.main_text_rectangle, image.data() + types.rectangle);
    put(row.data(), 4, image.data());
    require(!scroll(), "A different native row-point class activated");
    put(row.data(), 4, image.data() + types.point);
    put(text.data(), 0, image.data());
    require(!scroll(), "A different native row Text class activated");
    put(text.data(), 0, image.data() + profile.text);
    row_node.next = &row_node;
    require(!scroll(), "A cyclic native row list activated");
    row_node.next = nullptr;
    row_node.value = reinterpret_cast<std::byte*>(1);
    require(!scroll(), "An inaccessible native row activated");
    row_node.value = row.data();
    require(!scroll(RECT{1, 1, 5, 5}), "Stale arrow geometry activated");
    require(calls == baseline, "A rejected native state invoked a callback");
    put(group, 4, game::List<std::byte>{image.data() + types.rows, 0, nullptr, nullptr, nullptr});
    require(scroll(), "An owned empty native text list lost native clamp semantics");
}
}

int main() {
    try {
        const std::array profiles{&game::cd, &native_game::profile_cd_10019,
                                  &native_game::profile_cd_10020, &game::dvd};
        const std::array types{Types{0x25d87c, 0x25bf00, 0x25bc38, 0x25c1b0},
                               Types{0x260014, 0x25e698, 0x25e3d0, 0x25e948},
                               Types{0x261014, 0x25f800, 0x25f3d0, 0x25f948},
                               Types{0x25f7b4, 0x25f400, 0x25f540, 0x25f258}};
        for (unsigned i = 0; i < profiles.size(); ++i) {
            verify(*profiles[i], types[i], resource::pda_notes, {422, 95}, {422, 339});
            verify(*profiles[i], types[i], resource::pda_message, {428, 62}, {428, 351});
            verify(*profiles[i], types[i], resource::workstation_message, {607, 107}, {607, 443});
        }
        std::cout << "All four native scroll owners, arrows and failure guards passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
