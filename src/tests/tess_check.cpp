// NOT ORIGINAL - thug2_tess_check.exe: the smoothed curves' tessellation (engine/gfx/smooth_tess.h) on shapes whose
// smooth surface is known - a faceted cylinder with two texture seams, a quarter pipe with its deck and floor (in two
// meshes side by side), an eight-sided rail, flat and hard-edged shapes that must stay as they are - checked: the new
// points on the analytic surface within a tolerance, no cracks (every edge inside a mesh shared by two triangles wound
// opposite ways, positions equal to the bit across seams; meshes meeting only at their original vertices), texture
// coordinates continuous, the deviation from the original under the cap, displacement bounded and off on thin meshes,
// nothing NaN, the original vertices first and unchanged.
// Smoothing (Options::smooth, rt_tess_smooth): a flat-shaded six-sided rail made round (a prism) within a tolerance of
// the circle, crack-free and the same bytes twice; the strength's half pull; a wide column, a square beam, a box with
// averaged normals and a ledge's flat-shaded chamfer kept crisp; a big round shape held to the cap.
// `thug2_tess_check --level <a level's folder>...`: the game's own scene and collision read (offline) and every mesh the
// game would smooth smoothed with rt_tess_smooth 0 and 1 - the counts, a hash of the results (rt_tess_smooth=0 must
// hash as the build before), and the new points' gap to the collision on its quarter pipes and other slopes.
// Also the water's grid (engine/gfx/water_tess.h - rt_water_geometry): flat water quads split - every drawn triangle's
// edges at most 1.5 x the spacing, wound as the original, covering it exactly (the same area, every inside edge shared by two
// triangles wound opposite ways), texture coordinates and colours blended as the rasteriser would; two meshes side by
// side with different shapes meeting at the same points to the bit; a waterfall, a small quad and a strip index past
// the vertices left alone; a huge quad within 16 bits. Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/smooth_tess.h"
#include "engine/gfx/water_tess.h"
#include "formats/scene_format.h"
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
template <typename Result>
int open_edges(const Result& result, bool (*boundary)(P3, P3))
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

// --- smoothing (Options::smooth, rt_tess_smooth) ---

tess::Options smoothing(float strength = 1.0F)
{
    tess::Options options;
    options.smooth = true;
    options.smooth_strength = strength;
    return options;
}

bool same_result(const tess::Result& a, const tess::Result& b)
{
    return a.changed == b.changed && a.vertices == b.vertices && a.strip == b.strip && a.vertex_count == b.vertex_count;
}

// A flat-shaded prism along y: `sides` faces of their own vertices (each its face's normal, so the normals are split
// at every edge), `rings` segments high, open at both ends.
Mesh flat_prism(float radius, int sides, int rings)
{
    Mesh mesh;
    for (int side = 0; side < sides; ++side) {
        const float a0 = 2.0F * kPi * static_cast<float>(side) / static_cast<float>(sides);
        const float a1 = 2.0F * kPi * static_cast<float>((side + 1) % sides) / static_cast<float>(sides); // (welded)
        const float middle = 2.0F * kPi * (static_cast<float>(side) + 0.5F) / static_cast<float>(sides);
        const P3 normal{.x = std::cos(middle), .y = 0.0F, .z = std::sin(middle)};
        std::vector<std::array<uint16_t, 2>> at;
        for (int ring = 0; ring <= rings; ++ring) {
            const float y = kHeight * static_cast<float>(ring) / static_cast<float>(rings);
            const float v = static_cast<float>(ring) / static_cast<float>(rings);
            at.push_back({mesh.add({.x = radius * std::cos(a0), .y = y, .z = radius * std::sin(a0)}, normal, {0.0F, v}),
                          mesh.add({.x = radius * std::cos(a1), .y = y, .z = radius * std::sin(a1)}, normal, {1.0F, v})});
        }
        for (int ring = 0; ring < rings; ++ring) {
            const auto& below = core::at(at, ring);
            const auto& above = core::at(at, ring + 1);
            mesh.quad(core::at(below, 0), core::at(above, 0), core::at(above, 1), core::at(below, 1));
        }
    }
    return mesh;
}

// A ledge's profile along x (length kLedge): a front face, a 45-degree bevel and a top, flat-shaded (each face its own
// vertices and normal) - an edge meant to be hard under the crease angle.
Mesh bevelled_ledge()
{
    constexpr float kLedge = 200.0F;
    constexpr float kTall = 20.0F;
    constexpr float kBevel = 4.0F;
    constexpr float kDepth = 30.0F;
    const float diagonal = std::numbers::sqrt2_v<float> * 0.5F;
    // (the profile in (z, y): from the front's foot up, over the bevel, back along the top)
    const std::array<std::array<float, 2>, 4> profile{{{0.0F, 0.0F}, {0.0F, kTall - kBevel}, {kBevel, kTall}, {kDepth, kTall}}};
    const std::array<P3, 3> normals{P3{.x = 0.0F, .y = 0.0F, .z = -1.0F}, P3{.x = 0.0F, .y = diagonal, .z = -diagonal},
                                    P3{.x = 0.0F, .y = 1.0F, .z = 0.0F}};
    Mesh mesh;
    for (std::size_t face = 0; face < normals.size(); ++face) {
        const auto& low = core::at(profile, face);
        const auto& high = core::at(profile, face + 1);
        const P3 normal = core::at(normals, face);
        const uint16_t a = mesh.add({.x = 0.0F, .y = core::at(low, 1), .z = core::at(low, 0)}, normal, {0.0F, 0.0F});
        const uint16_t b = mesh.add({.x = 0.0F, .y = core::at(high, 1), .z = core::at(high, 0)}, normal, {0.0F, 1.0F});
        const uint16_t c = mesh.add({.x = kLedge, .y = core::at(high, 1), .z = core::at(high, 0)}, normal, {1.0F, 1.0F});
        const uint16_t d = mesh.add({.x = kLedge, .y = core::at(low, 1), .z = core::at(low, 0)}, normal, {1.0F, 0.0F});
        mesh.quad(a, b, c, d);
    }
    return mesh;
}

