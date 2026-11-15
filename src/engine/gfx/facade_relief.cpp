// NOT ORIGINAL - building relief (facade_relief.h): a mesh's facade walls cut along their structure's rectangles and
// built as relief, once at load.
#include "engine/gfx/facade_relief.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/smooth_tess.h"

namespace render::relief {

namespace {

constexpr std::size_t kPositionBytes = 12;
constexpr std::size_t kColourBytes = 4;
constexpr std::size_t kUvBytes = 8;
constexpr uint32_t kLastIndex = 0xFFFE;      // the highest vertex index made (0xFFFF: a strip's restart elsewhere)
constexpr std::size_t kMostIndices = 0xFFFF; // a level of detail's index count is 16 bits
constexpr float kDegenerateArea = 1.0e-6F;
constexpr double kSmallestUvArea = 1.0e-12;
constexpr float kSameFacing = 0.9995F; // two triangles' normals this close: one plane
constexpr double kAcross = 1.0e-4;     // (cell units) a step across a cut line to read the height beyond it
constexpr float kByteMax = 255.0F;
constexpr int kCorners = 3;
constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

struct V3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};
V3 operator+(V3 a, V3 b) { return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z}; }
V3 operator*(V3 a, float scale) { return {.x = a.x * scale, .y = a.y * scale, .z = a.z * scale}; }
float dot(V3 a, V3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
V3 cross(V3 a, V3 b)
{
    return {.x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x)};
}
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalised(V3 a)
{
    const float size = length(a);
    return size > 0.0F ? a * (1.0F / size) : V3{};
}

V3 read3(std::span<const uint8_t> bytes, std::size_t offset)
{
    std::array<float, 3> floats{};
    std::memcpy(floats.data(), bytes.subspan(offset, sizeof(floats)).data(), sizeof(floats));
    return {.x = floats[0], .y = floats[1], .z = floats[2]};
}
void write3(std::span<uint8_t> bytes, std::size_t offset, V3 value)
{
    const std::array<float, 3> floats{value.x, value.y, value.z};
    std::memcpy(bytes.subspan(offset, sizeof(floats)).data(), floats.data(), sizeof(floats));
}
std::array<float, 2> read2(std::span<const uint8_t> bytes, std::size_t offset)
{
    std::array<float, 2> floats{};
    std::memcpy(floats.data(), bytes.subspan(offset, sizeof(floats)).data(), sizeof(floats));
    return floats;
}

uint32_t float_bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
using PositionKey = std::array<uint32_t, 3>;
PositionKey key_of(V3 point) { return {float_bits(point.x), float_bits(point.y), float_bits(point.z)}; }

// A point of a piece, by what makes it (so that every triangle and piece sharing it makes it to the bit):
//   kCorner  the triangle's corner `corner`
//   kOnEdge  where the triangle's edge `edge` (its corners edge, edge + 1) crosses the cut line `line` of axis `axis`
//            (0: constant u in cell units, 1: constant v)
//   kInside  where the cut lines u = `line` and v = `line2` cross inside the triangle
enum class PointKind : uint8_t { kCorner, kOnEdge, kInside };
struct Point {
    PointKind kind = PointKind::kCorner;
    int corner = 0;
    int edge = 0;
    int axis = 0;
    double line = 0.0;
    double line2 = 0.0;
    std::array<double, 2> cell{}; // its cell coordinates (u, v x the repeats), for the cutting
};

bool on_edge(const Point& point, int edge)
{
    switch (point.kind) {
    case PointKind::kCorner:
        return point.corner == edge || point.corner == (edge + 1) % kCorners;
    case PointKind::kOnEdge:
        return point.edge == edge;
    case PointKind::kInside:
    default:
        return false;
    }
}

// The line of `axis` a point lies on exactly (a cut it was made by), or false.
bool on_line(const Point& point, int axis, double* line)
{
    if (point.kind == PointKind::kOnEdge && point.axis == axis) {
        *line = point.line;
        return true;
    }
    if (point.kind == PointKind::kInside) {
        *line = axis == 0 ? point.line : point.line2;
        return true;
    }
    return false;
}

using Polygon = std::vector<Point>;

// A wall triangle's facts.
struct Wall {
    std::array<uint16_t, 3> corners{};
    std::array<uint32_t, 3> welded{};
    std::array<V3, 3> position{};
    std::array<std::array<double, 2>, 3> cell{}; // corners' cell coordinates
    V3 normal;                                   // from its winding (the side it is seen from)
    float area = 0.0F;
    float per_u = 0.0F; // units the texture's width is drawn across here
    uint32_t plane = 0;
    std::array<bool, 3> outline{}; // its edges on its plane's outline
};

