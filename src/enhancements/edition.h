#pragma once

#include <cstdint>
#include <string_view>

namespace enhancements::game {

struct Edition {
    std::uint32_t application, draw_slot, draw_list, talk_list, history_list;
    std::uint32_t current_input, main_menu, script_root, script_control, input_graphic, text;
    std::uint32_t movie, action_movie, emotion, picture, viewport;
    std::uint32_t lookup, release, picture_bounds;
    std::uint32_t activate, remove, pause, restore_preferences, restore_view;
    std::uint32_t session_active, scene_active, menu_used;
    std::uint32_t children, control_rectangle;
    std::uint32_t string_create, string_destroy, save_state, save_file, load_file;
    std::uint32_t pending_load;
    std::uint32_t choice_viewport;
};

inline constexpr Edition dvd{
    .application = 0x2b6fec,
    .draw_slot = 0x25feac,
    .draw_list = 0x16e20,
    .talk_list = 0x2c0270,
    .history_list = 0x2c03c0,
    .current_input = 0x407c0,
    .main_menu = 0x25d8c0,
    .script_root = 0x25b848,
    .script_control = 0x25d830,
    .input_graphic = 0x25eae8,
    .text = 0x25f928,
    .movie = 0x25d270,
    .action_movie = 0x25dee8,
    .emotion = 0x258310,
    .picture = 0x25f5a0,
    .viewport = 0x2c10e8,
    .lookup = 0x41a30,
    .release = 0x2b610,
    .picture_bounds = 0x1e350,
    .activate = 0x405a0,
    .remove = 0x32850,
    .pause = 0x6c0b0,
    .restore_preferences = 0x52460,
    .restore_view = 0x96730,
    .session_active = 0x2b6b10,
    .scene_active = 0x2b6b14,
    .menu_used = 0x2b6b0c,
    .children = 0x804,
    .control_rectangle = 0x19c,
    .string_create = 0x7bb20,
    .string_destroy = 0x93e0,
    .save_state = 0x106ee0,
    .save_file = 0xcd380,
    .load_file = 0xcd7c0,
    .pending_load = 0x2b6614,
    .choice_viewport = 0x9c,
};

inline constexpr Edition cd{
    .application = 0x2b2ecc,
    .draw_slot = 0x25e844,
    .draw_list = 0x17070,
    .talk_list = 0x2bd018,
    .history_list = 0x2bd168,
    .current_input = 0x40c60,
    .main_menu = 0x256718,
    .script_root = 0x253a30,
    .script_control = 0x257510,
    .input_graphic = 0x25a290,
    .text = 0x25c1e0,
    .movie = 0x258d20,
    .action_movie = 0x254020,
    .emotion = 0x2545e0,
    .picture = 0x25cd10,
    .viewport = 0x2c11e8,
    .lookup = 0x41ed0,
    .release = 0x2b790,
    .picture_bounds = 0x1e500,
    .activate = 0x40a40,
    .remove = 0x32d80,
    .pause = 0x6c9b0,
    .restore_preferences = 0x52b20,
    .restore_view = 0x97210,
    .session_active = 0x2b1988,
    .scene_active = 0x2b198c,
    .menu_used = 0x2b1984,
    .children = 0x800,
    .control_rectangle = 0x198,
    .string_create = 0x7c460,
    .string_destroy = 0x95f0,
    .save_state = 0xef220,
    .save_file = 0x1173a0,
    .load_file = 0x1177e0,
    .pending_load = 0x2b19ac,
    .choice_viewport = 0x98,
};

inline const Edition* edition_named(std::string_view name) {
    return name == "DVD" ? &dvd : name == "CD" ? &cd : nullptr;
}

const Edition& edition();

}
