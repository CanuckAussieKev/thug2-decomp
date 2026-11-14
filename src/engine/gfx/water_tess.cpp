// NOT ORIGINAL - the water's moving surface: a flat water mesh split into a grid at load (water_tess.h).
#include "engine/gfx/water_tess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <unordered_map>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/smooth_tess.h"

namespace render::water_tess {

namespace {

constexpr std::size_t kPositionBytes = 12;
constexpr std::size_t kColourBytes = 4;
constexpr std::size_t kUvBytes = 8;
constexpr uint32_t kMostVertices = 0xFFFF;   // (indices 0 - 0xFFFE: a 16-bit index)
constexpr std::size_t kMostIndices = 0xFFFF; // a level of detail's index count is 16 bits
constexpr float kByteMax = 255.0F;
constexpr float kWiden = 1.5F;               // the spacing's step when the grid doesn't fit
constexpr int kMostTries = 16;
constexpr unsigned kHighBits = 32U;

using V3 = std::array<float, 3>;

V3 read3(std::span<const uint8_t> bytes, std::size_t offset)
{
    V3 floats{};
    std::memcpy(floats.data(), bytes.subspan(offset, kPositionBytes).data(), kPositionBytes);
    return floats;
}
void write3(std::span<uint8_t> bytes, std::size_t offset, const V3& value)
{
    std::memcpy(bytes.subspan(offset, kPositionBytes).data(), value.data(), kPositionBytes);
}

uint32_t float_bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    constexpr uint32_t kNegativeZero = 0x80000000U;
    return bits == kNegativeZero ? 0U : bits; // (+0 and -0 the same point)
}

// Whether position `a` comes before `b` in the one order every mesh uses (their bits, x then y then z).
bool position_before(const V3& a, const V3& b)
{
    for (std::size_t k = 0; k < 3; ++k) {
        const uint32_t left = float_bits(core::at(a, k));
        const uint32_t right = float_bits(core::at(b, k));
        if (left != right) {
            return left < right;
        }
    }
    return false;
}

float distance(const V3& a, const V3& b)
{
    const float dx = core::at(b, 0) - core::at(a, 0);
    const float dy = core::at(b, 1) - core::at(a, 1);
    const float dz = core::at(b, 2) - core::at(a, 2);
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

uint64_t pair_key(uint32_t a, uint32_t b)
{
    return a < b ? (uint64_t{a} << kHighBits) | b : (uint64_t{b} << kHighBits) | a;
}

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

// A new vertex: its position and the (up to three) original vertices its other parts are blended from.
struct NewVertex {
    V3 position{};
    std::array<uint16_t, 3> from{};
    std::array<float, 3> weight{};
};

class Grid {
public:
    Grid(std::span<const uint8_t> vertices, uint32_t count, const tess::VertexLayout& layout,
         const std::vector<std::array<uint16_t, 3>>& triangles, const Options& options)
        : vertices_(vertices), count_(count), layout_(layout), triangles_(triangles), options_(options)
    {
    }

    // The grid at `spacing`; false when it doesn't fit 16-bit indices.
    bool build(float spacing, Result* out);
    [[nodiscard]] bool split_any() const { return split_any_; }
    void write_vertices(Result* out) const;

private:
    [[nodiscard]] V3 position(uint32_t index) const { return read3(vertices_, std::size_t{layout_.stride} * index); }
    [[nodiscard]] int pieces(uint16_t a, uint16_t b) const
    {
        return pieces_of(distance(position(a), position(b)), options_, spacing_);
    }
    uint32_t edge_point(uint16_t from, uint16_t to, int step, int pieces);
    uint32_t add(const NewVertex& vertex)
    {
        made_.push_back(vertex);
        return count_ + static_cast<uint32_t>(made_.size() - 1);
    }

    std::span<const uint8_t> vertices_;
    uint32_t count_;
    const tess::VertexLayout& layout_;
    const std::vector<std::array<uint16_t, 3>>& triangles_;
    const Options& options_;
    float spacing_ = 0.0F;
    bool split_any_ = false; // some triangle split at this spacing
    std::vector<NewVertex> made_;
    std::unordered_map<uint64_t, uint32_t> edge_points_; // a vertex pair's first new point (its others follow)
};

// The point `step` of `pieces` along the edge from `from` to `to` (0: from; pieces: to). Made once a vertex pair, ordered
// from the end whose position comes first - so its positions are the same whoever asks, in this mesh or another.
uint32_t Grid::edge_point(uint16_t from, uint16_t to, int step, int pieces)
{
    if (step <= 0) {
        return from;
    }
    if (step >= pieces) {
        return to;
    }
    const bool from_low = !position_before(position(to), position(from));
    const uint16_t low = from_low ? from : to;
    const uint16_t high = from_low ? to : from;
    const int along = from_low ? step : pieces - step; // (counted from the low end)
    const uint64_t key = pair_key(from, to);
    if (const auto found = edge_points_.find(key); found != edge_points_.end()) {
        return found->second + static_cast<uint32_t>(along - 1);
    }
    const V3 start = position(low);
    const V3 end = position(high);
    const V3 span{core::at(end, 0) - core::at(start, 0), core::at(end, 1) - core::at(start, 1), core::at(end, 2) - core::at(start, 2)};
    uint32_t first = 0;
    for (int i = 1; i < pieces; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(pieces);
        NewVertex made;
        made.position = {core::at(start, 0) + (core::at(span, 0) * t), core::at(start, 1) + (core::at(span, 1) * t),
                         core::at(start, 2) + (core::at(span, 2) * t)};
        made.from = {low, high, high};
        made.weight = {1.0F - t, t, 0.0F};
        const uint32_t index = add(made);
        if (i == 1) {
            first = index;
        }
    }
    edge_points_.emplace(key, first);
    return first + static_cast<uint32_t>(along - 1);
}

bool Grid::build(float spacing, Result* out)
{
    spacing_ = spacing;
    split_any_ = false;
    made_.clear();
    edge_points_.clear();
    out->strip.clear();
    out->made = 0;
    std::vector<std::vector<uint32_t>> grid;
    std::vector<uint16_t> row_strip;
    for (const std::array<uint16_t, 3>& corner : triangles_) {
        const uint16_t a = core::at(corner, 0);
        const uint16_t b = core::at(corner, 1);
        const uint16_t c = core::at(corner, 2);
        const int ab = pieces(a, b);
        const int bc = pieces(b, c);
        const int ca = pieces(c, a);
        const int n = std::max({ab, bc, ca});
        split_any_ = split_any_ || n > 1;
        // (a border point at step i of n moved onto the edge's own nearest point: round(i x its pieces / n))
        const auto snap = [n](int step, int own) { return ((2 * step * own) + n) / (2 * n); };
        const V3 pa = position(a);
        const V3 pb = position(b);
        const V3 pc = position(c);
        grid.assign(static_cast<std::size_t>(n) + 1, {});
        for (int i = 0; i <= n; ++i) {
            auto& row = core::at(grid, static_cast<std::size_t>(i));
            row.resize(static_cast<std::size_t>(i) + 1);
            for (int j = 0; j <= i; ++j) {
                uint32_t index = 0;
                if (i == 0) {
                    index = a;
                } else if (j == 0) {
                    index = edge_point(a, b, snap(i, ab), ab); // (a to b)
                } else if (j == i) {
                    index = edge_point(a, c, snap(i, ca), ca); // (a to c)
                } else if (i == n) {
                    index = edge_point(b, c, snap(j, bc), bc); // (b to c)
                } else {
                    const float inverse = 1.0F / static_cast<float>(n);
                    const std::array<float, 3> weights{static_cast<float>(n - i) * inverse, static_cast<float>(i - j) * inverse,
                                                       static_cast<float>(j) * inverse};
                    NewVertex made;
                    for (std::size_t k = 0; k < 3; ++k) {
                        core::at(made.position, k) = (core::at(pa, k) * core::at(weights, 0)) + (core::at(pb, k) * core::at(weights, 1)) +
                                                     (core::at(pc, k) * core::at(weights, 2));
                    }
                    made.from = corner;
                    made.weight = weights;
                    index = add(made);
                }
                core::at(row, static_cast<std::size_t>(j)) = index;
            }
        }
        if (std::size_t{count_} + made_.size() > kMostVertices) {
            return false;
        }
        // row i to row i + 1, starting odd (so the first is wound as a, b, c)
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
            if (out->strip.size() > kMostIndices) {
                return false;
            }
        }
    }
    return true;
}

void Grid::write_vertices(Result* out) const
{
    const auto stride = static_cast<std::size_t>(layout_.stride);
    out->vertex_count = count_ + static_cast<uint32_t>(made_.size());
    const std::span<const uint8_t> source = vertices_.first(stride * count_);
    out->vertices.assign(source.begin(), source.end());
    out->vertices.resize(stride * out->vertex_count);
    const std::span<uint8_t> bytes(out->vertices);
    for (std::size_t made = 0; made < made_.size(); ++made) {
        const NewVertex& vertex = core::at(made_, made);
        const std::size_t at = stride * (count_ + made);
        const auto source_at = [&](std::size_t corner, int offset) {
            return (stride * core::at(vertex.from, corner)) + static_cast<std::size_t>(offset);
        };
        if (layout_.normal_offset >= 0) {
            V3 normal{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const V3 from = read3(vertices_, source_at(corner, layout_.normal_offset));
                for (std::size_t k = 0; k < 3; ++k) {
                    core::at(normal, k) += core::at(from, k) * core::at(vertex.weight, corner);
                }
            }
            const float length = std::sqrt((core::at(normal, 0) * core::at(normal, 0)) + (core::at(normal, 1) * core::at(normal, 1)) +
                                           (core::at(normal, 2) * core::at(normal, 2)));
            if (length > 0.0F && std::isfinite(length)) {
                for (float& part : normal) {
                    part /= length;
                }
            } else {
                normal = read3(vertices_, source_at(0, layout_.normal_offset));
            }
            write3(bytes, at + static_cast<std::size_t>(layout_.normal_offset), normal);
        }
        if (layout_.colour_offset >= 0) {
            for (std::size_t channel = 0; channel < kColourBytes; ++channel) {
                float sum = 0.0F;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    sum += static_cast<float>(core::at(vertices_, source_at(corner, layout_.colour_offset) + channel)) *
                           core::at(vertex.weight, corner);
                }
                core::at(bytes, at + static_cast<std::size_t>(layout_.colour_offset) + channel) =
                    static_cast<uint8_t>(std::clamp(std::lround(sum), 0L, static_cast<long>(kByteMax)));
            }
        }
        for (uint32_t set = 0; set < layout_.uv_sets && layout_.uv_offset >= 0; ++set) {
            const auto offset = static_cast<int>(static_cast<std::size_t>(layout_.uv_offset) + (kUvBytes * set));
            std::array<float, 2> uv{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                std::array<float, 2> from{};
                std::memcpy(from.data(), std::span<const uint8_t>(vertices_).subspan(source_at(corner, offset), kUvBytes).data(), kUvBytes);
                core::at(uv, 0) += core::at(from, 0) * core::at(vertex.weight, corner);
                core::at(uv, 1) += core::at(from, 1) * core::at(vertex.weight, corner);
            }
            std::memcpy(bytes.subspan(at + static_cast<std::size_t>(offset), kUvBytes).data(), uv.data(), kUvBytes);
        }
        write3(bytes, at, vertex.position);
    }
}

} // namespace

