#pragma once
#include <filesystem>
#include <array>

bool show_welcome(const std::filesystem::path& directory);

struct WelcomeChoices {
    std::array<bool, 10> enabled{};
    unsigned movie_speed = 2;
    unsigned captions = 0;
    unsigned movie_colors = 0;
};

bool save_welcome_choices(const std::filesystem::path& settings_file,
                          const WelcomeChoices& choices);
