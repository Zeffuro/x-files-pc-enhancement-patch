#pragma once

#include "types.h"
#include <stdexcept>

namespace quickdraw {

inline std::int32_t fixed_value(std::int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) {
        throw std::runtime_error("QuickDraw matrix overflow");
    }
    return static_cast<std::int32_t>(value);
}

inline Matrix rectangle_matrix(const Rect& source, const Rect& destination) {
    const int width = source.right - source.left;
    const int height = source.bottom - source.top;
    if (!width || !height) {
        throw std::runtime_error("QuickDraw matrix has an empty source");
    }
    Matrix matrix{};
    matrix.values[0] =
        fixed_value(std::int64_t(destination.right - destination.left) * 65536 / width);
    matrix.values[4] =
        fixed_value(std::int64_t(destination.bottom - destination.top) * 65536 / height);
    matrix.values[6] = fixed_value(std::int64_t(destination.left) * 65536 -
                                   std::int64_t(source.left) * matrix.values[0]);
    matrix.values[7] = fixed_value(std::int64_t(destination.top) * 65536 -
                                   std::int64_t(source.top) * matrix.values[4]);
    matrix.values[8] = 1 << 30;
    return matrix;
}

inline Rect transform_rectangle(const Rect& source, const Matrix& matrix) {
    if (matrix.values[1] || matrix.values[2] || matrix.values[3] || matrix.values[5] ||
        matrix.values[8] != 1 << 30) {
        throw std::runtime_error("QuickDraw matrix uses rotation or perspective");
    }
    auto coordinate = [&](int value, unsigned scale, unsigned offset) {
        const auto result =
            (std::int64_t(value) * matrix.values[scale] + matrix.values[offset]) / 65536;
        if (result < INT16_MIN || result > INT16_MAX) {
            throw std::runtime_error("QuickDraw coordinate overflow");
        }
        return static_cast<std::int16_t>(result);
    };
    return {coordinate(source.top, 4, 7), coordinate(source.left, 0, 6),
            coordinate(source.bottom, 4, 7), coordinate(source.right, 0, 6)};
}

}
