// NOT ORIGINAL - (round TRAIN) surfaces whose texture coordinates don't lay their texture on them (texture_mapping.h).
#include "engine/gfx/texture_mapping.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <vector>

#include "base/core/checked_index.h"

namespace render::texture_mapping {

namespace {

using Vec3 = std::array<float, 3>;
using Uv = std::array<float, 2>;
constexpr std::size_t kMostVertex = 0xFFFF; // (16-bit indices)
constexpr std::size_t kMostIndices = 0xFFFF;

Vec3 minus(const Vec3& a, const Vec3& b)
{
    return {std::get<0>(a) - std::get<0>(b), std::get<1>(a) - std::get<1>(b), std::get<2>(a) - std::get<2>(b)};
}
float dot(const Vec3& a, const Vec3& b)
{
    return (std::get<0>(a) * std::get<0>(b)) + (std::get<1>(a) * std::get<1>(b)) + (std::get<2>(a) * std::get<2>(b));
}
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(std::get<1>(a) * std::get<2>(b)) - (std::get<2>(a) * std::get<1>(b)),
            (std::get<2>(a) * std::get<0>(b)) - (std::get<0>(a) * std::get<2>(b)),
            (std::get<0>(a) * std::get<1>(b)) - (std::get<1>(a) * std::get<0>(b))};
}

Vec3 position_at(std::span<const uint8_t> vertices, uint32_t stride, uint32_t vertex)
{
    Vec3 out{};
    std::memcpy(out.data(), vertices.subspan(std::size_t{vertex} * stride, sizeof(Vec3)).data(), sizeof(Vec3));
    return out;
}

constexpr uint32_t kColourRgb = 0xFFFFFFU; // (the colour word's red, green and blue)

uint32_t colour_at(std::span<const uint8_t> vertices, uint32_t stride, uint32_t offset, uint32_t vertex)
{
    uint32_t out = 0;
    std::memcpy(&out, vertices.subspan((std::size_t{vertex} * stride) + offset, sizeof(out)).data(), sizeof(out));
    return out;
}

Uv uv_at(std::span<const uint8_t> vertices, uint32_t stride, uint32_t offset, uint32_t vertex)
{
    Uv out{};
    std::memcpy(out.data(), vertices.subspan((std::size_t{vertex} * stride) + offset, sizeof(Uv)).data(), sizeof(Uv));
    return out;
}

} // namespace

Stretch stretch_of(const std::array<Vec3, 3>& at, const std::array<Uv, 3>& uv)
{
    const Vec3 e1 = minus(std::get<1>(at), std::get<0>(at));
    const Vec3 e2 = minus(std::get<2>(at), std::get<0>(at));
    const float l1 = std::sqrt(dot(e1, e1));
    const Vec3 n = cross(e1, e2);
    const float twice_area = std::sqrt(dot(n, n));
    if (!(l1 > 0.0F) || !(twice_area > 0.0F)) {
        return {};
    }
    // (the corners in the triangle's plane: a at 0, b at (l1, 0), c at (px, py); the map J = D P^-1)
    const float px = dot(e2, e1) / l1;
    const float py = twice_area / l1;
    const float du1 = std::get<0>(std::get<1>(uv)) - std::get<0>(std::get<0>(uv));
    const float dv1 = std::get<1>(std::get<1>(uv)) - std::get<1>(std::get<0>(uv));
    const float du2 = std::get<0>(std::get<2>(uv)) - std::get<0>(std::get<0>(uv));
    const float dv2 = std::get<1>(std::get<2>(uv)) - std::get<1>(std::get<0>(uv));
    const float j00 = du1 / l1;
    const float j01 = (du2 - (du1 * px / l1)) / py;
    const float j10 = dv1 / l1;
    const float j11 = (dv2 - (dv1 * px / l1)) / py;
    const float sum = (j00 * j00) + (j01 * j01) + (j10 * j10) + (j11 * j11);
    const float det = std::abs((j00 * j11) - (j01 * j10));
    const float root = std::sqrt(std::max(0.0F, (sum * sum) - (4.0F * det * det)));
    return {.most = std::sqrt(0.5F * (sum + root)), .least = std::sqrt(std::max(0.0F, 0.5F * (sum - root)))};
}