class UnionFind {
public:
    explicit UnionFind(std::size_t size) : parent_(size) { std::iota(parent_.begin(), parent_.end(), 0U); }
    uint32_t find(uint32_t at)
    {
        while (core::at(parent_, at) != at) {
            core::at(parent_, at) = core::at(parent_, core::at(parent_, at));
            at = core::at(parent_, at);
        }
        return at;
    }
    void join(uint32_t a, uint32_t b)
    {
        const uint32_t ra = find(a);
        const uint32_t rb = find(b);
        if (ra != rb) {
            core::at(parent_, std::max(ra, rb)) = std::min(ra, rb);
        }
    }

private:
    std::vector<uint32_t> parent_;
};

struct Plane {
    V3 normal;
    float per_u = 0.0F;
    float area = 0.0F;
};

class Builder {
public:
    Builder(std::span<const uint8_t> vertices, uint32_t vertex_count, const tess::VertexLayout& layout,
            std::span<const uint16_t> strip, const Structure& structure, const Options& options)
        : vertices_(vertices), count_(vertex_count), layout_(layout), strip_(strip), structure_(structure),
          options_(options)
    {
    }
    Result run();

private:
    [[nodiscard]] V3 position(uint16_t vertex) const
    {
        return read3(vertices_, (std::size_t{layout_.stride} * vertex));
    }
    [[nodiscard]] std::array<float, 2> uv(uint16_t vertex) const
    {
        return read2(vertices_, (std::size_t{layout_.stride} * vertex) + static_cast<std::size_t>(layout_.uv_offset));
    }
    void find_walls();
    void join_planes();
    [[nodiscard]] bool lines(const Wall& wall, int axis, std::vector<double>* out) const;
    // A wall triangle cut into its pieces, each with its height and its middle (cell coordinates); false when it would
    // take too many cuts (it stays as it is).
    struct Pieces {
        std::vector<Polygon> pieces;
        std::vector<float> heights;
        std::vector<std::array<double, 2>> centres;
        bool moved = false;
    };
    [[nodiscard]] bool cut_wall(const Wall& wall, Pieces* out) const;
    void cut(const Polygon& polygon, int axis, double line, const Wall& wall, Polygon* below, Polygon* above) const;
    [[nodiscard]] Point crossing(const Point& from, const Point& to, int axis, double line, const Wall& wall) const;
    [[nodiscard]] std::array<double, 2> cell_of_edge_point(const Wall& wall, int edge, int axis, double line) const;
    [[nodiscard]] float displacement(const Wall& wall, std::array<double, 2> cell) const;
    // Where `point` of `wall` is on the original surface, and the corners it is blended from (`sources` of them).
    struct Place {
        V3 where;
        std::array<int, 3> from{0, 1, 2};
        std::array<float, 3> weight{0.0F, 0.0F, 0.0F};
        int sources = 1;
    };
    [[nodiscard]] Place place(const Wall& wall, const Point& point) const;
    // The vertex made at `point` of `wall`, moved `along` units along its plane's normal (`facing`: a side wall's
    // normal, else the blend's); its index.
    uint32_t vertex_at(const Wall& wall, const Point& point, float along, const V3* facing);
    void build_wall(const Wall& wall, const Pieces& cut, Result* out);
    void add_triangle(uint32_t a, uint32_t b, uint32_t c);

    std::span<const uint8_t> vertices_;
    uint32_t count_;
    tess::VertexLayout layout_;
    std::span<const uint16_t> strip_;
    const Structure& structure_;
    Options options_;
    std::vector<std::array<uint16_t, 3>> triangles_; // the original's, as drawn
    std::vector<Wall> walls_;
    std::vector<int> wall_of_; // a triangle's wall (-1: not one)
    std::vector<Plane> planes_;
    std::map<PositionKey, uint32_t> welds_;
    std::vector<uint8_t> out_vertices_;
    uint32_t out_count_ = 0;
    std::unordered_multimap<uint64_t, uint32_t> made_; // a new vertex's bytes' hash -> its index
    std::vector<std::array<uint32_t, 3>> out_triangles_;
    bool overflow_ = false;
};

