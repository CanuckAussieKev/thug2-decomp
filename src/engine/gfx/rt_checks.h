#pragma once

// NOT ORIGINAL - ray tracing's pure helpers (rt_scene.h): a Direct3D row-vector matrix as an instance's object-to-world
// rows, a triangle strip's triangles as a list, and the tests a contract check makes of a mesh's geometry and an
// instance (devtools/rt_check.h; src/tests/rt_scene_check.cpp). Nothing here touches a graphics API or the game.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

using Rows = std::array<std::array<float, 4>, 3>;

inline constexpr std::size_t kMatrixFloats = 16;      // a Direct3D matrix
inline constexpr float kSameRowsTolerance = 1.0e-4F; // same_rows' default: relative to the values' size

// A Direct3D matrix (16 floats, row vectors: p' = p * M, the translation in the last row) as rows that take a column
// (x, y, z, 1): row i is the matrix's column i.
inline Rows object_to_world_rows(std::span<const float, kMatrixFloats> d3d)
{
    Rows rows{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            core::at(core::at(rows, i), j) = core::at(d3d, (j * 4) + i);
        }
    }
    return rows;
}

// The identity's rows.
inline Rows identity_rows() { return {{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}}; }

// A point through the rows.
inline std::array<float, 3> apply_rows(const Rows& rows, const std::array<float, 3>& point)
{
    std::array<float, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::array<float, 4>& row = core::at(rows, i);
        core::at(out, i) = (core::at(row, 0) * core::at(point, 0)) + (core::at(row, 1) * core::at(point, 1)) +
                           (core::at(row, 2) * core::at(point, 2)) + core::at(row, 3);
    }
    return out;
}

inline bool finite_rows(const Rows& rows)
{
    for (const auto& row : rows) {
        for (const float value : row) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return true;
}

// Two transforms alike: each value within `tolerance` of the other's, relative to the larger of 1 and its size.
inline bool same_rows(const Rows& a, const Rows& b, float tolerance = kSameRowsTolerance)
{
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            const float x = core::at(core::at(a, i), j);
            const float y = core::at(core::at(b, i), j);
            const float scale = std::fmax(1.0F, std::fmax(std::fabs(x), std::fabs(y)));
            if (!(std::fabs(x - y) <= tolerance * scale)) {
                return false;
            }
        }
    }
    return true;
}

// The triangles a draw of `index_count` indices makes (a strip: each index past the second; a list: three each).
inline uint32_t triangle_count(uint32_t index_count, bool strip)
{
    if (strip) {
        return index_count >= 3 ? index_count - 2 : 0;
    }
    return index_count / 3;
}

// A strip's triangles as a list, the degenerate ones (two indices alike: the joins between strips) left out, every
// second one turned back so all face the way the first does (as the rasteriser reads a strip). The triangles kept.
template <typename Index>
uint32_t strip_to_list(std::span<const Index> strip, std::vector<uint32_t>& out)
{
    uint32_t kept = 0;
    for (std::size_t i = 2; i < strip.size(); ++i) {
        const uint32_t first = core::at(strip, i - 2);
        const uint32_t second = core::at(strip, i - 1);
        const uint32_t third = core::at(strip, i);
        if (first == second || second == third || first == third) {
            continue;
        }
        if ((i % 2) == 0) {
            out.insert(out.end(), {first, second, third});
        } else {
            out.insert(out.end(), {second, first, third});
        }
        ++kept;
    }
    return kept;
}

// What is wrong with a mesh's geometry ("" when nothing): buffers, sizes and ranges, and - with the indices' copy - every
// index inside the vertices.
inline std::string geometry_problem(const Geometry& geometry)
{
    const Geometry& gm = geometry;
    if (gm.vertices == nullptr) {
        return "no vertex buffer";
    }
    if (gm.vertex_stride == 0 || gm.vertex_count == 0) {
        return "no vertices (stride " + std::to_string(gm.vertex_stride) + ", count " + std::to_string(gm.vertex_count) +
               ")";
    }
    if (gm.position_offset + (3 * sizeof(float)) > gm.vertex_stride) {
        return "the position (at " + std::to_string(gm.position_offset) + ") past the vertex's " +
               std::to_string(gm.vertex_stride) + " bytes";
    }
    const uint64_t vertex_end = gm.vertex_offset + (uint64_t{gm.vertex_stride} * gm.vertex_count);
    if (gm.vertex_buffer_bytes != 0 && vertex_end > gm.vertex_buffer_bytes) {
        return "the vertices end at " + std::to_string(vertex_end) + ", past the buffer's " +
               std::to_string(gm.vertex_buffer_bytes) + " bytes";
    }
    if (gm.indices == nullptr) {
        return triangle_count(gm.vertex_count, gm.strip) == 0 ? "no triangles" : "";
    }
    if (gm.index_count < 3 || (!gm.strip && (gm.index_count % 3) != 0)) {
        return std::string("a ") + (gm.strip ? "strip" : "list") + " of " + std::to_string(gm.index_count) + " indices";
    }
    const uint32_t index_bytes = gm.indices_32bit ? sizeof(uint32_t) : sizeof(uint16_t);
    const uint64_t index_end = (uint64_t{gm.index_offset} + gm.index_count) * index_bytes;
    if (gm.index_buffer_bytes != 0 && index_end > gm.index_buffer_bytes) {
        return "the indices end at " + std::to_string(index_end) + ", past the buffer's " +
               std::to_string(gm.index_buffer_bytes) + " bytes";
    }
    if (gm.cpu_indices != nullptr) {
        const std::size_t end = std::size_t{gm.index_offset} + gm.index_count;
        const std::span<const uint32_t> wide(static_cast<const uint32_t*>(gm.cpu_indices), gm.indices_32bit ? end : 0);
        const std::span<const uint16_t> narrow(static_cast<const uint16_t*>(gm.cpu_indices), gm.indices_32bit ? 0 : end);
        for (std::size_t at = gm.index_offset; at < end; ++at) {
            const uint32_t index = gm.indices_32bit ? core::at(wide, at) : uint32_t{core::at(narrow, at)};
            if (index >= gm.vertex_count) {
                return "index " + std::to_string(at - gm.index_offset) + " is " + std::to_string(index) + ", past the " +
                       std::to_string(gm.vertex_count) + " vertices";
            }
        }
    }
    return "";
}

// What is wrong with an instance's flags ("" when nothing): world or object (one of them), opaque / alpha-tested /
// transparent (opaque alone; alpha-tested or transparent, not both with opaque), skinned only on an object.
inline std::string flags_problem(uint32_t flags)
{
    const bool world = (flags & kWorld) != 0U;
    const bool object = (flags & kObject) != 0U;
    if (world == object) {
        return world ? "both world and object" : "neither world nor object";
    }
    const bool opaque = (flags & kOpaque) != 0U;
    const bool tested = (flags & kAlphaTested) != 0U;
    const bool blended = (flags & kTransparent) != 0U;
    if (opaque == (tested || blended)) {
        return opaque ? "opaque and alpha-tested / transparent" : "none of opaque, alpha-tested, transparent";
    }
    if ((flags & kSkinned) != 0U && world) {
        return "a skinned world mesh";
    }
    return "";
}

} // namespace render::rt
