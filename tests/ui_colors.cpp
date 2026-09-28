#include "picture/inventory_colors.h"
#include "playback/menu_colors.h"
#include "playback/grading.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void inventory() {
    std::vector<std::uint8_t> pixels(7 * 7 * 4, 2);
    for (unsigned i = 0; i < 49; ++i) {
        pixels[i * 4 + 3] = static_cast<std::uint8_t>(i);
    }
    for (unsigned y = 2; y <= 4; ++y) {
        for (unsigned x = 2; x <= 4; ++x) {
            if (x != 3 || y != 3) {
                std::fill_n(pixels.begin() + (y * 7 + x) * 4, 3, std::uint8_t{80});
            }
        }
    }
    pixels[0] = 20;
    const auto before = pixels;
    picture::clean_inventory_background(pixels, 7, 7);
    require(pixels[4] == 0 && pixels[(3 * 7 + 3) * 4] == 2,
            "Inventory cleanup missed exterior gray or erased an enclosed shadow");
    require(pixels[0] == 20 && pixels[(2 * 7 + 2) * 4] == 80,
            "Inventory cleanup damaged colored edges or the object outline");
    for (unsigned i = 0; i < 49; ++i) {
        require(pixels[i * 4 + 3] == before[i * 4 + 3], "Inventory alpha changed");
    }
    require(picture::inventory_colors(before, std::vector<std::uint8_t>(3833), 30, 40).empty(),
            "An unrelated JPEG-sized payload matched inventory artwork");
    auto invalid = before;
    picture::clean_inventory_background(invalid, 0, 7);
    picture::clean_inventory_background(invalid, 8, 7);
    require(invalid == before, "Invalid inventory dimensions modified pixels");
}

void menu() {
    std::vector<std::uint8_t> pixels(9 * 5 * 4);
    for (unsigned y = 0; y < 5; ++y) {
        for (unsigned x = 0; x < 9; ++x) {
            const auto offset = (y * 9 + x) * 4;
            pixels[offset] = x < 4 ? 40 : 0;
            pixels[offset + 1] = x < 4 ? 30 : 0;
            pixels[offset + 2] = x < 4 ? 10 : 0;
            pixels[offset + 3] = static_cast<std::uint8_t>(x + y * 9);
        }
    }
    pixels[(2 * 9 + 2) * 4] = 46;
    const auto before = pixels;
    playback::clean_menu_colors(pixels, 9, 5);
    require(pixels[(2 * 9 + 2) * 4] < 46 && pixels[(2 * 9 + 2) * 4] >= 40,
            "Menu noise was not reduced");
    require(pixels[(2 * 9 + 3) * 4] >= 40 && pixels[(2 * 9 + 4) * 4] == 0,
            "Menu smoothing blurred a sharp letter edge into black");
    for (unsigned i = 0; i < 45; ++i) {
        require(pixels[i * 4 + 3] == before[i * 4 + 3], "Menu alpha changed");
    }
    auto invalid = before;
    playback::clean_menu_colors(invalid, 0, 5);
    playback::clean_menu_colors(invalid, 8, 5);
    require(invalid == before, "Invalid menu dimensions modified pixels");
    std::vector<std::uint8_t> single{2, 1, 3, 127};
    playback::clean_menu_colors(single, 1, 1);
    require(single == std::vector<std::uint8_t>{0, 0, 0, 127}, "Tiny menu cleanup failed");
}

void contrast() {
    const auto contrast_strength = [](MovieContrast mode, std::string_view path,
                                      std::string_view codec, std::size_t samples) {
        return playback::movie_grade(mode, path, codec, samples).contrast;
    };
    for (const auto path : {"xv/20273.xmv", "xv/20621.xmv", "xv/21230.xmv", "xv/99999.xmv"}) {
        require(!contrast_strength(MovieContrast::Scene, path, "cvid", 20),
                "Scene defaults changed an unapproved movie");
    }
    require(!contrast_strength(MovieContrast::Off, "xv/21782.xmv", "cvid", 20) &&
                !contrast_strength(MovieContrast::Medium, "xv/64421.xmv", "mjpa", 1) &&
                !contrast_strength(MovieContrast::Medium, "xv/21782.xmv", "cvid", 1) &&
                !contrast_strength(MovieContrast::Medium, "xg/21782.xmv", "cvid", 20) &&
                !contrast_strength(MovieContrast::Medium, "xv/../21782.xmv", "cvid", 20),
            "Contrast escaped its moving Cinepak movie scope");
    std::vector<std::uint8_t> ramp;
    for (unsigned i = 0; i < 256; ++i) {
        ramp.insert(ramp.end(), {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i),
                                 static_cast<std::uint8_t>(i), 73});
    }
    const auto original = ramp;
    playback::apply_grade(ramp, {});
    require(ramp == original, "Contrast off changed pixels");
    for (const unsigned strength : {10u, 15u, 25u}) {
        auto pixels = original;
        playback::apply_grade(pixels, {static_cast<int>(strength)});
        require(pixels.front() == 0 && pixels[255 * 4] == 255 && pixels[64 * 4] < 64 &&
                    pixels[192 * 4] > 192,
                "Contrast changed endpoints or failed to strengthen tonal separation");
        for (unsigned i = 0; i < 256; ++i) {
            require(pixels[i * 4 + 3] == 73 && (!i || pixels[i * 4] >= pixels[(i - 1) * 4]),
                    "Contrast changed alpha or inverted tone order");
        }
    }
    auto pixels = original;
    playback::Grade grade;
    grade.gamma = 120;
    playback::apply_grade(pixels, grade);
    require(pixels[64 * 4] > 64 && pixels[0] == 0 && pixels[255 * 4] == 255,
            "Gamma failed to lift midtones while preserving endpoints");
    pixels = original;
    grade = {};
    grade.black = 16;
    grade.white = 235;
    playback::apply_grade(pixels, grade);
    require(pixels[16 * 4] == 0 && pixels[235 * 4] == 255, "Input levels did not map correctly");
    pixels = original;
    grade = {};
    grade.brightness = 5;
    grade.red = 110;
    playback::apply_grade(pixels, grade);
    require(pixels[64 * 4] > 64 && pixels[64 * 4 + 2] > pixels[64 * 4] && pixels[64 * 4 + 3] == 73,
            "Brightness or BGRA color balance failed");
    pixels = original;
    grade.white = grade.black;
    playback::apply_grade(pixels, grade);
    require(pixels == original, "Invalid levels changed pixels");
    grade.black = std::numeric_limits<int>::max();
    grade.white = std::numeric_limits<int>::min();
    playback::apply_grade(pixels, grade);
    require(pixels == original, "Extreme invalid levels changed pixels");
}

}

int main() {
    try {
        inventory();
        menu();
        contrast();
        std::cout << "UI artwork cleanup boundaries passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