void Builder::find_walls()
{
    triangles_ = tess::strip_triangles(strip_);
    wall_of_.assign(triangles_.size(), -1);
    for (std::size_t t = 0; t < triangles_.size(); ++t) {
        const std::array<uint16_t, 3>& corners = core::at(triangles_, t);
        if (std::ranges::any_of(corners, [this](uint16_t v) { return v >= count_; })) {
            continue;
        }
        Wall wall;
        wall.corners = corners;
        for (int k = 0; k < kCorners; ++k) {
            core::at(wall.position, k) = position(core::at(corners, k));
            const std::array<float, 2> texture = uv(core::at(corners, k));
            core::at(wall.cell, k) = {static_cast<double>(texture[0]) * structure_.repeat_u,
                                      static_cast<double>(texture[1]) * structure_.repeat_v};
        }
        const V3 e1 = wall.position[1] - wall.position[0];
        const V3 e2 = wall.position[2] - wall.position[0];
        const V3 n = cross(e1, e2);
        const float twice = length(n);
        if (!(twice > kDegenerateArea) || !std::isfinite(twice)) {
            continue;
        }
        wall.normal = n * (1.0F / twice);
        wall.area = twice / 2;
        if (std::fabs(wall.normal.y) >= options_.upright) {
            continue;
        }
        // (units per texture width: the length of the position's derivative by u)
        const std::array<float, 2> t0 = uv(corners[0]);
        const std::array<float, 2> t1 = uv(corners[1]);
        const std::array<float, 2> t2 = uv(corners[2]);
        const double du1 = static_cast<double>(t1[0]) - t0[0];
        const double dv1 = static_cast<double>(t1[1]) - t0[1];
        const double du2 = static_cast<double>(t2[0]) - t0[0];
        const double dv2 = static_cast<double>(t2[1]) - t0[1];
        const double det = (du1 * dv2) - (du2 * dv1);
        if (!(std::fabs(det) > kSmallestUvArea) || !std::isfinite(det)) {
            continue;
        }
        const V3 dpdu = (e1 * static_cast<float>(dv2 / det)) - (e2 * static_cast<float>(dv1 / det));
        wall.per_u = length(dpdu);
        if (!(wall.per_u > 0.0F) || !std::isfinite(wall.per_u)) {
            continue;
        }
        for (int k = 0; k < kCorners; ++k) {
            const auto [found, added] = welds_.try_emplace(key_of(core::at(wall.position, k)),
                                                           static_cast<uint32_t>(welds_.size()));
            core::at(wall.welded, k) = found->second;
        }
        if (wall.welded[0] == wall.welded[1] || wall.welded[1] == wall.welded[2] || wall.welded[0] == wall.welded[2]) {
            continue;
        }
        core::at(wall_of_, t) = static_cast<int>(walls_.size());
        walls_.push_back(wall);
    }
}

// Walls joined into planes over edges two of them share (wound alike, facing the same way, the same texture
// coordinates at both ends); the edges not so shared are the planes' outlines.
void Builder::join_planes()
{
    struct Use {
        uint32_t wall = 0;
        int edge = 0;
    };
    std::map<std::pair<uint32_t, uint32_t>, std::vector<Use>> edges;
    for (uint32_t w = 0; w < walls_.size(); ++w) {
        const Wall& wall = core::at(walls_, w);
        for (int e = 0; e < kCorners; ++e) {
            const uint32_t a = core::at(wall.welded, e);
            const uint32_t b = core::at(wall.welded, (e + 1) % kCorners);
            edges[{std::min(a, b), std::max(a, b)}].push_back({.wall = w, .edge = e});
        }
    }
    UnionFind groups(walls_.size());
    for (Wall& wall : walls_) {
        wall.outline = {true, true, true};
    }
    for (const auto& [ends, uses] : edges) {
        if (uses.size() != 2) {
            continue;
        }
        Wall& first = core::at(walls_, uses[0].wall);
        Wall& second = core::at(walls_, uses[1].wall);
        const int e1 = uses[0].edge;
        const int e2 = uses[1].edge;
        // (wound alike: the second runs the edge the other way)
        const bool opposite = core::at(first.welded, e1) == core::at(second.welded, (e2 + 1) % kCorners);
        const bool facing = dot(first.normal, second.normal) >= kSameFacing;
        const auto same_uv = [this](uint16_t a, uint16_t b) {
            const std::array<float, 2> ua = uv(a);
            const std::array<float, 2> ub = uv(b);
            return float_bits(ua[0]) == float_bits(ub[0]) && float_bits(ua[1]) == float_bits(ub[1]);
        };
        const bool continuous = same_uv(core::at(first.corners, e1), core::at(second.corners, (e2 + 1) % kCorners)) &&
                                same_uv(core::at(first.corners, (e1 + 1) % kCorners), core::at(second.corners, e2));
        if (opposite && facing && continuous) {
            groups.join(uses[0].wall, uses[1].wall);
            core::at(first.outline, e1) = false;
            core::at(second.outline, e2) = false;
        }
    }
    std::map<uint32_t, uint32_t> plane_of_root;
    for (uint32_t w = 0; w < walls_.size(); ++w) {
        const auto [found, added] = plane_of_root.try_emplace(groups.find(w), static_cast<uint32_t>(planes_.size()));
        if (added) {
            planes_.emplace_back();
        }
        Wall& wall = core::at(walls_, w);
        wall.plane = found->second;
        Plane& plane = core::at(planes_, wall.plane);
        plane.normal = plane.normal + (wall.normal * wall.area);
        plane.per_u += wall.per_u * wall.area;
        plane.area += wall.area;
    }
    for (Plane& plane : planes_) {
        plane.normal = normalised(plane.normal);
        plane.per_u = plane.area > 0.0F ? plane.per_u / plane.area : 0.0F;
    }
}

