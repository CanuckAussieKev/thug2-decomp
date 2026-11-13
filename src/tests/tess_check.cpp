// NOT ORIGINAL - thug2_tess_check.exe: the smoothed curves' tessellation (engine/gfx/smooth_tess.h) on shapes whose
// smooth surface is known - a faceted cylinder with two texture seams, a quarter pipe with its deck and floor (in two
// meshes side by side), an eight-sided rail, flat and hard-edged shapes that must stay as they are - checked: the new
// points on the analytic surface within a tolerance, no cracks (every edge inside a mesh shared by two triangles wound
// opposite ways, positions equal to the bit across seams; meshes meeting only at their original vertices), texture
// coordinates continuous, the deviation from the original under the cap, displacement bounded and off on thin meshes,
// nothing NaN, the original vertices first and unchanged. Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/smooth_tess.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace tess = render::tess;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr uint32_t kStride = 36; // position, normal, colour, one pair of texture coordinates
constexpr int kNormalAt = 12;
constexpr int kColourAt = 24;
constexpr int kUvAt = 28;
constexpr std::size_t kFloat3 = 12;
constexpr std::size_t kFloat2 = 8;
constexpr float kTinyArea = 1.0e-6F;
constexpr float kSlack = 1.0e-4F;
constexpr uint8_t kGrey = 0x80;

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const char* what)
{
    if (!ok) {
        ++failures();
        std::printf("FAIL: %s\n", what);
    }
}

struct P3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};
P3 sub(P3 a, P3 b) { return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z}; }
P3 cross(P3 a, P3 b)
{
    return {.x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x)};
}
float dot(P3 a, P3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }

using Corners = std::array<uint16_t, 3>;

// A mesh being made: vertices (position, normal, colour, uv) and triangles, then a strip of them.
struct Mesh {
    std::vector<uint8_t> bytes;
    uint32_t count = 0;
    std::vector<Corners> triangles;

    uint16_t add(P3 point, P3 normal, std::array<float, 2> uv)
    {
        const std::size_t at = bytes.size();
        bytes.resize(at + kStride);
        const std::span<uint8_t> vertex = std::span(bytes).subspan(at, kStride);
        const std::array<float, 3> position{point.x, point.y, point.z};
        const std::array<float, 3> facing{normal.x, normal.y, normal.z};
        const std::array<uint8_t, 4> colour{kGrey, kGrey / 2, kGrey / 4, kGrey};
        std::memcpy(vertex.data(), position.data(), kFloat3);
        std::memcpy(vertex.subspan(kNormalAt).data(), facing.data(), kFloat3);
        std::memcpy(vertex.subspan(kColourAt).data(), colour.data(), colour.size());
        std::memcpy(vertex.subspan(kUvAt).data(), uv.data(), kFloat2);
        return static_cast<uint16_t>(count++);
    }
    void quad(uint16_t first, uint16_t second, uint16_t third, uint16_t fourth) // (counter-clockwise from outside)
    {
        triangles.push_back({first, second, third});
        triangles.push_back({first, third, fourth});
    }
    // The triangles as one strip: each its own run, joined by degenerate triangles, at an even position.
    [[nodiscard]] std::vector<uint16_t> strip() const
    {
        std::vector<uint16_t> out;
        for (const Corners& triangle : triangles) {
            if (!out.empty()) {
                out.push_back(out.back());
                out.push_back(core::at(triangle, 0));
                if ((out.size() & 1U) != 0U) {
                    out.push_back(core::at(triangle, 0));
                }
            }
            out.insert(out.end(), triangle.begin(), triangle.end());
        }
        return out;
    }
};

tess::VertexLayout layout()
{
    return {.stride = kStride, .normal_offset = kNormalAt, .colour_offset = kColourAt, .uv_offset = kUvAt, .uv_sets = 1};
}

