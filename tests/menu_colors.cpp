#include "picture/menu_art.h"
#include "playback/menu_colors.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void credits() {
    const auto matches = [](std::string_view path, std::string_view codec = "jpeg",
                            unsigned width = 600, unsigned height = 400, std::size_t samples = 19) {
        return playback::is_credit_pages(path, codec, width, height, samples);
    };
    require(matches("xv/19666.xmv") && matches("XV\\19666.XMV") &&
                matches("xv/19666.xmv", "jpeg", 600, 400, 18),
            "Known credit pages were not selected");
    require(!matches("xg/19666.xmv") && !matches("xv/19660.xmv") && !matches("xv/../19666.xmv") &&
                !matches("19666.xmv") && !matches("xv/19666.xmv", "cvid") &&
                !matches("xv/19666.xmv", "jpeg", 599) &&
                !matches("xv/19666.xmv", "jpeg", 600, 399) &&
                !matches("xv/19666.xmv", "jpeg", 600, 400, 20) &&
                !matches("xv/19666.xmv", "jpeg", 600, 400, 17),
            "Credit cleanup escaped its known movie scope");
    std::vector<std::uint8_t> pixels{8, 0, 3, 41, 0, 24, 1, 73, 154, 106, 38, 129, 0, 25, 0, 255};
    const auto before = pixels;
    playback::clean_credit_colors(pixels);
    require(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 && pixels[4] == 0 &&
                pixels[5] == 0 && pixels[6] == 0,
            "Credit background ringing remained");
    require(std::equal(pixels.begin() + 8, pixels.end(), before.begin() + 8),
            "Credit cleanup changed stronger text pixels");
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        require(pixels[i] == before[i], "Credit cleanup changed alpha");
    }
    const auto once = pixels;
    playback::clean_credit_colors(pixels);
    require(pixels == once, "Credit cleanup was not stable");
}
}

int main(int argc, char** argv) {
    try {
        credits();
        require(!picture::is_menu_return_art(std::vector<std::uint8_t>(11816)),
                "An unrelated Return-sized picture matched");
        if (argc == 3) {
            std::ifstream input(argv[1], std::ios::binary);
            std::vector<std::uint8_t> pixels{std::istreambuf_iterator<char>(input), {}};
            require(input && pixels.size() % 4 == 0, "Cannot read BGRA input");
            playback::clean_credit_colors(pixels);
            std::ofstream output(argv[2], std::ios::binary);
            output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
            require(bool(output), "Cannot write BGRA output");
        }
        std::cout << "Menu artwork selection and credit text preservation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