// The cut lines of `axis` across a wall triangle (cell units, ascending): every rectangle edge's, in every cell it
// spans; false when there would be more than kMostCuts.
bool Builder::lines(const Wall& wall, int axis, std::vector<double>* out) const
{
    out->clear();
    std::vector<double> edges;
    for (const Rect& rect : structure_.rects) {
        if (options_.windows_only && !rect.window) {
            continue;
        }
        for (const float edge : axis == 0 ? std::array<float, 2>{rect.u0, rect.u1} : std::array<float, 2>{rect.v0, rect.v1}) {
            edges.push_back(static_cast<double>(edge) - std::floor(static_cast<double>(edge)));
        }
    }
    std::ranges::sort(edges);
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    double low = core::at(wall.cell, 0)[axis];
    double high = low;
    for (int k = 1; k < kCorners; ++k) {
        low = std::min(low, core::at(core::at(wall.cell, k), axis));
        high = std::max(high, core::at(core::at(wall.cell, k), axis));
    }
    if (edges.empty()) {
        return true;
    }
    if (high - low > kMostCuts) {
        return false;
    }
    for (double cell = std::floor(low); cell <= high; cell += 1.0) {
        for (const double edge : edges) {
            const double line = cell + edge;
            if (line > low && line < high) {
                out->push_back(line);
                if (out->size() > kMostCuts) {
                    return false;
                }
            }
        }
    }
    return true;
}

// A cut line's point on the triangle's edge `edge`: its cell coordinates, from the edge's two ends in one order (the
// lower welded position first).
std::array<double, 2> Builder::cell_of_edge_point(const Wall& wall, int edge, int axis, double line) const
{
    int lo = edge;
    int hi = (edge + 1) % kCorners;
    if (core::at(wall.welded, hi) < core::at(wall.welded, lo)) {
        std::swap(lo, hi);
    }
    const std::array<double, 2>& a = core::at(wall.cell, lo);
    const std::array<double, 2>& b = core::at(wall.cell, hi);
    const double t = (line - core::at(a, axis)) / (core::at(b, axis) - core::at(a, axis));
    std::array<double, 2> out{a[0] + ((b[0] - a[0]) * t), a[1] + ((b[1] - a[1]) * t)};
    core::at(out, axis) = line;
    return out;
}

Point Builder::crossing(const Point& from, const Point& to, int axis, double line, const Wall& wall) const
{
    Point point;
    for (int e = 0; e < kCorners; ++e) {
        if (on_edge(from, e) && on_edge(to, e)) {
            point.kind = PointKind::kOnEdge;
            point.edge = e;
            point.axis = axis;
            point.line = line;
            point.cell = cell_of_edge_point(wall, e, axis, line);
            return point;
        }
    }
    // (along a cut line of the other axis: the two lines' crossing)
    double other = 0.0;
    const int other_axis = 1 - axis;
    if (on_line(from, other_axis, &other)) {
        double check = 0.0;
        if (on_line(to, other_axis, &check) && check == other) {
            point.kind = PointKind::kInside;
            point.line = axis == 0 ? line : other;
            point.line2 = axis == 0 ? other : line;
            point.cell = {point.line, point.line2};
            return point;
        }
    }
    // (not expected: a crossing of a segment that is neither - made from its two ends, inside)
    const double t = (line - core::at(from.cell, axis)) / (core::at(to.cell, axis) - core::at(from.cell, axis));
    point.kind = PointKind::kInside;
    std::array<double, 2> cell{from.cell[0] + ((to.cell[0] - from.cell[0]) * t),
                               from.cell[1] + ((to.cell[1] - from.cell[1]) * t)};
    core::at(cell, axis) = line;
    point.line = cell[0];
    point.line2 = cell[1];
    point.cell = cell;
    return point;
}