std::span<const uint8_t> vertex_of(std::span<const uint8_t> bytes, uint32_t index)
{
    return bytes.subspan(std::size_t{kStride} * index, kStride);
}
P3 position(std::span<const uint8_t> bytes, uint32_t index)
{
    std::array<float, 3> floats{};
    std::memcpy(floats.data(), vertex_of(bytes, index).data(), kFloat3);
    return {.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
}
std::array<float, 2> uv_of(std::span<const uint8_t> bytes, uint32_t index)
{
    std::array<float, 2> floats{};
    std::memcpy(floats.data(), vertex_of(bytes, index).subspan(kUvAt).data(), kFloat2);
    return floats;
}
bool finite_vertex(std::span<const uint8_t> bytes, uint32_t index)
{
    constexpr std::size_t kPositionAndNormal = 6;
    std::array<float, kPositionAndNormal> floats{};
    std::memcpy(floats.data(), vertex_of(bytes, index).data(), sizeof(floats));
    const std::array<float, 2> uv = uv_of(bytes, index);
    return std::ranges::all_of(floats, [](float value) { return std::isfinite(value); }) && std::isfinite(core::at(uv, 0)) &&
           std::isfinite(core::at(uv, 1));
}

using Bits = std::tuple<uint32_t, uint32_t, uint32_t>;
Bits bits_of(P3 point)
{
    std::array<uint32_t, 3> bits{};
    const std::array<float, 3> floats{point.x, point.y, point.z};
    std::memcpy(bits.data(), floats.data(), sizeof(bits));
    return {core::at(bits, 0), core::at(bits, 1), core::at(bits, 2)};
}
P3 point_of(const Bits& bits)
{
    const std::array<uint32_t, 3> words{std::get<0>(bits), std::get<1>(bits), std::get<2>(bits)};
    std::array<float, 3> floats{};
    std::memcpy(floats.data(), words.data(), sizeof(floats));
    return {.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
}

// The result's drawn triangles (not degenerate by position), each edge counted by welded position and direction:
// every edge not on `boundary` must be used once each way. The number of edges that aren't.
int open_edges(const tess::Result& result, bool (*boundary)(P3, P3))
{
    std::map<Bits, int> weld;
    std::map<std::pair<int, int>, int> directed;
    for (const Corners& triangle : tess::strip_triangles(result.strip)) {
        std::array<int, 3> welded{};
        for (std::size_t k = 0; k < 3; ++k) {
            const Bits bits = bits_of(position(result.vertices, core::at(triangle, k)));
            core::at(welded, k) = weld.try_emplace(bits, static_cast<int>(weld.size())).first->second;
        }
        if (std::get<0>(welded) == std::get<1>(welded) || std::get<1>(welded) == std::get<2>(welded) ||
            std::get<0>(welded) == std::get<2>(welded)) {
            continue; // (a sliver along a straight edge)
        }
        for (std::size_t k = 0; k < 3; ++k) {
            ++directed[{core::at(welded, k), core::at(welded, (k + 1) % 3)}];
        }
    }
    std::map<int, P3> where;
    for (const auto& [bits, id] : weld) {
        where.emplace(id, point_of(bits));
    }
    int open = 0;
    for (const auto& [edge, uses] : directed) {
        const auto back = directed.find({edge.second, edge.first});
        const int other = back == directed.end() ? 0 : back->second;
        if ((uses != 1 || other != 1) && !boundary(where.at(edge.first), where.at(edge.second))) {
            ++open;
        }
    }
    return open;
}

// --- the cylinder ---

constexpr float kRadius = 100.0F;
constexpr float kHeight = 200.0F;
constexpr int kSides = 16;
constexpr int kRings = 4;
constexpr int kSeamRing = 2; // (the ring made twice)
constexpr int kCylinderLevel = 4;
constexpr float kRoomy = 10.0F;    // a cap the cylinder's curve doesn't reach
constexpr float kTightCap = 0.5F;  // one it does
constexpr float kDisplaceCap = 2.0F;
constexpr float kDisplaceScale = 1.5F;
constexpr float kRidges = 4.0F;

// The ring a row of vertices is on (the seam ring's two copies: rows kSeamRing and kSeamRing + 1).
int ring_of(int row) { return row <= kSeamRing ? row : row - 1; }

// The v of a row (the seam ring's two copies: 1 below, 0 above).
float row_v(int row)
{
    if (row == kSeamRing) {
        return 1.0F;
    }
    if (row == kSeamRing + 1) {
        return 0.0F;
    }
    return static_cast<float>(ring_of(row)) / kRings;
}

Mesh cylinder(float radius, int sides)
{
    Mesh mesh;
    // Two texture seams: down the side (side `sides` is side 0 again, u 1 and 0) and round the middle ring (made twice:
    // v 1 for the faces below it, 0 for those above).
    std::vector<std::vector<uint16_t>> at(static_cast<std::size_t>(kRings) + 2);
    for (int row = 0; row <= kRings + 1; ++row) {
        const float y = kHeight * static_cast<float>(ring_of(row)) / static_cast<float>(kRings);
        for (int side = 0; side <= sides; ++side) {
            const float angle = 2.0F * kPi * static_cast<float>(side % sides) / static_cast<float>(sides);
            const P3 normal{.x = std::cos(angle), .y = 0.0F, .z = std::sin(angle)};
            core::at(at, row).push_back(mesh.add({.x = radius * normal.x, .y = y, .z = radius * normal.z}, normal,
                                                 {static_cast<float>(side) / static_cast<float>(sides), row_v(row)}));
        }
    }
    for (std::size_t row = 0; row <= static_cast<std::size_t>(kRings); ++row) {
        if (row == static_cast<std::size_t>(kSeamRing)) {
            continue; // (the seam ring's two copies: no faces between them)
        }
        const auto& below = core::at(at, row);
        const auto& above = core::at(at, row + 1);
        for (int side = 0; side < sides; ++side) {
            mesh.quad(core::at(below, side), core::at(above, side), core::at(above, side + 1), core::at(below, side + 1));
        }
    }
    return mesh;
}

bool on_cylinder_rims(P3 a, P3 b) { return (a.y == 0.0F && b.y == 0.0F) || (a.y == kHeight && b.y == kHeight); }

float radial(P3 point) { return std::sqrt((point.x * point.x) + (point.z * point.z)); }

float ridges(const void* /*context*/, float u, float /*v*/)
{
    constexpr float kMiddle = 0.5F;
    return kMiddle + (kMiddle * std::sin(u * kRidges * kPi * 2));
}
float highest(const void* /*context*/, float /*u*/, float /*v*/) { return 1.0F; }

void check_cylinder()
{
    const Mesh mesh = cylinder(kRadius, kSides);
    const std::vector<uint16_t> strip = mesh.strip();
    tess::Options options;
    options.max_level = kCylinderLevel;
    options.max_deviation = kRoomy;
    const tess::Result result = tess::smooth(mesh.bytes, mesh.count, layout(), strip, options);
    expect(result.changed, "cylinder: smoothed");
    expect(result.smoothed == result.triangles && result.triangles == 2U * kSides * kRings,
           "cylinder: every triangle split");
    expect(result.level == kCylinderLevel, "cylinder: level 4");
    expect(std::ranges::equal(std::span(result.vertices).first(mesh.bytes.size()), mesh.bytes),
           "cylinder: original vertices first");
    const float facet = kRadius * (1.0F - std::cos(kPi / kSides));
    float worst = 0.0F;
    bool finite = true;
    for (uint32_t i = 0; i < result.vertex_count; ++i) {
        finite = finite && finite_vertex(result.vertices, i);
        worst = std::max(worst, std::fabs(radial(position(result.vertices, i)) - kRadius));
    }
    std::printf("cylinder: %u -> %u vertices, %zu -> %zu indices; off the circle at most %.5f (facets %.4f); "
                "deviation %.4f\n",
                mesh.count, result.vertex_count, strip.size(), result.strip.size(), static_cast<double>(worst),
                static_cast<double>(facet), static_cast<double>(result.deviation));
    constexpr float kCircleShare = 0.02F;
    expect(finite, "cylinder: nothing NaN");
    expect(worst < facet * kCircleShare, "cylinder: the new points on the circle (2% of the facets' error)");
    expect(open_edges(result, on_cylinder_rims) == 0, "cylinder: no cracks (every inner edge shared, wound opposite)");
    // Winding kept (every drawn triangle faces out); texture coordinates continuous (no triangle spans more than its
    // share of the texture).
    bool outward = true;
    float widest_u = 0.0F;
    for (const Corners& triangle : tess::strip_triangles(result.strip)) {
        const P3 first = position(result.vertices, core::at(triangle, 0));
        const P3 normal = cross(sub(position(result.vertices, core::at(triangle, 1)), first),
                                sub(position(result.vertices, core::at(triangle, 2)), first));
        if (dot(normal, normal) > kTinyArea) {
            outward = outward && dot(normal, {.x = first.x, .y = 0.0F, .z = first.z}) > 0.0F;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            const float u0 = std::get<0>(uv_of(result.vertices, core::at(triangle, k)));
            const float u1 = std::get<0>(uv_of(result.vertices, core::at(triangle, (k + 1) % 3)));
            widest_u = std::max(widest_u, std::fabs(u0 - u1));
        }
    }
    expect(outward, "cylinder: winding kept (every triangle faces out)");
    expect(widest_u <= (1.0F / kSides) + kSlack, "cylinder: texture coordinates continuous");
    // The middle ring's seam: its curved edges' new points made on both sides (v 1 below, 0 above), at the same
    // positions to the bit.
    std::map<Bits, std::vector<float>> seam;
    for (uint32_t i = mesh.count; i < result.vertex_count; ++i) {
        const P3 point = position(result.vertices, i);
        if (point.y == kHeight * kSeamRing / kRings) {
            seam[bits_of(point)].push_back(std::get<1>(uv_of(result.vertices, i)));
        }
    }
    bool paired = seam.size() == static_cast<std::size_t>(kSides) * (kCylinderLevel - 1); // (new points an edge)
    for (const auto& [bits, vs] : seam) {
        paired = paired && vs.size() == 2 && std::ranges::count(vs, 0.0F) == 1 && std::ranges::count(vs, 1.0F) == 1;
    }
    expect(paired, "cylinder: the seam's new points made on both sides at the same positions (v 1 and 0)");
    // Capped: the shape factor scaled down to keep the points within the cap.
    tess::Options capped = options;
    capped.max_deviation = kTightCap;
    const tess::Result tight = tess::smooth(mesh.bytes, mesh.count, layout(), strip, capped);
    float furthest = 0.0F;
    for (uint32_t i = 0; i < tight.vertex_count; ++i) {
        furthest = std::max(furthest, kRadius - radial(position(tight.vertices, i)));
    }
    std::printf("cylinder capped at 0.5: deviation %.4f (uncapped %.4f), shape %.3f\n",
                static_cast<double>(tight.deviation), static_cast<double>(tight.uncapped),
                static_cast<double>(tight.shape));
    expect(tight.capped && tight.deviation <= kTightCap + kSlack, "cylinder: deviation capped");
    expect(furthest <= facet + kSlack, "cylinder: capped points between the facets and the circle");
    // Displacement (a test height map: ridges round it) within the cap; the seam not opened.
    tess::Options displaced = options;
    displaced.max_deviation = kDisplaceCap;
    displaced.height = {.lookup = ridges, .context = nullptr, .scale = kDisplaceScale};
    const tess::Result moved = tess::smooth(mesh.bytes, mesh.count, layout(), strip, displaced);
    std::printf("cylinder displaced: displacement up to %.4f, deviation %.4f\n", static_cast<double>(moved.displaced),
                static_cast<double>(moved.deviation));
    constexpr float kSomeDisplacement = 0.1F;
    expect(moved.displaced > kSomeDisplacement && moved.deviation <= kDisplaceCap + kSlack,
           "cylinder: displaced within the cap");
    expect(open_edges(moved, on_cylinder_rims) == 0, "cylinder: displaced without cracks");
}

void check_rail()
{
    constexpr float kRail = 3.0F;
    constexpr int kRailSides = 8;
    constexpr float kRailShare = 0.1F;
    const Mesh mesh = cylinder(kRail, kRailSides);
    tess::Options options;
    options.height = {.lookup = highest, .context = nullptr, .scale = 1.0F};
    const tess::Result result = tess::smooth(mesh.bytes, mesh.count, layout(), mesh.strip(), options);
    float worst = 0.0F;
    for (uint32_t i = 0; i < result.vertex_count; ++i) {
        worst = std::max(worst, std::fabs(radial(position(result.vertices, i)) - kRail));
    }
    const float facet = kRail * (1.0F - std::cos(kPi / kRailSides));
    std::printf("rail: level %d, off the circle at most %.5f (facets %.4f)\n", result.level, static_cast<double>(worst),
                static_cast<double>(facet));
    expect(result.changed && result.smoothed == result.triangles, "rail: smoothed (45-degree edges under the crease)");
    expect(worst < facet * kRailShare, "rail: on the circle (10% of the facets' error)");
    expect(result.displaced == 0.0F, "rail: thin, not displaced");
    expect(open_edges(result, on_cylinder_rims) == 0, "rail: no cracks");
}

// --- the quarter pipe: a floor, a quarter circle and a deck, in two meshes side by side ---

constexpr float kPipeRadius = 120.0F;
constexpr int kPipeSegments = 8;
constexpr float kFloor = 100.0F;
constexpr float kDeck = 80.0F;
constexpr float kSideX = 60.0F; // (the meshes: x -60 to 0 and 0 to 60)

struct ProfilePoint {
    P3 point;
    P3 normal;
};

// The profile (z, y) and its normals, from the floor's far edge to the deck's back.
std::vector<ProfilePoint> pipe_profile()
{
    const P3 up{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    std::vector<ProfilePoint> out;
    out.push_back({.point = {.x = 0.0F, .y = 0.0F, .z = -kFloor}, .normal = up});
    for (int segment = 0; segment <= kPipeSegments; ++segment) {
        const float angle = (kPi / 2.0F) * static_cast<float>(segment) / static_cast<float>(kPipeSegments);
        // (the circle's centre at (z 0, y R): the surface at `angle` below it, toward +z)
        out.push_back(
            {.point = {.x = 0.0F, .y = kPipeRadius - (kPipeRadius * std::cos(angle)), .z = kPipeRadius * std::sin(angle)},
             .normal = {.x = 0.0F, .y = std::cos(angle), .z = -std::sin(angle)}});
    }
    out.push_back({.point = {.x = 0.0F, .y = kPipeRadius, .z = kPipeRadius + kDeck}, .normal = up});
    return out;
}

Mesh pipe(float x0, float x1)
{
    Mesh mesh;
    const auto profile = pipe_profile();
    std::vector<std::array<uint16_t, 2>> at;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const auto& [point, normal] = core::at(profile, i);
        const float v = static_cast<float>(i) / static_cast<float>(profile.size() - 1);
        at.push_back({mesh.add({.x = x0, .y = point.y, .z = point.z}, normal, {0.0F, v}),
                      mesh.add({.x = x1, .y = point.y, .z = point.z}, normal, {1.0F, v})});
    }
    for (std::size_t i = 0; i + 1 < at.size(); ++i) {
        const auto& lower = core::at(at, i);
        const auto& upper = core::at(at, i + 1);
        mesh.quad(core::at(lower, 0), core::at(upper, 0), core::at(upper, 1), core::at(lower, 1)); // (facing up, -z)
    }
    return mesh;
}

// The distance of a point inside the pipe's arc from the circle (y, z in the circle's plane).
float off_arc(P3 point)
{
    return std::fabs(std::sqrt((point.z * point.z) + ((point.y - kPipeRadius) * (point.y - kPipeRadius))) - kPipeRadius);
}

bool on_pipe_sides(P3 a, P3 b)
{
    // (the meshes' outer edges: the floor's far edge, the deck's back, the two ends; and the side the two share)
    const auto end = [](P3 point) { return point.x == -kSideX || point.x == 0.0F || point.x == kSideX; };
    return (end(a) && end(b) && a.x == b.x) || (a.z == -kFloor && b.z == -kFloor) ||
           (a.z == kPipeRadius + kDeck && b.z == kPipeRadius + kDeck);
}

// The points a result draws on the plane x = 0 (where the two meshes meet).
std::vector<Bits> on_shared_side(const tess::Result& result)
{
    std::vector<Bits> out;
    for (const Corners& triangle : tess::strip_triangles(result.strip)) {
        for (const uint16_t index : triangle) {
            const P3 point = position(result.vertices, index);
            if (point.x == 0.0F) {
                out.push_back(bits_of(point));
            }
        }
    }
    std::ranges::sort(out);
    const auto repeated = std::ranges::unique(out);
    out.erase(repeated.begin(), repeated.end());
    return out;
}

void check_pipe()
{
    const Mesh left = pipe(-kSideX, 0.0F);
    const Mesh right = pipe(0.0F, kSideX);
    const tess::Options options;
    const tess::Result one = tess::smooth(left.bytes, left.count, layout(), left.strip(), options);
    const tess::Result two = tess::smooth(right.bytes, right.count, layout(), right.strip(), options);
    expect(one.changed && two.changed, "quarter pipe: smoothed");
    const float facet = kPipeRadius * (1.0F - std::cos(kPi / 4.0F / kPipeSegments));
    float ends = 0.0F;
    float middle = 0.0F;
    float deck = 0.0F;
    bool finite = true;
    for (uint32_t i = 0; i < one.vertex_count; ++i) {
        const P3 point = position(one.vertices, i);
        finite = finite && finite_vertex(one.vertices, i);
        if (point.z > kPipeRadius + 1.0F) {
            deck = std::max(deck, std::fabs(point.y - kPipeRadius)); // (the deck: past the coping's crease, flat)
        } else if (point.z > 0.0F && point.y < kPipeRadius) {
            // (the first and last segments' corners have one side's normals only - the floor's tilt, the coping's
            // crease -: the arc is followed best between them)
            const float segment = std::atan2(point.z, kPipeRadius - point.y) / (kPi / 2.0F) * kPipeSegments;
            float& slot = segment > 1.0F && segment < kPipeSegments - 1.0F ? middle : ends;
            slot = std::max(slot, off_arc(point));
        }
    }
    std::printf("quarter pipe: level %d, %u of %u triangles split, %u -> %u vertices; off the arc at most %.4f (%.4f "
                "between the end segments) (facets %.4f); deviation %.4f; deck moved %.6f\n",
                one.level, one.smoothed, one.triangles, left.count, one.vertex_count, static_cast<double>(std::max(ends, middle)),
                static_cast<double>(middle), static_cast<double>(facet), static_cast<double>(one.deviation),
                static_cast<double>(deck));
    constexpr float kEndShare = 0.5F;
    constexpr float kMiddleShare = 0.05F;
    expect(finite, "quarter pipe: nothing NaN");
    expect(deck == 0.0F, "quarter pipe: the deck behind the coping (a crease) untouched");
    expect(std::max(ends, middle) < facet * kEndShare, "quarter pipe: closer to the arc than the facets (half their error)");
    expect(middle < facet * kMiddleShare, "quarter pipe: on the arc between the end segments (5% of the facets' error)");
    expect(one.deviation <= options.max_deviation, "quarter pipe: deviation under the cap");
    expect(open_edges(one, on_pipe_sides) == 0 && open_edges(two, on_pipe_sides) == 0, "quarter pipe: no cracks inside");
    // Where the two meshes meet (x 0), each keeps only its original vertices: the same points, no T-junctions.
    expect(on_shared_side(one) == on_shared_side(two) && on_shared_side(one).size() == pipe_profile().size(),
           "quarter pipe: the two meshes meet at their original vertices only");
}

// --- shapes that stay as they are ---

void check_unchanged()
{
    constexpr int kGrid = 4;
    constexpr float kCell = 10.0F;
    Mesh flat;
    for (int row = 0; row < kGrid; ++row) {
        for (int column = 0; column < kGrid; ++column) {
            flat.add({.x = static_cast<float>(column) * kCell, .y = 0.0F, .z = static_cast<float>(row) * kCell},
                     {.x = 0.0F, .y = 1.0F, .z = 0.0F}, {0.0F, 0.0F});
        }
    }
    for (int row = 0; row + 1 < kGrid; ++row) {
        for (int column = 0; column + 1 < kGrid; ++column) {
            const auto first = static_cast<uint16_t>((row * kGrid) + column);
            flat.quad(first, static_cast<uint16_t>(first + kGrid), static_cast<uint16_t>(first + kGrid + 1),
                      static_cast<uint16_t>(first + 1));
        }
    }
    expect(!tess::smooth(flat.bytes, flat.count, layout(), flat.strip(), {}).changed, "a flat floor: unchanged");
    // A box: every edge 90 degrees - creases.
    constexpr float kBox = 50.0F;
    Mesh box;
    constexpr std::array<std::array<float, 3>, 8> kCorners{{{0, 0, 0},
                                                            {kBox, 0, 0},
                                                            {kBox, kBox, 0},
                                                            {0, kBox, 0},
                                                            {0, 0, kBox},
                                                            {kBox, 0, kBox},
                                                            {kBox, kBox, kBox},
                                                            {0, kBox, kBox}}};
    for (const auto& corner : kCorners) {
        box.add({.x = core::at(corner, 0), .y = core::at(corner, 1), .z = core::at(corner, 2)}, {}, {0.0F, 0.0F});
    }
    constexpr std::array<std::array<uint16_t, 4>, 6> kFaces{
        {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}};
    for (const auto& face : kFaces) {
        box.quad(core::at(face, 0), core::at(face, 1), core::at(face, 2), core::at(face, 3));
    }
    expect(!tess::smooth(box.bytes, box.count, layout(), box.strip(), {}).changed, "a box: unchanged");
    // Too big to split even in two (16-bit indices): left as it is, or within 16 bits.
    constexpr float kHuge = 20000.0F;
    constexpr int kManySides = 2000;
    const Mesh big = cylinder(kHuge, kManySides);
    const tess::Result result = tess::smooth(big.bytes, big.count, layout(), big.strip(), {});
    constexpr std::size_t kMost16 = 0xFFFF;
    expect(!result.changed || (result.vertex_count <= kMost16 && result.strip.size() <= kMost16),
           "a big mesh: within 16 bits");
    // Level 1 or 0: nothing done.
    tess::Options off;
    off.max_level = 1;
    const Mesh round = cylinder(kRadius, kSides);
    expect(!tess::smooth(round.bytes, round.count, layout(), round.strip(), off).changed, "rt_tess_level 1: unchanged");
}

// strip_triangles: odd triangles turned round, degenerate ones left out.
void check_strip()
{
    constexpr uint16_t kLast = 5;
    constexpr std::array<uint16_t, 7> kStrip{0, 1, 2, 3, 3, 4, kLast};
    const auto triangles = tess::strip_triangles(kStrip);
    expect(triangles.size() == 3 && core::at(triangles, 0) == Corners{0, 1, 2} && core::at(triangles, 1) == Corners{2, 1, 3} &&
               core::at(triangles, 2) == Corners{3, 4, kLast},
           "strip_triangles: order and winding");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_strip();
    check_cylinder();
    check_rail();
    check_pipe();
    check_unchanged();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("tess_check: all checks passed\n");
    return 0;
}
