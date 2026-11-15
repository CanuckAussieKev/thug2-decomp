// NOT ORIGINAL - thug2_scatter_check.exe: the placement rules of the decals and clutter (engine/gfx/scatter.h) on made-up
// collision, with no device and no game: the game's winding (the faces that face up are the ground - the same faces
// wound the other way are ceilings and get nothing); the "covered" rule (a face over a point keeps decals and clutter off it) - open
// ground under a tall canopy, under a tree's high collision, under a bridge deck that is out of reach and a sheet of
// scenery a hair above the ground are not covered; ground under a bench's top, a table or a low roof is; a covered spot
// gets no decals even on a big patch of dirt and asphalt.
// `thug2_scatter_check --level <a level's folder>...`: the level's own collision read offline (the terrain by the
// shipped table's order, no rails) and the placement made at decal and clutter density 1 - faces, decals, items, and
// what turned candidates away; `--why` also samples the flat ground and says what covers the points the rule covers.
// Exit code 0 when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/scatter.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace sc = render::scatter;

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

// --- made-up collision ----------------------------------------------------------------------------------------------

// A horizontal rectangle of two triangles, facing up (the game's winding: (v2 - v0) x (v1 - v0) up).
void add_rectangle(std::vector<sc::Face>* faces, float x0, float z0, float x1, float z1, float y, sc::Surface surface)
{
    const sc::Vec3 a{x0, y, z0};
    const sc::Vec3 b{x1, y, z0};
    const sc::Vec3 c{x1, y, z1};
    const sc::Vec3 d{x0, y, z1};
    faces->push_back({.corners = {a, c, b}, .surface = surface});
    faces->push_back({.corners = {a, d, c}, .surface = surface});
}

// A vertical wall rectangle along x at depth z from y0 to y1 (a post, a trunk's side).
void add_wall(std::vector<sc::Face>* faces, float x0, float x1, float z, float y0, float y1, sc::Surface surface)
{
    const sc::Vec3 a{x0, y0, z};
    const sc::Vec3 b{x1, y0, z};
    const sc::Vec3 c{x1, y1, z};
    const sc::Vec3 d{x0, y1, z};
    faces->push_back({.corners = {a, b, c}, .surface = surface});
    faces->push_back({.corners = {a, c, d}, .surface = surface});
}

struct Probe {
    const char* what;
    float x;
    float z;
    bool covered;
};

// Open dirt and asphalt with something over parts of it.
void synthetic_checks()
{
    std::vector<sc::Face> faces;
    add_rectangle(&faces, -2000.0F, -2000.0F, 2000.0F, 2000.0F, 0.0F, sc::Surface::kAsphalt);
    // 1. a low roof 96 above (a bench's or a shelter's top, a table): covers
    add_rectangle(&faces, 100.0F, 100.0F, 300.0F, 300.0F, 96.0F, sc::Surface::kOther);
    // 2. a tall canopy 400 above (a tree's top, a gantry, a bridge's deck far over the road): not cover
    add_rectangle(&faces, -900.0F, -900.0F, -500.0F, -500.0F, 400.0F, sc::Surface::kOther);
    // 3. a canopy of leaves a little under the old reach, 118 above, wide: a ceiling over a street-wide area
    add_rectangle(&faces, 500.0F, -900.0F, 900.0F, -500.0F, 118.0F, sc::Surface::kOther);
    // 4. a sheet 0.3 above the ground (a layer of the same ground, a painted mark): not cover
    add_rectangle(&faces, -300.0F, 500.0F, -100.0F, 700.0F, 0.3F, sc::Surface::kOther);
    // 5. a thin branch: 6 wide and 80 above, a long way round it open
    add_rectangle(&faces, 1200.0F, 1200.0F, 1206.0F, 1500.0F, 80.0F, sc::Surface::kOther);
    // 6. a tree trunk's side (vertical: no projected area) next to open ground
    add_wall(&faces, 1500.0F, 1520.0F, 1000.0F, 0.0F, 300.0F, sc::Surface::kOther);
    const std::vector<Probe> probes{
        {"under a low roof (96 above)", 200.0F, 200.0F, true},
        {"under a canopy 400 above", -700.0F, -700.0F, false},
        {"under a wide ceiling 118 above", 700.0F, -700.0F, true},
        {"ground under a sheet 0.3 above it", -200.0F, 600.0F, false},
        {"open ground far from everything", 1700.0F, -1700.0F, false},
        {"beside a trunk", 1500.0F, 990.0F, false},
    };
    for (const Probe& probe : probes) {
        const sc::Placement at = sc::placement_at(faces, {}, {probe.x, 0.0F, probe.z});
        std::string text = std::string("covered: ") + probe.what + (probe.covered ? " is covered" : " is open");
        expect(at.covered == probe.covered, text.c_str());
    }
    // the thin branch: ground right under it is covered only where the branch is, and a few units to its side is open
    {
        const sc::Placement under = sc::placement_at(faces, {}, {1203.0F, 0.0F, 1300.0F});
        const sc::Placement beside = sc::placement_at(faces, {}, {1225.0F, 0.0F, 1300.0F});
        expect(!beside.covered, "covered: ground beside a branch is open");
        std::printf("      under a thin branch: %s\n", under.covered ? "covered" : "open");
    }
}

