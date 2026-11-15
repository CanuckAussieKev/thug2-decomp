// NOT ORIGINAL - thug2_relief_check.exe: building relief (engine/gfx/facade_relief.h) on walls whose result is known -
// a wall of two triangles with a window, its sill and its frame in each of two cells: the window's middle in by the cap,
// the sill out, the wall between them where it was; the surface closed (every edge shared by two triangles wound
// opposite ways, but the original outline's, used once and lying on it - nothing moved off it); the original vertices
// first and unchanged; the same bytes twice; a wall of two quads with a texture seam between them (vertices repeated)
// joined without a side along the seam; a corner of two walls - each closed back to the corner; a floor, a wall
// without texture coordinates, a strength of 0 and too many cuts left as they were; a big wall of many cells within 16
// bits (everything, the windows alone, or as it was). Also the file (formats/relief_format.h): written and read back, damaged files refused, and the
// same bytes as the tool's writer (tools/pbr_maps/relief_file.test.ts's sample hash). Exit code 0 when every check
// passes.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/facade_relief.h"
#include "engine/gfx/smooth_tess.h"
#include "formats/relief_format.h"
#include "formats/surface_map_format.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace relief = render::relief;
namespace tess = render::tess;

constexpr uint32_t kStride = 36; // position, normal, colour, one pair of texture coordinates
constexpr int kNormalAt = 12;
constexpr int kColourAt = 24;
constexpr int kUvAt = 28;
constexpr float kSlack = 1.0e-3F;
constexpr uint8_t kGrey = 0x80;
// (tools/pbr_maps/relief_file.test.ts: the sample's bytes hash so)
constexpr uint64_t kSampleFileHash = 0x82402FC39A786D45ULL;

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

struct Mesh {
    std::vector<uint8_t> vertices;
    uint32_t count = 0;
    std::vector<uint16_t> strip;
};

void add_vertex(Mesh* mesh, P3 p, P3 n, float u, float v)
{
    std::array<uint8_t, kStride> bytes{};
    const std::array<float, 3> position{p.x, p.y, p.z};
    const std::array<float, 3> normal{n.x, n.y, n.z};
    const std::array<float, 2> uv{u, v};
    std::memcpy(bytes.data(), position.data(), sizeof(position));
    std::memcpy(&core::at(bytes, kNormalAt), normal.data(), sizeof(normal));
    for (int c = 0; c < 4; ++c) {
        core::at(bytes, kColourAt + c) = kGrey;
    }
    std::memcpy(&core::at(bytes, kUvAt), uv.data(), sizeof(uv));
    mesh->vertices.insert(mesh->vertices.end(), bytes.begin(), bytes.end());
    ++mesh->count;
}

tess::VertexLayout layout()
{
    return {.stride = kStride, .normal_offset = kNormalAt, .colour_offset = kColourAt, .uv_offset = kUvAt, .uv_sets = 1};
}

// A quad's two triangles as a strip of four (a b c d: a b c and b c d drawn, the second turned round) - a, b along the
// bottom, c, d along the top; seen from +z when wound so.
void quad_strip(Mesh* mesh, uint16_t first)
{
    mesh->strip = {first, static_cast<uint16_t>(first + 1), static_cast<uint16_t>(first + 2),
                   static_cast<uint16_t>(first + 3)};
}

// A wall in the plane z = 0 from x 0 - width, y 0 - height, facing +z, texture u along x (0 - 1 across), v down.
Mesh wall(float width, float height)
{
    Mesh mesh;
    const P3 n{.x = 0, .y = 0, .z = 1};
    add_vertex(&mesh, {.x = 0, .y = 0, .z = 0}, n, 0.0F, 1.0F);
    add_vertex(&mesh, {.x = width, .y = 0, .z = 0}, n, 1.0F, 1.0F);
    add_vertex(&mesh, {.x = 0, .y = height, .z = 0}, n, 0.0F, 0.0F);
    add_vertex(&mesh, {.x = width, .y = height, .z = 0}, n, 1.0F, 0.0F);
    quad_strip(&mesh, 0);
    return mesh;
}

