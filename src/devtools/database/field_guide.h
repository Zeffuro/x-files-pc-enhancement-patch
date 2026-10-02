#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace devtools {
std::wstring database_class_purpose(std::uint32_t class_id);
std::wstring database_field_meaning(std::uint32_t class_id, std::wstring_view group,
                                    std::wstring_view field);
}