// A box of shared corners whose normals are averaged (an export that smoothed everything): its 90-degree edges stay hard.
Mesh smoothed_box()
{
    constexpr float kBox = 50.0F;
    Mesh box;
    for (int corner = 0; corner < 8; ++corner) {
        const P3 point{.x = (corner & 1) != 0 ? kBox : 0.0F, .y = (corner & 2) != 0 ? kBox : 0.0F, .z = (corner & 4) != 0 ? kBox : 0.0F};
        const float third = 1.0F / std::sqrt(3.0F);
        const P3 normal{.x = (corner & 1) != 0 ? third : -third, .y = (corner & 2) != 0 ? third : -third,
                        .z = (corner & 4) != 0 ? third : -third};
        box.add(point, normal, {0.0F, 0.0F});
    }
    // (counter-clockwise from outside)
    constexpr std::array<std::array<uint16_t, 4>, 6> kFaces{
        {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}}};
    for (const auto& face : kFaces) {
        box.quad(core::at(face, 0), core::at(face, 1), core::at(face, 2), core::at(face, 3));
    }
    return box;
}

bool on_prism_rims(P3 a, P3 b) { return on_cylinder_rims(a, b); }

void check_smooth()
{
    // A flat-shaded six-sided rail: hard (60 degrees, over the crease) without smoothing, round with it - a prism.
    constexpr float kRail = 3.0F;
    constexpr int kRailSides = 6;
    constexpr int kRailRings = 4;
    const Mesh rail = flat_prism(kRail, kRailSides, kRailRings);
    const std::vector<uint16_t> rail_strip = rail.strip();
    expect(!tess::smooth(rail.bytes, rail.count, layout(), rail_strip, {}).changed,
           "smoothing off: a six-sided rail unchanged (its edges over the crease angle)");
    const tess::Result round = tess::smooth(rail.bytes, rail.count, layout(), rail_strip, smoothing());
    const float facet = kRail * (1.0F - std::cos(kPi / kRailSides));
    float worst = 0.0F;
    bool finite = true;
    for (uint32_t i = 0; i < round.vertex_count; ++i) {
        finite = finite && finite_vertex(round.vertices, i);
        worst = std::max(worst, std::fabs(radial(position(round.vertices, i)) - kRail));
    }
    // The flat facets' worst against the round one: the drawn surface's furthest point inside the circle, sampled
    // densely across each drawn triangle (its silhouette) - in the middle rings (the open rims are boundaries, kept
    // straight).
    float drawn = 0.0F;
    constexpr float kRing = kHeight / kRailRings;
    for (const Corners& triangle : tess::strip_triangles(round.strip)) {
        constexpr int kSamples = 8;
        const P3 a = position(round.vertices, core::at(triangle, 0));
        const P3 b = position(round.vertices, core::at(triangle, 1));
        const P3 c = position(round.vertices, core::at(triangle, 2));
        if (std::min({a.y, b.y, c.y}) < kRing || std::max({a.y, b.y, c.y}) > kHeight - kRing) {
            continue;
        }
        for (int i = 0; i <= kSamples; ++i) {
            for (int j = 0; j <= kSamples - i; ++j) {
                const float u = static_cast<float>(i) / kSamples;
                const float v = static_cast<float>(j) / kSamples;
                const P3 point{.x = a.x + (u * (b.x - a.x)) + (v * (c.x - a.x)), .y = 0.0F,
                               .z = a.z + (u * (b.z - a.z)) + (v * (c.z - a.z))};
                drawn = std::max(drawn, kRail - radial(point));
            }
        }
    }
    std::printf("smoothing: six-sided rail (flat-shaded, radius 3): level %d, %u prism edges, %u -> %u vertices, %zu -> "
                "%zu indices; new points off the circle at most %.5f; drawn surface inside the circle at most %.4f "
                "(the facets %.4f)\n",
                round.level, round.prism_edges, rail.count, round.vertex_count, rail_strip.size(), round.strip.size(),
                static_cast<double>(worst), static_cast<double>(drawn), static_cast<double>(facet));
    constexpr float kPointShare = 0.1F;
    constexpr float kDrawnShare = 0.15F;
    expect(round.changed && round.smoothed == round.triangles, "smoothing: the six-sided rail rounded (a prism)");
    expect(round.prism_edges == static_cast<uint32_t>(kRailSides * kRailRings), "smoothing: every side edge a prism's");
    expect(round.level == tess::kSmoothLevel, "smoothing: level 4 (60-degree edges, 15 degrees a piece)");
    expect(finite, "smoothing: the rail has nothing NaN");
    expect(worst < facet * kPointShare, "smoothing: the rail's new points on the circle (10% of the facets' error)");
    expect(drawn < facet * kDrawnShare, "smoothing: the rail's drawn surface round within 15% of the facets' error");
    expect(round.displaced == 0.0F && round.deviation <= tess::kDefaultMaxDeviation, "smoothing: the rail within the cap");
    expect(open_edges(round, on_prism_rims) == 0, "smoothing: the rail without cracks (split normals, welded edges)");
    expect(std::ranges::equal(std::span(round.vertices).first(rail.bytes.size()), rail.bytes),
           "smoothing: the rail's original vertices first and unchanged");
    // Deterministic: the same mesh smoothed twice, the same bytes.
    expect(same_result(round, tess::smooth(rail.bytes, rail.count, layout(), rail_strip, smoothing())),
           "smoothing: deterministic (the rail smoothed twice, the same bytes)");
    // The strength: half the pull (between the facets and the circle).
    const tess::Result half = tess::smooth(rail.bytes, rail.count, layout(), rail_strip, smoothing(0.5F));
    std::printf("smoothing: the rail at strength 0.5: deviation %.4f (1: %.4f)\n", static_cast<double>(half.deviation),
                static_cast<double>(round.deviation));
    constexpr float kHalfLow = 0.4F;
    constexpr float kHalfHigh = 0.6F;
    expect(half.changed && half.deviation > round.deviation * kHalfLow && half.deviation < round.deviation * kHalfHigh,
           "smoothing: strength 0.5 pulls half as far");
    expect(!tess::smooth(rail.bytes, rail.count, layout(), rail_strip, smoothing(0.0F)).changed,
           "smoothing: strength 0 changes nothing");
    // A wide hexagonal column (sides 40 wide): not a prism - stays hard.
    const Mesh column = flat_prism(40.0F, kRailSides, kRailRings);
    expect(!tess::smooth(column.bytes, column.count, layout(), column.strip(), smoothing()).changed,
           "smoothing: a wide six-sided column stays faceted (not a rail)");
    // A four-sided beam: 90 degrees, a box's edges - hard.
    const Mesh beam = flat_prism(kRail, 4, kRailRings);
    expect(!tess::smooth(beam.bytes, beam.count, layout(), beam.strip(), smoothing()).changed,
           "smoothing: a square beam keeps its corners");
    // A box with averaged normals keeps its corners.
    const Mesh box = smoothed_box();
    expect(!tess::smooth(box.bytes, box.count, layout(), box.strip(), smoothing()).changed,
           "smoothing: a box with averaged normals keeps its corners");
    // A ledge's 45-degree bevel, flat-shaded: rounded by the crease angle alone, kept crisp by its split normals.
    const Mesh ledge = bevelled_ledge();
    const tess::Result by_angle = tess::smooth(ledge.bytes, ledge.count, layout(), ledge.strip(), {});
    const tess::Result by_normals = tess::smooth(ledge.bytes, ledge.count, layout(), ledge.strip(), smoothing());
    std::printf("smoothing: a bevelled ledge: off %s, on %s (%u edges kept hard by their normals)\n",
                by_angle.changed ? "rounded" : "unchanged", by_normals.changed ? "rounded" : "unchanged",
                by_normals.normal_hard);
    expect(by_angle.changed, "smoothing off: the ledge's bevel rounded (under the crease angle)");
    expect(!by_normals.changed && by_normals.normal_hard == 2, "smoothing: the ledge's bevel kept crisp (split normals)");
    // The same cylinder with the cap: a shape whose curve would leave the original by more is held to it.
    const Mesh big = cylinder(kRadius, kRailSides);
    tess::Options capped = smoothing();
    capped.max_deviation = 1.0F;
    const tess::Result held = tess::smooth(big.bytes, big.count, layout(), big.strip(), capped);
    std::printf("smoothing: a six-sided cylinder of radius 100 (shared normals): deviation %.4f (uncapped %.4f)\n",
                static_cast<double>(held.deviation), static_cast<double>(held.uncapped));
    expect(held.changed && held.capped && held.deviation <= capped.max_deviation + kSlack,
           "smoothing: displacement capped (a big round shape held to 1 unit)");
    expect(open_edges(held, on_cylinder_rims) == 0, "smoothing: the capped cylinder without cracks");
    // The cylinder of the first checks, smoothed: still round, crack-free across its seams.
    const Mesh seams = cylinder(kRadius, kSides);
    const tess::Result seamed = tess::smooth(seams.bytes, seams.count, layout(), seams.strip(), smoothing());
    expect(seamed.changed && open_edges(seamed, on_cylinder_rims) == 0, "smoothing: the seamed cylinder without cracks");
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

// --- the water's grid (water_tess.h) ---

namespace water_tess = render::water_tess;

constexpr float kWaterTile = 64.0F; // (the quads' texture coordinates: the position / this)

// A flat quad at y = 0 from x0 to x1, z0 to z1 (two triangles facing up; texture coordinates the position / 64).
Mesh water_quad(float x0, float x1, float z0, float z1, bool flip = false)
{
    constexpr P3 kUp{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    Mesh mesh;
    const auto at = [&](float x, float z) { return mesh.add({.x = x, .y = 0.0F, .z = z}, kUp, {x / kWaterTile, z / kWaterTile}); };
    const uint16_t a = at(x0, z0);
    const uint16_t b = at(x1, z0);
    const uint16_t c = at(x1, z1);
    const uint16_t d = at(x0, z1);
    if (flip) {
        mesh.quad(a, d, c, b);
    } else {
        mesh.quad(a, b, c, d);
    }
    return mesh;
}

float signed_area_y(P3 a, P3 b, P3 c) { return 0.5F * cross(sub(b, a), sub(c, a)).y; }

// The drawn triangles' area (the y of their normals), the longest edge of those with an area, and how many of those are
// wound against `sign`.
struct Cover {
    double area = 0.0;
    float longest = 0.0F;
    int backwards = 0;
};
Cover cover_of(std::span<const uint8_t> vertices, std::span<const uint16_t> strip, float sign)
{
    Cover cover;
    for (const Corners& triangle : tess::strip_triangles(strip)) {
        const P3 a = position(vertices, core::at(triangle, 0));
        const P3 b = position(vertices, core::at(triangle, 1));
        const P3 c = position(vertices, core::at(triangle, 2));
        const float area = signed_area_y(a, b, c);
        cover.area += area;
        if (std::fabs(area) <= kTinyArea) {
            continue; // (a sliver along an edge with fewer pieces)
        }
        cover.backwards += area * sign < 0.0F ? 1 : 0;
        for (const auto& [from, to] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
            const P3 edge = sub(to, from);
            cover.longest = std::max(cover.longest, std::sqrt(dot(edge, edge)));
        }
    }
    return cover;
}

constexpr float kQuadX = 600.0F;
constexpr float kQuadNear = 20.0F;
constexpr float kQuadFar = 900.0F;
bool on_sea_rim(P3 a, P3 b)
{
    const auto both = [](float p, float q, float r) { return p == r && q == r; };
    return both(a.x, b.x, -kQuadX) || both(a.x, b.x, kQuadX) || both(a.z, b.z, kQuadNear) || both(a.z, b.z, kQuadFar);
}

// The points of a split mesh's drawn triangles on the line x = kQuadX, sorted, each once (as bits).
std::vector<Bits> on_shared_x(const water_tess::Result& result)
{
    std::vector<Bits> points;
    for (const Corners& triangle : tess::strip_triangles(result.strip)) {
        for (const uint16_t index : triangle) {
            const P3 point = position(result.vertices, index);
            if (point.x == kQuadX) {
                points.push_back(bits_of(point));
            }
        }
    }
    std::ranges::sort(points);
    const auto [first, last] = std::ranges::unique(points);
    points.erase(first, last);
    return points;
}

void check_water_grid()
{
    const water_tess::Options options;
    constexpr uint32_t kMost16 = 0xFFFF;
    // the sea: 1200 x 880 in two triangles
    const Mesh sea = water_quad(-kQuadX, kQuadX, kQuadNear, kQuadFar);
    const std::vector<uint16_t> strip = sea.strip();
    const water_tess::Result split = water_tess::subdivide(sea.bytes, sea.count, layout(), strip, options);
    expect(split.changed && split.spacing == options.spacing, "water grid: the sea split at the spacing asked for");
    expect(split.vertex_count > sea.count && split.vertex_count <= kMost16 && split.strip.size() <= kMost16,
           "water grid: more vertices, within 16 bits");
    expect(split.vertices.size() >= sea.bytes.size() && std::memcmp(split.vertices.data(), sea.bytes.data(), sea.bytes.size()) == 0,
           "water grid: the original vertices first and unchanged");
    const Cover before = cover_of(sea.bytes, strip, 1.0F);
    const float sign = before.area > 0.0 ? 1.0F : -1.0F;
    const Cover after = cover_of(split.vertices, split.strip, sign);
    constexpr double kAreaSlack = 1.0e-5;
    expect(std::fabs(after.area - before.area) <= std::fabs(before.area) * kAreaSlack, "water grid: the same area covered");
    expect(after.backwards == 0, "water grid: every triangle wound as the original");
    // (a cell's edges at most the spacing; one beside an edge with fewer pieces has its border point moved by up to half
    // of that edge's piece)
    constexpr float kBorderStretch = 1.5F;
    expect(after.longest <= options.spacing * kBorderStretch, "water grid: every drawn edge at most 1.5 x the spacing");
    expect(open_edges(split, on_sea_rim) == 0, "water grid: every inside edge shared by two triangles wound opposite ways");
    bool blended = true;
    bool finite = true;
    constexpr float kUvSlack = 1.0e-3F;
    constexpr std::size_t kColourBytes = 4;
    for (uint32_t index = 0; index < split.vertex_count; ++index) {
        const P3 point = position(split.vertices, index);
        const std::array<float, 2> uv = uv_of(split.vertices, index);
        blended = blended && std::fabs(core::at(uv, 0) - (point.x / kWaterTile)) <= kUvSlack &&
                  std::fabs(core::at(uv, 1) - (point.z / kWaterTile)) <= kUvSlack && point.y == 0.0F &&
                  std::memcmp(vertex_of(split.vertices, index).subspan(kColourAt).data(),
                              vertex_of(sea.bytes, 0).subspan(kColourAt).data(), kColourBytes) == 0;
        finite = finite && finite_vertex(split.vertices, index);
    }
    expect(blended, "water grid: texture coordinates, colours and height blended from the corners");
    expect(finite, "water grid: nothing NaN");

    // a canal beside it, narrower and wound the other way: the shared side x = 600 split at the same points
    constexpr float kCanal = 760.0F;
    const Mesh canal = water_quad(kQuadX, kCanal, kQuadNear, kQuadFar, true);
    const water_tess::Result beside = water_tess::subdivide(canal.bytes, canal.count, layout(), canal.strip(), options);
    const std::vector<Bits> left = on_shared_x(split);
    const std::vector<Bits> right = on_shared_x(beside);
    expect(beside.changed && left.size() > 2 && left == right, "water grid: two meshes side by side meet at the same points to the bit");

    // left alone: a waterfall (a vertical quad), a quad smaller than the spacing, an index past the vertices
    Mesh fall;
    constexpr P3 kOut{.x = 0.0F, .y = 0.0F, .z = -1.0F};
    constexpr float kFallSize = 400.0F;
    const uint16_t f0 = fall.add({.x = 0.0F, .y = 0.0F, .z = 0.0F}, kOut, {0.0F, 0.0F});
    const uint16_t f1 = fall.add({.x = kFallSize, .y = 0.0F, .z = 0.0F}, kOut, {1.0F, 0.0F});
    const uint16_t f2 = fall.add({.x = kFallSize, .y = kFallSize, .z = 0.0F}, kOut, {1.0F, 1.0F});
    const uint16_t f3 = fall.add({.x = 0.0F, .y = kFallSize, .z = 0.0F}, kOut, {0.0F, 1.0F});
    fall.quad(f0, f1, f2, f3);
    const water_tess::Result falls = water_tess::subdivide(fall.bytes, fall.count, layout(), fall.strip(), options);
    expect(!falls.changed && std::string_view(falls.why) == "not flat", "water grid: a waterfall left alone");
    constexpr float kSmall = 20.0F;
    const Mesh puddle = water_quad(0.0F, kSmall, 0.0F, kSmall);
    expect(!water_tess::subdivide(puddle.bytes, puddle.count, layout(), puddle.strip(), options).changed,
           "water grid: a quad smaller than the spacing left alone");
    std::vector<uint16_t> broken = strip;
    broken.back() = static_cast<uint16_t>(sea.count);
    expect(!water_tess::subdivide(sea.bytes, sea.count, layout(), broken, options).changed, "water grid: an index past the vertices left alone");

    // a huge quad: split coarser (the edges' cap, then a wider spacing) within 16 bits
    constexpr float kHugeSide = 40000.0F;
    const Mesh ocean = water_quad(-kHugeSide, kHugeSide, -kHugeSide, kHugeSide);
    const water_tess::Result huge = water_tess::subdivide(ocean.bytes, ocean.count, layout(), ocean.strip(), options);
    expect(huge.changed && huge.vertex_count <= kMost16 && huge.strip.size() <= kMost16 &&
               cover_of(huge.vertices, huge.strip, sign).backwards == 0,
           "water grid: a huge quad split within 16 bits");
    std::printf("water grid: the sea %u -> %u triangles (%u vertices, %zu indices); the canal %u -> %u; a huge quad %u -> %u at "
                "spacing %.0f\n",
                split.triangles, split.made, split.vertex_count, split.strip.size(), beside.triangles, beside.made, huge.triangles,
                huge.made, static_cast<double>(huge.spacing));
}

// --- the levels: `thug2_tess_check --level <a level's folder>...` (offline: the game's own scene and collision files
// read, nothing written) - every mesh the game would smooth smoothed with rt_tess_smooth 0 and 1, and the new points'
// gap to the collision measured where the collision is a slope (quarter pipes, banks, bowls, transitions) ---

constexpr uint32_t kPassTransparent = 0x40; // (render_pc/mesh_smoothing.cpp's filters)
constexpr uint32_t kCutOutAlphaRef = 8;
constexpr uint32_t kFvfXyz = 0x002U;
constexpr uint32_t kFvfNormal = 0x010U;
constexpr uint32_t kFvfDiffuse = 0x040U;
constexpr uint32_t kFvfPositionMask = 0x400EU;
constexpr uint32_t kFvfKnown = kFvfXyz | kFvfNormal | kFvfDiffuse | 0xF00U;
constexpr uint32_t kShaderTypeLimit = 6; // (a vertex type up to this is a shader's: skinned)
constexpr float kNearCollision = 4.0F;   // a point's collision face: the nearest within this
constexpr float kOnCollision = 0.1F;     // an original surface this near its collision face: on it
constexpr float kCell = 64.0F;
constexpr float kSlopeLow = 0.05F;  // a face whose normal's up share is between these: a slope
constexpr float kSlopeHigh = 0.97F;

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// mesh_smoothing::layout_of's rule: a position, an optional normal and colour, texture coordinate pairs.
bool mesh_layout(uint32_t fvf, const scene_format::MeshBody& body, tess::VertexLayout* out)
{
    constexpr uint32_t kTexShift = 8;
    constexpr uint32_t kTexMask = 0xF;
    constexpr uint32_t kMaxSets = 4;
    if ((fvf & kFvfPositionMask) != kFvfXyz || (fvf & ~kFvfKnown) != 0U || ((fvf >> kTexShift) & kTexMask) > kMaxSets) {
        return false;
    }
    const uint32_t sets = (fvf >> kTexShift) & kTexMask;
    uint32_t at = kFloat3;
    out->stride = body.stride;
    if ((fvf & kFvfNormal) != 0U) {
        if (core::at(body.offsets, 0) != at) {
            return false;
        }
        out->normal_offset = static_cast<int>(at);
        at += kFloat3;
    }
    if ((fvf & kFvfDiffuse) != 0U) {
        if (core::at(body.offsets, 1) != at) {
            return false;
        }
        out->colour_offset = static_cast<int>(at);
        at += 4;
    }
    if (sets > 0) {
        if (core::at(body.offsets, 2) != at) {
            return false;
        }
        out->uv_offset = static_cast<int>(at);
        out->uv_sets = sets;
        at += kFloat2 * sets;
    }
    return at == body.stride;
}

P3 add3(P3 a, P3 b) { return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z}; }
P3 scale3(P3 a, float s) { return {.x = a.x * s, .y = a.y * s, .z = a.z * s}; }

// The closest point of a triangle to `p` (Ericson's regions).
P3 closest_on_triangle(P3 p, P3 a, P3 b, P3 c)
{
    const P3 ab = sub(b, a);
    const P3 ac = sub(c, a);
    const P3 ap = sub(p, a);
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return a;
    }
    const P3 bp = sub(p, b);
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return b;
    }
    const float vc = (d1 * d4) - (d3 * d2);
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return add3(a, scale3(ab, d1 / (d1 - d3)));
    }
    const P3 cp = sub(p, c);
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return c;
    }
    const float vb = (d5 * d2) - (d1 * d6);
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return add3(a, scale3(ac, d2 / (d2 - d6)));
    }
    const float va = (d3 * d6) - (d5 * d4);
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return add3(b, scale3(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const float denominator = 1.0F / (va + vb + vc);
    return add3(a, add3(scale3(ab, vb * denominator), scale3(ac, vc * denominator)));
}

// A level's collision faces (collision_format.h's layout, read here with plain bounds checks), in a grid.
class Collision {
public:
    bool load(std::span<const uint8_t> file)
    {
        constexpr std::size_t kHeader = 0x20;
        constexpr std::size_t kObject = 0x40;
        constexpr uint16_t kNotCollidable = 0x10;
        constexpr uint16_t kTrigger = 0x40;
        constexpr uint16_t kVert = 0x8;
        constexpr float kSixteenth = 1.0F / 16.0F;
        if (file.size() < kHeader) {
            return false;
        }
        const auto objects = static_cast<std::size_t>(word(file, 4));
        const std::size_t vertex_bytes = word(file, 8);
        const std::size_t floats = word(file, 0x14);
        const std::size_t packed = word(file, 0x18);
        const std::size_t vertices_at = align(kHeader + (objects * kObject), 16);
        const std::size_t faces_at = align(vertices_at + (6 * (packed + (2 * floats))) + vertex_bytes, 4);
        for (std::size_t i = 0; i < objects; ++i) {
            const std::size_t record = kHeader + (i * kObject);
            if (record + kObject > file.size()) {
                return false;
            }
            const std::size_t count = half(file, record + 6);
            const std::size_t face_count = half(file, record + 8);
            const bool small = core::at(file, record + 0xA) != 0;
            const bool is_packed = core::at(file, record + 0xB) != 0;
            const std::size_t faces = faces_at + word(file, record + 0xC);
            const P3 low{.x = real(file, record + 0x10), .y = real(file, record + 0x14), .z = real(file, record + 0x18)};
            const std::size_t first = vertices_at + word(file, record + 0x30);
            std::vector<P3> points(count);
            for (std::size_t k = 0; k < count; ++k) {
                if (is_packed) {
                    const std::size_t at = first + (6 * k);
                    if (at + 6 > file.size()) {
                        return false;
                    }
                    core::at(points, k) = {.x = low.x + (static_cast<float>(half(file, at)) * kSixteenth),
                                           .y = low.y + (static_cast<float>(half(file, at + 2)) * kSixteenth),
                                           .z = low.z + (static_cast<float>(half(file, at + 4)) * kSixteenth)};
                } else {
                    const std::size_t at = first + (kFloat3 * k);
                    if (at + kFloat3 > file.size()) {
                        return false;
                    }
                    core::at(points, k) = {.x = real(file, at), .y = real(file, at + 4), .z = real(file, at + 8)};
                }
            }
            const std::size_t stride = small ? 8 : 10;
            for (std::size_t f = 0; f < face_count; ++f) {
                const std::size_t at = faces + (stride * f);
                if (at + stride > file.size()) {
                    return false;
                }
                const uint16_t flags = half(file, at);
                if ((flags & (kNotCollidable | kTrigger)) != 0) {
                    continue;
                }
                std::array<std::size_t, 3> corner{};
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(corner, k) = small ? core::at(file, at + 4 + k) : half(file, at + 4 + (2 * k));
                }
                if (std::ranges::any_of(corner, [&](std::size_t index) { return index >= count; })) {
                    continue;
                }
                add_face(core::at(points, core::at(corner, 0)), core::at(points, core::at(corner, 1)),
                         core::at(points, core::at(corner, 2)), (flags & kVert) != 0);
            }
        }
        return !faces_.empty();
    }

    // The nearest face within `radius` (none: distance -1): the point's distance from it, its gap to it along its
    // normal turned up (positive: above it), the normal's up share, whether it is a vert face (quarter pipes' tops).
    struct Hit {
        float distance = -1.0F;
        float gap = 0.0F;
        float up = 0.0F;
        bool vert = false;
    };
    [[nodiscard]] Hit nearest(P3 point, float radius) const
    {
        Hit hit;
        float best = radius;
        const int x0 = cell(point.x - radius);
        const int x1 = cell(point.x + radius);
        const int z0 = cell(point.z - radius);
        const int z1 = cell(point.z + radius);
        const int y0 = cell(point.y - radius);
        const int y1 = cell(point.y + radius);
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    const auto list = grid_.find(key(x, y, z));
                    if (list == grid_.end()) {
                        continue;
                    }
                    for (const uint32_t index : list->second) {
                        const Face& face = core::at(faces_, index);
                        const P3 closest = closest_on_triangle(point, face.a, face.b, face.c);
                        const P3 apart = sub(point, closest);
                        const float distance = std::sqrt(dot(apart, apart));
                        if (distance < best) {
                            best = distance;
                            const P3 normal = face.normal.y < 0.0F ? scale3(face.normal, -1.0F) : face.normal;
                            hit = {.distance = distance, .gap = dot(apart, normal), .up = normal.y, .vert = face.vert};
                        }
                    }
                }
            }
        }
        return hit;
    }

    [[nodiscard]] std::size_t faces() const { return faces_.size(); }