// Two cells across; in each a window (in), its sill under it (out) and its frame round it (out a little).
relief::Structure facade()
{
    relief::Structure s;
    s.repeat_u = 2;
    s.repeat_v = 1;
    s.rects = {{.u0 = 0.2F, .v0 = 0.25F, .u1 = 0.8F, .v1 = 0.75F, .height = 0.004F, .window = false},
               {.u0 = 0.15F, .v0 = 0.75F, .u1 = 0.85F, .v1 = 0.8F, .height = 0.01F, .window = false},
               {.u0 = 0.25F, .v0 = 0.3F, .u1 = 0.75F, .v1 = 0.7F, .height = -0.05F, .window = true}};
    return s;
}

P3 position_of(const relief::Result& r, uint32_t index)
{
    std::array<float, 3> p{};
    std::memcpy(p.data(), &core::at(r.vertices, std::size_t{kStride} * index), sizeof(p));
    return {.x = p[0], .y = p[1], .z = p[2]};
}

// The surface's edges (by position, to the bit) and how often each is run in each direction.
struct EdgeCount {
    int forward = 0;
    int backward = 0;
};
using Key = std::array<uint32_t, 3>;
Key key(P3 p)
{
    Key k{};
    std::memcpy(k.data(), &p, sizeof(k));
    return k;
}

std::map<std::pair<Key, Key>, EdgeCount> edges_of(const relief::Result& r)
{
    std::map<std::pair<Key, Key>, EdgeCount> edges;
    for (const std::array<uint16_t, 3>& t : tess::strip_triangles(r.strip)) {
        for (int e = 0; e < 3; ++e) {
            const Key a = key(position_of(r, core::at(t, e)));
            const Key b = key(position_of(r, core::at(t, (e + 1) % 3)));
            if (a == b) {
                continue;
            }
            if (a < b) {
                ++edges[{a, b}].forward;
            } else {
                ++edges[{b, a}].backward;
            }
        }
    }
    return edges;
}

P3 from_key(const Key& k)
{
    P3 p;
    std::memcpy(&p, k.data(), sizeof(p));
    return p;
}

// The surface's open stretches: its edges not shared by two triangles wound opposite ways, put together along each
// line (where three heights meet at a side's end, one side's edge runs along two others' - the same line, split; no
// gap): each line's edges' runs one way and the other summed, and what doesn't cancel kept.
struct Stretch {
    P3 from;
    P3 to;
};
std::vector<Stretch> open_stretches(const relief::Result& r)
{
    struct Run {
        double from = 0.0;
        double to = 0.0;
        int weight = 0;
    };
    struct Line {
        std::array<double, 3> point{};
        std::array<double, 3> direction{};
        std::vector<Run> runs;
    };
    std::map<std::array<long long, 6>, Line> lines;
    constexpr double kDirectionGrid = 1.0e4;
    constexpr double kPlaceGrid = 1.0e3;
    for (const auto& [ends, count] : edges_of(r)) {
        const int weight = count.forward - count.backward;
        if (weight == 0) {
            continue;
        }
        const P3 a = from_key(ends.first);
        const P3 b = from_key(ends.second);
        std::array<double, 3> d{static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y,
                                static_cast<double>(b.z) - a.z};
        const double size = std::hypot(d[0], d[1], d[2]);
        for (double& c : d) {
            c /= size;
        }
        int sign = 1;
        for (const double c : d) {
            if (std::fabs(c) > 1.0e-9) {
                sign = c > 0.0 ? 1 : -1;
                break;
            }
        }
        for (double& c : d) {
            c *= sign;
        }
        const std::array<double, 3> pa{a.x, a.y, a.z};
        const double ta = (pa[0] * d[0]) + (pa[1] * d[1]) + (pa[2] * d[2]);
        const double tb = ta + (size * sign);
        const std::array<double, 3> perpendicular{pa[0] - (ta * d[0]), pa[1] - (ta * d[1]), pa[2] - (ta * d[2])};
        const std::array<long long, 6> line_key{std::llround(d[0] * kDirectionGrid), std::llround(d[1] * kDirectionGrid),
                                                std::llround(d[2] * kDirectionGrid),
                                                std::llround(perpendicular[0] * kPlaceGrid),
                                                std::llround(perpendicular[1] * kPlaceGrid),
                                                std::llround(perpendicular[2] * kPlaceGrid)};
        Line& line = lines[line_key];
        line.point = perpendicular;
        line.direction = d;
        line.runs.push_back({.from = std::min(ta, tb), .to = std::max(ta, tb), .weight = ta < tb ? weight : -weight});
    }
    std::vector<Stretch> open;
    for (const auto& [line_key, line] : lines) {
        std::map<double, int> events;
        for (const Run& run : line.runs) {
            events[run.from] += run.weight;
            events[run.to] -= run.weight;
        }
        int sum = 0;
        double start = 0.0;
        for (const auto& [t, change] : events) {
            if (sum != 0) {
                const auto at = [&line](double along) {
                    return P3{.x = static_cast<float>(line.point[0] + (along * line.direction[0])),
                              .y = static_cast<float>(line.point[1] + (along * line.direction[1])),
                              .z = static_cast<float>(line.point[2] + (along * line.direction[2]))};
                };
                if (t - start > 1.0e-4) {
                    open.push_back({.from = at(start), .to = at(t)});
                }
            }
            sum += change;
            start = t;
        }
    }
    return open;
}

