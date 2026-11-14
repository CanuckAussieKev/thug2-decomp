// NOT ORIGINAL - smoothed curved surfaces (smooth_tess.h): Phong tessellation of a mesh's curved triangles at load.
#include "engine/gfx/smooth_tess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"

namespace render::tess {

namespace {

constexpr std::size_t kPositionBytes = 12;
constexpr std::size_t kColourBytes = 4;
constexpr std::size_t kUvBytes = 8;
constexpr uint32_t kLastIndex = 0xFFFE;      // the highest vertex index made (0xFFFF: a strip's restart elsewhere)
constexpr std::size_t kMostIndices = 0xFFFF; // a level of detail's index count is 16 bits
constexpr int kLowestLevel = 2;
constexpr int kHighestLevel = 8;
constexpr float kDegenerateArea = 1.0e-6F; // a triangle's doubled area under this: degenerate
constexpr float kHalf = 0.5F;
constexpr float kByteMax = 255.0F;
constexpr float kDegreesPerHalfTurn = 180.0F;
constexpr float kSmallestTarget = 1.0e-4F;
constexpr int kEdges = 3;
constexpr unsigned kHighBits = 32U;

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
V3 normalised(V3 a, V3 fallback)
{
    const float len = length(a);
    return len > 0.0F && std::isfinite(len) ? a * (1.0F / len) : fallback;
}

V3 read3(std::span<const uint8_t> bytes, std::size_t offset)
{
    std::array<float, 3> floats{};
    std::memcpy(floats.data(), bytes.subspan(offset, kPositionBytes).data(), kPositionBytes);
    return {.x = core::at(floats, 0), .y = core::at(floats, 1), .z = core::at(floats, 2)};
}
void write3(std::span<uint8_t> bytes, std::size_t offset, V3 value)
{
    const std::array<float, 3> floats{value.x, value.y, value.z};
    std::memcpy(bytes.subspan(offset, kPositionBytes).data(), floats.data(), kPositionBytes);
}
std::array<float, 2> read2(std::span<const uint8_t> bytes, std::size_t offset)
{
    std::array<float, 2> floats{};
    std::memcpy(floats.data(), bytes.subspan(offset, kUvBytes).data(), kUvBytes);
    return floats;
}
void write2(std::span<uint8_t> bytes, std::size_t offset, std::array<float, 2> floats)
{
    std::memcpy(bytes.subspan(offset, kUvBytes).data(), floats.data(), kUvBytes);
}

// A position's bits as a key (texture seams repeat a position exactly).
struct PositionKey {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    bool operator==(const PositionKey&) const = default;
};
struct PositionHash {
    std::size_t operator()(const PositionKey& key) const noexcept
    {
        constexpr uint64_t kMix1 = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t kMix2 = 0xC2B2AE3D27D4EB4FULL;
        uint64_t hash = (uint64_t{key.x} * kMix1) ^ (uint64_t{key.y} * kMix2) ^ (uint64_t{key.z} + (uint64_t{key.x} << kHighBits));
        hash ^= hash >> kHighBits;
        return static_cast<std::size_t>(hash);
    }
};
uint32_t float_bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    constexpr uint32_t kNegativeZero = 0x80000000U;
    return bits == kNegativeZero ? 0U : bits; // (+0 and -0 the same point)
}
PositionKey key_of(V3 point) { return {.x = float_bits(point.x), .y = float_bits(point.y), .z = float_bits(point.z)}; }

uint64_t pair_key(uint32_t a, uint32_t b)
{
    return a < b ? (uint64_t{a} << kHighBits) | b : (uint64_t{b} << kHighBits) | a;
}

// A triangle of the strip: its corners as drawn and where in the strip it starts.
struct Triangle {
    std::array<uint16_t, 3> corner{};
    uint32_t at = 0;
};

std::vector<Triangle> decode(std::span<const uint16_t> strip)
{
    std::vector<Triangle> out;
    for (std::size_t k = 0; k + 2 < strip.size(); ++k) {
        const uint16_t first = core::at(strip, k);
        const uint16_t second = core::at(strip, k + 1);
        const uint16_t third = core::at(strip, k + 2);
        if (first == second || second == third || first == third) {
            continue;
        }
        // (odd triangles of a strip are wound the other way)
        out.push_back({.corner = (k & 1U) != 0U ? std::array<uint16_t, 3>{second, first, third}
                                                : std::array<uint16_t, 3>{first, second, third},
                       .at = static_cast<uint32_t>(k)});
    }
    return out;
}

// Corners joined into smoothing groups.
class Groups {
public:
    explicit Groups(std::size_t count) : parent_(count)
    {
        for (std::size_t i = 0; i < count; ++i) {
            core::at(parent_, i) = static_cast<uint32_t>(i);
        }
    }
    uint32_t find(uint32_t slot)
    {
        while (core::at(parent_, slot) != slot) {
            core::at(parent_, slot) = core::at(parent_, core::at(parent_, slot));
            slot = core::at(parent_, slot);
        }
        return slot;
    }
    void join(uint32_t a, uint32_t b)
    {
        a = find(a);
        b = find(b);
        if (a != b) {
            core::at(parent_, std::max(a, b)) = std::min(a, b);
        }
    }

private:
    std::vector<uint32_t> parent_;
};

// An edge's uses: the triangles on it (the first two), how many, and what was decided.
struct EdgeUse {
    int count = 0;
    std::array<uint32_t, 2> triangle{};
    std::array<uint8_t, 2> edge{};
    bool smooth = false;
    bool curved = false;
    bool same_indices = false; // both triangles name the same two vertices (a new point shared outright)
    bool prism = false;        // smoothing: a prism's side edge (find_prisms)
};

// Phong tessellation's pull at a point of the flat triangle, for a shape factor of 1: each corner's tangent plane's
// pull, weighted by the point's barycentric weights.
V3 pull_at(V3 flat, std::span<const V3> corners, std::span<const V3> normals, std::span<const float> weights)
{
    V3 out{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const V3 normal = core::at(normals, i);
        out = out - (normal * (dot(flat - core::at(corners, i), normal) * core::at(weights, i)));
    }
    return out;
}

// The factor an edge's pull is raised by so that, with the shape factor 0.5, its middle lies on the circular arc its two
// normals describe: 2 (1 / cos h - 1) / sin^2 h for h half the angle between them (1 for parallel normals, 1.16 at
// the crease angle). The same from either end.
float arc_factor(V3 n0, V3 n1)
{
    constexpr float kParallel = 1.0e-6F;
    constexpr float kMostFactor = 2.0F;
    const float cosine = std::clamp(dot(n0, n1), -1.0F, 1.0F);
    const float sin2 = (1.0F - cosine) * kHalf;
    const float cos_half = std::sqrt((1.0F + cosine) * kHalf);
    if (sin2 < kParallel || !(cos_half > 0.0F)) {
        return 1.0F;
    }
    return std::clamp(((1.0F / cos_half) - 1.0F) * 2 / sin2, 1.0F, kMostFactor);
}

// An edge's curve: its two ends' positions and normals in one order (the lower welded position first), whichever
// triangle asks.
struct EdgeEnds {
    V3 p0, n0, p1, n1;
};
V3 edge_pull(const EdgeEnds& ends, float along, V3* flat)
{
    *flat = ends.p0 + ((ends.p1 - ends.p0) * along);
    const std::array<V3, 2> corners{ends.p0, ends.p1};
    const std::array<V3, 2> normals{ends.n0, ends.n1};
    const std::array<float, 2> weights{1.0F - along, along};
    return pull_at(*flat, corners, normals, weights) * arc_factor(ends.n0, ends.n1);
}

// Inside a triangle: its edges' arc factors, each weighted toward its own edge (the product of its two ends' weights).
float inner_factor(std::span<const V3, 3> normals, std::span<const float, 3> weights)
{
    const float ab = core::at(weights, 0) * core::at(weights, 1);
    const float bc = core::at(weights, 1) * core::at(weights, 2);
    const float ca = core::at(weights, 2) * core::at(weights, 0);
    const float sum = ab + bc + ca;
    if (!(sum > 0.0F)) {
        return 1.0F;
    }
    return ((ab * arc_factor(core::at(normals, 0), core::at(normals, 1))) +
            (bc * arc_factor(core::at(normals, 1), core::at(normals, 2))) +
            (ca * arc_factor(core::at(normals, 2), core::at(normals, 0)))) /
           sum;
}

// A new vertex: where on the flat triangle, its pull, the normal it is displaced along, its sources' blend.
struct NewVertex {
    V3 flat;
    V3 pull;
    V3 normal;
    bool displaceable = false;
    std::array<uint16_t, 3> from{};
    std::array<float, 3> weight{};
};

uint32_t slot_of(uint32_t triangle, int corner) { return (triangle * 3U) + static_cast<uint32_t>(corner); }

// One run of a strip appended to another, starting at the parity it was made for (degenerate joins between).
void append_run(std::vector<uint16_t>* out, std::span<const uint16_t> run, uint32_t parity)
{
    if (run.empty()) {
        return;
    }
    if (!out->empty()) {
        const uint16_t last = out->back();
        out->push_back(last);
        out->push_back(run.front());
    }
    if ((out->size() & 1U) != (parity & 1U)) {
        out->push_back(run.front());
    }
    out->insert(out->end(), run.begin(), run.end());
}

class Smoother {
public:
    struct Input {
        std::span<const uint8_t> vertices;
        uint32_t count = 0;
        VertexLayout layout;
        std::span<const uint16_t> strip;
        Options options;
    };
    explicit Smoother(const Input& input) : in_(input) {}