// A convex polygon cut by the line `axis` = `line` into the part below it and the part above (either may be empty).
void Builder::cut(const Polygon& polygon, int axis, double line, const Wall& wall, Polygon* below, Polygon* above) const
{
    below->clear();
    above->clear();
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point& from = core::at(polygon, i);
        const Point& to = core::at(polygon, (i + 1) % n);
        const double a = core::at(from.cell, axis);
        const double b = core::at(to.cell, axis);
        if (a <= line) {
            below->push_back(from);
        }
        if (a >= line) {
            above->push_back(from);
        }
        if ((a < line && b > line) || (a > line && b < line)) {
            const Point point = crossing(from, to, axis, line, wall);
            below->push_back(point);
            above->push_back(point);
        }
    }
    if (below->size() < 3) {
        below->clear();
    }
    if (above->size() < 3) {
        above->clear();
    }
}

// The units a point at cell coordinates `cell` of a wall moves (its plane's scale, the strength, the caps).
float Builder::displacement(const Wall& wall, std::array<double, 2> cell) const
{
    const float height = height_at(structure_, cell[0] / structure_.repeat_u, cell[1] / structure_.repeat_v,
                                   options_.windows_only);
    const float units = height * options_.strength * core::at(planes_, wall.plane).per_u;
    const float capped = std::clamp(units, -options_.most_recess, options_.most_raise);
    return std::fabs(capped) < options_.least ? 0.0F : capped;
}

Builder::Place Builder::place(const Wall& wall, const Point& point) const
{
    Place at;
    if (point.kind == PointKind::kCorner) {
        at.from[0] = point.corner;
        at.weight[0] = 1.0F;
        at.where = core::at(wall.position, point.corner);
    } else if (point.kind == PointKind::kOnEdge) {
        int lo = point.edge;
        int hi = (point.edge + 1) % kCorners;
        if (core::at(wall.welded, hi) < core::at(wall.welded, lo)) {
            std::swap(lo, hi);
        }
        const double a = core::at(core::at(wall.cell, lo), point.axis);
        const double b = core::at(core::at(wall.cell, hi), point.axis);
        const auto t = static_cast<float>((point.line - a) / (b - a));
        at.from = {lo, hi, 0};
        at.weight = {1.0F - t, t, 0.0F};
        at.sources = 2;
        const V3 pa = core::at(wall.position, lo);
        at.where = pa + ((core::at(wall.position, hi) - pa) * t);
    } else {
        // (inside: the barycentric weights of the lines' crossing, solved in cell coordinates)
        const std::array<double, 2>& c0 = wall.cell[0];
        const std::array<double, 2>& c1 = wall.cell[1];
        const std::array<double, 2>& c2 = wall.cell[2];
        const double a1 = c1[0] - c0[0];
        const double b1 = c1[1] - c0[1];
        const double a2 = c2[0] - c0[0];
        const double b2 = c2[1] - c0[1];
        const double det = (a1 * b2) - (a2 * b1);
        const double pu = point.line - c0[0];
        const double pv = point.line2 - c0[1];
        const auto s = static_cast<float>(((pu * b2) - (a2 * pv)) / det);
        const auto r = static_cast<float>(((a1 * pv) - (pu * b1)) / det);
        at.weight = {1.0F - s - r, s, r};
        at.sources = 3;
        const V3 p0 = wall.position[0];
        at.where = p0 + ((wall.position[1] - p0) * s) + ((wall.position[2] - p0) * r);
    }
    return at;
}