bool about(float a, float b) { return std::fabs(a - b) < kSlack; }

// The z of the surface's triangles over (x, y) seen from +z (the highest), or NaN where none covers it.
float surface_z(const relief::Result& r, float x, float y)
{
    float best = std::nanf("");
    for (const std::array<uint16_t, 3>& t : tess::strip_triangles(r.strip)) {
        const P3 a = position_of(r, t[0]);
        const P3 b = position_of(r, t[1]);
        const P3 c = position_of(r, t[2]);
        const float det = ((b.x - a.x) * (c.y - a.y)) - ((c.x - a.x) * (b.y - a.y));
        if (std::fabs(det) < 1.0e-6F) {
            continue;
        }
        const float s = (((x - a.x) * (c.y - a.y)) - ((c.x - a.x) * (y - a.y))) / det;
        const float q = (((b.x - a.x) * (y - a.y)) - ((x - a.x) * (b.y - a.y))) / det;
        if (s < -1.0e-5F || q < -1.0e-5F || s + q > 1.0F + 1.0e-5F) {
            continue;
        }
        const float z = a.z + (s * (b.z - a.z)) + (q * (c.z - a.z));
        if (std::isnan(best) || z > best) {
            best = z;
        }
    }
    return best;
}

void check_wall()
{
    // 400 units across: a texture width is 400 units; the window's 0.05 is 20 units in - the cap's 12; the sill's
    // 0.01 is 4 out - the cap's; the frame's 0.004 is 1.6 out.
    const Mesh mesh = wall(400.0F, 300.0F);
    const relief::Result r = relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, facade(), {});
    expect(r.changed, "wall: relieved");
    expect(r.walls == 2 && r.relieved == 2 && r.planes == 1, "wall: two wall triangles, one plane, both relieved");
    expect(std::memcmp(r.vertices.data(), mesh.vertices.data(), mesh.vertices.size()) == 0,
           "wall: the original vertices first and unchanged");
    std::printf("wall: %u vertices, %zu strip indices, %u pieces, %u sides, deepest %.2f, highest %.2f\n", r.vertex_count,
                r.strip.size(), r.pieces, r.sides, static_cast<double>(r.deepest), static_cast<double>(r.highest));
    expect(std::fabs(r.deepest - relief::kDefaultMostRecess) < kSlack, "wall: the window in by the cap");
    expect(std::fabs(r.highest - relief::kDefaultMostRaise) < kSlack, "wall: the sill out by the cap");
    // (the first cell: u 0 - 0.5, x 0 - 200; v down from the top - y = 300 (1 - v))
    const auto at = [&](float u_in_cell, float v) {
        return surface_z(r, u_in_cell * 200.0F, 300.0F * (1.0F - v));
    };
    expect(std::fabs(at(0.5F, 0.5F) + relief::kDefaultMostRecess) < kSlack, "wall: the window's middle 12 in");
    expect(std::fabs(at(0.5F, 0.775F) - relief::kDefaultMostRaise) < kSlack, "wall: the sill 4 out");
    expect(std::fabs(at(0.22F, 0.5F) - 1.6F) < kSlack, "wall: the frame 1.6 out");
    expect(std::fabs(at(0.05F, 0.1F)) < kSlack, "wall: the wall where it was");
    // (closed: every edge twice, once each way - or along the same line as others - but the outline's, on it)
    int outline = 0;
    int bad = 0;
    for (const Stretch& stretch : open_stretches(r)) {
        const auto on_outline = [](P3 p, P3 q) {
            return about(p.z, 0.0F) && about(q.z, 0.0F) &&
                   ((about(p.x, q.x) && (about(p.x, 0.0F) || about(p.x, 400.0F))) ||
                    (about(p.y, q.y) && (about(p.y, 0.0F) || about(p.y, 300.0F))));
        };
        if (on_outline(stretch.from, stretch.to)) {
            ++outline;
            continue;
        }
        if (bad < 8) {
            std::printf("  open: (%.3f %.3f %.3f) - (%.3f %.3f %.3f)\n", static_cast<double>(stretch.from.x),
                        static_cast<double>(stretch.from.y), static_cast<double>(stretch.from.z),
                        static_cast<double>(stretch.to.x), static_cast<double>(stretch.to.y),
                        static_cast<double>(stretch.to.z));
        }
        ++bad;
    }
    std::printf("wall: %d stretches of outline open, %d elsewhere\n", outline, bad);
    expect(bad == 0, "wall: closed - every edge inside shared by two triangles wound opposite ways (or along their line)");
    expect(outline >= 4, "wall: the outline where the original was");
    // (each new vertex's sources: the wall triangle's corners, weights summing to 1 - what the relit light blends)
    bool sources_ok = r.sources.size() == r.vertex_count - mesh.count;
    for (const relief::VertexSource& source : r.sources) {
        float sum = 0.0F;
        for (uint8_t k = 0; k < source.count; ++k) {
            sum += core::at(source.weight, k);
            sources_ok = sources_ok && core::at(source.from, k) < mesh.count;
        }
        sources_ok = sources_ok && source.count >= 1 && source.count <= 3 && std::fabs(sum - 1.0F) < kSlack;
    }
    expect(sources_ok, "wall: every new vertex's sources the original corners, weights summing to 1");
    const relief::Result again = relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, facade(), {});
    expect(again.vertices == r.vertices && again.strip == r.strip, "wall: the same bytes twice");
    // (half the strength: the frame at half its height)
    const relief::Result half =
        relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, facade(), {.strength = 0.5F});
    expect(std::fabs(surface_z(half, 0.22F * 200.0F, 150.0F) - 0.8F) < kSlack, "wall: half the strength, half the height");
}