    Result run();

private:
    [[nodiscard]] bool readable() const;
    void weld_and_face();
    void find_edges();
    void find_prisms();
    [[nodiscard]] bool joins(const EdgeUse& use, const std::array<uint16_t, 4>& ends, float cosine, float crease,
                             Result* out) const;
    [[nodiscard]] bool file_normal(uint16_t vertex, V3* out) const;
    [[nodiscard]] int smooth_level(int level) const;
    void make_groups(Result* out);
    void mark_curves();
    [[nodiscard]] float largest_edge_pull() const;
    bool build(int level, Result* out);
    uint32_t edge_point(uint32_t triangle, int edge, uint16_t from, uint16_t to, int step);
    uint32_t add_vertex(const NewVertex& vertex);
    void write_vertices(Result* out, float shape);
    void write_strip(Result* out);
    [[nodiscard]] V3 position(uint32_t vertex) const { return core::at(position_, vertex); }
    [[nodiscard]] uint32_t weld(uint32_t vertex) const { return core::at(weld_, vertex); }
    [[nodiscard]] V3 corner_normal(uint32_t triangle, int corner) const
    {
        return core::at(corner_normal_, slot_of(triangle, corner));
    }
    [[nodiscard]] const std::array<uint16_t, 3>& corners(uint32_t triangle) const
    {
        return core::at(tris_, triangle).corner;
    }
    [[nodiscard]] EdgeEnds ends_of(uint32_t triangle, int edge) const;

