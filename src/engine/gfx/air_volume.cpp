// NOT ORIGINAL - the air's way in from the open sky (air_volume.h).
#include "engine/gfx/air_volume.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <ratio>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"

namespace render::air {

namespace {

constexpr int kB = kBrickCells;
constexpr int kBrickVolume = kB * kB * kB;
constexpr float kBrickUnits = kCell * static_cast<float>(kB);
constexpr uint8_t kOpen = 255;
constexpr uint8_t kWallMark = 255;   // (a kept cell's wind: a wall, left out of the sampling)
constexpr float kWindScale = 254.0F; // (a kept wind 0 - 1 as 0 - 254)
constexpr float kByte = 255.0F;
constexpr std::size_t kMostBricks = std::size_t{1} << 24; // (a level spread wider than this: no volume - a stray face)
constexpr int kSpanMost = (2 * kOpenCells) + 1;           // a passage's run counted to this many cells
constexpr float kLeastWind = 1.0F / 512.0F;               // a draught weaker than this is still air
constexpr float kBestScale = 65535.0F;
constexpr uint32_t kFree = 1;
constexpr uint32_t kSettled = 2;

std::atomic<uint64_t>& serials()
{
    static std::atomic<uint64_t> next{0};
    return next;
}

struct Published {
    std::mutex mutex;
    std::shared_ptr<const Volume> volume;
};
Published& published()
{
    static Published instance;
    return instance;
}

// The cells' build-time state.
struct Builder {
    Vec3 origin{};
    std::array<int, 3> bricks{};
    std::array<int, 3> cells{};
    std::vector<int32_t> slots;   // per brick (-1: far)
    std::vector<uint8_t> open;    // per slot x kBrickVolume
    std::vector<uint8_t> width;   // ... the passage width (cells, at most kSpanMost)
    std::vector<uint8_t> sky;     // ... the sky's reach (0 - 255)
    std::vector<uint8_t> far_sky; // per brick
    int32_t slot_count = 0;