int pieces_of(float length, const Options& options, float spacing)
{
    if (!(spacing > 0.0F) || !std::isfinite(length) || length <= spacing) {
        return 1;
    }
    const float pieces = std::ceil(length / spacing);
    return pieces >= static_cast<float>(options.most_pieces) ? std::max(options.most_pieces, 1) : static_cast<int>(pieces);
}

Result subdivide(std::span<const uint8_t> vertices, uint32_t vertex_count, const tess::VertexLayout& layout,
                 std::span<const uint16_t> strip, const Options& options)
{
    Result out;
    const std::size_t stride = layout.stride;
    if (stride < kPositionBytes || vertex_count < 3 || strip.size() < 3 || vertices.size() < stride * vertex_count ||
        !(options.spacing > 0.0F) || options.most_pieces < 2) {
        out.why = "unreadable";
        return out;
    }
    if (!std::ranges::all_of(strip, [vertex_count](uint16_t index) { return index < vertex_count; })) {
        out.why = "an index past the vertices";
        return out;
    }
    const std::vector<std::array<uint16_t, 3>> triangles = tess::strip_triangles(strip);
    out.triangles = static_cast<uint32_t>(triangles.size());
    // every triangle near horizontal (a sliver with no area says nothing), and one with an edge to split
    bool long_edge = false;
    for (const std::array<uint16_t, 3>& corner : triangles) {
        std::array<V3, 3> p{};
        for (std::size_t k = 0; k < 3; ++k) {
            core::at(p, k) = read3(vertices, stride * core::at(corner, k));
        }
        const V3 e1{core::at(core::at(p, 1), 0) - core::at(core::at(p, 0), 0), core::at(core::at(p, 1), 1) - core::at(core::at(p, 0), 1),
                    core::at(core::at(p, 1), 2) - core::at(core::at(p, 0), 2)};
        const V3 e2{core::at(core::at(p, 2), 0) - core::at(core::at(p, 0), 0), core::at(core::at(p, 2), 1) - core::at(core::at(p, 0), 1),
                    core::at(core::at(p, 2), 2) - core::at(core::at(p, 0), 2)};
        const V3 n{(core::at(e1, 1) * core::at(e2, 2)) - (core::at(e1, 2) * core::at(e2, 1)),
                   (core::at(e1, 2) * core::at(e2, 0)) - (core::at(e1, 0) * core::at(e2, 2)),
                   (core::at(e1, 0) * core::at(e2, 1)) - (core::at(e1, 1) * core::at(e2, 0))};
        const float length = std::sqrt((core::at(n, 0) * core::at(n, 0)) + (core::at(n, 1) * core::at(n, 1)) + (core::at(n, 2) * core::at(n, 2)));
        if (!std::isfinite(length)) {
            out.why = "not finite";
            return out;
        }
        constexpr float kNoArea = 1.0e-4F;
        if (length > kNoArea && std::fabs(core::at(n, 1)) < options.least_up * length) {
            out.why = "not flat";
            return out;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            long_edge = long_edge || distance(core::at(p, k), core::at(p, (k + 1) % 3)) > options.spacing;
        }
    }
    if (triangles.empty() || !long_edge) {
        out.why = "small";
        return out;
    }
    Grid grid(vertices, vertex_count, layout, triangles, options);
    float spacing = options.spacing;
    for (int attempt = 0; attempt < kMostTries; ++attempt, spacing *= kWiden) {
        if (grid.build(spacing, &out)) {
            if (!grid.split_any()) {
                break; // (nothing split at this spacing)
            }
            grid.write_vertices(&out);
            out.spacing = spacing;
            out.made = static_cast<uint32_t>(tess::strip_triangles(out.strip).size());
            out.changed = true;
            return out;
        }
    }
    out = Result{.triangles = out.triangles};
    out.why = "too big to split";
    return out;
}

} // namespace render::water_tess