// A big flat patch with one low roof over a part: no decal's centre or clutter item's root under the roof, plenty round it.
void patch_checks()
{
    std::vector<sc::Face> faces;
    add_rectangle(&faces, -3000.0F, -3000.0F, 3000.0F, 3000.0F, 0.0F, sc::Surface::kAsphalt);
    add_rectangle(&faces, -500.0F, -500.0F, 500.0F, 500.0F, 100.0F, sc::Surface::kOther);
    // a canopy far above all of it
    add_rectangle(&faces, -3000.0F, -3000.0F, 3000.0F, 3000.0F, 900.0F, sc::Surface::kOther);
    const sc::Scatter made = sc::make_scatter({.faces = faces, .seed = 7, .decal_density = 4.0F, .clutter_density = 1.0F});
    std::size_t under = 0;
    std::size_t outside = 0;
    for (const sc::Decal& decal : made.decals) {
        const bool in = std::abs(decal.centre[0]) < 500.0F && std::abs(decal.centre[2]) < 500.0F;
        under += in ? 1U : 0U;
        outside += in ? 0U : 1U;
    }
    std::printf("      decals: %zu outside the roof, %zu under it; %u candidates covered\n", outside, under,
                made.decal_rejected.covered);
    expect(outside > 100, "decals: open ground under a tall canopy gets decals");
    expect(under == 0, "decals: none under a low roof");
    expect(made.decal_rejected.covered > 0, "decals: the roof turned candidates away");
}

// The game's winding ((v1 - v0) x (v2 - v0), TriMeshSource::get_triangle_normal) faces up on the ground: a patch wound
// that way gets decals and clutter; the same triangles the other way are ceilings (undersides) and get none.
// (Measured on the levels' collision: the flat faces with nothing above them at all are the ones that winding turns up.)
void winding_checks()
{
    std::vector<sc::Face> ground;
    add_rectangle(&ground, -3000.0F, -3000.0F, 3000.0F, 3000.0F, 0.0F, sc::Surface::kConcrete);
    std::vector<sc::Face> ceiling = ground;
    for (sc::Face& face : ceiling) {
        std::swap(core::at(face.corners, 1), core::at(face.corners, 2));
    }
    const sc::Scatter up = sc::make_scatter({.faces = ground, .seed = 3, .decal_density = 4.0F, .clutter_density = 1.0F});
    const sc::Scatter down = sc::make_scatter({.faces = ceiling, .seed = 3, .decal_density = 4.0F, .clutter_density = 1.0F});
    std::printf("      wound up: %zu decals, %zu items; wound down: %zu decals, %zu items\n", up.decals.size(), up.items.size(),
                down.decals.size(), down.items.size());
    expect(up.decals.size() > 100, "winding: ground wound as the game's collision gets decals");
    expect(down.decals.empty() && down.items.empty(), "winding: the same faces wound the other way (a ceiling) get none");
    expect(sc::placement_at(ground, {}, {0.0F, 0.0F, 0.0F}).flat, "winding: the game's winding is flat ground");
    expect(!sc::placement_at(ceiling, {}, {0.0F, 0.0F, 0.0F}).flat, "winding: the other way is not");
}

