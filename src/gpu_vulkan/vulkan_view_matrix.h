#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - (round FL) a view's pixel <-> world matrices as the compute passes take them (the formulas ReSTIR DI's
// and GI's passes use - vulkan_restir_di.cpp / vulkan_restir_gi.cpp keep their own copies): `pixel_to_world` maps (pixel x,
// pixel y, depth, 1) in the target's pixels - x right, y down, Direct3D's pixel centres at +0.5 - and the viewport's
// depth range to the world (homogeneous: divide by w); its inverse the world to the pixel. Rows are the matrices' rows;
// `column_major` lays one out for GLSL's mat4.
namespace render::vulkan::view_matrix {

inline constexpr std::size_t kSide = 4;
using Matrix = std::array<std::array<double, kSide>, kSide>;

[[nodiscard]] inline Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix out{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            double sum = 0.0;
            for (std::size_t k = 0; k < kSide; ++k) {
                sum += core::at(core::at(a, row), k) * core::at(core::at(b, k), column);
            }
            core::at(core::at(out, row), column) = sum;
        }
    }
    return out;
}

// Gauss-Jordan with partial pivoting; false for a singular matrix.
inline bool invert(Matrix m, Matrix* inverse)
{
    constexpr double kSingular = 1e-300;
    Matrix out{};
    for (std::size_t i = 0; i < kSide; ++i) {
        core::at(core::at(out, i), i) = 1.0;
    }
    for (std::size_t column = 0; column < kSide; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kSide; ++row) {
            if (std::abs(core::at(core::at(m, row), column)) > std::abs(core::at(core::at(m, pivot), column))) {
                pivot = row;
            }
        }
        if (std::abs(core::at(core::at(m, pivot), column)) < kSingular) {
            return false;
        }
        std::swap(core::at(m, pivot), core::at(m, column));
        std::swap(core::at(out, pivot), core::at(out, column));
        const double scale = 1.0 / core::at(core::at(m, column), column);
        for (std::size_t k = 0; k < kSide; ++k) {
            core::at(core::at(m, column), k) *= scale;
            core::at(core::at(out, column), k) *= scale;
        }
        for (std::size_t row = 0; row < kSide; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(m, row), column);
            for (std::size_t k = 0; k < kSide; ++k) {
                core::at(core::at(m, row), k) -= factor * core::at(core::at(m, column), k);
                core::at(core::at(out, row), k) -= factor * core::at(core::at(out, column), k);
            }
        }
    }
    *inverse = out;
    return true;
}

// The view's (pixel x, pixel y, depth, 1) -> world (Direct3D's row-vector view x projection, transposed and inverted, after
// the viewport's pixel and depth mapping). False: the camera's matrices are singular.
inline bool pixel_to_world(const rt::ViewDrawn& view, Matrix* out)
{
    Matrix view_m{};
    Matrix projection{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(view_m, row), column) = core::at(core::at(view.camera.view, row), column);
            core::at(core::at(projection, row), column) = core::at(core::at(view.camera.projection, row), column);
        }
    }
    const Matrix row_vectors = multiply(view_m, projection);
    Matrix clip_from_world{};
    for (std::size_t row = 0; row < kSide; ++row) {
        for (std::size_t column = 0; column < kSide; ++column) {
            core::at(core::at(clip_from_world, row), column) = core::at(core::at(row_vectors, column), row);
        }
    }
    Matrix world_from_clip{};
    if (!invert(clip_from_world, &world_from_clip)) {
        return false;
    }
    const double x = core::at(view.rect, 0);
    const double y = core::at(view.rect, 1);
    const double width = std::max(1U, core::at(view.rect, 2));
    const double height = std::max(1U, core::at(view.rect, 3));
    const double near_z = core::at(view.depth_range, 0);
    const double depth_span = std::max(1e-6, static_cast<double>(core::at(view.depth_range, 1)) - near_z);
    const Matrix clip_from_pixel{{{2.0 / width, 0.0, 0.0, (-2.0 * x / width) - 1.0},
                                  {0.0, -2.0 / height, 0.0, (2.0 * y / height) + 1.0},
                                  {0.0, 0.0, 1.0 / depth_span, -near_z / depth_span},
                                  {0.0, 0.0, 0.0, 1.0}}};
    *out = multiply(world_from_clip, clip_from_pixel);
    return true;
}

[[nodiscard]] inline std::array<float, kSide * kSide> column_major(const Matrix& m)
{
    std::array<float, kSide * kSide> out{};
    for (std::size_t column = 0; column < kSide; ++column) {
        for (std::size_t row = 0; row < kSide; ++row) {
            core::at(out, (column * kSide) + row) = static_cast<float>(core::at(core::at(m, row), column));
        }
    }
    return out;
}

} // namespace render::vulkan::view_matrix
