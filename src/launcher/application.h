#pragma once

#include "staging.h"
#include "session.h"

int execute_game(const StagedGame& game, const Session& session, bool diagnostic = false);
int resume_game(const std::filesystem::path& directory);
std::filesystem::path application_directory();