uint32_t Builder::vertex_at(const Wall& wall, const Point& point, float along, const V3* facing)
{
    const Place at = place(wall, point);
    const std::size_t stride = layout_.stride;
    std::vector<uint8_t> bytes(stride);
    const auto source = [&](int k) {
        return vertices_.subspan(stride * core::at(wall.corners, core::at(at.from, k)), stride);
    };
    std::memcpy(bytes.data(), source(0).data(), stride);
    const std::span<uint8_t> out(bytes);
    const V3 normal = core::at(planes_, wall.plane).normal;
    write3(out, 0, at.where + (normal * along));
    if (at.sources > 1) {
        if (layout_.normal_offset >= 0) {
            V3 blended;
            for (int k = 0; k < at.sources; ++k) {
                blended = blended + (read3(source(k), static_cast<std::size_t>(layout_.normal_offset)) * core::at(at.weight, k));
            }
            write3(out, static_cast<std::size_t>(layout_.normal_offset), normalised(blended));
        }
        if (layout_.colour_offset >= 0) {
            for (std::size_t c = 0; c < kColourBytes; ++c) {
                const std::size_t byte = static_cast<std::size_t>(layout_.colour_offset) + c;
                float sum = 0.0F;
                for (int k = 0; k < at.sources; ++k) {
                    sum += static_cast<float>(source(k)[byte]) * core::at(at.weight, k);
                }
                out[byte] = static_cast<uint8_t>(std::clamp(std::round(sum), 0.0F, kByteMax));
            }
        }
        for (uint32_t set = 0; set < layout_.uv_sets; ++set) {
            const std::size_t offset = static_cast<std::size_t>(layout_.uv_offset) + (set * kUvBytes);
            std::array<float, 2> sum{};
            for (int k = 0; k < at.sources; ++k) {
                const std::array<float, 2> value = read2(source(k), offset);
                sum[0] += value[0] * core::at(at.weight, k);
                sum[1] += value[1] * core::at(at.weight, k);
            }
            std::memcpy(out.subspan(offset, kUvBytes).data(), sum.data(), kUvBytes);
        }
    }
    if (facing != nullptr && layout_.normal_offset >= 0) {
        write3(out, static_cast<std::size_t>(layout_.normal_offset), *facing);
    }
    // (the same bytes: the same vertex)
    uint64_t hash = kFnvBasis;
    for (const uint8_t byte : bytes) {
        hash = (hash ^ byte) * kFnvPrime;
    }
    const auto [first, last] = made_.equal_range(hash);
    for (auto found = first; found != last; ++found) {
        if (std::memcmp(&core::at(out_vertices_, stride * found->second), bytes.data(), stride) == 0) {
            return found->second;
        }
    }
    if (out_count_ > kLastIndex) {
        overflow_ = true;
        return 0;
    }
    out_vertices_.insert(out_vertices_.end(), bytes.begin(), bytes.end());
    made_.emplace(hash, out_count_);
    return out_count_++;
}

void Builder::add_triangle(uint32_t a, uint32_t b, uint32_t c)
{
    if (a != b && b != c && a != c) {
        out_triangles_.push_back({a, b, c});
    }
}

// A wall triangle cut along its lines into pieces, each moved by its height; the walls between pieces of different
// heights and along the plane's outline.
bool Builder::cut_wall(const Wall& wall, Pieces* out) const
{
    std::vector<double> across;
    std::vector<double> down;
    if (!lines(wall, 0, &across) || !lines(wall, 1, &down)) {
        return false;
    }
    Polygon whole;
    for (int k = 0; k < kCorners; ++k) {
        Point corner;
        corner.kind = PointKind::kCorner;
        corner.corner = k;
        corner.cell = core::at(wall.cell, k);
        whole.push_back(corner);
    }
    // (into slabs along u, then each into pieces along v)
    std::vector<Polygon> slabs;
    Polygon rest = whole;
    Polygon below;
    Polygon above;
    for (const double line : across) {
        cut(rest, 0, line, wall, &below, &above);
        if (!below.empty()) {
            slabs.push_back(below);
        }
        rest = above;
        if (rest.empty()) {
            break;
        }
    }
    if (!rest.empty()) {
        slabs.push_back(rest);
    }
    std::vector<Polygon>& pieces = out->pieces;
    for (const Polygon& slab : slabs) {
        Polygon left = slab;
        for (const double line : down) {
            cut(left, 1, line, wall, &below, &above);
            if (!below.empty()) {
                pieces.push_back(below);
            }
            left = above;
            if (left.empty()) {
                break;
            }
        }
        if (!left.empty()) {
            pieces.push_back(left);
        }
    }
    for (const Polygon& piece : pieces) {
        std::array<double, 2> centre{};
        for (const Point& point : piece) {
            centre[0] += point.cell[0];
            centre[1] += point.cell[1];
        }
        centre[0] /= static_cast<double>(piece.size());
        centre[1] /= static_cast<double>(piece.size());
        out->centres.push_back(centre);
        out->heights.push_back(displacement(wall, centre));
        out->moved = out->moved || out->heights.back() != 0.0F;
    }
    return true;
}

