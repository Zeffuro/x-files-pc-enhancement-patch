#include "launcher/configuration.h"
#include "setup/destination.h"
#include "setup/media.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <typename Action> void reject(Action action, const char* message) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, message);
}
}

int main() {
    try {
        const auto base =
            fs::temp_directory_path() /
            ("xfiles-install-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(base), "Cannot create test directory");

        struct Cleanup {
            fs::path path;

            ~Cleanup() {
                std::error_code ignored;
                fs::remove_all(path, ignored);
            }
        } cleanup{base};

        const auto game = base / "game";
        fs::create_directories(game / "defaults");
        std::ofstream(game / "defaults/ddraw.ini") << "display defaults\n";
        std::ofstream(game / "defaults/patch.ini") << "patch defaults\n";
        initialize_configuration(game);
        require(read(game / "ddraw.ini") == read(game / "defaults/ddraw.ini") &&
                    read(game / "patch.ini") == read(game / "defaults/patch.ini"),
                "Initial settings do not match defaults");

        std::ofstream(game / "ddraw.ini") << "custom display\n";
        std::ofstream(game / "patch.ini") << "custom controls\n";
        std::ofstream(game / "preferences.ini") << "saved preferences\n";
        std::ofstream(game / "session.ini") << "saved session\n";
        std::ofstream(game / "QUICKSAVE.x") << "saved game\n";
        const auto display = read(game / "ddraw.ini");
        const auto controls = read(game / "patch.ini");
        const auto preferences = read(game / "preferences.ini");
        const auto session = read(game / "session.ini");
        const auto save = read(game / "QUICKSAVE.x");
        std::ofstream(game / "defaults/ddraw.ini") << "new display defaults\n";
        std::ofstream(game / "defaults/patch.ini") << "new patch defaults\n";
        initialize_configuration(game);
        require(read(game / "ddraw.ini") == display && read(game / "patch.ini") == controls &&
                    read(game / "preferences.ini") == preferences &&
                    read(game / "session.ini") == session && read(game / "QUICKSAVE.x") == save,
                "Updating defaults changed live settings or saves");
        fs::remove_all(game / "defaults");
        initialize_configuration(game);
        require(read(game / "ddraw.ini") == display && read(game / "patch.ini") == controls,
                "Existing settings require defaults unnecessarily");
        fs::remove(game / "patch.ini");
        reject([&] { initialize_configuration(game); }, "Missing defaults accepted");
        fs::create_directory(game / "defaults");
        std::ofstream(game / "defaults/patch.ini") << "restored defaults\n";
        initialize_configuration(game);
        require(read(game / "patch.ini") == read(game / "defaults/patch.ini") &&
                    read(game / "ddraw.ini") == display,
                "Missing config was not restored without changing the other config");
        fs::remove(game / "patch.ini");
        fs::create_directory(game / "patch.ini");
        reject([&] { initialize_configuration(game); }, "Settings directory accepted as a file");

        const auto source = base / "source";
        fs::create_directory(source);
        MediaSource media;
        media.root = fs::canonical(source);
        validate_destination(media, base / "Games/The X-Files");
        require(!fs::exists(base / "Games"), "Validation created destination folders");
        require(install_parent(base / "Games/The X-Files") == fs::canonical(base),
                "Missing parent folders were not resolved to an existing ancestor");
        for (const auto& destination : {source, source / "nested", source / "new/nested", base,
                                        base / "new/../source/inside"}) {
            reject([&] { validate_destination(media, destination); },
                   "Existing or source-contained destination accepted");
        }
        std::ofstream(base / "not-a-directory") << "keep";
        reject([&] { validate_destination(media, base / "not-a-directory/game"); },
               "A file was accepted as an installation parent");
        media.bytes = std::numeric_limits<std::uintmax_t>::max();
        reject([&] { validate_destination(media, base / "too-large"); },
               "Oversized installation overflowed the disk-space check");
        media.bytes = 0;
        std::ofstream(base / "dvd.iso") << "image placeholder";
        media.root = fs::canonical(base / "dvd.iso");
        validate_destination(media, base / "beside-the-iso");
        std::cout << "Installation paths and update-safe settings passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
