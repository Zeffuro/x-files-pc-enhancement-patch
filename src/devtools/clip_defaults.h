#pragma once
#include "clip_notes.h"
#include <map>
#include <string_view>

namespace devtools {
struct ClipDefault : ClipNote {
    std::optional<std::wstring> place;
};

using ClipDefaults = std::map<std::wstring, ClipDefault>;
ClipDefaults parse_defaults(std::string_view text);
ClipDefaults load_defaults(const std::filesystem::path& root);
}
