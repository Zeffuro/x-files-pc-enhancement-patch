#pragma once

namespace enhancements::resource {
inline constexpr unsigned options = 0xc24a;
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
inline constexpr unsigned workstation_inbox = 0x3b43;
inline constexpr unsigned workstation_message = 0xd63b;
inline constexpr unsigned incorrect_password = 0xead3;
inline constexpr unsigned inventory_gun = 0x1be8;
}

namespace enhancements::script_control {
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