private:
    struct Face {
        P3 a, b, c, normal;
        bool vert = false;
    };
    static std::size_t align(std::size_t at, std::size_t to) { return (at + to - 1) & ~(to - 1); }
    static uint16_t half(std::span<const uint8_t> file, std::size_t at)
    {
        uint16_t value = 0;
        if (at + 2 <= file.size()) {
            std::memcpy(&value, file.subspan(at, 2).data(), 2);
        }
        return value;
    }
    static uint32_t word(std::span<const uint8_t> file, std::size_t at)
    {
        uint32_t value = 0;
        if (at + 4 <= file.size()) {
            std::memcpy(&value, file.subspan(at, 4).data(), 4);
        }
        return value;
    }
    static float real(std::span<const uint8_t> file, std::size_t at)
    {
        const uint32_t bits = word(file, at);
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    static int cell(float at) { return static_cast<int>(std::floor(at / kCell)); }
    static uint64_t key(int x, int y, int z)
    {
        constexpr uint64_t kMask = 0x1FFFFF;
        constexpr unsigned kShiftY = 21;
        constexpr unsigned kShiftZ = 42;
        return (static_cast<uint64_t>(x) & kMask) | ((static_cast<uint64_t>(y) & kMask) << kShiftY) |
               ((static_cast<uint64_t>(z) & kMask) << kShiftZ);
    }
    void add_face(P3 a, P3 b, P3 c, bool vert)
    {
        const P3 normal = cross(sub(b, a), sub(c, a));
        const float area = std::sqrt(dot(normal, normal));
        if (!(area > kTinyArea) || !std::isfinite(area)) {
            return;
        }
        const auto index = static_cast<uint32_t>(faces_.size());
        faces_.push_back({.a = a, .b = b, .c = c, .normal = scale3(normal, 1.0F / area), .vert = vert});
        const int x0 = cell(std::min({a.x, b.x, c.x}));
        const int x1 = cell(std::max({a.x, b.x, c.x}));
        const int y0 = cell(std::min({a.y, b.y, c.y}));
        const int y1 = cell(std::max({a.y, b.y, c.y}));
        const int z0 = cell(std::min({a.z, b.z, c.z}));
        const int z1 = cell(std::max({a.z, b.z, c.z}));
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                for (int z = z0; z <= z1; ++z) {
                    grid_[key(x, y, z)].push_back(index);
                }
            }
        }
    }

    std::vector<Face> faces_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid_;
};