// --- a level's collision, offline -----------------------------------------------------------------------------------

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint32_t word(std::span<const uint8_t> file, std::size_t at)
{
    uint32_t value = 0;
    if (at + sizeof(value) <= file.size()) {
        std::memcpy(&value, file.data() + at, sizeof(value));
    }
    return value;
}

uint16_t half(std::span<const uint8_t> file, std::size_t at)
{
    uint16_t value = 0;
    if (at + sizeof(value) <= file.size()) {
        std::memcpy(&value, file.data() + at, sizeof(value));
    }
    return value;
}

float real(std::span<const uint8_t> file, std::size_t at)
{
    float value = 0.0F;
    if (at + sizeof(value) <= file.size()) {
        std::memcpy(&value, file.data() + at, sizeof(value));
    }
    return value;
}

std::size_t align(std::size_t value, std::size_t to) { return (value + to - 1) / to * to; }

// The shipped terrain table's order (scatter_world.cpp's fallback - the level's own table isn't read here).
constexpr std::array kTerrains{
    "TERRAIN_DEFAULT",      "TERRAIN_CONCSMOOTH",  "TERRAIN_CONCROUGH",    "TERRAIN_METALSMOOTH", "TERRAIN_METALROUGH",
    "TERRAIN_METALCORRUGATED", "TERRAIN_METALGRATING", "TERRAIN_METALTIN", "TERRAIN_WOOD",        "TERRAIN_WOODMASONITE",
    "TERRAIN_WOODPLYWOOD",  "TERRAIN_WOODFLIMSY",  "TERRAIN_WOODSHINGLE",  "TERRAIN_WOODPIER",    "TERRAIN_BRICK",
    "TERRAIN_TILE",         "TERRAIN_ASPHALT",     "TERRAIN_ROCK",         "TERRAIN_GRAVEL",      "TERRAIN_SIDEWALK",
    "TERRAIN_GRASS",        "TERRAIN_GRASSDRIED",  "TERRAIN_DIRT",         "TERRAIN_DIRTPACKED",  "TERRAIN_WATER",
    "TERRAIN_ICE",          "TERRAIN_SNOW",        "TERRAIN_SAND",         "TERRAIN_PLEXIGLASS",  "TERRAIN_FIBERGLASS",
    "TERRAIN_CARPET",       "TERRAIN_CONVEYOR",    "TERRAIN_CHAINLINK",    "TERRAIN_METALFUTURE", "TERRAIN_GENERIC1",
    "TERRAIN_GENERIC2",     "TERRAIN_WHEELS",      "TERRAIN_WETCONC",
};