float narrow_size(const std::array<Vec3, 3>& at)
{
    float longest = 0.0F;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3 edge = minus(core::at(at, (i + 1) % 3), core::at(at, i));
        longest = std::max(longest, std::sqrt(dot(edge, edge)));
    }
    const Vec3 n = cross(minus(std::get<1>(at), std::get<0>(at)), minus(std::get<2>(at), std::get<0>(at)));
    return longest > 0.0F ? std::sqrt(dot(n, n)) / longest : 0.0F;
}

bool places_nothing(const std::array<Vec3, 3>& at, const std::array<Uv, 3>& uv, uint32_t texels)
{
    const Stretch stretch = stretch_of(at, uv);
    if (!(stretch.most > 0.0F) || stretch.least >= kRankOneShare * stretch.most) {
        return false;
    }
    return narrow_size(at) * stretch.most * static_cast<float>(texels) >= kVisibleTexels;
}

Result world_aligned(std::span<const uint8_t> vertices, uint32_t count, const tess::VertexLayout& layout,
                     std::span<const uint16_t> strip, uint32_t texels)
{
    Result result;
    if (layout.uv_offset < 0 || layout.stride < sizeof(Vec3) || texels == 0 ||
        vertices.size() < std::size_t{count} * layout.stride) {
        return result;
    }
    const auto uv_offset = static_cast<uint32_t>(layout.uv_offset);
    if (uv_offset + sizeof(Uv) > layout.stride) {
        return result;
    }
    const std::vector<std::array<uint16_t, 3>> triangles = tess::strip_triangles(strip);
    std::vector<uint8_t> nothing(triangles.size(), 0);
    std::vector<uint8_t> axis_of(triangles.size(), 0); // (the axis a triangle faces most: 0 x, 1 y, 2 z)
    std::vector<uint8_t> kept_uses(count, 0);          // (a vertex a triangle with a sound map draws)
    std::vector<float> densities;
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        const std::array<uint16_t, 3>& corners = core::at(triangles, t);
        if (std::ranges::any_of(corners, [count](uint16_t v) { return v >= count; })) {
            continue;
        }
        ++result.triangles;
        std::array<Vec3, 3> at{};
        std::array<Uv, 3> uv{};
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(at, c) = position_at(vertices, layout.stride, core::at(corners, c));
            core::at(uv, c) = uv_at(vertices, layout.stride, uv_offset, core::at(corners, c));
        }
        const bool found = places_nothing(at, uv, texels);
        result.placing_nothing += found ? 1U : 0U;
        // (only where the original never showed the mapping: every corner's colour black - a rank-one map the art lit
        // is the artists' own (a gradient, a fade strip) and is kept)
        const bool unseen =
            found && layout.colour_offset >= 0 && std::ranges::all_of(corners, [&](uint16_t vertex) {
                return (colour_at(vertices, layout.stride, static_cast<uint32_t>(layout.colour_offset), vertex) &
                        kColourRgb) == 0U;
            });
        result.shown += found && !unseen ? 1U : 0U;
        if (!unseen) {
            for (const uint16_t vertex : corners) {
                core::at(kept_uses, vertex) = 1;
            }
            continue;
        }
        core::at(nothing, t) = 1;
        densities.push_back(stretch_of(at, uv).most);
        const Vec3 n = cross(minus(std::get<1>(at), std::get<0>(at)), minus(std::get<2>(at), std::get<0>(at)));
        const float ax = std::abs(std::get<0>(n));
        const float ay = std::abs(std::get<1>(n));
        const float az = std::abs(std::get<2>(n));
        core::at(axis_of, t) = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
    }
    if (densities.empty()) {
        return result;
    }
    const auto middle = densities.begin() + static_cast<std::ptrdiff_t>(densities.size() / 2);
    std::ranges::nth_element(densities, middle);
    result.density = *middle;
    // (each corner of such a triangle - a vertex and the axis plane it is projected on - gets one vertex: the original in
    // place when no sound triangle draws it and it is its first plane, else a copy appended)
    std::vector<uint8_t> out(vertices.begin(), vertices.begin() + (static_cast<std::ptrdiff_t>(count) * layout.stride));
    std::map<uint32_t, uint16_t> placed; // (vertex x 3 + axis -> its vertex)
    std::vector<uint8_t> taken(count, 0);
    std::vector<std::array<uint16_t, 3>> drawn = triangles;
    for (std::size_t t = 0; t < triangles.size(); ++t) {
        if (core::at(nothing, t) == 0) {
            continue;
        }
        for (std::size_t c = 0; c < 3; ++c) {
            const uint16_t vertex = core::at(core::at(triangles, t), c);
            const uint32_t key = (uint32_t{vertex} * 3U) + core::at(axis_of, t);
            auto found = placed.find(key);
            if (found == placed.end()) {
                uint16_t made = vertex;
                if (core::at(kept_uses, vertex) != 0 || core::at(taken, vertex) != 0) {
                    const std::size_t index = out.size() / layout.stride;
                    if (index > kMostVertex) {
                        return Result{.triangles = result.triangles};
                    }
                    made = static_cast<uint16_t>(index);
                    const auto from = vertices.subspan(std::size_t{vertex} * layout.stride, layout.stride);
                    out.insert(out.end(), from.begin(), from.end());
                    result.sources.push_back(vertex);
                }
                core::at(taken, vertex) = 1;
                found = placed.emplace(key, made).first;
            }
            core::at(core::at(drawn, t), c) = found->second;
        }
    }
    // (the coordinates near 0: the planes' origin moved by whole repeats of the texture to the first vertex given new
    // ones - a float keeps a texel's precision there)
    Vec3 origin{};
    const Vec3 first = position_at(out, layout.stride, placed.begin()->second);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(origin, axis) = std::floor(core::at(first, axis) * result.density) / result.density;
    }
    for (const auto& [key, vertex] : placed) {
        const Vec3 p = minus(position_at(out, layout.stride, vertex), origin);
        // (x faces -> (z, y), y faces -> (x, z), z faces -> (x, y))
        const uint32_t axis = key % 3U;
        Uv uv = axis == 0   ? Uv{std::get<2>(p), std::get<1>(p)}
                : axis == 1 ? Uv{std::get<0>(p), std::get<2>(p)}
                            : Uv{std::get<0>(p), std::get<1>(p)};
        uv = {std::get<0>(uv) * result.density, std::get<1>(uv) * result.density};
        result.largest = std::max({result.largest, std::abs(std::get<0>(uv)), std::abs(std::get<1>(uv))});
        std::memcpy(std::span<uint8_t>(out).subspan((std::size_t{vertex} * layout.stride) + uv_offset, sizeof(Uv)).data(),
                    uv.data(), sizeof(Uv));
        ++result.vertices;
    }
    if (!result.sources.empty()) {
        // (the triangles as one strip again: each starting at an even place, joined by degenerate ones)
        std::vector<uint16_t> made;
        made.reserve(drawn.size() * 5);
        for (const std::array<uint16_t, 3>& triangle : drawn) {
            if (!made.empty()) {
                made.push_back(made.back());
                made.push_back(std::get<0>(triangle));
                if (made.size() % 2 != 0) {
                    made.push_back(std::get<0>(triangle));
                }
            }
            made.insert(made.end(), triangle.begin(), triangle.end());
        }
        if (made.size() > kMostIndices) {
            return Result{.triangles = result.triangles};
        }
        result.strip = std::move(made);
    }
    result.vertex_count = static_cast<uint32_t>(out.size() / layout.stride);
    result.vertex_bytes = std::move(out);
    result.changed = true;
    return result;
}

} // namespace render::texture_mapping