// A setting's totals over a level.
struct LevelTotals {
    uint32_t looked_at = 0;
    uint32_t meshes = 0;
    uint64_t triangles_split = 0;
    uint64_t indices_before = 0;
    uint64_t indices_after = 0;
    uint64_t vertices_added = 0;
    uint32_t capped = 0;
    uint32_t prism_edges = 0;
    uint32_t normal_smooth = 0;
    uint32_t normal_hard = 0;
    float deviation = 0.0F;
    std::vector<float> deviations; // each smoothed mesh's
    // the new points made from the collision's vert faces (quarter pipes' tops) and its other slopes (banks, bowls,
    // transitions) - their place on the original surface on the collision -: their gaps to it (positive: drawn above)
    std::array<std::vector<float>, 2> gaps; // (vert, other slopes)
    double milliseconds = 0.0;
    uint64_t hash = 0xCBF29CE484222325ULL; // the results' bytes (FNV-1a)
};

void hash_bytes(uint64_t* hash, std::span<const uint8_t> bytes)
{
    constexpr uint64_t kPrime = 0x100000001B3ULL;
    for (const uint8_t byte : bytes) {
        *hash = (*hash ^ byte) * kPrime;
    }
}

void measure_mesh(const scene_format::MeshBody& body, const tess::VertexLayout& vertex_layout, const tess::Options& options,
                  const Collision& collision, LevelTotals* totals)
{
    const std::span<const uint16_t> strip(core::at(body.indices, 0).data(), core::at(body.index_counts, 0));
    ++totals->looked_at;
    const auto started = std::chrono::steady_clock::now();
    const tess::Result result = tess::smooth(body.vertices, body.vertex_count, vertex_layout, strip, options);
    totals->milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    hash_bytes(&totals->hash, result.vertices);
    for (const uint16_t index : result.strip) {
        const std::array<uint8_t, 2> bytes{static_cast<uint8_t>(index & 0xFFU), static_cast<uint8_t>(index >> 8U)};
        hash_bytes(&totals->hash, bytes);
    }
    if (!result.changed) {
        return;
    }
    ++totals->meshes;
    totals->triangles_split += result.smoothed;
    totals->indices_before += strip.size();
    totals->indices_after += result.strip.size();
    totals->vertices_added += result.vertex_count - body.vertex_count;
    totals->capped += result.capped ? 1U : 0U;
    totals->prism_edges += result.prism_edges;
    totals->normal_smooth += result.normal_smooth;
    totals->normal_hard += result.normal_hard;
    totals->deviation = std::max(totals->deviation, result.deviation);
    totals->deviations.push_back(result.deviation);
    // The new points whose place on the original surface lies on a slope of the collision (the original drawn there
    // where the skater rides): how far the smoothing put them off it.
    const auto stride = static_cast<std::size_t>(vertex_layout.stride);
    for (uint32_t i = body.vertex_count; i < result.vertex_count; ++i) {
        const std::array<float, 3>& flat = core::at(result.flat, i - body.vertex_count);
        const P3 original{.x = core::at(flat, 0), .y = core::at(flat, 1), .z = core::at(flat, 2)};
        const Collision::Hit under = collision.nearest(original, kNearCollision);
        if (under.distance < 0.0F || under.distance > kOnCollision || under.up < kSlopeLow || under.up > kSlopeHigh) {
            continue;
        }
        std::array<float, 3> floats{};
        std::memcpy(floats.data(), std::span(result.vertices).subspan(stride * i, kFloat3).data(), kFloat3);
        const P3 point{.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
        const Collision::Hit drawn = collision.nearest(point, kNearCollision);
        if (drawn.distance >= 0.0F) {
            core::at(totals->gaps, under.vert ? 0 : 1).push_back(drawn.gap);
        }
    }
}

float percentile(std::vector<float> values, float share)
{
    if (values.empty()) {
        return 0.0F;
    }
    std::ranges::sort(values);
    const auto at = static_cast<std::size_t>(share * static_cast<float>(values.size() - 1));
    return core::at(values, at);
}

void report(const char* name, const LevelTotals& totals)
{
    std::printf("  %s: %u of %u meshes smoothed, %llu triangles split, indices %llu -> %llu, +%llu vertices, %.1f ms; "
                "deviation at most %.3f (median %.3f, 95%% %.3f; %u capped); %u prism edges, %u joined by shared normals, "
                "%u kept hard by split normals; hash %016llx\n",
                name, totals.meshes, totals.looked_at, static_cast<unsigned long long>(totals.triangles_split),
                static_cast<unsigned long long>(totals.indices_before), static_cast<unsigned long long>(totals.indices_after),
                static_cast<unsigned long long>(totals.vertices_added), totals.milliseconds,
                static_cast<double>(totals.deviation), static_cast<double>(percentile(totals.deviations, 0.5F)),
                static_cast<double>(percentile(totals.deviations, 0.95F)), totals.capped, totals.prism_edges,
                totals.normal_smooth, totals.normal_hard, static_cast<unsigned long long>(totals.hash));
    constexpr std::array<const char*, 2> kKinds{"vert faces (quarter pipes)", "other slopes (banks, bowls, transitions)"};
    for (std::size_t kind = 0; kind < kKinds.size(); ++kind) {
        const std::vector<float>& gaps = core::at(totals.gaps, kind);
        std::vector<float> sizes;
        sizes.reserve(gaps.size());
        double sum = 0.0;
        std::size_t over_one = 0;
        for (const float gap : gaps) {
            sizes.push_back(std::fabs(gap));
            sum += std::fabs(gap);
            over_one += std::fabs(gap) > 1.0F ? 1U : 0U;
        }
        std::printf("    %s: %zu new points from the drawn surface on them - gap to the collision mean %.3f, median %.3f, "
                    "95%% %.3f, 99%% %.3f, over 1 unit %.1f%%; above it at most %.3f, below it at most %.3f\n",
                    core::at(kKinds, kind), gaps.size(), gaps.empty() ? 0.0 : sum / static_cast<double>(gaps.size()),
                    static_cast<double>(percentile(sizes, 0.5F)), static_cast<double>(percentile(sizes, 0.95F)),
                    static_cast<double>(percentile(sizes, 0.99F)),
                    gaps.empty() ? 0.0 : 100.0 * static_cast<double>(over_one) / static_cast<double>(gaps.size()),
                    static_cast<double>(std::max(0.0F, percentile(gaps, 1.0F))),
                    static_cast<double>(std::max(0.0F, -percentile(gaps, 0.0F))));
    }
}

bool measure_level(const std::filesystem::path& folder)
{
    const std::string name = folder.filename().string();
    const std::vector<uint8_t> scene = read_file(folder / (name + ".scn.xbx"));
    const std::vector<uint8_t> collision_file = read_file(folder / (name + ".col.xbx"));
    Collision collision;
    if (scene.empty() || !collision.load(collision_file)) {
        std::printf("%s: no scene or collision there\n", name.c_str());
        return false;
    }
    scene_format::MemoryStream in(scene.data(), scene.size());
    in.skip(scene_format::kHeaderSize);
    const auto materials = scene_format::take<uint32_t>(in);
    std::map<uint32_t, scene_format::Material> by_checksum;
    for (uint32_t i = 0; i < materials; ++i) {
        scene_format::Material material;
        if (!scene_format::read_material(in, &material)) {
            std::printf("%s: material %u unreadable\n", name.c_str(), i);
            return false;
        }
        by_checksum.emplace(material.checksum, std::move(material));
    }
    tess::Options off;
    off.keep_flat = true;
    tess::Options on = off;
    on.smooth = true;
    tess::Options tight = on;
    tight.max_deviation = 1.0F;
    LevelTotals plain;
    LevelTotals smoothed;
    LevelTotals smoothed_tight;
    const auto sectors = scene_format::take<uint32_t>(in);
    scene_format::Sector sector;
    scene_format::SectorMesh mesh;
    scene_format::MeshBody body;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!scene_format::read_sector(in, &sector)) {
            std::printf("%s: sector %u unreadable\n", name.c_str(), i);
            return false;
        }
        for (uint32_t j = 0; j < sector.mesh_count; ++j) {
            if (!scene_format::read_sector_mesh(in, &mesh) || !scene_format::read_mesh_body(in, &body)) {
                std::printf("%s: sector %u mesh %u unreadable\n", name.c_str(), i, j);
                return false;
            }
            // (the meshes mesh_smoothing::smooth_body takes: a plain format, no per-vertex bytes or scaled positions,
            // a material neither transparent nor cut out)
            const uint32_t fvf = core::at(body.vertex_types, 0);
            const auto material = by_checksum.find(mesh.material);
            tess::VertexLayout vertex_layout;
            if (fvf <= kShaderTypeLimit || body.has_vertex_bytes || !body.scaled.empty() || material == by_checksum.end() ||
                (core::at(material->second.passes, 0).flags & kPassTransparent) != 0U ||
                material->second.alpha_ref > kCutOutAlphaRef || core::at(body.index_counts, 0) < 3 ||
                !scene_format::offsets_fit(body) || !mesh_layout(fvf, body, &vertex_layout) ||
                !scene_format::vertices_fit(body, body.stride)) {
                continue;
            }
            measure_mesh(body, vertex_layout, off, collision, &plain);
            measure_mesh(body, vertex_layout, on, collision, &smoothed);
            measure_mesh(body, vertex_layout, tight, collision, &smoothed_tight);
        }
    }
    std::printf("%s: %zu collision faces\n", name.c_str(), collision.faces());
    report("rt_tess_smooth=0", plain);
    report("rt_tess_smooth=1", smoothed);
    report("rt_tess_smooth=1, rt_tess_max_deviation=1", smoothed_tight);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() > 2 && std::string_view(core::at(args, 1)) == "--level") {
        bool read = true;
        for (std::size_t i = 2; i < args.size(); ++i) {
            read = measure_level(core::at(args, i)) && read;
        }
        return read ? 0 : 1;
    }
    check_strip();
    check_cylinder();
    check_rail();
    check_pipe();
    check_unchanged();
    check_smooth();
    check_water_grid();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("tess_check: all checks passed\n");
    return 0;
}
