#include "enhancements/script_scroll.h"
#include "enhancements/game_resources.h"

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
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

void* photo_queue = nullptr;
void* photo_app = nullptr;
std::function<void()> photo_effect;
std::function<void()> flush_effect;
int flushes = 0;

void __stdcall photo_up(void* queue) {
    photo_queue = queue;
    direction = -1;
    ++calls;
    if (photo_effect) {
        photo_effect();
    }
}

void __stdcall photo_down(void* queue) {
    photo_queue = queue;
    direction = 1;
    ++calls;
    if (photo_effect) {
        photo_effect();
    }
}

void __stdcall photo_flush(void* app) {
    photo_app = app;
    ++flushes;
    if (flush_effect) {
        flush_effect();
    }
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

struct PhotoTypes {
    unsigned gallery, gallery_type, thumbnail, queue, state_list, point;
};

void verify_photos(const game::Edition& profile, PhotoTypes types) {
    std::vector<std::byte> image(0x300000), state(0x400), view(0x900), app(0x400);
    std::array<std::byte, 0x30> root{}, resource_data{};
    std::array<std::array<std::byte, 0x1c0>, 2> controls{};
    std::array<std::array<std::byte, 0x80>, 2> thumbs{};
    std::array<game::List<std::byte>::Node, 2> inputs{}, gallery{}, owned{};
    std::array<game::List<game::InventoryItem>::Node, 2> displayed{};
    std::array<game::InventoryItem, 2> rows{};
    std::array<void*, 100> storage{};
    auto* group = view.data() + 0x52c;
    auto* queue = app.data() + 0x268;
    put(image.data(), profile.application, app.data());
    put(app.data(), 0xe4, state.data());
    put(app.data(), 0xec, view.data());
    for (const auto offset : {0x268, 0x280}) {
        put(app.data(), offset, image.data() + types.queue);
        put(app.data(), offset + 8, std::numeric_limits<unsigned>::max());
        put(app.data(), offset + 12, storage.data());
        put(app.data(), offset + 16, 100u);
        put(app.data(), offset + 20, std::numeric_limits<unsigned>::max());
    }
    put(root.data(), 0, image.data() + profile.script_root);
    put(root.data(), 0x18, resource_data.data());
    put(resource_data.data(), 4, resource::workstation_photo);
    game::List<std::byte>::Node root_node{root.data(), nullptr, nullptr};
    put(state.data(), 0x25c, game::List<std::byte>{nullptr, 1, &root_node, &root_node, nullptr});
    const std::array<RECT, 2> arrows{RECT{566, 158, 586, 177}, RECT{565, 412, 584, 429}};
    for (unsigned i = 0; i < 2; ++i) {
        auto* control = controls[i].data();
        put(control, 0, image.data() + profile.script_control);
        put(control, 8, 1u);
        put(control, 0x20, 1u);
        put(control, 0x144, script_control::input_first);
        put(control, profile.control_rectangle + 4, arrows[i]);
        inputs[i] = {control, i ? nullptr : &inputs[1], i ? &inputs[0] : nullptr};
        put(thumbs[i].data(), 0, image.data() + types.thumbnail);
        put(thumbs[i].data(), 0x5c, root.data());
        put(thumbs[i].data(), 0x68, image.data() + types.point);
        put(thumbs[i].data(), 0x6c, POINT{138 + int(i) * 86, 165});
        gallery[i] = {thumbs[i].data(), i ? nullptr : &gallery[1], i ? &gallery[0] : nullptr};
        owned[i] = {thumbs[i].data(), i ? nullptr : &owned[1], i ? &owned[0] : nullptr};
        const unsigned source = 1 - i;
        rows[i] = {reinterpret_cast<game::InventoryIcon*>(thumbs[source].data() + 0x18),
                   image.data() + types.point,
                   {138 + int(source) * 86, 165}};
        displayed[i] = {&rows[i], i ? nullptr : &displayed[1], i ? &displayed[0] : nullptr};
    }
    put(state.data(), 0x270, game::List<std::byte>{nullptr, 2, &inputs[0], &inputs[1], nullptr});
    put(image.data(), types.gallery,
        game::List<std::byte>{image.data() + types.gallery_type, 2, &gallery[0], &gallery[1],
                              nullptr});
    put(state.data(), 0x298,
        game::List<std::byte>{image.data() + types.state_list, 2, &owned[0], &owned[1], nullptr});
    put(group, 0, image.data() + profile.cursor_group);
    put(group, 4,
        game::List<game::InventoryItem>{image.data() + profile.cursor_group_list, 2, &displayed[0],
                                        &displayed[1], nullptr});
    game::ChildView child{group};
    game::List<game::ChildView>::Node child_node{&child, nullptr, nullptr};
    put(view.data(), profile.children,
        game::List<game::ChildView>{nullptr, 1, &child_node, &child_node, nullptr});
    const auto scroll = [&](unsigned arrow = 0) {
        return game::scroll_photo_button(state.data(), view.data(), image.data(), profile,
                                         arrows[arrow], photo_up, photo_down, photo_flush);
    };
    require(scroll() == game::ScrollResult::activated && photo_queue == queue && direction == -1,
            "Photo Up did not receive the primary application's native command queue");
    require(scroll(1) == game::ScrollResult::activated && direction == 1,
            "Photo Down used the wrong native callback");
    require(photo_app == app.data(), "Native photo processing received a stale application");
    const auto baseline = calls;
    const auto baseline_flushes = flushes;
    put(queue, 4, 10u);
    require(scroll() == game::ScrollResult::pending, "Queued photo replacement was not deferred");
    put(queue, 4, 0u);
    put(queue, 8, 0u);
    require(scroll() == game::ScrollResult::pending, "Executing photo commands were not deferred");
    put(queue, 8, std::numeric_limits<unsigned>::max());
    put(app.data(), 0x284, 1u);
    require(scroll() == game::ScrollResult::pending,
            "Pending native input did not defer photo scrolling");
    put(app.data(), 0x284, 0u);
    const auto rejected = [&] {
        require(scroll() == game::ScrollResult::unavailable,
                "Invalid photo ownership or command storage accepted native scrolling");
    };
    put(queue, 0, image.data());
    rejected();
    put(queue, 0, image.data() + types.queue);
    put(queue, 12, reinterpret_cast<void*>(1));
    rejected();
    put(queue, 12, storage.data());
    put(queue, 16, 9u);
    rejected();
    put(queue, 16, 100u);
    put(queue, 4, 101u);
    rejected();
    put(queue, 4, 0u);
    put(queue, 20, 0u);
    rejected();
    put(queue, 20, std::numeric_limits<unsigned>::max());
    put(app.data(), 0xe4, view.data());
    rejected();
    put(app.data(), 0xe4, state.data());
    put(app.data(), 0xec, state.data());
    rejected();
    put(app.data(), 0xec, view.data());
    child.object = nullptr;
    rejected();
    child.object = group;
    game::ChildView duplicate_child{group};
    game::List<game::ChildView>::Node duplicate_node{&duplicate_child, nullptr, &child_node};
    child_node.next = &duplicate_node;
    put(view.data(), profile.children,
        game::List<game::ChildView>{nullptr, 2, &child_node, &duplicate_node, nullptr});
    rejected();
    child_node.next = nullptr;
    put(view.data(), profile.children,
        game::List<game::ChildView>{nullptr, 1, &child_node, &child_node, nullptr});
    put(group, 0, image.data());
    rejected();
    put(group, 0, image.data() + profile.cursor_group);
    put(group, 4, image.data());
    rejected();
    put(group, 4, image.data() + profile.cursor_group_list);
    put(image.data(), types.gallery, image.data());
    rejected();
    put(image.data(), types.gallery, image.data() + types.gallery_type);
    put(state.data(), 0x298, image.data());
    rejected();
    put(state.data(), 0x298, image.data() + types.state_list);
    put(image.data(), types.gallery + 0x14, 1u);
    rejected();
    put(image.data(), types.gallery + 0x14, std::numeric_limits<unsigned>::max());
    rejected();
    put(image.data(), types.gallery + 0x14, 0u);
    put(thumbs[0].data(), 0, image.data());
    rejected();
    put(thumbs[0].data(), 0, image.data() + types.thumbnail);
    put(thumbs[0].data(), 0x5c, nullptr);
    rejected();
    put(thumbs[0].data(), 0x5c, root.data());
    put(thumbs[0].data(), 0x68, image.data());
    rejected();
    put(thumbs[0].data(), 0x68, image.data() + types.point);
    ++rows[0].position.x;
    rejected();
    --rows[0].position.x;
    auto* original = rows[0].icon;
    rows[0].icon = rows[1].icon;
    rejected();
    rows[0].icon = original;
    gallery[1].value = gallery[0].value;
    rejected();
    gallery[1].value = thumbs[1].data();
    owned[1].value = owned[0].value;
    rejected();
    owned[1].value = thumbs[1].data();
    owned[1].previous = nullptr;
    rejected();
    owned[1].previous = &owned[0];
    gallery[1].next = &gallery[0];
    rejected();
    gallery[1].next = nullptr;
    displayed[1].value = reinterpret_cast<game::InventoryItem*>(1);
    rejected();
    displayed[1].value = &rows[1];
    put(controls[1].data(), 12, 1u);
    rejected();
    put(controls[1].data(), 12, 0u);
    put(resource_data.data(), 4, resource::workstation_search);
    rejected();
    put(resource_data.data(), 4, resource::workstation_photo);
    require(calls == baseline, "A rejected or pending photo frame called native code");
    require(flushes == baseline_flushes,
            "Rejected photo state processed unrelated native commands");
    photo_effect = [&] { put(app.data(), 0xec, state.data()); };
    require(scroll() == game::ScrollResult::unavailable && flushes == baseline_flushes,
            "Photo callback view replacement flushed a stale application's commands");
    put(app.data(), 0xec, view.data());
    photo_effect = [&] { put(resource_data.data(), 4, resource::workstation_search); };
    require(scroll() == game::ScrollResult::unavailable && flushes == baseline_flushes,
            "Photo callback page replacement flushed stale photo commands");
    put(resource_data.data(), 4, resource::workstation_photo);
    photo_effect = [&] {
        require(scroll() == game::ScrollResult::unavailable,
                "Reentrant photo callback dispatched another native action");
    };
    require(scroll() == game::ScrollResult::activated && flushes == baseline_flushes + 1,
            "Reentrant photo rejection lost the outer native command processing");
    photo_effect = {};
    flush_effect = [&] { put(app.data(), 0xec, state.data()); };
    require(scroll() == game::ScrollResult::unavailable,
            "Native processing changed view without canceling the photo request");
    put(app.data(), 0xec, view.data());
    flush_effect = {};
    std::array<std::byte, 0x80> preview{};
    put(preview.data(), 0, image.data() + types.thumbnail);
    put(preview.data(), 0x5c, resource_data.data());
    put(preview.data(), 0x68, image.data() + types.point);
    put(preview.data(), 0x6c, POINT{420, 32});
    put(image.data(), types.gallery - 4, preview.data());
    game::List<std::byte>::Node preview_node{preview.data(), nullptr, &owned[1]};
    owned[1].next = &preview_node;
    put(state.data(), 0x298,
        game::List<std::byte>{image.data() + types.state_list, 3, &owned[0], &preview_node,
                              nullptr});
    game::InventoryItem preview_row{reinterpret_cast<game::InventoryIcon*>(preview.data() + 0x18),
                                    image.data() + types.point,
                                    {420, 32}};
    game::List<game::InventoryItem>::Node preview_display{&preview_row, nullptr, &displayed[1]};
    displayed[1].next = &preview_display;
    put(group, 4,
        game::List<game::InventoryItem>{image.data() + profile.cursor_group_list, 3, &displayed[0],
                                        &preview_display, nullptr});
    require(scroll() == game::ScrollResult::activated && scroll(1) == game::ScrollResult::activated,
            "A selected photo preview blocked native gallery scrolling");
    put(image.data(), types.gallery - 4, nullptr);
    rejected();
    put(image.data(), types.gallery - 4, thumbs[0].data());
    rejected();
    put(image.data(), types.gallery - 4, preview.data());
    ++preview_row.position.y;
    rejected();
    --preview_row.position.y;
    put(preview.data(), 0, image.data());
    rejected();
    put(preview.data(), 0, image.data() + types.thumbnail);
    put(image.data(), types.gallery - 4, nullptr);
    put(image.data(), types.gallery,
        game::List<std::byte>{image.data() + types.gallery_type, 0, nullptr, nullptr, nullptr});
    put(state.data(), 0x298,
        game::List<std::byte>{image.data() + types.state_list, 0, nullptr, nullptr, nullptr});
    put(group, 4,
        game::List<game::InventoryItem>{image.data() + profile.cursor_group_list, 0, nullptr,
                                        nullptr, nullptr});
    require(scroll() == game::ScrollResult::activated,
            "Owned empty photo gallery did not retain native clamp behavior");
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
        const std::array photo_types{
            PhotoTypes{0x2bbaf8, 0x258390, 0x254678, 0x2567a0, 0x253638, 0x25c1b0},
            PhotoTypes{0x2bebc0, 0x25a6c0, 0x2566b0, 0x258830, 0x2554e8, 0x25e948},
            PhotoTypes{0x2bfc20, 0x25b6b8, 0x2576a8, 0x259828, 0x2564e8, 0x25f948},
            PhotoTypes{0x2bd6e0, 0x257af0, 0x25d0b8, 0x25a3d0, 0x256278, 0x25f258}};
        const std::array callbacks{
            std::array<unsigned, 5>{0x150c40, 0x150da0, 0xe2be0, 0xe2e80, 0x2830},
            std::array<unsigned, 5>{0x153c60, 0x153dc0, 0xe4a60, 0xe4d00, 0x2830},
            std::array<unsigned, 5>{0x154200, 0x154360, 0xe4e50, 0xe50f0, 0x2830},
            std::array<unsigned, 5>{0x13a9d0, 0x13ab30, 0xb26e0, 0xb2980, 0x2830}};
        std::vector<std::byte> callback_image(0x300000);
        for (unsigned i = 0; i < profiles.size(); ++i) {
            const auto functions =
                native_game::NativeFunctions(callback_image.data(), *profiles[i]);
            const std::array actual{
                reinterpret_cast<std::uintptr_t>(functions.main_scroll_up_target()),
                reinterpret_cast<std::uintptr_t>(functions.main_scroll_down_target()),
                reinterpret_cast<std::uintptr_t>(functions.photo_scroll_up_target()),
                reinterpret_cast<std::uintptr_t>(functions.photo_scroll_down_target()),
                reinterpret_cast<std::uintptr_t>(functions.process_commands_target())};
            for (unsigned callback = 0; callback < actual.size(); ++callback) {
                require(actual[callback] ==
                            reinterpret_cast<std::uintptr_t>(callback_image.data()) +
                                callbacks[i][callback],
                        "A profiled scroll callback differs from the verified native address");
            }

            verify(*profiles[i], types[i], resource::pda_notes, {422, 95}, {422, 339});
            verify(*profiles[i], types[i], resource::pda_message, {428, 62}, {428, 351});
            verify(*profiles[i], types[i], resource::workstation_message, {607, 107}, {607, 443});
            verify(*profiles[i], types[i], resource::workstation_search, {309, 373}, {309, 456});
            verify(*profiles[i], types[i], resource::workstation_search_capture, {309, 373},
                   {309, 456});
            verify_photos(*profiles[i], photo_types[i]);
        }
        std::cout << "All four native scroll owners, arrows and failure guards passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