// collision_format.h's layout, read with plain bounds checks; the faces the game's scatter_world reads (not triggers).
bool load_collision(std::span<const uint8_t> file, std::vector<sc::Face>* out)
{
    constexpr std::size_t kHeader = 0x20;
    constexpr std::size_t kObject = 0x40;
    constexpr std::size_t kFloat3 = 12;
    constexpr uint16_t kTrigger = 0x40;
    constexpr uint16_t kVert = 0x8;
    constexpr float kSixteenth = 1.0F / 16.0F;
    if (file.size() < kHeader) {
        return false;
    }
    std::array<sc::Surface, 256> surfaces{};
    for (std::size_t k = 0; k < kTerrains.size(); ++k) {
        core::at(surfaces, k) = sc::surface_of_terrain(core::at(kTerrains, k));
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
        const std::array<float, 3> low{real(file, record + 0x10), real(file, record + 0x14), real(file, record + 0x18)};
        const std::size_t first = vertices_at + word(file, record + 0x30);
        std::vector<sc::Vec3> points(count);
        for (std::size_t k = 0; k < count; ++k) {
            if (is_packed) {
                const std::size_t at = first + (6 * k);
                if (at + 6 > file.size()) {
                    return false;
                }
                core::at(points, k) = {low[0] + (static_cast<float>(half(file, at)) * kSixteenth),
                                       low[1] + (static_cast<float>(half(file, at + 2)) * kSixteenth),
                                       low[2] + (static_cast<float>(half(file, at + 4)) * kSixteenth)};
            } else {
                const std::size_t at = first + (kFloat3 * k);
                if (at + kFloat3 > file.size()) {
                    return false;
                }
                core::at(points, k) = {real(file, at), real(file, at + 4), real(file, at + 8)};
            }
        }
        const std::size_t stride = small ? 8 : 10;
        for (std::size_t f = 0; f < face_count; ++f) {
            const std::size_t at = faces + (stride * f);
            if (at + stride > file.size()) {
                return false;
            }
            const uint16_t flags = half(file, at);
            if ((flags & kTrigger) != 0) {
                continue;
            }
            std::array<std::size_t, 3> corner{};
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(corner, k) = small ? core::at(file, at + 4 + k) : half(file, at + 4 + (2 * k));
            }
            if (std::ranges::any_of(corner, [&](std::size_t index) { return index >= count; })) {
                continue;
            }
            sc::Face face;
            for (std::size_t k = 0; k < 3; ++k) {
                core::at(face.corners, k) = core::at(points, core::at(corner, k));
            }
            face.surface = core::at(surfaces, half(file, at + 2) & 0xFFU);
            face.vert = (flags & kVert) != 0;
            out->push_back(face);
        }
    }
    return !out->empty();
}

// What covers a point, as the rule asks (a face above it, seen from above), for the report.
struct Cover {
    float height = 0.0F;
    float up = 0.0F;
    float area = 0.0F;
    sc::Surface surface = sc::Surface::kOther;
    std::array<sc::Vec3, 3> c{};
};

std::vector<Cover> covers_of(const std::vector<sc::Face>& faces, const sc::Vec3& point, float reach = sc::kCoverReach)
{
    std::vector<Cover> found;
    for (const sc::Face& face : faces) {
        const sc::Vec3& a = core::at(face.corners, 0);
        const sc::Vec3& b = core::at(face.corners, 1);
        const sc::Vec3& c = core::at(face.corners, 2);
        const float area = ((b[0] - a[0]) * (c[2] - a[2])) - ((c[0] - a[0]) * (b[2] - a[2]));
        if (std::abs(area) < 1.0e-3F) {
            continue;
        }
        const float u = (((point[0] - a[0]) * (c[2] - a[2])) - ((c[0] - a[0]) * (point[2] - a[2]))) / area;
        const float v = (((b[0] - a[0]) * (point[2] - a[2])) - ((point[0] - a[0]) * (b[2] - a[2]))) / area;
        if (u < 0.0F || v < 0.0F || u + v > 1.0F) {
            continue;
        }
        const float height = a[1] + (u * (b[1] - a[1])) + (v * (c[1] - a[1]));
        const float above = height - point[1];
        if (above < sc::kCoverFloor || above > reach) {
            continue;
        }
        const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const std::array<float, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<float, 3> n{(ab[1] * ac[2]) - (ab[2] * ac[1]), (ab[2] * ac[0]) - (ab[0] * ac[2]),
                                     (ab[0] * ac[1]) - (ab[1] * ac[0])};
        const float length = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
        found.push_back({.height = above,
                         .up = length > 0.0F ? n[1] / length : 0.0F,
                         .area = length * 0.5F,
                         .surface = face.surface,
                         .c = face.corners});
    }
    return found;
}

