// NOT ORIGINAL - the level's water bodies measured from its geometry (water_bodies.h).
#include "engine/gfx/water_bodies.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/water_waves.h"

namespace render::water {

namespace {

constexpr float kNone = -std::numeric_limits<float>::infinity();
constexpr float kFacingUp = 0.7F;      // a water triangle facing up at least this much (not a waterfall)
constexpr float kEdgeStep = 0.5F;      // the collision's edges sampled every half cell
constexpr int kVoidCells = 3;          // the water ends at nothing when this many cells past it hold no collision
constexpr double kLeastDepth = 0.01;   // metres (a film of water)
constexpr std::size_t kLevels = 4;     // water surfaces kept a cell (a harbour under the sea's far plane, a pool on a roof)
constexpr float kOneLevel = 1.0F;      // surfaces closer than this in a cell are one
constexpr int kBucketCells = 16;       // the collision's faces listed by buckets of this many cells a side (the beds)

struct Grid {
    float left = 0.0F; // x of column 0's edge
    float near = 0.0F; // z of row 0's edge
    float cell = 1.0F;
    int columns = 0;
    int rows = 0;
    std::vector<std::array<float, kLevels>> water; // the water surfaces' heights in each cell
    std::vector<uint8_t> levels;                    // ... how many
    std::vector<float> top;                         // the collision's highest point (kNone: nothing)
    std::vector<std::vector<float>> spans;          // the heights of the faces whose shadow covers the cell's centre
    int bucket_columns = 0;
    int bucket_rows = 0;
    std::vector<std::vector<uint32_t>> buckets;     // the collision's faces whose shadow on the ground meets each bucket

