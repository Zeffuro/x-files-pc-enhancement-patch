#pragma once
#include <cstddef>
#include <cstdint>

// Serialized database values, independent of native in-memory layouts.
namespace game_assets::stored_format {
inline constexpr std::size_t header_size = 32;
inline constexpr std::size_t max_database_bytes = 128u * 1024u * 1024u;
inline constexpr std::uint8_t definition_flag = 0x80;

namespace class_id {
inline constexpr std::uint32_t title = 0x27;
inline constexpr std::uint32_t action = 0x41;
inline constexpr std::uint32_t action_list = 0x42;
inline constexpr std::uint32_t enabled_object = 0x46;
inline constexpr std::uint32_t trigger = 0x51;
inline constexpr std::uint32_t trigger_list = 0x52;
inline constexpr std::uint32_t variable = 0x53;
inline constexpr std::uint32_t standard_action = 0x54;
}

namespace action {
inline constexpr std::size_t descriptor_size = 15;
inline constexpr std::uint8_t type_mask = 0x7f;
inline constexpr std::uint8_t conditional_flag = 0x80;
inline constexpr unsigned statement = 0, asset = 1, timer = 2, enable = 3;
inline constexpr unsigned set_view = 4, interface_action = 5, function = 6, sound = 7;
inline constexpr std::uint8_t variable_operand = 0;
}

namespace reference_list {
inline constexpr std::size_t descriptor_size = 32;
}

namespace trigger {
inline constexpr std::size_t descriptor_size = 11;
}

namespace title {
inline constexpr std::uint32_t trigger_list_offset = 64;
}

namespace standard_action {
inline constexpr std::uint32_t action_offset = 14;
}

namespace enabled_object {
inline constexpr std::size_t object_scalar = 0, class_scalar = 2, scalar_count = 3;
}
}
