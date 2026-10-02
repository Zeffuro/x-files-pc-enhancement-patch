#pragma once

#include "slots.h"
#include <optional>

namespace saves {
inline constexpr unsigned autosave_count = 5;

std::vector<Slot> read_autosaves(const std::filesystem::path& game);
std::vector<Slot> read_quicksaves(const std::filesystem::path& game);
std::optional<Slot> newest_save(const std::filesystem::path& game, bool requires_current_database);
Slot write_autosave(const std::filesystem::path& game, const std::filesystem::path& prepared_save,
                    const Thumbnail& thumbnail);
// Call only after the new quick-save has successfully replaced the current file.
void record_quicksave(const std::filesystem::path& game);
}