    Input in_;
    float shape_ = 0.0F; // the shape factor (smoothing: times its strength)
    std::vector<V3> position_;
    std::vector<uint32_t> weld_;
    std::vector<V3> welded_; // each welded position
    std::vector<Triangle> tris_;
    std::vector<V3> face_;
    std::vector<float> angle_; // each corner's
    std::vector<bool> degenerate_;
    std::unordered_map<uint64_t, EdgeUse> edges_;
    std::vector<V3> corner_normal_;
    std::vector<std::array<bool, 3>> curved_; // each triangle's edges (a-b, b-c, c-a)
    std::vector<bool> split_;
    int level_ = 0;
    std::vector<NewVertex> made_;
    std::unordered_map<uint64_t, uint32_t> edge_points_;     // a vertex pair's first new point
    std::vector<std::vector<std::vector<uint32_t>>> grids_;   // each split triangle's grid (row i: i + 1 points)
    std::vector<uint32_t> grid_of_;                           // a triangle's grid, or none
};

bool Smoother::readable() const
{
    const auto stride = static_cast<std::size_t>(in_.layout.stride);
    if (stride < kPositionBytes || in_.count < 3 || in_.strip.size() < 3 || in_.options.max_level < kLowestLevel ||
        in_.vertices.size() < stride * in_.count) {
        return false;
    }
    const auto fits = [&](int offset, std::size_t bytes) {
        return offset < 0 || static_cast<std::size_t>(offset) + bytes <= stride;
    };
    if (!fits(in_.layout.normal_offset, kPositionBytes) || !fits(in_.layout.colour_offset, kColourBytes) ||
        !fits(in_.layout.uv_offset, kUvBytes * std::max<std::size_t>(1, in_.layout.uv_sets))) {
        return false;
    }
    return std::ranges::all_of(in_.strip, [&](uint16_t index) { return index < in_.count; });
}

void Smoother::weld_and_face()
{
    const auto stride = static_cast<std::size_t>(in_.layout.stride);
    position_.resize(in_.count);
    weld_.resize(in_.count);
    std::unordered_map<PositionKey, uint32_t, PositionHash> welded;
    for (uint32_t i = 0; i < in_.count; ++i) {
        core::at(position_, i) = read3(in_.vertices, stride * i);
        const auto [where, added] = welded.try_emplace(key_of(position(i)), static_cast<uint32_t>(welded.size()));
        core::at(weld_, i) = where->second;
        if (added) {
            welded_.push_back(position(i));
        }
    }
    tris_ = decode(in_.strip);
    face_.resize(tris_.size());
    degenerate_.assign(tris_.size(), false);
    angle_.assign(tris_.size() * 3, 0.0F);
    for (uint32_t tri = 0; tri < tris_.size(); ++tri) {
        const auto& corner = corners(tri);
        const std::array<V3, 3> points{position(core::at(corner, 0)), position(core::at(corner, 1)), position(core::at(corner, 2))};
        const V3 normal = cross(core::at(points, 1) - core::at(points, 0), core::at(points, 2) - core::at(points, 0));
        const float area = length(normal);
        const bool same = weld(core::at(corner, 0)) == weld(core::at(corner, 1)) || weld(core::at(corner, 1)) == weld(core::at(corner, 2)) ||
                          weld(core::at(corner, 0)) == weld(core::at(corner, 2));
        const bool degenerate = same || !(area > kDegenerateArea) || !std::isfinite(area);
        core::at(degenerate_, tri) = degenerate;
        core::at(face_, tri) = degenerate ? V3{} : normal * (1.0F / area);
        if (degenerate) {
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            const V3 edge1 = normalised(core::at(points, (k + 1) % 3) - core::at(points, k), {});
            const V3 edge2 = normalised(core::at(points, (k + 2) % 3) - core::at(points, k), {});
            core::at(angle_, slot_of(tri, k)) = std::acos(std::clamp(dot(edge1, edge2), -1.0F, 1.0F));
        }
    }
}

void Smoother::find_edges()
{
    for (uint32_t tri = 0; tri < tris_.size(); ++tri) {
        if (core::at(degenerate_, tri)) {
            continue;
        }
        const auto& corner = corners(tri);
        for (int side = 0; side < kEdges; ++side) {
            EdgeUse& use = edges_[pair_key(weld(core::at(corner, side)), weld(core::at(corner, (side + 1) % 3)))];
            if (use.count < 2) {
                core::at(use.triangle, use.count) = tri;
                core::at(use.edge, use.count) = static_cast<uint8_t>(side);
            }
            ++use.count;
        }
    }
}

void Smoother::make_groups(Result* out)
{
    const float crease = std::cos(in_.options.crease_degrees * std::numbers::pi_v<float> / kDegreesPerHalfTurn);
    if (in_.options.smooth) {
        find_prisms();
    }
    Groups groups(tris_.size() * 3);
    for (auto& [key, use] : edges_) {
        if (use.count != 2) {
            continue; // (a boundary, or more than two faces on it: hard)
        }
        const uint32_t t1 = core::at(use.triangle, 0);
        const uint32_t t2 = core::at(use.triangle, 1);
        const int e1 = core::at(use.edge, 0);
        const int e2 = core::at(use.edge, 1);
        const auto& c1 = corners(t1);
        const auto& c2 = corners(t2);
        const uint16_t a1 = core::at(c1, e1);
        const uint16_t b1 = core::at(c1, (e1 + 1) % 3);
        const uint16_t a2 = core::at(c2, (e2 + 1) % 3); // (wound the same way: the second walks the edge backwards)
        const uint16_t b2 = core::at(c2, e2);
        const bool opposite = weld(a1) == weld(a2) && weld(b1) == weld(b2);
        if (!opposite ||
            !joins(use, {a1, b1, a2, b2}, dot(core::at(face_, t1), core::at(face_, t2)), crease, out)) {
            continue;
        }
        use.smooth = true;
        use.same_indices = a1 == a2 && b1 == b2;
        groups.join(slot_of(t1, e1), slot_of(t2, (e2 + 1) % 3));
        groups.join(slot_of(t1, (e1 + 1) % 3), slot_of(t2, e2));
    }
    std::vector<V3> sum(tris_.size() * 3);
    for (uint32_t tri = 0; tri < tris_.size(); ++tri) {
        for (int k = 0; k < 3; ++k) {
            const uint32_t root = groups.find(slot_of(tri, k));
            core::at(sum, root) = core::at(sum, root) + (core::at(face_, tri) * core::at(angle_, slot_of(tri, k)));
        }
    }
    corner_normal_.resize(tris_.size() * 3);
    for (uint32_t tri = 0; tri < tris_.size(); ++tri) {
        for (int k = 0; k < 3; ++k) {
            core::at(corner_normal_, slot_of(tri, k)) = normalised(core::at(sum, groups.find(slot_of(tri, k))), core::at(face_, tri));
        }
    }
}

float cosine_of(float degrees) { return std::cos(degrees * std::numbers::pi_v<float> / kDegreesPerHalfTurn); }
float degrees_of(float cosine) { return std::acos(std::clamp(cosine, -1.0F, 1.0F)) * kDegreesPerHalfTurn / std::numbers::pi_v<float>; }

// A vertex's own normal (the file's), when the mesh has them and it is one.
bool Smoother::file_normal(uint16_t vertex, V3* out) const
{
    constexpr float kShortest = 0.5F;
    constexpr float kLongest = 1.5F;
    if (in_.layout.normal_offset < 0) {
        return false;
    }
    const V3 normal = read3(in_.vertices, (static_cast<std::size_t>(in_.layout.stride) * vertex) +
                                              static_cast<std::size_t>(in_.layout.normal_offset));
    const float len = length(normal);
    if (!(len > kShortest && len < kLongest)) {
        return false;
    }
    *out = normal * (1.0F / len);
    return true;
}

// Whether an edge's two faces (wound alike, `cosine` between their normals; `ends` the first's two corners on it, then
// the second's at the same places) are one smoothing group: the crease angle's rule, or - smoothing - the mesh's
// normals' and the prisms' (smooth_tess.h).
bool Smoother::joins(const EdgeUse& use, const std::array<uint16_t, 4>& ends, float cosine, float crease,
                     Result* out) const
{
    const bool by_angle = !(cosine < crease);
    if (!in_.options.smooth) {
        return by_angle;
    }
    if (cosine < cosine_of(in_.options.smooth_degrees)) {
        return false; // (a box's corners: hard whatever the normals say)
    }
    // The normals at each end: the same vertex, or two normals alike (unknown without normals).
    const float agree = cosine_of(in_.options.normal_agree_degrees);
    bool known = in_.layout.normal_offset >= 0;
    bool shared = true;
    for (std::size_t end = 0; end < 2 && known; ++end) {
        const uint16_t first = core::at(ends, end);
        const uint16_t second = core::at(ends, end + 2);
        V3 n1;
        V3 n2;
        if (first == second) {
            continue;
        }
        if (!file_normal(first, &n1) || !file_normal(second, &n2)) {
            known = false;
        } else {
            shared = shared && dot(n1, n2) >= agree;
        }
    }
    if (use.prism) {
        ++out->prism_edges; // (round whatever its normals: a rail's flat-shaded sides too)
        return true;
    }
    if (known && shared) {
        out->normal_smooth += by_angle ? 0U : 1U;
        return true;
    }
    if (known && cosine <= cosine_of(in_.options.split_degrees)) {
        out->normal_hard += by_angle ? 1U : 0U;
        return false;
    }
    return by_angle;
}

// Smoothing: the edges of long thin prisms (rails, pipes, poles of a few sides) - over the crease angle, under the
// prism's most; both faces running along the edge, alike and narrow, the edge at least twice their width; and across
// each face the next edge parallel to it such an edge at about the same angle (a ring of equal sides).
void Smoother::find_prisms()
{
    constexpr float kAlongEdge = 0.03F;      // |a face's normal . the edge| under this: the face runs along it
    constexpr float kParallelEdges = 0.995F; // two edges' directions this close: parallel
    constexpr float kLongest = 2.0F;         // the edge at least this times the faces' width
    constexpr float kAlike = 0.25F;          // the faces' widths within this share of the wider
    constexpr float kAngleAgree = 10.0F;     // degrees between the angles of a ring's edges
    const float crease = cosine_of(in_.options.crease_degrees);
    const float most = cosine_of(in_.options.prism_degrees);
    struct Candidate {
        V3 along;
        float degrees = 0.0F;
        std::array<uint32_t, 2> across{}; // each face's third corner (welded)
    };
    std::unordered_map<uint64_t, Candidate> candidates;
    std::unordered_map<uint32_t, std::vector<uint64_t>> at_vertex;
    for (const auto& [key, use] : edges_) {
        if (use.count != 2) {
            continue;
        }
        const uint32_t t1 = core::at(use.triangle, 0);
        const uint32_t t2 = core::at(use.triangle, 1);
        const float cosine = dot(core::at(face_, t1), core::at(face_, t2));
        if (cosine >= crease || cosine < most) {
            continue;
        }
        const int e1 = core::at(use.edge, 0);
        const uint32_t start = weld(core::at(corners(t1), e1));
        const uint32_t end = weld(core::at(corners(t1), (e1 + 1) % 3));
        const V3 from = core::at(welded_, start);
        const V3 edge = core::at(welded_, end) - from;
        const float span = length(edge);
        const V3 along = normalised(edge, {});
        if (!(span > 0.0F) || std::fabs(dot(core::at(face_, t1), along)) > kAlongEdge ||
            std::fabs(dot(core::at(face_, t2), along)) > kAlongEdge) {
            continue;
        }
        Candidate made{.along = along, .degrees = degrees_of(cosine)};
        std::array<float, 2> width{};
        for (std::size_t side = 0; side < 2; ++side) {
            const uint32_t tri = core::at(use.triangle, side);
            const uint16_t third = core::at(corners(tri), (core::at(use.edge, side) + 2) % 3);
            core::at(made.across, side) = weld(third);
            const V3 offset = position(third) - from;
            core::at(width, side) = length(offset - (along * dot(offset, along)));
        }
        const float wide = std::max(core::at(width, 0), core::at(width, 1));
        if (!(wide > 0.0F) || wide > in_.options.prism_width || span < kLongest * wide ||
            std::fabs(core::at(width, 0) - core::at(width, 1)) > kAlike * wide) {
            continue;
        }
        candidates.emplace(key, made);
        at_vertex[start].push_back(key);
        at_vertex[end].push_back(key);
    }
    for (const auto& [key, made] : candidates) {
        bool ring = true;
        for (const uint32_t across : made.across) {
            bool found = false;
            const auto list = at_vertex.find(across);
            if (list != at_vertex.end()) {
                for (const uint64_t other : list->second) {
                    const Candidate& next = candidates.at(other);
                    found = found || (other != key && std::fabs(dot(next.along, made.along)) >= kParallelEdges &&
                                      std::fabs(next.degrees - made.degrees) <= kAngleAgree);
                }
            }
            ring = ring && found;
        }
        if (ring) {
            edges_.at(key).prism = true;
        }
    }
}

// Smoothing's level: the error's, or more - enough pieces that none of an edge's turns by more than the segment angle
// (up to kSmoothLevel, or rt_tess_level when higher). A curve the cap flattens turns that much less (its pull scaled
// down).
int Smoother::smooth_level(int level) const
{
    float widest = 0.0F;
    for (const auto& [key, use] : edges_) {
        if (use.curved) {
            const EdgeEnds ends = ends_of(core::at(use.triangle, 0), core::at(use.edge, 0));
            widest = std::max(widest, degrees_of(dot(ends.n0, ends.n1)));
        }
    }
    const float bulge = largest_edge_pull() * shape_;
    if (bulge > in_.options.max_deviation && bulge > 0.0F) {
        widest *= in_.options.max_deviation / bulge;
    }
    const int pieces = static_cast<int>(std::ceil(widest / std::max(in_.options.segment_degrees, 1.0F)));
    // (the error's level up to rt_tess_level as without smoothing; the turns' up to kSmoothLevel)
    const int top = std::max(std::min(in_.options.max_level, kHighestLevel), kLowestLevel);
    const int turns_top = std::max(std::min(std::max(in_.options.max_level, kSmoothLevel), kHighestLevel), kLowestLevel);
    return std::max(std::clamp(level, kLowestLevel, top), std::clamp(pieces, kLowestLevel, turns_top));
}

EdgeEnds Smoother::ends_of(uint32_t triangle, int edge) const
{
    const auto& corner = corners(triangle);
    const uint16_t start = core::at(corner, edge);
    const uint16_t end = core::at(corner, (edge + 1) % 3);
    const V3 normal_start = corner_normal(triangle, edge);
    const V3 normal_end = corner_normal(triangle, (edge + 1) % 3);
    return weld(start) < weld(end)
               ? EdgeEnds{.p0 = position(start), .n0 = normal_start, .p1 = position(end), .n1 = normal_end}
               : EdgeEnds{.p0 = position(end), .n0 = normal_end, .p1 = position(start), .n1 = normal_start};
}

void Smoother::mark_curves()
{
    curved_.assign(tris_.size(), {false, false, false});
    split_.assign(tris_.size(), false);
    for (auto& [key, use] : edges_) {
        if (!use.smooth) {
            continue;
        }
        V3 flat;
        const float sag = length(edge_pull(ends_of(core::at(use.triangle, 0), core::at(use.edge, 0)), kHalf, &flat)) * shape_;
        use.curved = sag > in_.options.curved_epsilon && std::isfinite(sag);
        if (!use.curved) {
            continue;
        }
        for (std::size_t side = 0; side < 2; ++side) {
            const uint32_t on = core::at(use.triangle, side);
            core::at(core::at(curved_, on), core::at(use.edge, side)) = true;
            core::at(split_, on) = true;
        }
    }
}

float Smoother::largest_edge_pull() const
{
    float largest = 0.0F;
    for (const auto& [key, use] : edges_) {
        if (use.curved) {
            V3 flat;
            largest = std::max(largest, length(edge_pull(ends_of(core::at(use.triangle, 0), core::at(use.edge, 0)), kHalf, &flat)));
        }
    }
    return largest;
}

uint32_t Smoother::add_vertex(const NewVertex& vertex)
{
    made_.push_back(vertex);
    return in_.count + static_cast<uint32_t>(made_.size() - 1);
}

// The vertex at the step-th of level_ points along a triangle's edge from `from` to `to` (0: from; level_: to): a
// curved edge's shared new point, or - straight - the nearer end (the lower index at the middle).
uint32_t Smoother::edge_point(uint32_t triangle, int edge, uint16_t from, uint16_t to, int step)
{
    if (step == 0) {
        return from;
    }
    if (step == level_) {
        return to;
    }
    if (!core::at(core::at(curved_, triangle), edge)) {
        if (2 * step == level_) {
            return std::min(from, to);
        }
        return 2 * step < level_ ? from : to;
    }
    const uint64_t key = pair_key(from, to);
    const int step_low = from < to ? step : level_ - step; // (counted from the lower vertex index)
    if (const auto found = edge_points_.find(key); found != edge_points_.end()) {
        return found->second + static_cast<uint32_t>(step_low - 1);
    }
    // Made once a vertex pair, counted from the lower index; positions from the lower welded position.
    const uint16_t low = std::min(from, to);
    const uint16_t high = std::max(from, to);
    const EdgeEnds ends = ends_of(triangle, edge);
    const bool low_first = weld(low) < weld(high);
    const V3 normal_low = low_first ? ends.n0 : ends.n1;
    const V3 normal_high = low_first ? ends.n1 : ends.n0;
    const EdgeUse& use = edges_.at(pair_key(weld(from), weld(to)));
    uint32_t first = 0;
    for (int i = 1; i < level_; ++i) {
        const int along = low_first ? i : level_ - i;
        NewVertex made;
        made.pull = edge_pull(ends, static_cast<float>(along) / static_cast<float>(level_), &made.flat);
        const float t_low = static_cast<float>(i) / static_cast<float>(level_);
        made.normal = normalised((normal_low * (1.0F - t_low)) + (normal_high * t_low), normal_low);
        made.displaceable = use.same_indices;
        made.from = {low, high, high};
        made.weight = {1.0F - t_low, t_low, 0.0F};
        const uint32_t index = add_vertex(made);
        if (i == 1) {
            first = index;
        }
    }
    edge_points_.emplace(key, first);
    return first + static_cast<uint32_t>(step_low - 1);
}

bool Smoother::build(int level, Result* out)
{
    level_ = level;
    made_.clear();
    edge_points_.clear();
    grids_.clear();
    grid_of_.assign(tris_.size(), UINT32_MAX);
    const float inverse = 1.0F / static_cast<float>(level);
    for (uint32_t tri = 0; tri < tris_.size(); ++tri) {
        if (!core::at(split_, tri)) {
            continue;
        }
        const auto& corner = corners(tri);
        const std::array<V3, 3> points{position(core::at(corner, 0)), position(core::at(corner, 1)), position(core::at(corner, 2))};
        const std::array<V3, 3> normals{corner_normal(tri, 0), corner_normal(tri, 1), corner_normal(tri, 2)};
        std::vector<std::vector<uint32_t>> grid(static_cast<std::size_t>(level) + 1);
        for (int i = 0; i <= level; ++i) {
            auto& row = core::at(grid, i);
            row.resize(static_cast<std::size_t>(i) + 1);
            for (int j = 0; j <= i; ++j) {
                uint32_t index = 0;
                if (i == 0) {
                    index = core::at(corner, 0);
                } else if (j == 0) {
                    index = edge_point(tri, 0, core::at(corner, 0), core::at(corner, 1), i); // (a to b)
                } else if (j == i) {
                    index = edge_point(tri, 2, core::at(corner, 2), core::at(corner, 0), level - i); // (c to a: i from a)
                } else if (i == level) {
                    index = edge_point(tri, 1, core::at(corner, 1), core::at(corner, 2), j); // (b to c)
                } else {
                    const std::array<float, 3> weights{static_cast<float>(level - i) * inverse,
                                                       static_cast<float>(i - j) * inverse,
                                                       static_cast<float>(j) * inverse};
                    NewVertex made;
                    made.flat = (core::at(points, 0) * core::at(weights, 0)) + (core::at(points, 1) * core::at(weights, 1)) + (core::at(points, 2) * core::at(weights, 2));
                    made.pull = pull_at(made.flat, points, normals, weights) * inner_factor(normals, weights);
                    made.normal = normalised((core::at(normals, 0) * core::at(weights, 0)) + (core::at(normals, 1) * core::at(weights, 1)) +
                                                 (core::at(normals, 2) * core::at(weights, 2)),
                                             core::at(face_, tri));
                    made.displaceable = true;
                    made.from = corner;
                    made.weight = weights;
                    index = add_vertex(made);
                }
                core::at(row, j) = index;
            }
        }
        core::at(grid_of_, tri) = static_cast<uint32_t>(grids_.size());
        grids_.push_back(std::move(grid));
    }
    if (in_.count + made_.size() > std::size_t{kLastIndex} + 1) {
        return false;
    }
    write_strip(out);
    return out->strip.size() <= kMostIndices;
}

void Smoother::write_strip(Result* out)
{
    out->strip.clear();
    // Where in the strip each split triangle starts.
    std::unordered_map<uint32_t, uint32_t> split_at;
    for (uint32_t tri = 0; tri < tris_.size(); ++tri) {
        if (core::at(split_, tri)) {
            split_at.emplace(core::at(tris_, tri).at, tri);
        }
    }
    const std::span<const uint16_t> strip = in_.strip;
    const auto drawn = [&](std::size_t at) {
        const uint16_t first = core::at(strip, at);
        const uint16_t second = core::at(strip, at + 1);
        const uint16_t third = core::at(strip, at + 2);
        return first != second && second != third && first != third;
    };
    std::size_t run_start = 0;
    bool run_drawn = false;
    const std::size_t positions = strip.size() - 2;
    const auto close_run = [&](std::size_t end) { // (positions run_start .. end - 1 kept as they were)
        if (run_drawn && end > run_start) {
            append_run(&out->strip, strip.subspan(run_start, (end - run_start) + 2), static_cast<uint32_t>(run_start));
        }
    };
    std::vector<uint16_t> row_strip;
    for (std::size_t k = 0; k < positions; ++k) {
        const auto found = split_at.find(static_cast<uint32_t>(k));
        if (found == split_at.end()) {
            run_drawn = run_drawn || drawn(k);
            continue;
        }
        close_run(k);
        run_start = k + 1;
        run_drawn = false;
        // The split triangle row by row: row i to row i + 1, starting odd (so the first is wound as a, b, c).
        const auto& grid = core::at(grids_, core::at(grid_of_, found->second));
        for (std::size_t i = 0; i + 1 < grid.size(); ++i) {
            row_strip.clear();
            const auto& upper = core::at(grid, i);
            const auto& lower = core::at(grid, i + 1);
            for (std::size_t j = 0; j <= i; ++j) {
                row_strip.push_back(static_cast<uint16_t>(core::at(lower, j)));
                row_strip.push_back(static_cast<uint16_t>(core::at(upper, j)));
            }
            row_strip.push_back(static_cast<uint16_t>(core::at(lower, i + 1)));
            append_run(&out->strip, row_strip, 1);
        }
    }
    close_run(positions);
}

void Smoother::write_vertices(Result* out, float shape)
{
    const auto stride = static_cast<std::size_t>(in_.layout.stride);
    const VertexLayout& layout = in_.layout;
    const Options& options = in_.options;
    out->vertex_count = in_.count + static_cast<uint32_t>(made_.size());
    const std::span<const uint8_t> source = in_.vertices.first(stride * in_.count);
    out->vertices.assign(source.begin(), source.end());
    out->vertices.resize(stride * out->vertex_count);
    const std::span<uint8_t> bytes(out->vertices);
    // The thin meshes (rails) aren't displaced: their bounds' smallest side.
    V3 low = position(0);
    V3 high = position(0);
    for (const V3& point : position_) {
        low = {.x = std::min(low.x, point.x), .y = std::min(low.y, point.y), .z = std::min(low.z, point.z)};
        high = {.x = std::max(high.x, point.x), .y = std::max(high.y, point.y), .z = std::max(high.z, point.z)};
    }
    const bool displace = options.height.lookup != nullptr && options.height.scale > 0.0F &&
                          std::min({high.x - low.x, high.y - low.y, high.z - low.z}) >= options.thin_size &&
                          layout.uv_offset >= 0 && layout.uv_sets > 0;
    for (std::size_t made = 0; made < made_.size(); ++made) {
        const NewVertex& vertex = core::at(made_, made);
        const std::size_t at = stride * (in_.count + made);
        const auto source_at = [&](std::size_t source, int offset) {
            return (stride * core::at(vertex.from, source)) + static_cast<std::size_t>(offset);
        };
        // Everything blended from its sources (the normal renormalised; the position written after).
        if (layout.normal_offset >= 0) {
            V3 normal{};
            for (std::size_t src = 0; src < 3; ++src) {
                normal = normal + (read3(in_.vertices, source_at(src, layout.normal_offset)) * core::at(vertex.weight, src));
            }
            write3(bytes, at + static_cast<std::size_t>(layout.normal_offset), normalised(normal, vertex.normal));
        }
        if (layout.colour_offset >= 0) {
            for (std::size_t channel = 0; channel < kColourBytes; ++channel) {
                float sum = 0.0F;
                for (std::size_t src = 0; src < 3; ++src) {
                    sum += static_cast<float>(core::at(in_.vertices, source_at(src, layout.colour_offset) + channel)) *
                           core::at(vertex.weight, src);
                }
                core::at(bytes, at + static_cast<std::size_t>(layout.colour_offset) + channel) =
                    static_cast<uint8_t>(std::clamp(std::lround(sum), 0L, static_cast<long>(kByteMax)));
            }
        }
        std::array<float, 2> uv0{};
        for (uint32_t set = 0; set < layout.uv_sets && layout.uv_offset >= 0; ++set) {
            const auto offset = static_cast<int>(static_cast<std::size_t>(layout.uv_offset) + (kUvBytes * set));
            std::array<float, 2> uv{};
            for (std::size_t src = 0; src < 3; ++src) {
                const std::array<float, 2> from = read2(in_.vertices, source_at(src, offset));
                core::at(uv, 0) += core::at(from, 0) * core::at(vertex.weight, src);
                core::at(uv, 1) += core::at(from, 1) * core::at(vertex.weight, src);
            }
            write2(bytes, at + static_cast<std::size_t>(offset), uv);
            if (set == 0) {
                uv0 = uv;
            }
        }
        V3 point = vertex.flat + (vertex.pull * shape);
        if (displace && vertex.displaceable) {
            const float height = options.height.lookup(options.height.context, core::at(uv0, 0), core::at(uv0, 1));
            if (height >= 0.0F && std::isfinite(height)) {
                const float budget = std::max(0.0F, options.max_deviation - (length(vertex.pull) * shape));
                const float moved = std::clamp(options.height.scale * (height - kHalf), -budget, budget);
                point = point + (vertex.normal * moved);
                out->displaced = std::max(out->displaced, std::fabs(moved));
            }
        }
        write3(bytes, at, point);
        if (options.keep_flat) {
            out->flat.push_back({vertex.flat.x, vertex.flat.y, vertex.flat.z});
        }
        out->deviation = std::max(out->deviation, length(point - vertex.flat));
    }
}

Result Smoother::run()
{
    Result out;
    if (!readable()) {
        return out;
    }
    shape_ = in_.options.smooth ? in_.options.shape * std::clamp(in_.options.smooth_strength, 0.0F, 1.0F)
                                : in_.options.shape;
    weld_and_face();
    find_edges();
    make_groups(&out);
    mark_curves();
    out.triangles = static_cast<uint32_t>(std::ranges::count(degenerate_, false));
    out.smoothed = static_cast<uint32_t>(std::ranges::count(split_, true));
    out.curved_edges =
        static_cast<uint32_t>(std::ranges::count_if(edges_, [](const auto& entry) { return entry.second.curved; }));
    if (out.smoothed == 0) {
        return out;
    }
    // The level from the largest pull at the curved edges' middles (the shape factor capped): the curve followed to
    // about the target error.
    const float estimate = std::min(largest_edge_pull() * shape_, in_.options.max_deviation);
    const int top = std::max(std::min(in_.options.max_level, kHighestLevel), kLowestLevel);
    int level = static_cast<int>(std::ceil(std::sqrt(estimate / std::max(in_.options.target_error, kSmallestTarget))));
    level = in_.options.smooth ? smooth_level(level) : std::clamp(level, kLowestLevel, top);
    for (; level >= kLowestLevel; --level) {
        if (build(level, &out)) {
            break;
        }
    }
    if (level < kLowestLevel) {
        return Result{}; // (too many vertices or indices even split in two)
    }
    out.level = level;
    float pull = 0.0F;
    for (const NewVertex& vertex : made_) {
        pull = std::max(pull, length(vertex.pull));
    }
    out.uncapped = pull * shape_;
    float shape = shape_;
    if (out.uncapped > in_.options.max_deviation && pull > 0.0F) {
        shape = in_.options.max_deviation / pull;
        out.capped = true;
    }
    if (!std::isfinite(shape)) {
        return Result{};
    }
    out.shape = shape;
    write_vertices(&out, shape);
    out.changed = true;
    return out;
}

} // namespace

std::vector<std::array<uint16_t, 3>> strip_triangles(std::span<const uint16_t> strip)
{
    std::vector<std::array<uint16_t, 3>> out;
    for (const Triangle& triangle : decode(strip)) {
        out.push_back(triangle.corner);
    }
    return out;
}

Result smooth(std::span<const uint8_t> vertices, uint32_t vertex_count, const VertexLayout& layout,
              std::span<const uint16_t> strip, const Options& options)
{
    Smoother smoother({.vertices = vertices, .count = vertex_count, .layout = layout, .strip = strip, .options = options});
    return smoother.run();
}

} // namespace render::tess
