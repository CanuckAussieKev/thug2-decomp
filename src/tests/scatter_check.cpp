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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/air_volume.h"
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

// (round W2) Leaves and paper are brought by the wind, weeds need the sky: on dirt round a closed shed (walls and a roof
// 300 units up - too high to count as cover) the level's air transport keeps them out of the shed; without it they lie
// inside as outside. Pebbles keep their own rules.
void air_checks()
{
    std::vector<sc::Face> faces;
    add_rectangle(&faces, -2000.0F, -2000.0F, 2000.0F, 2000.0F, 0.0F, sc::Surface::kDirt);
    std::vector<render::air::Face> shed;
    const auto both = [&](const sc::Vec3& a, const sc::Vec3& b, const sc::Vec3& c) {
        faces.push_back({.corners = {a, b, c}, .surface = sc::Surface::kOther});
        shed.push_back({.corners = {a, b, c}, .air = 0.0F});
    };
    constexpr float kSide = 500.0F;
    constexpr float kTop = 300.0F;
    const auto quad = [&](const sc::Vec3& a, const sc::Vec3& b, const sc::Vec3& c, const sc::Vec3& d) {
        both(a, b, c);
        both(a, c, d);
    };
    quad({-kSide, 0.0F, -kSide}, {kSide, 0.0F, -kSide}, {kSide, kTop, -kSide}, {-kSide, kTop, -kSide});
    quad({-kSide, 0.0F, kSide}, {-kSide, kTop, kSide}, {kSide, kTop, kSide}, {kSide, 0.0F, kSide});
    quad({-kSide, 0.0F, -kSide}, {-kSide, kTop, -kSide}, {-kSide, kTop, kSide}, {-kSide, 0.0F, kSide});
    quad({kSide, 0.0F, -kSide}, {kSide, 0.0F, kSide}, {kSide, kTop, kSide}, {kSide, kTop, -kSide});
    quad({-kSide, kTop, -kSide}, {kSide, kTop, -kSide}, {kSide, kTop, kSide}, {-kSide, kTop, kSide});
    // the ground the air volume sees too
    shed.push_back({.corners = {{{-2000.0F, 0.0F, -2000.0F}, {2000.0F, 0.0F, 2000.0F}, {2000.0F, 0.0F, -2000.0F}}}, .air = 0.0F});
    shed.push_back({.corners = {{{-2000.0F, 0.0F, -2000.0F}, {-2000.0F, 0.0F, 2000.0F}, {2000.0F, 0.0F, 2000.0F}}}, .air = 0.0F});
    const auto volume = std::make_shared<const render::air::Volume>(render::air::Volume::build(shed));
    const auto count = [&](const sc::Scatter& made, bool inside, sc::ItemKind kind) {
        std::size_t n = 0;
        for (const sc::Item& item : made.items) {
            const bool in = std::abs(item.root[0]) < kSide - 40.0F && std::abs(item.root[2]) < kSide - 40.0F;
            const auto of = static_cast<sc::ItemKind>((item.packed >> sc::kItemKindShift) & 0xFU);
            n += in == inside && of == kind ? 1U : 0U;
        }
        return n;
    };
    const sc::Scatter open = sc::make_scatter({.faces = faces, .seed = 5, .clutter_density = 2.0F});
    const sc::Scatter aired = sc::make_scatter({.faces = faces, .seed = 5, .clutter_density = 2.0F, .air = volume});
    const sc::Scatter again = sc::make_scatter({.faces = faces, .seed = 5, .clutter_density = 2.0F, .air = volume});
    std::printf("      the shed without the air: %zu leaves, %zu weeds, %zu pebbles inside; with it: %zu, %zu, %zu "
                "(outside %zu leaves); %u turned away\n",
                count(open, true, sc::ItemKind::kLeaf), count(open, true, sc::ItemKind::kWeed),
                count(open, true, sc::ItemKind::kPebble), count(aired, true, sc::ItemKind::kLeaf),
                count(aired, true, sc::ItemKind::kWeed), count(aired, true, sc::ItemKind::kPebble),
                count(aired, false, sc::ItemKind::kLeaf), aired.item_rejected.no_air);
    expect(count(open, true, sc::ItemKind::kLeaf) > 0, "air: without the air volume leaves lie inside the shed");
    expect(count(aired, true, sc::ItemKind::kLeaf) == 0 && count(aired, true, sc::ItemKind::kWeed) == 0,
           "air: no leaves or weeds inside a closed shed (the wind and the sky don't reach)");
    expect(count(aired, true, sc::ItemKind::kPebble) > 0, "air: pebbles keep their own rule inside");
    expect(count(aired, false, sc::ItemKind::kLeaf) > 0, "air: leaves outside it");
    expect(aired.items.size() == again.items.size() &&
               std::memcmp(aired.items.data(), again.items.data(), aired.items.size() * sizeof(sc::Item)) == 0,
           "air: the same input, the same items");
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

// (round DB) The decals as material layers (scatter.h kDecalTile, decal_material): each kind's layer from the one rule -
// oil a film of motor oil (F0 0.036, roughness 0.3: glossier than the asphalt under it), tyre and gum rubber, the wet
// patch water (F0 0.02, still water's roughness), the rest ground; the atlas holds each mark's colour and coverage -
// oil's core dark (its colour the art's 0.2: an albedo about 0.03 linear), a clear texel white with no coverage.
void layer_checks()
{
    const sc::DecalMaterial oil = sc::decal_material(sc::DecalKind::kOil);
    const sc::DecalMaterial wet = sc::decal_material(sc::DecalKind::kWet);
    const sc::DecalMaterial tyre = sc::decal_material(sc::DecalKind::kTyre);
    const sc::DecalMaterial crack = sc::decal_material(sc::DecalKind::kCrack);
    expect(oil.layer == sc::DecalLayer::kPigment && std::abs(oil.f0 - 0.0362F) < 0.0005F && oil.roughness < 0.5F,
           "oil is a glossy film (F0 0.036, roughness under 0.5)");
    expect(wet.layer == sc::DecalLayer::kWater && std::abs(wet.f0 - 0.0204F) < 0.0005F && wet.roughness <= 0.05F,
           "the wet patch is still water");
    expect(tyre.layer == sc::DecalLayer::kPigment && tyre.roughness > oil.roughness && crack.roughness > 0.9F,
           "rubber and grit are rougher than the oil");
    const std::vector<uint8_t> atlas = sc::make_decal_atlas();
    const auto texel = [&atlas](uint32_t kind, uint32_t u, uint32_t v) {
        const std::size_t at = ((static_cast<std::size_t>((kind * sc::kDecalTile) + v) * sc::kAtlasWidth) + u) * 4;
        return std::array<uint8_t, 4>{core::at(atlas, at), core::at(atlas, at + 1), core::at(atlas, at + 2), core::at(atlas, at + 3)};
    };
    const uint32_t middle = sc::kDecalTile / 2;
    const std::array<uint8_t, 4> oil_core = texel(static_cast<uint32_t>(sc::DecalKind::kOil), middle, middle);
    expect(core::at(oil_core, 0) < 70 && core::at(oil_core, 3) > 150, "oil's core: its own dark colour, mostly covered");
    const std::array<uint8_t, 4> corner = texel(static_cast<uint32_t>(sc::DecalKind::kOil), 0, 0);
    expect(core::at(corner, 3) == 0 && core::at(corner, 0) == 255, "a clear texel: white, no coverage");
}

// (round PG) The clutter in the rays (scatter.h "the clutter in the rays"): the items whose instances are in the scene are
// exactly those within the range (the game's LOD distance x lod_distance_scale) - none beyond it; each item's transform
// places its mesh's vertices where the clutter's vertex shader does (its steps done here one by one: a pebble's
// proportions, the tilt about x, the turn about y, the size, the root); the parts hold every triangle of their kind's
// mesh (a can's body and label between them); each part's colour a colour.
void ray_checks()
{
    std::vector<sc::Face> faces;
    add_rectangle(&faces, -4000.0F, -4000.0F, 4000.0F, 4000.0F, 0.0F, sc::Surface::kDirt);
    add_wall(&faces, -4000.0F, 4000.0F, 0.0F, 0.0F, 50.0F, sc::Surface::kBrick); // (a wall: leaves and litter by it)
    const sc::Scatter made = sc::make_scatter({.faces = faces, .seed = 11, .clutter_density = 2.0F});
    const sc::Vec3 camera{150.0F, 60.0F, -40.0F};
    for (const float scale : {0.5F, 1.0F, 2.0F}) {
        const float range = sc::kClutterLodDistance * scale;
        std::vector<uint32_t> listed;
        sc::ray_items(made, camera, range, &listed);
        std::vector<bool> in(made.items.size(), false);
        for (const uint32_t i : listed) {
            in.at(i) = true;
        }
        std::size_t wrong = 0;
        std::size_t within = 0;
        for (std::size_t i = 0; i < made.items.size(); ++i) {
            const sc::Vec3& root = made.items.at(i).root;
            const float dx = root[0] - camera[0];
            const float dy = root[1] - camera[1];
            const float dz = root[2] - camera[2];
            const bool reached = (dx * dx) + (dy * dy) + (dz * dz) < range * range;
            within += reached ? 1U : 0U;
            wrong += reached != in.at(i) ? 1U : 0U;
        }
        std::printf("      rays' clutter at lod_distance_scale %.1f (range %.0f): %zu of %zu items in the scene, %zu "
                    "wrong\n",
                    static_cast<double>(scale), static_cast<double>(range), listed.size(), made.items.size(), wrong);
        expect(wrong == 0 && within == listed.size() && !listed.empty() && listed.size() < made.items.size(),
               "rays' clutter: exactly the items within the range are in the scene - none beyond it");
    }

    // the transform against the vertex shader's steps
    const std::vector<sc::MeshVertex> meshes = sc::make_meshes();
    float worst = 0.0F;
    std::size_t tried = 0;
    for (std::size_t i = 0; i < made.items.size(); i += 37) {
        const sc::Item& item = made.items.at(i);
        const uint32_t kind = std::min<uint32_t>((item.packed >> sc::kItemKindShift) & 15U, 4U);
        const float variant = static_cast<float>((item.packed >> sc::kItemVariantShift) & 15U) / 15.0F;
        const float turn = static_cast<float>((item.packed >> sc::kItemFacingShift) & 255U) / 256.0F * 6.2831853F;
        const float h = static_cast<float>((item.packed >> sc::kItemHashShift) & 255U) / 255.0F;
        const std::array<std::array<float, 4>, 3> rows = sc::ray_item_transform(item);
        for (uint32_t v = 0; v < sc::kMeshVertices; ++v) {
            const sc::MeshVertex& m = meshes.at((kind * sc::kMeshVertices) + v);
            float x = m.position[0];
            float y = m.position[1];
            float z = m.position[2];
            if (kind == static_cast<uint32_t>(sc::ItemKind::kPebble)) {
                x *= 0.75F + (0.5F * variant);
                y *= 0.7F + (0.6F * h);
                z *= 0.75F + (0.5F * ((variant * 5.3F) - std::floor(variant * 5.3F)));
            }
            const float ct = std::cos(item.tilt);
            const float st = std::sin(item.tilt);
            const float ty = (y * ct) - (z * st);
            const float tz = (y * st) + (z * ct);
            const float c = std::cos(turn);
            const float s = std::sin(turn);
            const std::array<float, 3> shader{item.root[0] + (((x * c) - (tz * s)) * item.size),
                                              item.root[1] + (ty * item.size),
                                              item.root[2] + (((x * s) + (tz * c)) * item.size)};
            const std::array<float, 3> local{m.position[0], m.position[1], m.position[2]};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const std::array<float, 4>& row = rows.at(axis);
                const float placed = (row[0] * local[0]) + (row[1] * local[1]) + (row[2] * local[2]) + row[3];
                worst = std::max(worst, std::abs(placed - shader.at(axis))); // (units: the float rounding of a root ~4,000 out)
            }
        }
        ++tried;
    }
    std::printf("      rays' clutter: %zu items' vertices placed as the vertex shader places them, worst %.2g "
                "units\n",
                tried, static_cast<double>(worst));
    expect(tried > 10 && worst < 2e-3F, "rays' clutter: an item's transform is the vertex shader's placing");

    // the parts: every triangle of a kind in its parts, a can's split between body and label
    for (uint32_t kind = 0; kind < sc::kItemKinds; ++kind) {
        std::size_t whole = 0;
        for (uint32_t t = 0; t < sc::kMeshVertices / 3; ++t) {
            const sc::MeshVertex& a = meshes.at((kind * sc::kMeshVertices) + (t * 3));
            const sc::MeshVertex& b = meshes.at((kind * sc::kMeshVertices) + (t * 3) + 1);
            whole += a.position != b.position ? 1U : 0U; // (a collapsed one's corners all at the origin)
        }
        std::size_t parts = 0;
        for (const uint32_t part : sc::ray_parts_of(static_cast<sc::ItemKind>(kind))) {
            const std::vector<sc::MeshVertex> triangles = sc::ray_part_triangles(meshes, part);
            expect(!triangles.empty() && triangles.size() % 3 == 0, "rays' clutter: a part has triangles");
            parts += triangles.size() / 3;
            for (uint32_t shade = 0; shade < sc::kRayShades; ++shade) {
                const sc::Vec3 colour = sc::ray_part_colour(part, shade);
                expect(std::ranges::all_of(colour, [](float c) { return c > 0.0F && c <= 1.0F; }),
                       "rays' clutter: a part's colour is a colour");
            }
        }
        expect(parts == whole, "rays' clutter: a kind's parts hold every triangle of its mesh");
    }
    expect(sc::ray_parts_of(sc::ItemKind::kCan).size() == 2, "rays' clutter: a can is its body and its label");
}

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
        air_checks();
        layer_checks();
        ray_checks(); // (round PG)
        std::printf("%s: %d failure(s)\n", failures() == 0 ? "ok" : "FAILED", failures());
        return failures() == 0 ? 0 : 1;
    }
    bool ok = true;
    for (const std::filesystem::path& level : levels) {
        ok = measure_level(level, why) && ok;
    }
    return ok ? 0 : 1;
}
