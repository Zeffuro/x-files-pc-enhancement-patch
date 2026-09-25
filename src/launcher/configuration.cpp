#include "configuration.h"

#include <stdexcept>

void initialize_configuration(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    for (const auto* name : {L"ddraw.ini", L"patch.ini"}) {
        const auto live = directory / name;
        if (fs::exists(live)) {
            if (!fs::is_regular_file(live)) {
                throw std::runtime_error("The settings path is not a file: " + live.string());
            }
            continue;
        }
        const auto defaults = directory / L"defaults" / name;
        if (!fs::is_regular_file(defaults)) {
            throw std::runtime_error("Missing default settings. Extract the complete release ZIP "
                                     "into the installed game folder.");
        }
        fs::copy_file(defaults, live, fs::copy_options::skip_existing);
    }
}
