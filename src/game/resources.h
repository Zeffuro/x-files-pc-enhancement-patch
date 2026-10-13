#pragma once

namespace native_game::resource {
inline constexpr unsigned options = 0xc24a;
inline constexpr unsigned options_audio = 0xc275;
inline constexpr unsigned save = 0x14269;
inline constexpr unsigned load = 0x142ca;
inline constexpr unsigned help = 0xc51c;
inline constexpr unsigned phone = 0x3bc1;
inline constexpr unsigned pda_region = 0x84d8;
inline constexpr unsigned pda_city = 0x84da;
inline constexpr unsigned pda_notes = 0x8035;
inline constexpr unsigned pda_addresses = 0x8037;
inline constexpr unsigned pda_inbox = 0x8031;
inline constexpr unsigned pda_message = 0xed5d;
inline constexpr unsigned workstation_root = 0xdefa;
inline constexpr unsigned workstation_welcome = 0x43d5;
inline constexpr unsigned workstation_login = 0x3b99;
inline constexpr unsigned workstation_landing = 0x3b01;
inline constexpr unsigned workstation_photo = 0x3b80;
inline constexpr unsigned workstation_apb = 0x3b30;
inline constexpr unsigned workstation_media = 0x3b68;
inline constexpr unsigned workstation_search = 0x3b04;
inline constexpr unsigned workstation_search_capture = 0xf160;
inline constexpr unsigned workstation_inbox = 0x3b43;
inline constexpr unsigned workstation_message = 0xd63b;
inline constexpr unsigned incorrect_password = 0xead3;
inline constexpr unsigned inventory_gun = 0x1be8;
}

namespace native_game::script_control {
inline constexpr unsigned acknowledgement = 0x7fe42;
inline constexpr unsigned dialog_text = 0x7fe73;
inline constexpr unsigned text_cursor = 0x7ff73;
inline constexpr unsigned dialog_background = 0x7fed8;
inline constexpr unsigned input_first = 0x7ff00;
inline constexpr unsigned input_end = 0x80000;
inline constexpr unsigned auxiliary_first = 0x7fe74;
inline constexpr unsigned auxiliary_last = 0x7fe75;
inline constexpr unsigned hover = 0x7ff74;

constexpr bool selectable(unsigned id) {
    return (id >= input_first && id < input_end) || id == auxiliary_first || id == auxiliary_last;
}
}

namespace native_game::cursor_id {
inline constexpr unsigned view = 0x1bfc;
inline constexpr unsigned talk = 0x122;
inline constexpr unsigned use = 0x4454;
inline constexpr unsigned left = 0x11c;
inline constexpr unsigned right = 0x11d;
inline constexpr unsigned forward = 0x11e;
inline constexpr unsigned forward_alternate = 0x120;
inline constexpr unsigned forward_alternate_2 = 0xea2e;
inline constexpr unsigned back = 0x11f;
inline constexpr unsigned back_alternate = 0x117a;
}
