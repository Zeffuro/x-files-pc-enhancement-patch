#include "grading.h"
#include "grading_catalog_generated.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace playback {
namespace {
std::string canonical(std::string_view path) {
    std::string name(path);
    for (auto& c : name) {
        if (c == '\\') {
            c = '/';
        } else if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return name;
}

bool movie_path(std::string_view name) {
    return name.size() == 12 && name.starts_with("xv/") && name.ends_with(".xmv") &&
           std::all_of(name.begin() + 3, name.begin() + 8,
                       [](char c) { return c >= '0' && c <= '9'; });
}
}

Grade scene_grade(std::string_view path) {
    const auto name = canonical(path);
    for (const auto& row : grading_catalog) {
        if (row.path == name) {
            return row.grade;
        }
    }
    return {};
}

Grade movie_grade(MovieContrast mode, std::string_view path, std::string_view codec,
                  std::size_t samples) {
    if (mode == MovieContrast::Off || codec != "cvid" || samples < 2 ||
        !movie_path(canonical(path))) {
        return {};
    }
    if (mode == MovieContrast::Scene) {
        return scene_grade(path);
    }
    return {mode == MovieContrast::Mild ? 15 : mode == MovieContrast::Medium ? 25 : 0};
}

void apply_grade(std::span<std::uint8_t> pixels, const Grade& grade) {
    if (grade == Grade{} || grade.contrast < -25 || grade.contrast > 25 || grade.brightness < -20 ||
        grade.brightness > 20 || grade.gamma < 50 || grade.gamma > 200 || grade.black < 0 ||
        grade.black > 127 || grade.white < 128 || grade.white > 255 ||
        grade.white - grade.black < 128 || grade.red < 75 || grade.red > 125 || grade.green < 75 ||
        grade.green > 125 || grade.blue < 75 || grade.blue > 125) {
        return;
    }
    std::array<std::array<std::uint8_t, 256>, 3> tables{};
    const std::array gains{grade.blue, grade.green, grade.red};
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned i = 0; i < 256; ++i) {
            double x = std::clamp(
                (static_cast<double>(i) - grade.black) / (grade.white - grade.black), 0.0, 1.0);
            x = std::pow(x, 100.0 / grade.gamma);
            x += 4 * (grade.contrast / 100.0) * x * (1 - x) * (x - 0.5);
            x = (x + grade.brightness / 100.0) * gains[c] / 100.0;
            tables[c][i] = static_cast<std::uint8_t>(std::clamp(x * 255 + 0.5, 0.0, 255.0));
        }
    }
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        for (unsigned c = 0; c < 3; ++c) {
            pixels[i + c] = tables[c][pixels[i + c]];
        }
    }
}
}