    [[nodiscard]] bool inside(int column, int row) const { return column >= 0 && row >= 0 && column < columns && row < rows; }
    [[nodiscard]] std::size_t index(int column, int row) const
    {
        return (static_cast<std::size_t>(row) * static_cast<std::size_t>(columns)) + static_cast<std::size_t>(column);
    }
    [[nodiscard]] int column_of(float x) const { return static_cast<int>(std::floor((x - left) / cell)); }
    [[nodiscard]] int row_of(float z) const { return static_cast<int>(std::floor((z - near) / cell)); }
    [[nodiscard]] float x_of(int column) const { return left + ((static_cast<float>(column) + 0.5F) * cell); }
    [[nodiscard]] float z_of(int row) const { return near + ((static_cast<float>(row) + 0.5F) * cell); }
    // whether the cell holds water at about height y (the same body's surface)
    [[nodiscard]] bool water_near(std::size_t at, float y) const
    {
        const std::array<float, kLevels>& here = core::at(water, at);
        for (uint8_t i = 0; i < core::at(levels, at); ++i) {
            if (std::abs(core::at(here, i) - y) < kSameWater) {
                return true;
            }
        }
        return false;
    }
    void add_water(std::size_t at, float y)
    {
        std::array<float, kLevels>& here = core::at(water, at);
        uint8_t& count = core::at(levels, at);
        for (uint8_t i = 0; i < count; ++i) {
            if (std::abs(core::at(here, i) - y) < kOneLevel) {
                return;
            }
        }
        if (count < kLevels) {
            core::at(here, count) = y;
            ++count;
        }
    }
};

float x_of(const Corner& c) { return core::at(c, 0); }
float y_of(const Corner& c) { return core::at(c, 1); }
float z_of(const Corner& c) { return core::at(c, 2); }

// The triangle's height at (x, z) when the point is inside its shadow on the ground (barycentric), else false.
bool height_at(const Triangle& t, float x, float z, float* y)
{
    const Corner& a = core::at(t, 0);
    const Corner& b = core::at(t, 1);
    const Corner& c = core::at(t, 2);
    const float d = ((z_of(b) - z_of(c)) * (x_of(a) - x_of(c))) + ((x_of(c) - x_of(b)) * (z_of(a) - z_of(c)));
    if (std::abs(d) < 1e-6F) {
        return false;
    }
    const float wa = (((z_of(b) - z_of(c)) * (x - x_of(c))) + ((x_of(c) - x_of(b)) * (z - z_of(c)))) / d;
    const float wb = (((z_of(c) - z_of(a)) * (x - x_of(c))) + ((x_of(a) - x_of(c)) * (z - z_of(c)))) / d;
    const float wc = 1.0F - wa - wb;
    constexpr float kEdge = -1e-5F;
    if (wa < kEdge || wb < kEdge || wc < kEdge) {
        return false;
    }
    *y = (wa * y_of(a)) + (wb * y_of(b)) + (wc * y_of(c));
    return true;
}

struct CellRange {
    int c0 = 0;
    int c1 = -1;
    int r0 = 0;
    int r1 = -1;
};
CellRange range_of(const Grid& grid, const Triangle& t)
{
    float x0 = x_of(core::at(t, 0));
    float x1 = x0;
    float z0 = z_of(core::at(t, 0));
    float z1 = z0;
    for (const Corner& c : t) {
        x0 = std::min(x0, x_of(c));
        x1 = std::max(x1, x_of(c));
        z0 = std::min(z0, z_of(c));
        z1 = std::max(z1, z_of(c));
    }
    return {.c0 = std::max(grid.column_of(x0), 0),
            .c1 = std::min(grid.column_of(x1), grid.columns - 1),
            .r0 = std::max(grid.row_of(z0), 0),
            .r1 = std::min(grid.row_of(z1), grid.rows - 1)};
}

// Each cell whose centre lies in the triangle's shadow on the ground, with the triangle's height there.
template <typename Visit>
int for_each_covered(const Grid& grid, const Triangle& t, Visit visit)
{
    const CellRange range = range_of(grid, t);
    int covered = 0;
    for (int row = range.r0; row <= range.r1; ++row) {
        for (int column = range.c0; column <= range.c1; ++column) {
            float y = 0.0F;
            if (height_at(t, grid.x_of(column), grid.z_of(row), &y)) {
                visit(grid.index(column, row), y);
                ++covered;
            }
        }
    }
    return covered;
}

bool faces_up(const Triangle& t)
{
    const Corner& a = core::at(t, 0);
    const Corner& b = core::at(t, 1);
    const Corner& c = core::at(t, 2);
    const std::array<float, 3> u{x_of(b) - x_of(a), y_of(b) - y_of(a), z_of(b) - z_of(a)};
    const std::array<float, 3> v{x_of(c) - x_of(a), y_of(c) - y_of(a), z_of(c) - z_of(a)};
    const float nx = (core::at(u, 1) * core::at(v, 2)) - (core::at(u, 2) * core::at(v, 1));
    const float ny = (core::at(u, 2) * core::at(v, 0)) - (core::at(u, 0) * core::at(v, 2));
    const float nz = (core::at(u, 0) * core::at(v, 1)) - (core::at(u, 1) * core::at(v, 0));
    const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
    return length > 0.0F && std::abs(ny) / length >= kFacingUp;
}

void add_collision_point(Grid* grid, int column, int row, float y)
{
    if (grid->inside(column, row)) {
        float& top = core::at(grid->top, grid->index(column, row));
        top = std::max(top, y);
    }
}

// The bed under the water at height y over the cell's centre: the highest collision face there below the surface (kNone:
// none - deep water).
float bed_at(const Grid& grid, std::span<const Triangle> collision, int column, int row, float y)
{
    const int bucket = ((row / kBucketCells) * grid.bucket_columns) + (column / kBucketCells);
    float bed = kNone;
    const float x = grid.x_of(column);
    const float z = grid.z_of(row);
    for (const uint32_t face : core::at(grid.buckets, static_cast<std::size_t>(bucket))) {
        float here = 0.0F;
        if (height_at(core::at(collision, face), x, z, &here) && here < y - kBedGap) {
            bed = std::max(bed, here);
        }
    }
    return bed;
}

// The share of the sky's hemisphere open above (x, z) at height y: 1 - sin of the horizon's elevation, averaged over
// kSkyDirections ways round, past the collision's highest points within kSkyReach; 0 under something overhead.
double openness_at(const Grid& grid, int column, int row, float y)
{
    // (something over the point itself: a roof, a bridge - a face whose shadow covers it, not a wall's edge beside it)
    for (const float over : core::at(grid.spans, grid.index(column, row))) {
        if (over > y + kOverhead) {
            return 0.0;
        }
    }
    double open = 0.0;
    const int steps = std::max(static_cast<int>(kSkyReach / grid.cell), 1);
    for (uint32_t way = 0; way < kSkyDirections; ++way) {
        const double angle = 2.0 * std::numbers::pi * (static_cast<double>(way) + 0.5) / kSkyDirections;
        const double dx = std::cos(angle);
        const double dz = std::sin(angle);
        double highest = 0.0; // sin of the horizon's elevation
        for (int step = 1; step <= steps; ++step) {
            const double r = static_cast<double>(step) * grid.cell;
            const int c = grid.column_of(static_cast<float>(grid.x_of(column) + (dx * r)));
            const int w = grid.row_of(static_cast<float>(grid.z_of(row) + (dz * r)));
            if (!grid.inside(c, w)) {
                break;
            }
            const double h = static_cast<double>(core::at(grid.top, grid.index(c, w))) - static_cast<double>(y);
            if (h > 0.0) {
                highest = std::max(highest, h / std::hypot(h, r));
            }
        }
        open += 1.0 - highest;
    }
    return open / kSkyDirections;
}

// The fetch from (column, row): world units walked upwind over the same water to where it ends; `open` true when it ends
// at nothing (the world stops there).
double fetch_at(const Grid& grid, int column, int row, float y, std::array<float, 2> downwind, bool* open)
{
    *open = false;
    const double ux = -static_cast<double>(core::at(downwind, 0));
    const double uz = -static_cast<double>(core::at(downwind, 1));
    const double x = grid.x_of(column);
    const double z = grid.z_of(row);
    const int most = (grid.columns + grid.rows) * 2;
    for (int step = 1; step <= most; ++step) {
        const double r = static_cast<double>(step) * grid.cell;
        const int c = grid.column_of(static_cast<float>(x + (ux * r)));
        const int w = grid.row_of(static_cast<float>(z + (uz * r)));
        if (!grid.inside(c, w)) {
            *open = true;
            return r;
        }
        if (grid.water_near(grid.index(c, w), y)) {
            continue;
        }
        // the water ends here: at a shore (anything), or at nothing - the world's end - for a few cells on
        bool nothing = true;
        for (int past = 0; past < kVoidCells && nothing; ++past) {
            const double rr = r + (static_cast<double>(past) * grid.cell);
            const int pc = grid.column_of(static_cast<float>(x + (ux * rr)));
            const int pw = grid.row_of(static_cast<float>(z + (uz * rr)));
            if (!grid.inside(pc, pw)) {
                break; // (off the grid: nothing known past it - taken as nothing)
            }
            const std::size_t at = grid.index(pc, pw);
            nothing = core::at(grid.top, at) == kNone && core::at(grid.levels, at) == 0;
        }
        *open = nothing;
        return r - (0.5 * grid.cell);
    }
    *open = true;
    return static_cast<double>(most) * grid.cell;
}

double median(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

} // namespace

std::vector<BodyMeasure> measure_bodies(std::span<const BodyMesh> meshes, std::span<const Triangle> collision,
                                        std::array<float, 2> downwind, BodiesReport* report)
{
    std::vector<BodyMeasure> out(meshes.size());
    float x0 = std::numeric_limits<float>::max();
    float x1 = -x0;
    float z0 = x0;
    float z1 = -x0;
    for (const BodyMesh& mesh : meshes) {
        for (const Triangle& t : mesh.triangles) {
            for (const Corner& c : t) {
                x0 = std::min(x0, x_of(c));
                x1 = std::max(x1, x_of(c));
                z0 = std::min(z0, z_of(c));
                z1 = std::max(z1, z_of(c));
            }
        }
    }
    if (!(x1 >= x0) || !(z1 >= z0)) {
        return out;
    }
    Grid grid;
    const float width = (x1 - x0) + (2.0F * kSkyReach);
    const float depth = (z1 - z0) + (2.0F * kSkyReach);
    grid.cell = std::max(std::max(width, depth) / static_cast<float>(kMostCells), kLeastCell);
    grid.left = x0 - kSkyReach;
    grid.near = z0 - kSkyReach;
    grid.columns = std::max(static_cast<int>(std::ceil(width / grid.cell)), 1);
    grid.rows = std::max(static_cast<int>(std::ceil(depth / grid.cell)), 1);
    const std::size_t cells = static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows);
    grid.water.assign(cells, {});
    grid.levels.assign(cells, 0);
    grid.top.assign(cells, kNone);
    grid.spans.assign(cells, {});
    grid.bucket_columns = (grid.columns + kBucketCells - 1) / kBucketCells;
    grid.bucket_rows = (grid.rows + kBucketCells - 1) / kBucketCells;
    grid.buckets.assign(static_cast<std::size_t>(grid.bucket_columns) * static_cast<std::size_t>(grid.bucket_rows), {});
    // the water: each mesh's triangles facing up (a triangle smaller than a cell its centre's), each mesh's own cells kept
    std::vector<std::vector<std::pair<std::size_t, float>>> covered(meshes.size());
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        for (const Triangle& t : core::at(meshes, m).triangles) {
            if (!faces_up(t)) {
                continue;
            }
            const auto mark = [&](std::size_t at, float y) {
                core::at(covered, m).emplace_back(at, y);
                grid.add_water(at, y);
            };
            if (for_each_covered(grid, t, mark) == 0) {
                const float cx = (x_of(core::at(t, 0)) + x_of(core::at(t, 1)) + x_of(core::at(t, 2))) / 3.0F;
                const float cy = (y_of(core::at(t, 0)) + y_of(core::at(t, 1)) + y_of(core::at(t, 2))) / 3.0F;
                const float cz = (z_of(core::at(t, 0)) + z_of(core::at(t, 1)) + z_of(core::at(t, 2))) / 3.0F;
                const int c = grid.column_of(cx);
                const int w = grid.row_of(cz);
                if (grid.inside(c, w)) {
                    mark(grid.index(c, w), cy);
                }
            }
        }
    }
    // the collision: every face's shadow and its edges (walls have no shadow on the ground) - the highest point a cell;
    // and the faces by bucket (the beds)
    for (std::size_t f = 0; f < collision.size(); ++f) {
        const Triangle& t = core::at(collision, f);
        (void)for_each_covered(grid, t, [&](std::size_t at, float y) {
            float& top = core::at(grid.top, at);
            top = std::max(top, y);
            if (core::at(grid.levels, at) > 0) {
                core::at(grid.spans, at).push_back(y); // (only over water: the roofs a sample may be under)
            }
        });
        for (std::size_t e = 0; e < t.size(); ++e) {
            const Corner& a = core::at(t, e);
            const Corner& b = core::at(t, (e + 1) % t.size());
            const float length = std::hypot(x_of(b) - x_of(a), z_of(b) - z_of(a));
            const int points = static_cast<int>(std::ceil(length / (kEdgeStep * grid.cell))) + 1;
            for (int i = 0; i <= points; ++i) {
                const float s = static_cast<float>(i) / static_cast<float>(points);
                add_collision_point(&grid, grid.column_of(x_of(a) + ((x_of(b) - x_of(a)) * s)),
                                    grid.row_of(z_of(a) + ((z_of(b) - z_of(a)) * s)), y_of(a) + ((y_of(b) - y_of(a)) * s));
            }
        }
        const CellRange range = range_of(grid, t);
        for (int br = range.r0 / kBucketCells; br <= range.r1 / kBucketCells && range.r1 >= range.r0; ++br) {
            for (int bc = range.c0 / kBucketCells; bc <= range.c1 / kBucketCells && range.c1 >= range.c0; ++bc) {
                core::at(grid.buckets, static_cast<std::size_t>((br * grid.bucket_columns) + bc)).push_back(static_cast<uint32_t>(f));
            }
        }
    }
    uint32_t water_cells = 0;
    for (std::size_t at = 0; at < cells; ++at) {
        water_cells += core::at(grid.levels, at) > 0 ? 1U : 0U;
    }
    // the bodies: the water's surfaces joined cell to cell (four neighbours) where their heights are within kSameWater -
    // a sea drawn as many tiles, a harbour drawn twice, one body; each measured once over its cells and every mesh of it
    // given the same (no seams between tiles)
    const std::size_t slots = cells * kLevels;
    std::vector<int32_t> label(slots, -1);
    std::vector<std::vector<std::pair<std::size_t, float>>> bodies;
    std::vector<std::size_t> queue;
    for (std::size_t at = 0; at < cells; ++at) {
        for (uint8_t level = 0; level < core::at(grid.levels, at); ++level) {
            if (core::at(label, (at * kLevels) + level) >= 0) {
                continue;
            }
            const auto id = static_cast<int32_t>(bodies.size());
            bodies.emplace_back();
            core::at(label, (at * kLevels) + level) = id;
            queue.assign(1, (at * kLevels) + level);
            while (!queue.empty()) {
                const std::size_t slot = queue.back();
                queue.pop_back();
                const std::size_t here = slot / kLevels;
                const float y = core::at(core::at(grid.water, here), slot % kLevels);
                bodies.back().emplace_back(here, y);
                const int column = static_cast<int>(here % static_cast<std::size_t>(grid.columns));
                const int row = static_cast<int>(here / static_cast<std::size_t>(grid.columns));
                const std::array<std::array<int, 2>, 4> ways{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (const std::array<int, 2>& way : ways) {
                    const int c = column + core::at(way, 0);
                    const int w = row + core::at(way, 1);
                    if (!grid.inside(c, w)) {
                        continue;
                    }
                    const std::size_t next = grid.index(c, w);
                    for (uint8_t l = 0; l < core::at(grid.levels, next); ++l) {
                        int32_t& mark = core::at(label, (next * kLevels) + l);
                        if (mark < 0 && std::abs(core::at(core::at(grid.water, next), l) - y) < kSameWater) {
                            mark = id;
                            queue.push_back((next * kLevels) + l);
                        }
                    }
                }
            }
        }
    }
    // the body each mesh belongs to: the one most of its cells are in (at its own height)
    std::vector<int32_t> body_of(meshes.size(), -1);
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        std::vector<std::pair<std::size_t, float>>& mine = core::at(covered, m);
        std::ranges::sort(mine);
        mine.erase(std::unique(mine.begin(), mine.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), mine.end());
        core::at(out, m).cells = static_cast<uint32_t>(mine.size());
        std::vector<int32_t> ids;
        for (const auto& [at, y] : mine) {
            for (uint8_t level = 0; level < core::at(grid.levels, at); ++level) {
                if (std::abs(core::at(core::at(grid.water, at), level) - y) < kOneLevel) {
                    ids.push_back(core::at(label, (at * kLevels) + level));
                    break;
                }
            }
        }
        if (ids.empty()) {
            continue;
        }
        std::ranges::sort(ids);
        int32_t best = ids.front();
        std::size_t best_run = 0;
        for (std::size_t i = 0; i < ids.size();) {
            std::size_t j = i;
            while (j < ids.size() && core::at(ids, j) == core::at(ids, i)) {
                ++j;
            }
            if (j - i > best_run) {
                best_run = j - i;
                best = core::at(ids, i);
            }
            i = j;
        }
        core::at(body_of, m) = best;
    }
    // each body at up to kSamples of its cells, evenly through them
    std::vector<BodyMeasure> measured(bodies.size());
    std::vector<uint8_t> done(bodies.size(), 0);
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const int32_t id = core::at(body_of, m);
        if (id < 0) {
            continue;
        }
        const auto body = static_cast<std::size_t>(id);
        BodyMeasure& measure = core::at(measured, body);
        if (core::at(done, body) == 0) {
            core::at(done, body) = 1;
            const std::vector<std::pair<std::size_t, float>>& list = core::at(bodies, body);
            const std::size_t count = std::min<std::size_t>(list.size(), kSamples);
            std::vector<double> fetches;
            std::vector<double> depths;
            double open_sum = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                const auto [at, y] = core::at(list, (i * list.size()) / count);
                const int column = static_cast<int>(at % static_cast<std::size_t>(grid.columns));
                const int row = static_cast<int>(at / static_cast<std::size_t>(grid.columns));
                bool open = false;
                const double fetch = fetch_at(grid, column, row, y, downwind, &open);
                fetches.push_back(open ? kOpenFetch : std::max(fetch, 0.5 * grid.cell) / kUnitsPerMetre);
                measure.open_samples += open ? 1U : 0U;
                const float bed = bed_at(grid, collision, column, row, y);
                depths.push_back(bed > kNone ? std::max(static_cast<double>(y - bed) / kUnitsPerMetre, kLeastDepth) : kDeepWater);
                open_sum += openness_at(grid, column, row, y);
            }
            measure.measured = true;
            measure.samples = static_cast<uint32_t>(count);
            measure.surface_cells = static_cast<uint32_t>(list.size());
            measure.fetch = median(fetches);
            measure.depth = median(depths);
            measure.openness = open_sum / static_cast<double>(count);
        }
        const uint32_t own_cells = core::at(out, m).cells;
        core::at(out, m) = measure;
        core::at(out, m).cells = own_cells;
        core::at(out, m).body = static_cast<uint32_t>(id);
    }
    if (report != nullptr) {
        report->columns = static_cast<uint32_t>(grid.columns);
        report->rows = static_cast<uint32_t>(grid.rows);
        report->cell = grid.cell;
        report->water_cells = water_cells;
        report->collision_faces = static_cast<uint32_t>(collision.size());
    }
    return out;
}

} // namespace render::water