void check_seam()
{
    // Two quads side by side, the vertices along the seam repeated (the same positions and texture coordinates): one
    // plane - no side along the seam, closed across it.
    Mesh mesh;
    const P3 n{.x = 0, .y = 0, .z = 1};
    for (int q = 0; q < 2; ++q) {
        const auto x0 = static_cast<float>(q) * 200.0F;
        const float u0 = static_cast<float>(q) * 0.5F;
        add_vertex(&mesh, {.x = x0, .y = 0, .z = 0}, n, u0, 1.0F);
        add_vertex(&mesh, {.x = x0 + 200.0F, .y = 0, .z = 0}, n, u0 + 0.5F, 1.0F);
        add_vertex(&mesh, {.x = x0, .y = 300.0F, .z = 0}, n, u0, 0.0F);
        add_vertex(&mesh, {.x = x0 + 200.0F, .y = 300.0F, .z = 0}, n, u0 + 0.5F, 0.0F);
    }
    // (two strips joined by degenerate triangles: 0 1 2 3 3 4 4 5 6 7)
    mesh.strip = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    relief::Structure s = facade();
    s.rects = {{.u0 = 0.4F, .v0 = 0.3F, .u1 = 0.6F, .v1 = 0.7F, .height = -0.05F, .window = true}}; // (across the seam)
    s.repeat_u = 1;
    const relief::Result r = relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, s, {});
    expect(r.changed && r.planes == 1, "seam: one plane across the repeated vertices");
    int bad = 0;
    for (const Stretch& stretch : open_stretches(r)) {
        const P3 p = stretch.from;
        const P3 q = stretch.to;
        const bool outline = about(p.z, 0.0F) && about(q.z, 0.0F) &&
                             ((about(p.x, q.x) && (about(p.x, 0.0F) || about(p.x, 400.0F))) ||
                              (about(p.y, q.y) && (about(p.y, 0.0F) || about(p.y, 300.0F))));
        bad += outline ? 0 : 1;
    }
    expect(bad == 0, "seam: closed across the seam");
    expect(std::fabs(surface_z(r, 200.0F, 150.0F) + relief::kDefaultMostRecess) < kSlack,
           "seam: the window across the seam in, no side at the seam");
}

