// NOT ORIGINAL - the level editor's ghosts (ours). See level_editor_ghost.h.

#include "editor/level_editor_ghost.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_scenery.h"
#include "engine/gfx/ghost_draws.h"

namespace frontend::level_editor::ghost {

namespace {

constexpr int kSide = 4;                 // a matrix's rows and columns
constexpr float kFlatDeterminant = 1e-12F; // (an inverse refused below it)

float& cell(Matrix& matrix, int row, int column) { return core::at(matrix, (row * kSide) + column); }
float cell_of(const Matrix& matrix, int row, int column) { return core::at(matrix, (row * kSide) + column); }

std::vector<Part>& asked()
{
    static core::NoDestroy<std::vector<Part>> parts;
    return parts.get();
}

} // namespace

Matrix matrix_of(const Placement& placement)
{
    const V3 origin = get_apply(placement, {.x = 0.0F, .y = 0.0F, .z = 0.0F});
    const std::array<V3, 3> axes = {{get_apply(placement, {.x = 1.0F, .y = 0.0F, .z = 0.0F}) - origin,
                                     get_apply(placement, {.x = 0.0F, .y = 1.0F, .z = 0.0F}) - origin,
                                     get_apply(placement, {.x = 0.0F, .y = 0.0F, .z = 1.0F}) - origin}};
    Matrix matrix = kIdentity;
    for (int row = 0; row < 3; ++row) {
        cell(matrix, row, 0) = core::at(axes, row).x;
        cell(matrix, row, 1) = core::at(axes, row).y;
        cell(matrix, row, 2) = core::at(axes, row).z;
    }
    cell(matrix, 3, 0) = origin.x;
    cell(matrix, 3, 1) = origin.y;
    cell(matrix, 3, 2) = origin.z;
    return matrix;
}

Matrix moved(V3 delta)
{
    Matrix matrix = kIdentity;
    cell(matrix, 3, 0) = delta.x;
    cell(matrix, 3, 1) = delta.y;
    cell(matrix, 3, 2) = delta.z;
    return matrix;
}

Matrix after(const Matrix& first, const Matrix& then)
{
    Matrix out{};
    for (int row = 0; row < kSide; ++row) {
        for (int column = 0; column < kSide; ++column) {
            float sum = 0.0F;
            for (int k = 0; k < kSide; ++k) {
                sum += cell_of(first, row, k) * cell_of(then, k, column);
            }
            cell(out, row, column) = sum;
        }
    }
    return out;
}

bool inverse_of(const Matrix& matrix, Matrix* inverse)
{
    // (the 3 x 3 part's inverse from its cofactors; the move undone through it)
    auto at = [&](int row, int column) { return cell_of(matrix, row, column); };
    const float c00 = (at(1, 1) * at(2, 2)) - (at(1, 2) * at(2, 1));
    const float c01 = (at(1, 2) * at(2, 0)) - (at(1, 0) * at(2, 2));
    const float c02 = (at(1, 0) * at(2, 1)) - (at(1, 1) * at(2, 0));
    const float determinant = (at(0, 0) * c00) + (at(0, 1) * c01) + (at(0, 2) * c02);
    if (std::fabs(determinant) < kFlatDeterminant) {
        return false;
    }
    const float scale = 1.0F / determinant;
    Matrix out = kIdentity;
    cell(out, 0, 0) = c00 * scale;
    cell(out, 1, 0) = c01 * scale;
    cell(out, 2, 0) = c02 * scale;
    cell(out, 0, 1) = ((at(0, 2) * at(2, 1)) - (at(0, 1) * at(2, 2))) * scale;
    cell(out, 1, 1) = ((at(0, 0) * at(2, 2)) - (at(0, 2) * at(2, 0))) * scale;
    cell(out, 2, 1) = ((at(0, 1) * at(2, 0)) - (at(0, 0) * at(2, 1))) * scale;
    cell(out, 0, 2) = ((at(0, 1) * at(1, 2)) - (at(0, 2) * at(1, 1))) * scale;
    cell(out, 1, 2) = ((at(0, 2) * at(1, 0)) - (at(0, 0) * at(1, 2))) * scale;
    cell(out, 2, 2) = ((at(0, 0) * at(1, 1)) - (at(0, 1) * at(1, 0))) * scale;
    for (int column = 0; column < 3; ++column) {
        float sum = 0.0F;
        for (int k = 0; k < 3; ++k) {
            sum += at(3, k) * cell_of(out, k, column);
        }
        cell(out, 3, column) = -sum;
    }
    *inverse = out;
    return true;
}

V3 transformed(const Matrix& matrix, V3 point)
{
    return {.x = (point.x * cell_of(matrix, 0, 0)) + (point.y * cell_of(matrix, 1, 0)) + (point.z * cell_of(matrix, 2, 0)) +
                 cell_of(matrix, 3, 0),
            .y = (point.x * cell_of(matrix, 0, 1)) + (point.y * cell_of(matrix, 1, 1)) + (point.z * cell_of(matrix, 2, 1)) +
                 cell_of(matrix, 3, 1),
            .z = (point.x * cell_of(matrix, 0, 2)) + (point.y * cell_of(matrix, 1, 2)) + (point.z * cell_of(matrix, 2, 2)) +
                 cell_of(matrix, 3, 2)};
}

void show(std::span<const Part> parts)
{
    // (each once: an overlay gathered for every view of a split view asks for the same ghost in each)
    for (const Part& part : parts) {
        const bool known = std::ranges::any_of(
            asked(), [&](const Part& other) { return other.sector == part.sector && other.world == part.world; });
        if (!known) {
            asked().push_back(part);
        }
    }
}

void hand_over()
{
    std::vector<render::ghost_draws::Ghost> ghosts;
    ghosts.reserve(asked().size());
    for (const Part& part : asked()) {
        if (render::Geom* geom = scenery::sector_geometry(part.sector)) {
            ghosts.push_back({.geom = geom, .world = part.world, .opacity = kOpacity});
        }
    }
    asked().clear();
    render::ghost_draws::set_ghosts(ghosts);
}

} // namespace frontend::level_editor::ghost