    [[nodiscard]] std::size_t brick_count() const
    {
        return static_cast<std::size_t>(bricks[0]) * static_cast<std::size_t>(bricks[1]) *
               static_cast<std::size_t>(bricks[2]);
    }
    [[nodiscard]] std::size_t brick_index(int bx, int by, int bz) const
    {
        return (((static_cast<std::size_t>(bz) * static_cast<std::size_t>(bricks[1])) + static_cast<std::size_t>(by)) *
                static_cast<std::size_t>(bricks[0])) +
               static_cast<std::size_t>(bx);
    }
    [[nodiscard]] static std::size_t local_index(int lx, int ly, int lz)
    {
        return (((static_cast<std::size_t>(lz) * kB) + static_cast<std::size_t>(ly)) * kB) +
               static_cast<std::size_t>(lx);
    }
    [[nodiscard]] bool inside(int x, int y, int z) const
    {
        return x >= 0 && y >= 0 && z >= 0 && x < cells[0] && y < cells[1] && z < cells[2];
    }
    // The slot holding cell (x, y, z) (-1: far or outside) and its index there.
    [[nodiscard]] int32_t slot_of(int x, int y, int z, std::size_t* local) const
    {
        if (!inside(x, y, z)) {
            return -1;
        }
        *local = local_index(x % kB, y % kB, z % kB);
        return core::at(slots, brick_index(x / kB, y / kB, z / kB));
    }
    [[nodiscard]] uint8_t open_at(int x, int y, int z) const
    {
        std::size_t local = 0;
        const int32_t slot = slot_of(x, y, z, &local);
        return slot < 0 ? kOpen : core::at(open, (static_cast<std::size_t>(slot) * kBrickVolume) + local);
    }
    void mark(int x, int y, int z, uint8_t value)
    {
        const std::size_t brick = brick_index(x / kB, y / kB, z / kB);
        int32_t& slot = core::at(slots, brick);
        if (slot < 0) {
            slot = slot_count++;
            open.resize(static_cast<std::size_t>(slot_count) * kBrickVolume, kOpen);
        }
        uint8_t& cell =
            core::at(open, (static_cast<std::size_t>(slot) * kBrickVolume) + local_index(x % kB, y % kB, z % kB));
        cell = std::min(cell, value);
    }
};

// Whether a triangle (2D) and the square [x0, x0 + 1] x [y0, y0 + 1] overlap (separating axes: the square's two and the
// triangle's three edge normals).
bool overlaps_square(const std::array<std::array<float, 2>, 3>& t, float x0, float y0)
{
    for (int axis = 0; axis < 2; ++axis) {
        const float low = axis == 0 ? x0 : y0;
        float least = core::at(core::at(t, 0), axis);
        float most = least;
        for (std::size_t i = 1; i < 3; ++i) {
            least = std::min(least, core::at(core::at(t, i), axis));
            most = std::max(most, core::at(core::at(t, i), axis));
        }
        if (most < low || least > low + 1.0F) {
            return false;
        }
    }
    for (std::size_t e = 0; e < 3; ++e) {
        const std::array<float, 2>& a = core::at(t, e);
        const std::array<float, 2>& b = core::at(t, (e + 1) % 3);
        const float nx = core::at(b, 1) - core::at(a, 1);
        const float ny = core::at(a, 0) - core::at(b, 0);
        float tl = (nx * core::at(core::at(t, 0), 0)) + (ny * core::at(core::at(t, 0), 1));
        float th = tl;
        for (std::size_t i = 1; i < 3; ++i) {
            const float v = (nx * core::at(core::at(t, i), 0)) + (ny * core::at(core::at(t, i), 1));
            tl = std::min(tl, v);
            th = std::max(th, v);
        }
        float sl = (nx * x0) + (ny * y0);
        float sh = sl;
        for (const std::array<float, 2>& c :
             {std::array{x0 + 1.0F, y0}, std::array{x0, y0 + 1.0F}, std::array{x0 + 1.0F, y0 + 1.0F}}) {
            const float v = (nx * core::at(c, 0)) + (ny * core::at(c, 1));
            sl = std::min(sl, v);
            sh = std::max(sh, v);
        }
        if (th < sl || tl > sh) {
            return false;
        }
    }
    return true;
}

// A face into the cells it touches (in cell units from the origin): along its plane's steepest axis, each column of the
// other two that the face overlaps, the cells its plane crosses there (within the face's own extent).
void rasterise(Builder& b, const Face& face, uint8_t value)
{
    std::array<Vec3, 3> p{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t a = 0; a < 3; ++a) {
            core::at(core::at(p, i), a) = (core::at(core::at(face.corners, i), a) - core::at(b.origin, a)) / kCell;
        }
    }
    const Vec3 e1{p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
    const Vec3 e2{p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
    const Vec3 n{(e1[1] * e2[2]) - (e1[2] * e2[1]), (e1[2] * e2[0]) - (e1[0] * e2[2]),
                 (e1[0] * e2[1]) - (e1[1] * e2[0])};
    const std::array<float, 3> size{std::abs(n[0]), std::abs(n[1]), std::abs(n[2])};
    std::size_t d = 0;
    if (size[1] > core::at(size, d)) {
        d = 1;
    }
    if (size[2] > core::at(size, d)) {
        d = 2;
    }
    if (!(core::at(size, d) > 1e-12F)) {
        return; // (degenerate)
    }
    const std::size_t a = (d + 1) % 3;
    const std::size_t c = (d + 2) % 3;
    const float k = (n[0] * p[0][0]) + (n[1] * p[0][1]) + (n[2] * p[0][2]);
    std::array<std::array<float, 2>, 3> flat{};
    float a_low = 1e30F;
    float a_high = -1e30F;
    float c_low = 1e30F;
    float c_high = -1e30F;
    float d_low = 1e30F;
    float d_high = -1e30F;
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec3& q = core::at(p, i);
        core::at(flat, i) = {core::at(q, a), core::at(q, c)};
        a_low = std::min(a_low, core::at(q, a));
        a_high = std::max(a_high, core::at(q, a));
        c_low = std::min(c_low, core::at(q, c));
        c_high = std::max(c_high, core::at(q, c));
        d_low = std::min(d_low, core::at(q, d));
        d_high = std::max(d_high, core::at(q, d));
    }
    const int a_cells = core::at(b.cells, a);
    const int c_cells = core::at(b.cells, c);
    const int d_cells = core::at(b.cells, d);
    const int a0 = std::max(0, static_cast<int>(std::floor(a_low)));
    const int a1 = std::min(a_cells - 1, static_cast<int>(std::floor(a_high)));
    const int c0 = std::max(0, static_cast<int>(std::floor(c_low)));
    const int c1 = std::min(c_cells - 1, static_cast<int>(std::floor(c_high)));
    const float nd = core::at(n, d);
    const float na = core::at(n, a);
    const float nc = core::at(n, c);
    for (int ic = c0; ic <= c1; ++ic) {
        for (int ia = a0; ia <= a1; ++ia) {
            const auto fa = static_cast<float>(ia);
            const auto fc = static_cast<float>(ic);
            if (!overlaps_square(flat, fa, fc)) {
                continue;
            }
            float low = 1e30F;
            float high = -1e30F;
            for (const std::array<float, 2>& corner : {std::array{fa, fc}, std::array{fa + 1.0F, fc},
                                                       std::array{fa, fc + 1.0F}, std::array{fa + 1.0F, fc + 1.0F}}) {
                const float at = (k - (na * corner[0]) - (nc * corner[1])) / nd;
                low = std::min(low, at);
                high = std::max(high, at);
            }
            low = std::max(low, d_low);
            high = std::min(high, d_high);
            const int d0 = std::max(0, static_cast<int>(std::floor(low)));
            const int d1 = std::min(d_cells - 1, static_cast<int>(std::floor(high)));
            for (int id = d0; id <= d1; ++id) {
                std::array<int, 3> cell{};
                core::at(cell, d) = id;
                core::at(cell, a) = ia;
                core::at(cell, c) = ic;
                b.mark(cell[0], cell[1], cell[2], value);
            }
        }
    }
}

// Each mixed cell's passage width: the narrowest of its runs of cells air passes along x, y and z (each side counted to
// kOpenCells - exactly the neighbouring brick's cells along the line, kOpenCells being a brick). The bricks shared out
// among worker threads (each writes its own slots' cells).
static_assert(kOpenCells == kB, "a run's side is one neighbouring brick");
void brick_widths(Builder& b, int bx, int by, int bz)
{
    const int32_t slot = core::at(b.slots, b.brick_index(bx, by, bz));
    if (slot < 0) {
        return;
    }
    const uint8_t wall = static_cast<uint8_t>(kWallShare * kByte);
    const std::size_t base = static_cast<std::size_t>(slot) * kBrickVolume;
    const std::array<int, 3> place{bx, by, bz};
    std::array<uint8_t, kBrickVolume> narrowest{};
    narrowest.fill(static_cast<uint8_t>(kSpanMost));
    std::array<uint8_t, static_cast<std::size_t>(3 * kB)> line{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const std::size_t u = (axis + 1) % 3;
        const std::size_t v = (axis + 2) % 3;
        // the bricks before and after along the axis (-1: far or outside - open)
        std::array<int32_t, 2> sides{-1, -1};
        for (int side = 0; side < 2; ++side) {
            std::array<int, 3> nb = place;
            core::at(nb, axis) += side == 0 ? -1 : 1;
            if (core::at(nb, axis) >= 0 && core::at(nb, axis) < core::at(b.bricks, axis)) {
                core::at(sides, side) = core::at(b.slots, b.brick_index(nb[0], nb[1], nb[2]));
            }
        }
        for (int iv = 0; iv < kB; ++iv) {
            for (int iu = 0; iu < kB; ++iu) {
                for (int i = 0; i < 3 * kB; ++i) {
                    const int part = i / kB; // 0 before, 1 this brick, 2 after
                    const int32_t from = part == 1 ? slot : core::at(sides, part == 0 ? 0 : 1);
                    if (from < 0) {
                        core::at(line, i) = 1;
                        continue;
                    }
                    std::array<int, 3> l{};
                    core::at(l, axis) = i % kB;
                    core::at(l, u) = iu;
                    core::at(l, v) = iv;
                    core::at(line, i) = core::at(b.open, (static_cast<std::size_t>(from) * kBrickVolume) +
                                                             Builder::local_index(l[0], l[1], l[2])) >= wall
                                            ? 1
                                            : 0;
                }
                for (int i = 0; i < kB; ++i) {
                    const int at = i + kB;
                    std::array<int, 3> l{};
                    core::at(l, axis) = i;
                    core::at(l, u) = iu;
                    core::at(l, v) = iv;
                    uint8_t& out = core::at(narrowest, Builder::local_index(l[0], l[1], l[2]));
                    if (core::at(line, at) == 0) {
                        out = 0;
                        continue;
                    }
                    if (out == 0) {
                        continue;
                    }
                    int run = 1;
                    for (int s = 1; s <= kOpenCells && core::at(line, at - s) != 0; ++s) {
                        ++run;
                    }
                    for (int s = 1; s <= kOpenCells && core::at(line, at + s) != 0; ++s) {
                        ++run;
                    }
                    out = std::min(out, static_cast<uint8_t>(run));
                }
            }
        }
    }
    std::copy(narrowest.begin(), narrowest.end(), b.width.begin() + static_cast<std::ptrdiff_t>(base));
}

void passage_widths(Builder& b)
{
    b.width.assign(b.open.size(), 0);
    const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2U, 1U, 8U);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&b, t, threads]() {
            for (int bz = static_cast<int>(t); bz < b.bricks[2]; bz += static_cast<int>(threads)) {
                for (int by = 0; by < b.bricks[1]; ++by) {
                    for (int bx = 0; bx < b.bricks[0]; ++bx) {
                        brick_widths(b, bx, by, bz);
                    }
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
}

// The sky's reach: each cell's column above it, the open shares multiplied together (a far brick: its columns' mean).
void sky_reach(Builder& b)
{
    b.sky.assign(b.open.size(), kOpen);
    b.far_sky.assign(b.brick_count(), kOpen);
    std::array<float, static_cast<std::size_t>(kB * kB)> through{};
    for (int bz = 0; bz < b.bricks[2]; ++bz) {
        for (int bx = 0; bx < b.bricks[0]; ++bx) {
            through.fill(1.0F);
            for (int by = b.bricks[1] - 1; by >= 0; --by) {
                const std::size_t brick = b.brick_index(bx, by, bz);
                const int32_t slot = core::at(b.slots, brick);
                if (slot < 0) {
                    float sum = 0.0F;
                    for (const float t : through) {
                        sum += t;
                    }
                    core::at(b.far_sky, brick) =
                        static_cast<uint8_t>(std::lround(kByte * sum / static_cast<float>(through.size())));
                    continue;
                }
                const std::size_t base = static_cast<std::size_t>(slot) * kBrickVolume;
                for (int ly = kB - 1; ly >= 0; --ly) {
                    for (int lz = 0; lz < kB; ++lz) {
                        for (int lx = 0; lx < kB; ++lx) {
                            float& t = core::at(through, (lz * kB) + lx);
                            const std::size_t at = base + Builder::local_index(lx, ly, lz);
                            core::at(b.sky, at) = static_cast<uint8_t>(std::lround(kByte * t));
                            t *= static_cast<float>(core::at(b.open, at)) / kByte;
                        }
                    }
                }
            }
        }
    }
}

// The search's nodes: a mixed cell (slot x kBrickVolume + its index) or a far brick (after every cell: + its brick).
struct Graph {
    const Builder& b;
    std::size_t far_first = 0;

    [[nodiscard]] bool is_far(uint32_t node) const { return node >= far_first; }
    [[nodiscard]] std::size_t brick_of(uint32_t node) const { return node - far_first; }
    [[nodiscard]] std::array<int, 3> brick_place(std::size_t brick) const
    {
        const auto bx = static_cast<int>(brick % static_cast<std::size_t>(b.bricks[0]));
        const auto rest = brick / static_cast<std::size_t>(b.bricks[0]);
        return {bx, static_cast<int>(rest % static_cast<std::size_t>(b.bricks[1])),
                static_cast<int>(rest / static_cast<std::size_t>(b.bricks[1]))};
    }
    // a mixed cell's global place
    [[nodiscard]] std::array<int, 3> cell_place(uint32_t node, const std::vector<std::array<int, 3>>& slot_bricks) const
    {
        const std::size_t slot = node / kBrickVolume;
        const std::size_t local = node % kBrickVolume;
        const std::array<int, 3>& brick = core::at(slot_bricks, slot);
        return {(brick[0] * kB) + static_cast<int>(local % kB), (brick[1] * kB) + static_cast<int>((local / kB) % kB),
                (brick[2] * kB) + static_cast<int>(local / (kB * kB))};
    }
    [[nodiscard]] uint32_t node_at(int x, int y, int z) const
    {
        const std::size_t brick = b.brick_index(x / kB, y / kB, z / kB);
        const int32_t slot = core::at(b.slots, brick);
        if (slot < 0) {
            return static_cast<uint32_t>(far_first + brick);
        }
        return static_cast<uint32_t>((static_cast<std::size_t>(slot) * kBrickVolume) +
                                     Builder::local_index(x % kB, y % kB, z % kB));
    }
    [[nodiscard]] float open(uint32_t node) const
    {
        return is_far(node) ? 1.0F : static_cast<float>(core::at(b.open, node)) / kByte;
    }
    [[nodiscard]] float width_units(uint32_t node) const
    {
        return is_far(node) ? 2.0F * kBrickUnits : static_cast<float>(core::at(b.width, node)) * kCell;
    }
    [[nodiscard]] bool free_stream(uint32_t node) const
    {
        return is_far(node) || (core::at(b.open, node) == kOpen && core::at(b.width, node) >= kOpenCells);
    }
    // each neighbour of a node and the step to it (world units)
    template <typename Visit>
    void neighbours(uint32_t node, const std::vector<std::array<int, 3>>& slot_bricks, Visit&& visit) const
    {
        static constexpr std::array<std::array<int, 3>, 6> kSteps{
            {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        constexpr float kToFar = (kBrickUnits + kCell) * 0.5F;
        if (!is_far(node)) {
            const std::array<int, 3> g = cell_place(node, slot_bricks);
            for (const std::array<int, 3>& s : kSteps) {
                const int x = g[0] + s[0];
                const int y = g[1] + s[1];
                const int z = g[2] + s[2];
                if (!b.inside(x, y, z)) {
                    continue;
                }
                const uint32_t next = node_at(x, y, z);
                visit(next, is_far(next) ? kToFar : kCell);
            }
            return;
        }
        const std::array<int, 3> place = brick_place(brick_of(node));
        for (const std::array<int, 3>& s : kSteps) {
            const std::array<int, 3> nb{place[0] + s[0], place[1] + s[1], place[2] + s[2]};
            if (nb[0] < 0 || nb[1] < 0 || nb[2] < 0 || nb[0] >= b.bricks[0] || nb[1] >= b.bricks[1] ||
                nb[2] >= b.bricks[2]) {
                continue;
            }
            const std::size_t brick = b.brick_index(nb[0], nb[1], nb[2]);
            const int32_t slot = core::at(b.slots, brick);
            if (slot < 0) {
                visit(static_cast<uint32_t>(far_first + brick), kBrickUnits);
                continue;
            }
            // the face of the mixed brick toward this one: its kB x kB cells
            const std::size_t axis = s[0] != 0 ? 0 : (s[1] != 0 ? 1 : 2);
            const int layer = (core::at(s, axis) > 0) ? 0 : kB - 1;
            const std::size_t u = (axis + 1) % 3;
            const std::size_t v = (axis + 2) % 3;
            for (int iv = 0; iv < kB; ++iv) {
                for (int iu = 0; iu < kB; ++iu) {
                    std::array<int, 3> l{};
                    core::at(l, axis) = layer;
                    core::at(l, u) = iu;
                    core::at(l, v) = iv;
                    visit(static_cast<uint32_t>((static_cast<std::size_t>(slot) * kBrickVolume) +
                                                Builder::local_index(l[0], l[1], l[2])),
                          kToFar);
                }
            }
        }
    }
};

struct Label {
    float wind = 0.0F;
    float screens = 1.0F; // T: the open shares passed
    float width = 0.0F;   // w: the narrowest passage (units)
    float travel = 0.0F;  // s: since the free stream (units)
    uint32_t node = 0;
    bool operator<(const Label& other) const { return wind < other.wind; }
};

float draught(float screens, float width, float travel)
{
    return screens * std::min(1.0F, kJetCore * width / std::max(travel, kCell));
}

} // namespace

Volume Volume::build(std::span<const Face> faces, Report* report)
{
    const auto started = std::chrono::steady_clock::now();
    Volume out;
    Report r;
    r.faces = faces.size();
    if (faces.empty()) {
        if (report != nullptr) {
            *report = r;
        }
        return out;
    }
    Vec3 low{1e30F, 1e30F, 1e30F};
    Vec3 high{-1e30F, -1e30F, -1e30F};
    for (const Face& face : faces) {
        for (const Vec3& corner : face.corners) {
            for (std::size_t a = 0; a < 3; ++a) {
                core::at(low, a) = std::min(core::at(low, a), core::at(corner, a));
                core::at(high, a) = std::max(core::at(high, a), core::at(corner, a));
            }
        }
    }
    Builder b;
    for (std::size_t a = 0; a < 3; ++a) {
        if (!std::isfinite(core::at(low, a)) || !std::isfinite(core::at(high, a))) {
            return out;
        }
        core::at(b.origin, a) = core::at(low, a) - (kPad * kBrickUnits);
        core::at(b.bricks, a) = static_cast<int>(std::ceil((core::at(high, a) - core::at(low, a)) / kBrickUnits)) +
                                static_cast<int>(2.0F * kPad) + 1;
        core::at(b.cells, a) = core::at(b.bricks, a) * kB;
    }
    r.bricks = b.bricks;
    if (b.brick_count() > kMostBricks) {
        if (report != nullptr) {
            *report = r;
        }
        return out;
    }
    b.slots.assign(b.brick_count(), -1);
    for (const Face& face : faces) {
        const float air = std::clamp(std::isfinite(face.air) ? face.air : 0.0F, 0.0F, 1.0F);
        if (air >= 1.0F) {
            continue;
        }
        if (air > 0.0F) {
            ++r.screens;
        }
        rasterise(b, face, static_cast<uint8_t>(std::lround(air * kByte)));
    }
    r.phases[0] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    passage_widths(b);
    r.phases[1] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    sky_reach(b);
    r.phases[2] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    // where the slots' bricks are
    std::vector<std::array<int, 3>> slot_bricks(static_cast<std::size_t>(b.slot_count));
    for (int bz = 0; bz < b.bricks[2]; ++bz) {
        for (int by = 0; by < b.bricks[1]; ++by) {
            for (int bx = 0; bx < b.bricks[0]; ++bx) {
                const int32_t slot = core::at(b.slots, b.brick_index(bx, by, bz));
                if (slot >= 0) {
                    core::at(slot_bricks, slot) = {bx, by, bz};
                }
            }
        }
    }
    const Graph graph{.b = b, .far_first = b.open.size()};
    const std::size_t nodes = b.open.size() + b.brick_count();
    std::vector<uint8_t> state(nodes, 0);
    std::vector<uint16_t> best(nodes, 0);
    std::priority_queue<Label> frontier;
    const auto seed = [&](uint32_t next, float step) {
        if (core::at(state, next) != 0 || graph.free_stream(next)) {
            return;
        }
        const float open = graph.open(next);
        if (!(open > 0.0F)) {
            return;
        }
        const float width = graph.width_units(next);
        const float f = draught(open, width, step);
        const auto mark = static_cast<uint16_t>(std::lround(f * kBestScale));
        if (mark > core::at(best, next)) {
            core::at(best, next) = mark;
            frontier.push({.wind = f, .screens = open, .width = width, .travel = step, .node = next});
        }
    };
    // 4. the free stream, flooded from the sky over the level (the top layer of bricks: open air by the padding)
    std::vector<uint32_t> queue;
    for (int bz = 0; bz < b.bricks[2]; ++bz) {
        for (int bx = 0; bx < b.bricks[0]; ++bx) {
            const std::size_t brick = b.brick_index(bx, b.bricks[1] - 1, bz);
            if (core::at(b.slots, brick) < 0) {
                const auto node = static_cast<uint32_t>(graph.far_first + brick);
                core::at(state, node) = kFree;
                queue.push_back(node);
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const uint32_t node = core::at(queue, head);
        graph.neighbours(node, slot_bricks, [&](uint32_t next, float step) {
            if (core::at(state, next) != 0) {
                return;
            }
            if (graph.free_stream(next)) {
                core::at(state, next) = kFree;
                queue.push_back(next);
                return;
            }
            seed(next, step);
        });
    }
    r.phases[3] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    // 5. the rest air reaches: best first out of the free stream
    while (!frontier.empty()) {
        const Label label = frontier.top();
        frontier.pop();
        if (core::at(state, label.node) != 0) {
            continue;
        }
        core::at(state, label.node) = kSettled; // (its wind: the best label, the first popped - best[node])
        graph.neighbours(label.node, slot_bricks, [&](uint32_t next, float step) {
            if (core::at(state, next) != 0) {
                return;
            }
            const float open = graph.open(next);
            if (!(open > 0.0F)) {
                return;
            }
            const float screens = label.screens * open;
            const float width = std::min(label.width, graph.width_units(next));
            const float travel = label.travel + step;
            const float f = draught(screens, width, travel);
            if (f < kLeastWind) {
                return;
            }
            const auto mark = static_cast<uint16_t>(std::lround(f * kBestScale));
            if (mark > core::at(best, next)) {
                core::at(best, next) = mark;
                frontier.push({.wind = f, .screens = screens, .width = width, .travel = travel, .node = next});
            }
        });
    }
    r.phases[4] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    // the volume kept: wind and sky per cell, a wall's cell marked (kWallMark: the sampling leaves it out - a surface's air
    // is the air beside it, whichever side)
    out.origin_ = b.origin;
    out.bricks_ = b.bricks;
    out.slots_ = b.slots;
    out.far_.assign(b.brick_count(), {static_cast<uint8_t>(kWindScale), kOpen});
    out.cells_.assign(b.open.size(), {0, 0});
    const auto wind_of = [&](uint32_t node) {
        const uint8_t s = core::at(state, node);
        if (s == kFree) {
            return 1.0F;
        }
        return s == kSettled ? static_cast<float>(core::at(best, node)) / kBestScale : 0.0F;
    };
    const auto stored = [](float w) {
        return static_cast<uint8_t>(std::lround(kWindScale * std::clamp(w, 0.0F, 1.0F)));
    };
    for (std::size_t brick = 0; brick < b.brick_count(); ++brick) {
        if (core::at(b.slots, brick) < 0) {
            const auto node = static_cast<uint32_t>(graph.far_first + brick);
            core::at(out.far_, brick) = {stored(wind_of(node)), core::at(b.far_sky, brick)};
        }
    }
    for (std::size_t i = 0; i < b.open.size(); ++i) {
        uint8_t w = stored(wind_of(static_cast<uint32_t>(i)));
        if (core::at(b.open, i) == 0) {
            ++r.walls;
            w = kWallMark;
        } else if (core::at(state, i) == kFree) {
            ++r.free_cells;
        } else if (core::at(state, i) == kSettled) {
            ++r.sheltered;
        } else {
            ++r.stilled;
        }
        core::at(out.cells_, i) = {w, core::at(b.sky, i)};
    }
    out.serial_ = ++serials();
    r.mixed = static_cast<std::size_t>(b.slot_count);
    r.bytes = (out.cells_.size() + out.far_.size()) * 2 + (out.slots_.size() * sizeof(int32_t));
    r.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (report != nullptr) {
        *report = r;
    }
    return out;
}

std::array<uint8_t, 2> Volume::cell(int x, int y, int z) const
{
    const std::array<int, 3> grid = cells();
    if (x < 0 || y < 0 || z < 0 || x >= grid[0] || y >= grid[1] || z >= grid[2] || slots_.empty()) {
        return {static_cast<uint8_t>(kWindScale), kOpen};
    }
    const std::size_t brick = (((static_cast<std::size_t>(z / kB) * static_cast<std::size_t>(bricks_[1])) +
                                static_cast<std::size_t>(y / kB)) *
                               static_cast<std::size_t>(bricks_[0])) +
                              static_cast<std::size_t>(x / kB);
    const int32_t slot = core::at(slots_, brick);
    if (slot < 0) {
        return core::at(far_, brick);
    }
    return core::at(cells_,
                    (static_cast<std::size_t>(slot) * kBrickVolume) + Builder::local_index(x % kB, y % kB, z % kB));
}

Sample Volume::at(const Vec3& point) const
{
    if (slots_.empty()) {
        return {};
    }
    std::array<int, 3> first{};
    std::array<float, 3> t{};
    for (std::size_t a = 0; a < 3; ++a) {
        const float at = ((core::at(point, a) - core::at(origin_, a)) / kCell) - 0.5F;
        if (!std::isfinite(at)) {
            return {};
        }
        const float floor = std::floor(at);
        core::at(first, a) = static_cast<int>(std::clamp(floor, -2.0F, 1e8F));
        core::at(t, a) = at - floor;
    }
    float wind = 0.0F;
    float sky = 0.0F;
    float weights = 0.0F;
    float all_sky = 0.0F;
    for (int corner = 0; corner < 8; ++corner) {
        const int dx = corner & 1;
        const int dy = (corner >> 1) & 1;
        const int dz = (corner >> 2) & 1;
        const float weight =
            (dx != 0 ? t[0] : 1.0F - t[0]) * (dy != 0 ? t[1] : 1.0F - t[1]) * (dz != 0 ? t[2] : 1.0F - t[2]);
        const std::array<uint8_t, 2> v = cell(first[0] + dx, first[1] + dy, first[2] + dz);
        all_sky += weight * static_cast<float>(v[1]);
        if (v[0] == kWallMark) {
            continue; // (a wall: the air beside it answers)
        }
        weights += weight;
        wind += weight * static_cast<float>(v[0]);
        sky += weight * static_cast<float>(v[1]);
    }
    if (!(weights > 0.0F)) {
        return {.wind = 0.0F, .sky = all_sky / kByte}; // (inside a solid)
    }
    return {.wind = wind / (weights * kWindScale), .sky = sky / (weights * kByte)};
}

void set_volume(std::shared_ptr<const Volume> volume)
{
    Published& own = published();
    const std::scoped_lock lock(own.mutex);
    own.volume = std::move(volume);
}

std::shared_ptr<const Volume> volume()
{
    Published& own = published();
    const std::scoped_lock lock(own.mutex);
    return own.volume;
}

Sample sample_at(const Vec3& point)
{
    const std::shared_ptr<const Volume> now = volume();
    return now != nullptr ? now->at(point) : Sample{};
}

std::array<int, 3> window_first(const Vec3& centre)
{
    return {static_cast<int>(std::floor(centre[0] / kWindowCell)) - (kWindowCells / 2),
            static_cast<int>(std::floor(centre[1] / kWindowCell)) - (kWindowLayers / 2),
            static_cast<int>(std::floor(centre[2] / kWindowCell)) - (kWindowCells / 2)};
}

Window air_window(const Volume& volume, const Vec3& centre)
{
    Window out;
    out.first = window_first(centre);
    out.serial = volume.serial();
    out.texels.resize(static_cast<std::size_t>(kWindowCells) * kWindowLayers * kWindowCells * 2);
    std::size_t at = 0;
    for (int y = 0; y < kWindowLayers; ++y) {
        for (int z = 0; z < kWindowCells; ++z) {
            for (int x = 0; x < kWindowCells; ++x) {
                const Vec3 point{(static_cast<float>(out.first[0] + x) + 0.5F) * kWindowCell,
                                 (static_cast<float>(out.first[1] + y) + 0.5F) * kWindowCell,
                                 (static_cast<float>(out.first[2] + z) + 0.5F) * kWindowCell};
                const Sample s = volume.at(point);
                core::at(out.texels, at++) = static_cast<uint8_t>(std::lround(std::clamp(s.wind, 0.0F, 1.0F) * kByte));
                core::at(out.texels, at++) = static_cast<uint8_t>(std::lround(std::clamp(s.sky, 0.0F, 1.0F) * kByte));
            }
        }
    }
    return out;
}

} // namespace render::air