void check_corner()
{
    // Two walls meeting at a right angle (x = 0, facing -x... and z = 0, facing +z), sharing the corner's edge: two
    // planes; each closed back to the corner - every point of the corner's edge at x = 0, z = 0.
    Mesh mesh;
    const P3 nz{.x = 0, .y = 0, .z = 1};
    const P3 nx{.x = -1, .y = 0, .z = 0};
    add_vertex(&mesh, {.x = 0, .y = 0, .z = 0}, nz, 0.0F, 1.0F);
    add_vertex(&mesh, {.x = 400, .y = 0, .z = 0}, nz, 1.0F, 1.0F);
    add_vertex(&mesh, {.x = 0, .y = 300, .z = 0}, nz, 0.0F, 0.0F);
    add_vertex(&mesh, {.x = 400, .y = 300, .z = 0}, nz, 1.0F, 0.0F);
    add_vertex(&mesh, {.x = 0, .y = 0, .z = -400}, nx, 0.0F, 1.0F);
    add_vertex(&mesh, {.x = 0, .y = 0, .z = 0}, nx, 1.0F, 1.0F);
    add_vertex(&mesh, {.x = 0, .y = 300, .z = -400}, nx, 0.0F, 0.0F);
    add_vertex(&mesh, {.x = 0, .y = 300, .z = 0}, nx, 1.0F, 0.0F);
    mesh.strip = {0, 1, 2, 3, 3, 4, 4, 5, 6, 7};
    relief::Structure s = facade();
    s.repeat_u = 1;
    s.rects = {{.u0 = 0.9F, .v0 = 0.3F, .u1 = 1.1F, .v1 = 0.7F, .height = -0.05F, .window = true}}; // (at the corner)
    const relief::Result r = relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, s, {});
    expect(r.changed && r.planes == 2, "corner: two planes");
    expect(r.sides > 0, "corner: closed back to the corner by sides");
    // (the outline's open edges: on the original edges)
    int off = 0;
    for (const Stretch& stretch : open_stretches(r)) {
        for (const P3 p : {stretch.from, stretch.to}) {
            const bool on_corner = about(p.x, 0.0F) && about(p.z, 0.0F);
            const bool on_rim = (about(p.z, 0.0F) && (about(p.x, 400.0F) || about(p.y, 0.0F) || about(p.y, 300.0F))) ||
                                (about(p.x, 0.0F) && (about(p.z, -400.0F) || about(p.y, 0.0F) || about(p.y, 300.0F)));
            if (!(on_corner || on_rim) && off < 8) {
                std::printf("  corner open off the outline: (%.3f %.3f %.3f)\n", static_cast<double>(p.x),
                            static_cast<double>(p.y), static_cast<double>(p.z));
            }
            off += on_corner || on_rim ? 0 : 1;
        }
    }
    expect(off == 0, "corner: the open edges on the original outline (the corner, the rims)");
}