bool measure_level(const std::filesystem::path& folder, bool why)
{
    const std::string name = folder.filename().string();
    std::vector<sc::Face> faces;
    if (!load_collision(read_file(folder / (name + ".col.xbx")), &faces)) {
        std::printf("%s: no collision there\n", name.c_str());
        return false;
    }
    const std::vector<sc::Rail> no_rails;
    std::printf("%s: %zu faces\n", name.c_str(), faces.size());
    for (const float density : {1.0F}) {
        const sc::Scatter made = sc::make_scatter({.faces = faces, .rails = no_rails, .seed = 1U, .decal_density = density,
                                                   .clutter_density = density});
        const sc::Rejected& d = made.decal_rejected;
        const sc::Rejected& i = made.item_rejected;
        std::printf("  faces by surface (other, asphalt, concrete, paving, brick, dirt, gravel, grass):");
        for (const uint32_t count : made.faces_of) {
            std::printf(" %u", count);
        }
        std::printf("\n  density %.0f: %zu decals%s (turned away: ramps %u, edges %u, rails %u, covered %u, no dip %u), "
                    "%zu clutter items (far from walls %u, ramps %u, edges %u, stairs %u, covered %u)\n",
                    static_cast<double>(density), made.decals.size(), made.decals_capped ? " (capped)" : "", d.ramp, d.drop, d.rail, d.covered, d.no_dip,
                    made.items.size(), i.far_from_wall, i.ramp, i.drop, i.stairs, i.covered);
    }
    if (why) {
        // the area (square metres, 1 unit = 1 inch) of each surface by shape: flat ground, slope, wall
        std::array<std::array<double, 3>, sc::kSurfaceCount> area{};
        for (const sc::Face& face : faces) {
            const sc::Vec3& a = core::at(face.corners, 0);
            const sc::Vec3& b = core::at(face.corners, 1);
            const sc::Vec3& c = core::at(face.corners, 2);
            const float nx = ((b[1] - a[1]) * (c[2] - a[2])) - ((b[2] - a[2]) * (c[1] - a[1]));
            const float ny = ((b[2] - a[2]) * (c[0] - a[0])) - ((b[0] - a[0]) * (c[2] - a[2])); // the game's winding: (v1 - v0) x (v2 - v0)
            const float nz = ((b[0] - a[0]) * (c[1] - a[1])) - ((b[1] - a[1]) * (c[0] - a[0]));
            const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
            if (length <= 0.0F) {
                continue;
            }
            const float up = ny / length;
            const std::size_t shape = up >= sc::kFlatUp ? 0U : (std::abs(up) < sc::kWallUp ? 2U : 1U);
            core::at(core::at(area, static_cast<std::size_t>(face.surface)), shape) += static_cast<double>(length) * 0.5 / 1550.0;
        }
        {
            // the up-facing slope of dirt, grass and concrete by area: how steep the ground is
            constexpr std::array<float, 7> kEdges{0.99F, 0.96F, 0.93F, 0.9F, 0.8F, 0.6F, 0.25F};
            std::array<std::array<double, 8>, 3> bins{};
            for (const sc::Face& face : faces) {
                const std::size_t row = face.surface == sc::Surface::kDirt ? 0U : (face.surface == sc::Surface::kGrass ? 1U : (face.surface == sc::Surface::kConcrete ? 2U : 3U));
                if (row == 3U) {
                    continue;
                }
                const sc::Vec3& a = core::at(face.corners, 0);
                const sc::Vec3& b = core::at(face.corners, 1);
                const sc::Vec3& c = core::at(face.corners, 2);
                const float nx = ((b[1] - a[1]) * (c[2] - a[2])) - ((b[2] - a[2]) * (c[1] - a[1]));
                const float ny = ((b[2] - a[2]) * (c[0] - a[0])) - ((b[0] - a[0]) * (c[2] - a[2])); // the game's winding: (v1 - v0) x (v2 - v0)
                const float nz = ((b[0] - a[0]) * (c[1] - a[1])) - ((b[1] - a[1]) * (c[0] - a[0]));
                const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
                if (length <= 0.0F || ny <= 0.0F) {
                    continue;
                }
                const float up = ny / length;
                std::size_t bin = kEdges.size();
                for (std::size_t k = 0; k < kEdges.size(); ++k) {
                    if (up >= core::at(kEdges, k)) {
                        bin = k;
                        break;
                    }
                }
                core::at(core::at(bins, row), bin) += static_cast<double>(length) * 0.5 / 1550.0;
            }
            std::printf("  up-facing area m2 by normal y (>=.99, .96, .93, .90, .80, .60, .25, below) - dirt / grass / concrete:\n");
            for (const auto& row : bins) {
                std::printf("    ");
                for (const double value : row) {
                    std::printf(" %7.0f", value);
                }
                std::printf("\n");
            }
        }
        {
            // flat faces the game's winding calls ceilings (normal y <= -kFlatUp) with nothing at all above them (open sky):
            // ground wound the other way; with a face above them: the undersides of slabs
            constexpr float kSky = 1.0e6F;
            std::size_t checked = 0;
            std::size_t open = 0;
            std::size_t stride = 0;
            std::array<std::size_t, sc::kSurfaceCount> open_of{};
            for (const sc::Face& face : faces) {
                const sc::Vec3& a = core::at(face.corners, 0);
                const sc::Vec3& b = core::at(face.corners, 1);
                const sc::Vec3& c = core::at(face.corners, 2);
                const float nx = ((b[1] - a[1]) * (c[2] - a[2])) - ((b[2] - a[2]) * (c[1] - a[1]));
                const float ny = ((b[2] - a[2]) * (c[0] - a[0])) - ((b[0] - a[0]) * (c[2] - a[2])); // the game's winding: (v1 - v0) x (v2 - v0)
                const float nz = ((b[0] - a[0]) * (c[1] - a[1])) - ((b[1] - a[1]) * (c[0] - a[0]));
                const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
                if (length > 0.0F && (std::getenv("SCATTER_UP") != nullptr ? ny / length >= sc::kFlatUp : ny / length <= -sc::kFlatUp) &&
                    (stride++ % 20) == 0) {
                    ++checked;
                    const sc::Vec3 centre{(a[0] + b[0] + c[0]) / 3.0F, (a[1] + b[1] + c[1]) / 3.0F,
                                          (a[2] + b[2] + c[2]) / 3.0F};
                    if (covers_of(faces, centre, kSky).empty()) {
                        ++open;
                        ++core::at(open_of, static_cast<std::size_t>(face.surface));
                    }
                }
            }
            std::printf("  down-facing flat faces sampled: %zu, with nothing above them at all: %zu (by surface:", checked, open);
            for (const std::size_t count : open_of) {
                std::printf(" %zu", count);
            }
            std::printf(")\n");
        }
        std::printf("  area m2 (flat up, slope, wall) by surface (other, asphalt, concrete, paving, brick, dirt, gravel, grass):");
        for (const auto& surface : area) {
            std::printf(" (%.0f %.0f %.0f)", surface[0], surface[1], surface[2]);
        }
        std::printf("\n");
        // the flat ground's samples: how many the rule covers, and by what
        std::vector<sc::Vec3> samples;
        std::size_t stride = 0;
        for (const sc::Face& face : faces) {
            const sc::Vec3& a = core::at(face.corners, 0);
            const sc::Vec3& b = core::at(face.corners, 1);
            const sc::Vec3& c = core::at(face.corners, 2);
            const float nx = ((b[1] - a[1]) * (c[2] - a[2])) - ((b[2] - a[2]) * (c[1] - a[1]));
            const float ny = ((b[2] - a[2]) * (c[0] - a[0])) - ((b[0] - a[0]) * (c[2] - a[2])); // the game's winding: (v1 - v0) x (v2 - v0)
            const float nz = ((b[0] - a[0]) * (c[1] - a[1])) - ((b[1] - a[1]) * (c[0] - a[0]));
            const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
            if (length > 0.0F && ny / length >= sc::kFlatUp && (stride++ % 40) == 0) {
                samples.push_back({(a[0] + b[0] + c[0]) / 3.0F, (a[1] + b[1] + c[1]) / 3.0F, (a[2] + b[2] + c[2]) / 3.0F});
            }
        }
        std::size_t covered = 0;
        std::map<std::string, std::size_t> by_kind;
        for (const sc::Vec3& point : samples) {
            const std::vector<Cover> covers = covers_of(faces, point);
            if (covers.empty()) {
                continue;
            }
            ++covered;
            const Cover& first = covers.front();
            if (std::getenv("SCATTER_DUMP") != nullptr && first.height >= 12.0F) {
                std::printf("      point %.0f %.0f %.0f (surface of ground ?): cover %.0f above, up %.2f, area %.0f, surface %d, corners (%.0f %.0f %.0f) (%.0f %.0f %.0f) (%.0f %.0f %.0f)\n",
                            static_cast<double>(point[0]), static_cast<double>(point[1]), static_cast<double>(point[2]),
                            static_cast<double>(first.height), static_cast<double>(first.up), static_cast<double>(first.area),
                            static_cast<int>(first.surface), static_cast<double>(first.c[0][0]), static_cast<double>(first.c[0][1]), static_cast<double>(first.c[0][2]),
                            static_cast<double>(first.c[1][0]), static_cast<double>(first.c[1][1]), static_cast<double>(first.c[1][2]),
                            static_cast<double>(first.c[2][0]), static_cast<double>(first.c[2][1]), static_cast<double>(first.c[2][2]));
            }
            const std::string kind = std::string(first.up >= sc::kFlatUp ? "top flat" : (first.up <= -sc::kFlatUp ? "underside flat" : (std::abs(first.up) < sc::kWallUp ? "wall" : (first.up > 0.0F ? "slope up" : "slope down")))) +
                                     (first.height < 12.0F ? ", under 12 above" : (first.height < 40.0F ? ", 12-40 above" : (first.height < 80.0F ? ", 40-80 above" : ", 80-120 above"))) +
                                     (first.area < 400.0F ? ", face under 400 sq" : (first.area < 5000.0F ? ", face under 5000 sq" : ", face bigger")) +
                                     ", surface " + std::to_string(static_cast<int>(first.surface));
            ++by_kind[kind];
        }
        std::printf("  flat ground samples: %zu, covered by the rule: %zu\n", samples.size(), covered);
        for (const auto& [kind, count] : by_kind) {
            std::printf("    cover %s: %zu\n", kind.c_str(), count);
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    std::vector<std::filesystem::path> levels;
    bool why = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = core::at(std::span(argv, static_cast<std::size_t>(argc)), static_cast<std::size_t>(i));
        if (argument == "--why") {
            why = true;
        } else if (argument != "--level") {
            levels.emplace_back(argument);
        }
    }
    if (levels.empty()) {
        synthetic_checks();
        patch_checks();
        winding_checks();
        std::printf("%s: %d failure(s)\n", failures() == 0 ? "ok" : "FAILED", failures());
        return failures() == 0 ? 0 : 1;
    }
    bool ok = true;
    for (const std::filesystem::path& level : levels) {
        ok = measure_level(level, why) && ok;
    }
    return ok ? 0 : 1;
}