// A wall triangle's pieces built, each moved by its height; the sides between pieces of different heights and along
// the plane's outline.
void Builder::build_wall(const Wall& wall, const Pieces& cut, Result* out)
{
    const std::vector<Polygon>& pieces = cut.pieces;
    const std::vector<float>& heights = cut.heights;
    const std::vector<std::array<double, 2>>& centres = cut.centres;
    ++out->relieved;
    const V3 normal = core::at(planes_, wall.plane).normal;
    for (std::size_t p = 0; p < pieces.size(); ++p) {
        const Polygon& piece = core::at(pieces, p);
        const float height = core::at(heights, p);
        out->deepest = std::max(out->deepest, -height);
        out->highest = std::max(out->highest, height);
        ++out->pieces;
        std::vector<uint32_t> made;
        made.reserve(piece.size());
        for (const Point& point : piece) {
            made.push_back(vertex_at(wall, point, height, nullptr));
        }
        for (std::size_t k = 1; k + 1 < made.size(); ++k) {
            add_triangle(made[0], core::at(made, k), core::at(made, k + 1));
        }
        // (its sides: down to the lower piece across a cut line, or back to the original edge along the outline)
        for (std::size_t k = 0; k < piece.size(); ++k) {
            const Point& a = core::at(piece, k);
            const Point& b = core::at(piece, (k + 1) % piece.size());
            float other = height;
            bool side = false;
            bool outline = false;
            for (int e = 0; e < kCorners && !side; ++e) {
                if (on_edge(a, e) && on_edge(b, e)) {
                    side = true;
                    if (core::at(wall.outline, e)) {
                        outline = true;
                        other = 0.0F;
                    } else {
                        // (the plane's next triangle: its height just across the edge - a rectangle may end on it)
                        const std::array<double, 2> middle{(a.cell[0] + b.cell[0]) / 2, (a.cell[1] + b.cell[1]) / 2};
                        const std::array<double, 2>& centre = core::at(centres, p);
                        const double du = middle[0] - centre[0];
                        const double dv = middle[1] - centre[1];
                        const double size = std::hypot(du, dv);
                        other = size > 0.0
                                    ? displacement(wall, {middle[0] + (du / size * kAcross), middle[1] + (dv / size * kAcross)})
                                    : height;
                    }
                }
            }
            if (!side) {
                for (int axis = 0; axis < 2 && !side; ++axis) {
                    double la = 0.0;
                    double lb = 0.0;
                    if (on_line(a, axis, &la) && on_line(b, axis, &lb) && la == lb) {
                        side = true;
                        // (the height just across the line, away from this piece)
                        std::array<double, 2> beyond{(a.cell[0] + b.cell[0]) / 2, (a.cell[1] + b.cell[1]) / 2};
                        core::at(beyond, axis) = la + (core::at(core::at(centres, p), axis) < la ? kAcross : -kAcross);
                        other = displacement(wall, beyond);
                    }
                }
            }
            // (between pieces the higher one builds it - along the outline this one, either way; nothing where level)
            if (!side || other == height || (!outline && other > height)) {
                continue;
            }
            const V3 a0 = place(wall, a).where;
            const V3 b0 = place(wall, b).where;
            V3 away = normalised(cross(normal, b0 - a0));
            const V3 middle = (a0 + b0) * 0.5F;
            V3 centre_world{};
            for (const Point& point : piece) {
                centre_world = centre_world + place(wall, point).where;
            }
            centre_world = centre_world * (1.0F / static_cast<float>(piece.size()));
            if (dot(away, middle - centre_world) < 0.0F) {
                away = away * -1.0F;
            }
            // (it faces the lower side: away from this piece, or - a piece in from the outline - into it)
            if (other > height) {
                away = away * -1.0F;
            }
            const uint32_t top_a = vertex_at(wall, a, height, &away);
            const uint32_t top_b = vertex_at(wall, b, height, &away);
            const uint32_t low_b = vertex_at(wall, b, other, &away);
            const uint32_t low_a = vertex_at(wall, a, other, &away);
            // (wound to be seen from `away`)
            const V3 pa = a0 + (normal * height);
            const V3 pb = b0 + (normal * height);
            const V3 pc = b0 + (normal * other);
            if (dot(cross(pb - pa, pc - pa), away) >= 0.0F) {
                add_triangle(top_a, top_b, low_b);
                add_triangle(top_a, low_b, low_a);
            } else {
                add_triangle(top_a, low_b, top_b);
                add_triangle(top_a, low_a, low_b);
            }
            ++out->sides;
        }
    }
}