void check_unchanged()
{
    const Mesh mesh = wall(400.0F, 300.0F);
    // (a floor: the same quad lying flat)
    Mesh floor;
    const P3 up{.x = 0, .y = 1, .z = 0};
    add_vertex(&floor, {.x = 0, .y = 0, .z = 0}, up, 0.0F, 1.0F);
    add_vertex(&floor, {.x = 400, .y = 0, .z = 0}, up, 1.0F, 1.0F);
    add_vertex(&floor, {.x = 0, .y = 0, .z = -300}, up, 0.0F, 0.0F);
    add_vertex(&floor, {.x = 400, .y = 0, .z = -300}, up, 1.0F, 0.0F);
    quad_strip(&floor, 0);
    expect(!relief::build(floor.vertices, floor.count, layout(), floor.strip, facade(), {}).changed, "a floor: unchanged");
    tess::VertexLayout no_uv = layout();
    no_uv.uv_offset = -1;
    no_uv.uv_sets = 0;
    expect(!relief::build(mesh.vertices, mesh.count, no_uv, mesh.strip, facade(), {}).changed,
           "no texture coordinates: unchanged");
    expect(!relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, facade(), {.strength = 0.0F}).changed,
           "strength 0: unchanged");
    expect(!relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, relief::Structure{}, {}).changed,
           "no rectangles: unchanged");
    relief::Structure dense = facade();
    dense.repeat_u = relief_format::kMostRepeats;
    dense.repeat_v = relief_format::kMostRepeats;
    expect(!relief::build(mesh.vertices, mesh.count, layout(), mesh.strip, dense, {}).changed,
           "too many cuts: unchanged");
    // (a big wall of many cells: everything doesn't fit 16 bits; the windows alone do)
    relief::Structure many = facade();
    many.repeat_u = 16;
    many.repeat_v = 12;
    const Mesh big = wall(8000.0F, 6000.0F);
    const relief::Result r = relief::build(big.vertices, big.count, layout(), big.strip, many, {});
    std::printf("big wall: changed %d, windows only %d, too big %d, %u vertices, %zu indices\n", r.changed ? 1 : 0,
                r.windows_only ? 1 : 0, r.too_big ? 1 : 0, r.vertex_count, r.strip.size());
    expect((r.changed && r.strip.size() <= 0xFFFF && r.vertex_count <= 0xFFFF) || r.too_big,
           "a big wall: within 16 bits - everything, the windows alone, or left as it was");
}

relief_format::File sample()
{
    relief_format::File file;
    file.detector = 1;
    file.sources = 0x1122334455667788ULL;
    file.entries = {{.checksum = 0x1234ABCDU, .content_hash = 0x0102030405060708ULL, .repeat_u = 3, .repeat_v = 2,
                     .first = 0, .count = 2}};
    file.rects = {{.kind = relief_format::Kind::kWindow, .u0 = 0.25F, .v0 = 0.3F, .u1 = 0.75F, .v1 = 0.7F, .height = -0.05F},
                  {.kind = relief_format::Kind::kSill, .u0 = 0.2F, .v0 = 0.7F, .u1 = 0.8F, .v1 = 0.75F, .height = 0.01F}};
    return file;
}

void check_file()
{
    const std::vector<uint8_t> bytes = relief_format::write_file(sample());
    relief_format::File read;
    expect(relief_format::read_file(bytes, &read), "file: read back");
    expect(read.entries.size() == 1 && read.rects.size() == 2 && read.entries[0].repeat_u == 3 &&
               read.rects[1].kind == relief_format::Kind::kSill && read.sources == 0x1122334455667788ULL,
           "file: the same entries and rectangles");
    expect(relief_format::find(read, 0x1234ABCDU, 0x0102030405060708ULL) != nullptr &&
               relief_format::find(read, 0x1234ABCDU, 1) == nullptr,
           "file: found by checksum and content hash only");
    const uint64_t hash = surface_map_format::content_hash(bytes);
    std::printf("file: %zu bytes, hash %016llx\n", bytes.size(), static_cast<unsigned long long>(hash));
    expect(hash == kSampleFileHash, "file: the same bytes as the tool's writer");
    std::vector<uint8_t> damaged = bytes;
    damaged.pop_back();
    expect(!relief_format::read_file(damaged, &read), "file: cut short - refused");
    damaged = bytes;
    core::at(damaged, relief_format::kHeaderSize + relief_format::kEntrySize) = 9; // (an unknown kind)
    expect(!relief_format::read_file(damaged, &read), "file: an unknown kind - refused");
    damaged = bytes;
    core::at(damaged, 0) = 0;
    expect(!relief_format::read_file(damaged, &read), "file: not one - refused");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_wall();
    check_seam();
    check_corner();
    check_unchanged();
    check_file();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("relief_check: all checks passed\n");
    return 0;
}