Result Builder::run()
{
    Result out;
    if (layout_.uv_offset < 0 || layout_.uv_sets == 0 || layout_.stride < kPositionBytes || structure_.rects.empty() ||
        structure_.repeat_u == 0 || structure_.repeat_v == 0 || !(options_.strength > 0.0F)) {
        return out;
    }
    find_walls();
    out.triangles = static_cast<uint32_t>(triangles_.size());
    out.walls = static_cast<uint32_t>(walls_.size());
    if (walls_.empty()) {
        return out;
    }
    join_planes();
    out.planes = static_cast<uint32_t>(planes_.size());
    out_vertices_.assign(vertices_.begin(), vertices_.begin() + static_cast<std::ptrdiff_t>(std::size_t{layout_.stride} * count_));
    out_vertices_.reserve(out_vertices_.size() * 4);
    out_count_ = count_;
    // (cut first: a plane with any piece off its surface is built whole - every triangle of it cut the same way, so the
    // points along their shared edges are the same ones, no T-junctions)
    std::vector<Pieces> cut(walls_.size());
    std::vector<bool> usable(walls_.size(), false);
    std::vector<bool> plane_moved(planes_.size(), false);
    std::vector<bool> plane_kept(planes_.size(), false); // (a triangle of it too finely cut: the plane stays whole)
    for (std::size_t w = 0; w < walls_.size(); ++w) {
        usable[w] = cut_wall(core::at(walls_, w), &core::at(cut, w));
        if (usable[w] && core::at(cut, w).moved) {
            plane_moved[core::at(walls_, w).plane] = true;
        }
        if (!usable[w]) {
            plane_kept[core::at(walls_, w).plane] = true;
        }
    }
    for (std::size_t t = 0; t < triangles_.size(); ++t) {
        const int w = core::at(wall_of_, t);
        const bool build_it = w >= 0 && plane_moved[core::at(walls_, w).plane] && !plane_kept[core::at(walls_, w).plane];
        if (!build_it) {
            const std::array<uint16_t, 3>& kept = core::at(triangles_, t);
            add_triangle(kept[0], kept[1], kept[2]);
            continue;
        }
        build_wall(core::at(walls_, w), core::at(cut, static_cast<std::size_t>(w)), &out);
        if (overflow_) {
            return out;
        }
    }
    if (out.relieved == 0) {
        return out;
    }
    // (the triangles as one strip: each starting at an even place, joined by degenerate ones)
    std::vector<uint16_t> strip;
    strip.reserve(out_triangles_.size() * 6);
    for (const std::array<uint32_t, 3>& triangle : out_triangles_) {
        if (!strip.empty()) {
            strip.push_back(strip.back());
            strip.push_back(static_cast<uint16_t>(triangle[0]));
            if (strip.size() % 2 != 0) {
                strip.push_back(static_cast<uint16_t>(triangle[0]));
            }
        }
        for (const uint32_t index : triangle) {
            strip.push_back(static_cast<uint16_t>(index));
        }
        if (strip.size() > kMostIndices) {
            return out;
        }
    }
    out.changed = true;
    out.vertices = std::move(out_vertices_);
    out.vertex_count = out_count_;
    out.strip = std::move(strip);
    return out;
}

} // namespace

float height_at(const Structure& structure, double u, double v, bool windows_only)
{
    const double cu = u * structure.repeat_u;
    const double cv = v * structure.repeat_v;
    const double fu = cu - std::floor(cu);
    const double fv = cv - std::floor(cv);
    float height = 0.0F;
    for (const Rect& rect : structure.rects) {
        if (windows_only && !rect.window) {
            continue;
        }
        const auto inside = [](double f, float low, float high) {
            return (f >= low && f < high) || (f + 1.0 >= low && f + 1.0 < high);
        };
        if (inside(fu, rect.u0, rect.u1) && inside(fv, rect.v0, rect.v1)) {
            height = rect.height;
        }
    }
    return height;
}

Result build(std::span<const uint8_t> vertices, uint32_t vertex_count, const tess::VertexLayout& layout,
             std::span<const uint16_t> strip, const Structure& structure, const Options& options)
{
    Result result = Builder(vertices, vertex_count, layout, strip, structure, options).run();
    if (result.changed || result.relieved == 0) {
        return result;
    }
    // (too big with everything: the windows alone, else as it was)
    Options windows = options;
    windows.windows_only = true;
    Result fewer = Builder(vertices, vertex_count, layout, strip, structure, windows).run();
    if (fewer.changed) {
        fewer.windows_only = true;
        return fewer;
    }
    Result kept;
    kept.triangles = result.triangles;
    kept.walls = result.walls;
    kept.too_big = true;
    return kept;
}

} // namespace render::relief
