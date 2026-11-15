// NOT ORIGINAL - snow: where it settles, the trails pressed into it, the spray kicked up (snow.h).
#include "engine/gfx/snow.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/surface_trail.h"

namespace render::snow {

namespace {

using Vec3 = std::array<float, 3>;
using Vec2 = std::array<float, 2>;
using surface_trail::smoothstep;

constexpr uint32_t kByte = 0xFFU;
constexpr float kByteMax = 255.0F;
constexpr uint32_t kHeightMask = 0xFFFFU;
constexpr uint32_t kCoverShift = 16;
constexpr float kUnorm16 = 65535.0F;
constexpr float kHeightSteps = 65534.0F; // (1 - 65535: a height; 0: no face)
constexpr float kHalf = 0.5F;
constexpr float kInsideSlack = 1.0e-4F;  // (barycentric: a centre on an edge is inside)
constexpr float kLeastArea = 1.0e-4F;    // (twice a triangle's projected area: smaller - its centroid alone)
constexpr float kSoftEdge = 0.6F;        // (a press's edge softened over its outer 40%)
constexpr float kBermStart = 0.85F;      // (a berm rises from this share of the radius)
constexpr float kBermPeak = 4.0F;        // (4 t (1 - t): 1 at the ring's middle)
constexpr float kSteepFace = 0.3F;       // (a face this steep or steeper: no centroid sample - a wall)
constexpr double kForgetSeconds = 2.0;   // a presser not seen this long is forgotten

using Random = surface_trail::Random; // (the surface trails' own random numbers: never the game's)
constexpr uint64_t kSeed = 0x534E4F57'5452414CULL; // (the session's: the same every run)

// A presser's memory between steps.
struct Memory {
    Vec3 last{};
    Vec2 heading{};      // the way it last moved (x, z)
    bool moved = false;  // heading known
    float air = 0.0F;    // seconds in the air
    float stride = 0.0F; // distance walked since the last footprint
    int side = 1;        // the next footprint's side
    float spray_carry = 0.0F;
    double seen = 0.0;
};

struct State {
    std::mutex mutex;
    Settings settings;
    std::shared_ptr<const SkyMap> map;
    TrailField field{trail_texel(kDefaultQuality)};
    std::vector<SprayParticle> spray; // a ring of kMostSpray
    std::size_t spray_next = 0;
    Random random;
    std::unordered_map<uint64_t, Memory> memory;
    double clock = 0.0;
    double fill_carry = 0.0;
    Counts counts;
};

State& state()
{
    static State instance;
    return instance;
}

uint32_t to_byte(float value) { return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByteMax)); }

// --- the sky map's cells -----------------------------------------------------------------------------------------------
struct Cell {
    float height = 0.0F;
    float cover = 0.0F;
    bool found = false;
};

Cell cell_at(const SkyMap& map, int column, int row)
{
    if (column < 0 || row < 0 || std::cmp_greater_equal(column, map.columns) || std::cmp_greater_equal(row, map.rows)) {
        return {};
    }
    const uint32_t packed = core::at(map.cells, (static_cast<std::size_t>(row) * map.columns) + static_cast<std::size_t>(column));
    const uint32_t height = packed & kHeightMask;
    if (height == 0) {
        return {};
    }
    return {.height = map.low + ((static_cast<float>(height - 1) / kHeightSteps) * map.span),
            .cover = static_cast<float>(packed >> kCoverShift) / kUnorm16,
            .found = true};
}

// --- the presses --------------------------------------------------------------------------------------------------------
void add_spray(State& own, const SprayParticle& particle)
{
    if (own.spray.size() < kMostSpray) {
        own.spray.push_back(particle);
    } else {
        core::at(own.spray, own.spray_next) = particle;
    }
    own.spray_next = (own.spray_next + 1) % kMostSpray;
    ++own.counts.spray;
}

// Puffs off a riding board: thrown back and up off its sides, faster with the speed.
void carve_spray(State& own, const Vec3& at, const Vec2& heading, float speed, int count)
{
    const Vec2 side{-core::at(heading, 1), core::at(heading, 0)};
    constexpr float kBack = 0.18F;
    constexpr float kSideSpeed = 70.0F;
    constexpr float kUpLeast = 30.0F;
    constexpr float kUpMost = 110.0F;
    constexpr float kUpPerSpeed = 1.0F / 900.0F;
    for (int k = 0; k < count; ++k) {
        Random& random = own.random;
        const float out = random.between(-1.0F, 1.0F);
        const float back = speed * kBack * random.between(0.2F, 1.0F);
        const float up = random.between(kUpLeast, kUpMost) * (kHalf + (speed * kUpPerSpeed));
        const float across = out * kSideSpeed * (kHalf + (speed * kUpPerSpeed));
        SprayParticle particle;
        particle.at = {core::at(at, 0) + (core::at(side, 0) * out * kBoardHalfWidth) + random.between(-2.0F, 2.0F),
                       core::at(at, 1) + random.between(0.0F, 2.0F),
                       core::at(at, 2) + (core::at(side, 1) * out * kBoardHalfWidth) + random.between(-2.0F, 2.0F)};
        particle.velocity = {(-core::at(heading, 0) * back) + (core::at(side, 0) * across), up,
                             (-core::at(heading, 1) * back) + (core::at(side, 1) * across)};
        particle.born = static_cast<float>(own.clock);
        particle.life = random.between(0.45F, 1.1F);
        particle.size = random.between(1.2F, 3.2F);
        particle.shade = random.unit();
        add_spray(own, particle);
    }
}

// A landing's burst: all round, higher and bigger.
void landing_spray(State& own, const Vec3& at, float fall, int count)
{
    constexpr float kTau = 6.28318530718F;
    constexpr float kOutLeast = 50.0F;
    constexpr float kOutMost = 170.0F;
    constexpr float kUpLeast = 60.0F;
    constexpr float kUpMost = 200.0F;
    constexpr float kFallShare = 1.0F / 600.0F;
    const float strength = std::clamp(kHalf + (fall * kFallShare), kHalf, 2.0F);
    for (int k = 0; k < count; ++k) {
        Random& random = own.random;
        const float angle = random.unit() * kTau;
        const float out = random.between(kOutLeast, kOutMost) * strength;
        const float reach = random.between(0.3F, 1.0F) * kLandingRadius;
        SprayParticle particle;
        particle.at = {core::at(at, 0) + (std::cos(angle) * reach), core::at(at, 1) + random.between(0.0F, 2.0F),
                       core::at(at, 2) + (std::sin(angle) * reach)};
        particle.velocity = {std::cos(angle) * out, random.between(kUpLeast, kUpMost) * strength, std::sin(angle) * out};
        particle.born = static_cast<float>(own.clock);
        particle.life = random.between(0.6F, 1.5F);
        particle.size = random.between(2.0F, 4.5F);
        particle.shade = random.unit();
        add_spray(own, particle);
    }
}

void press_character(State& own, const Presser& presser, Memory& memory, const Ground& ground, double seconds)
{
    const Settings& settings = own.settings;
    const Vec3& at = presser.at;
    const Vec3& last = memory.last;
    const float dx = core::at(at, 0) - core::at(last, 0);
    const float dz = core::at(at, 2) - core::at(last, 2);
    const float moved = std::sqrt((dx * dx) + (dz * dz));
    const auto step = static_cast<float>(seconds);
    const float speed = step > 0.0F ? moved / step : 0.0F;
    const float fall = step > 0.0F ? (core::at(last, 1) - core::at(at, 1)) / step : 0.0F;
    const float top = ground.height + (settings.depth * ground.cover);
    const float over = core::at(at, 1) - top;
    const bool on = ground.found && ground.cover > 0.0F && over < kFootReach && over > -kAirborne;
    Vec2 heading = memory.heading;
    float turn = 0.0F;
    constexpr float kLeastMove = 0.05F;
    if (moved > kLeastMove) {
        const Vec2 now{dx / moved, dz / moved};
        if (memory.moved && step > 0.0F) {
            const float dot = std::clamp((core::at(now, 0) * core::at(heading, 0)) + (core::at(now, 1) * core::at(heading, 1)),
                                         -1.0F, 1.0F);
            turn = std::acos(dot) / step;
        }
        heading = now;
        memory.moved = true;
    } else if (!memory.moved) {
        heading = presser.facing;
    }
    const bool landed = on && memory.air > 0.15F;
    if (over > kAirborne || !ground.found) {
        memory.air += step; // (in the air: counted until it comes down on the snow)
    } else if (on) {
        memory.air = 0.0F;
    }
    memory.heading = heading;
    if (!on) {
        return;
    }
    const Vec2 here{core::at(at, 0), core::at(at, 2)};
    const Vec2 before{core::at(last, 0), core::at(last, 2)};
    if (landed) {
        own.field.stamp({.from = here, .to = here, .radius = kLandingRadius, .press = 1.0F, .berm = 1.0F});
        ++own.counts.stamps;
        ++own.counts.landings;
        constexpr int kLandingLeast = 24;
        constexpr int kLandingMost = 90;
        constexpr float kPerFall = 1.0F / 12.0F;
        landing_spray(own, {core::at(at, 0), top, core::at(at, 2)}, fall,
                      std::clamp(kLandingLeast + static_cast<int>(std::max(fall, 0.0F) * kPerFall), kLandingLeast, kLandingMost));
    }
    if (speed >= kRideSpeed) {
        // the board's slice, its berm higher the faster it goes
        constexpr float kBermLeast = 0.35F;
        constexpr float kBermPerSpeed = 1.0F / 1500.0F;
        constexpr float kBermMost = 0.95F;
        own.field.stamp({.from = before, .to = here, .radius = kBoardHalfWidth, .press = 1.0F,
                         .berm = std::min(kBermLeast + (speed * kBermPerSpeed), kBermMost)});
        ++own.counts.stamps;
        // puffs: a few a second straight on, many more as it carves
        constexpr float kPerSecond = 40.0F;
        constexpr float kSpeedScale = 1.0F / 600.0F;
        constexpr float kStraightShare = 0.2F;
        constexpr float kTurnScale = 1.0F / 1.5F;
        constexpr float kMostTurn = 2.5F;
        constexpr int kMostAStep = 24;
        memory.spray_carry += kPerSecond * speed * kSpeedScale * (kStraightShare + std::min(turn * kTurnScale, kMostTurn)) * step;
        const int count = std::min(static_cast<int>(memory.spray_carry), kMostAStep);
        memory.spray_carry -= static_cast<float>(count);
        memory.spray_carry = std::min(memory.spray_carry, 1.0F);
        carve_spray(own, {core::at(at, 0), top, core::at(at, 2)}, heading, speed, count);
        memory.stride = 0.0F;
        return;
    }
    // walking: a footprint each stride, side by side
    memory.stride += moved;
    if (memory.stride < kStride) {
        return;
    }
    memory.stride -= kStride;
    memory.side = -memory.side;
    const Vec2 side{-core::at(heading, 1), core::at(heading, 0)};
    const float offset = kFootSide * static_cast<float>(memory.side);
    const Vec2 foot{core::at(here, 0) + (core::at(side, 0) * offset), core::at(here, 1) + (core::at(side, 1) * offset)};
    constexpr float kFootPress = 0.8F;
    constexpr float kFootBerm = 0.25F;
    own.field.stamp({.from = foot, .to = foot, .radius = kFootRadius, .press = kFootPress, .berm = kFootBerm});
    ++own.counts.stamps;
}

void press_wheel(State& own, const Presser& presser, const Memory& memory, const Ground& ground)
{
    const float over = core::at(presser.at, 1) - ground.height;
    if (!ground.found || ground.cover <= 0.0F || over > kWheelReach || over < -kWheelReach) {
        return;
    }
    constexpr float kWheelBerm = 0.5F;
    own.field.stamp({.from = {core::at(memory.last, 0), core::at(memory.last, 2)},
                     .to = {core::at(presser.at, 0), core::at(presser.at, 2)},
                     .radius = kWheelHalfWidth,
                     .press = 1.0F,
                     .berm = kWheelBerm});
    ++own.counts.stamps;
}

void clear_session(State& own)
{
    own.field = TrailField(trail_texel(own.settings.quality));
    own.spray.clear();
    own.spray_next = 0;
    own.random.seed(kSeed);
    own.memory.clear();
    own.clock = 0.0;
    own.fill_carry = 0.0;
    own.counts = {};
}

} // namespace

// --- the settings -----------------------------------------------------------------------------------------------------

void set_settings(const Settings& settings)
{
    Settings clean = settings;
    clean.depth = std::isfinite(settings.depth) ? std::clamp(settings.depth, kLeastDepth, kMostDepth) : kDefaultDepth;
    clean.quality = std::clamp(settings.quality, kLeastQuality, kMostQuality);
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    const bool texel_changed = trail_texel(clean.quality) != own.field.texel();
    own.settings = clean;
    if (texel_changed || !clean.on) {
        clear_session(own);
    }
}

Settings settings()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.settings;
}

float trail_texel(int quality)
{
    constexpr std::array<float, 3> kTexels{3.0F, 2.0F, 1.5F};
    return core::at(kTexels, static_cast<std::size_t>(std::clamp(quality, kLeastQuality, kMostQuality) - 1));
}

int window_tiles(int quality)
{
    constexpr std::array<int, 3> kTiles{6, 10, 16}; // (1152, 1280, 1536 units across)
    return core::at(kTiles, static_cast<std::size_t>(std::clamp(quality, kLeastQuality, kMostQuality) - 1));
}

// --- the sky map ------------------------------------------------------------------------------------------------------

SkyMap make_sky_map(std::span<const Face> faces)
{
    static std::atomic<uint64_t> serials{0};
    SkyMap map;
    map.serial = ++serials;
    map.faces = static_cast<uint32_t>(faces.size());
    if (faces.empty()) {
        return map;
    }
    constexpr float kHuge = std::numeric_limits<float>::max();
    Vec3 low{kHuge, kHuge, kHuge};
    Vec3 high{-kHuge, -kHuge, -kHuge};
    for (const Face& face : faces) {
        for (const Vec3& corner : face.corners) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (std::isfinite(core::at(corner, axis))) {
                    core::at(low, axis) = std::min(core::at(low, axis), core::at(corner, axis));
                    core::at(high, axis) = std::max(core::at(high, axis), core::at(corner, axis));
                }
            }
        }
    }
    if (!(core::at(high, 0) >= core::at(low, 0)) || !(core::at(high, 2) >= core::at(low, 2))) {
        return map;
    }
    const float extent = std::max(core::at(high, 0) - core::at(low, 0), core::at(high, 2) - core::at(low, 2));
    map.cell = std::max(kLeastCell, extent / static_cast<float>(kMostCells - 1));
    map.x0 = core::at(low, 0) - map.cell;
    map.z0 = core::at(low, 2) - map.cell;
    map.columns = std::min(kMostCells, static_cast<uint32_t>(std::ceil((core::at(high, 0) - map.x0) / map.cell)) + 2);
    map.rows = std::min(kMostCells, static_cast<uint32_t>(std::ceil((core::at(high, 2) - map.z0) / map.cell)) + 2);
    map.low = core::at(low, 1);
    map.span = std::max(core::at(high, 1) - core::at(low, 1), 1.0F);
    const std::size_t count = static_cast<std::size_t>(map.columns) * map.rows;
    std::vector<float> tops(count, -kHuge);
    std::vector<float> covers(count, 0.0F);
    const auto put = [&](int column, int row, float height, float cover) {
        if (column < 0 || row < 0 || std::cmp_greater_equal(column, map.columns) || std::cmp_greater_equal(row, map.rows)) {
            return;
        }
        const std::size_t index = (static_cast<std::size_t>(row) * map.columns) + static_cast<std::size_t>(column);
        if (height > core::at(tops, index)) {
            core::at(tops, index) = height;
            core::at(covers, index) = cover;
        }
    };
    for (const Face& face : faces) {
        const Vec3& a = core::at(face.corners, 0);
        const Vec3& b = core::at(face.corners, 1);
        const Vec3& c = core::at(face.corners, 2);
        const Vec3 ab{core::at(b, 0) - core::at(a, 0), core::at(b, 1) - core::at(a, 1), core::at(b, 2) - core::at(a, 2)};
        const Vec3 ac{core::at(c, 0) - core::at(a, 0), core::at(c, 1) - core::at(a, 1), core::at(c, 2) - core::at(a, 2)};
        const Vec3 normal{(core::at(ab, 1) * core::at(ac, 2)) - (core::at(ab, 2) * core::at(ac, 1)),
                          (core::at(ab, 2) * core::at(ac, 0)) - (core::at(ab, 0) * core::at(ac, 2)),
                          (core::at(ab, 0) * core::at(ac, 1)) - (core::at(ab, 1) * core::at(ac, 0))};
        const float length = std::sqrt((core::at(normal, 0) * core::at(normal, 0)) + (core::at(normal, 1) * core::at(normal, 1)) +
                                       (core::at(normal, 2) * core::at(normal, 2)));
        if (!(length > 0.0F) || !std::isfinite(length)) {
            continue;
        }
        const float up = std::abs(core::at(normal, 1)) / length;
        const float cover = face.settles ? smoothstep(kLeastUp, kFullUp, up) : 0.0F;
        map.indoor_faces += face.settles ? 0U : 1U;
        // the projected triangle's cells: each centre inside it takes the face's height there
        const float area2 = core::at(normal, 1); // (twice the signed projected area, x z)
        const float min_x = std::min({core::at(a, 0), core::at(b, 0), core::at(c, 0)});
        const float max_x = std::max({core::at(a, 0), core::at(b, 0), core::at(c, 0)});
        const float min_z = std::min({core::at(a, 2), core::at(b, 2), core::at(c, 2)});
        const float max_z = std::max({core::at(a, 2), core::at(b, 2), core::at(c, 2)});
        if (std::abs(area2) > kLeastArea) {
            const int first_column = static_cast<int>(std::ceil(((min_x - map.x0) / map.cell) - kHalf));
            const int last_column = static_cast<int>(std::floor(((max_x - map.x0) / map.cell) - kHalf));
            const int first_row = static_cast<int>(std::ceil(((min_z - map.z0) / map.cell) - kHalf));
            const int last_row = static_cast<int>(std::floor(((max_z - map.z0) / map.cell) - kHalf));
            for (int row = first_row; row <= last_row; ++row) {
                const float z = map.z0 + ((static_cast<float>(row) + kHalf) * map.cell);
                for (int column = first_column; column <= last_column; ++column) {
                    const float x = map.x0 + ((static_cast<float>(column) + kHalf) * map.cell);
                    // barycentric weights in x z (the plane's height from them)
                    const float wb = (((x - core::at(a, 0)) * core::at(ac, 2)) - ((z - core::at(a, 2)) * core::at(ac, 0))) / -area2;
                    const float wc = (((z - core::at(a, 2)) * core::at(ab, 0)) - ((x - core::at(a, 0)) * core::at(ab, 2))) / -area2;
                    const float wa = 1.0F - wb - wc;
                    if (wa < -kInsideSlack || wb < -kInsideSlack || wc < -kInsideSlack) {
                        continue;
                    }
                    put(column, row, (wa * core::at(a, 1)) + (wb * core::at(b, 1)) + (wc * core::at(c, 1)), cover);
                }
            }
        }
        if (up >= kSteepFace) { // (a ledge's or a rail's narrow top: its middle's cell at least)
            constexpr float kThird = 1.0F / 3.0F;
            const float x = (core::at(a, 0) + core::at(b, 0) + core::at(c, 0)) * kThird;
            const float z = (core::at(a, 2) + core::at(b, 2) + core::at(c, 2)) * kThird;
            put(static_cast<int>(std::floor((x - map.x0) / map.cell)), static_cast<int>(std::floor((z - map.z0) / map.cell)),
                (core::at(a, 1) + core::at(b, 1) + core::at(c, 1)) * kThird, cover);
        }
    }
    map.cells.assign(count, 0U);
    for (std::size_t index = 0; index < count; ++index) {
        const float top = core::at(tops, index);
        if (top == -kHuge) {
            continue;
        }
        const auto height = static_cast<uint32_t>(std::lround(std::clamp((top - map.low) / map.span, 0.0F, 1.0F) * kHeightSteps)) + 1U;
        const auto cover = static_cast<uint32_t>(std::lround(std::clamp(core::at(covers, index), 0.0F, 1.0F) * kUnorm16));
        core::at(map.cells, index) = height | (cover << kCoverShift);
        map.snowy_area += static_cast<double>(core::at(covers, index)) * static_cast<double>(map.cell * map.cell);
    }
    return map;
}

Ground ground_at(const SkyMap& map, float x, float z)
{
    if (map.columns == 0 || map.rows == 0) {
        return {};
    }
    const float fx = ((x - map.x0) / map.cell) - kHalf;
    const float fz = ((z - map.z0) / map.cell) - kHalf;
    if (!std::isfinite(fx) || !std::isfinite(fz)) {
        return {};
    }
    const auto column = static_cast<int>(std::floor(fx));
    const auto row = static_cast<int>(std::floor(fz));
    const float tx = fx - static_cast<float>(column);
    const float tz = fz - static_cast<float>(row);
    const std::array<Cell, 4> cells{cell_at(map, column, row), cell_at(map, column + 1, row), cell_at(map, column, row + 1),
                                    cell_at(map, column + 1, row + 1)};
    bool all = true;
    float least = std::numeric_limits<float>::max();
    float most = -std::numeric_limits<float>::max();
    for (const Cell& cell : cells) {
        all = all && cell.found;
        if (cell.found) {
            least = std::min(least, cell.height);
            most = std::max(most, cell.height);
        }
    }
    if (all && most - least <= kStepHeight) {
        const auto blend = [&](float c00, float c10, float c01, float c11) {
            const float top = c00 + ((c10 - c00) * tx);
            const float bottom = c01 + ((c11 - c01) * tx);
            return top + ((bottom - top) * tz);
        };
        return {.height = blend(core::at(cells, 0).height, core::at(cells, 1).height, core::at(cells, 2).height,
                                core::at(cells, 3).height),
                .cover = blend(core::at(cells, 0).cover, core::at(cells, 1).cover, core::at(cells, 2).cover,
                               core::at(cells, 3).cover),
                .found = true};
    }
    const Cell nearest = core::at(cells, (tz >= kHalf ? 2U : 0U) + (tx >= kHalf ? 1U : 0U));
    return {.height = nearest.height, .cover = nearest.cover, .found = nearest.found};
}

// --- the trails -------------------------------------------------------------------------------------------------------

void TrailField::stamp(const Stamp& stamp)
{
    if (!(stamp.radius > 0.0F)) {
        return;
    }
    const float reach = stamp.radius + kBermWidth + kActiveReach;
    const float berm_span = ((1.0F - kBermStart) * stamp.radius) + kBermWidth;
    const float active_from = stamp.radius + kBermWidth;
    stroke(stamp.from, stamp.to, reach, [&](float d, const surface_trail::Point& /*centre*/, surface_trail::TexelRef ref) {
        const float press = stamp.press * (1.0F - smoothstep(kSoftEdge * stamp.radius, stamp.radius, d));
        const float t = (d - (kBermStart * stamp.radius)) / berm_span;
        const float berm = t > 0.0F && t < 1.0F ? stamp.berm * kBermPeak * t * (1.0F - t) : 0.0F;
        const float active = 1.0F - smoothstep(active_from, reach, d);
        uint32_t& texel = ref.word(0);
        const uint32_t old_press = (texel >> kPressShift) & kByte;
        const uint32_t old_berm = (texel >> kBermShift) & kByte;
        const uint32_t old_active = (texel >> kActiveShift) & kByte;
        const uint32_t new_press = std::max(old_press, to_byte(press));
        const uint32_t new_berm = std::max(old_berm, to_byte(berm));
        const uint32_t new_active = std::max(old_active, to_byte(active));
        const uint32_t made = (new_press << kPressShift) | (new_berm << kBermShift) | (new_active << kActiveShift) |
                              (texel & ~((kByte << kPressShift) | (kByte << kBermShift) | (kByte << kActiveShift)));
        if (made == texel) {
            return false;
        }
        texel = made;
        return true;
    });
}

void TrailField::fill(uint32_t steps)
{
    if (steps == 0) {
        return;
    }
    const uint32_t by = std::min(steps, kByte);
    change_tiles([by](surface_trail::Tile& tile) {
        bool changed = false;
        for (uint32_t& texel : tile.texels) {
            const uint32_t press = (texel >> kPressShift) & kByte;
            const uint32_t berm = (texel >> kBermShift) & kByte;
            if (press == 0 && berm == 0) {
                continue;
            }
            const uint32_t new_press = press > by ? press - by : 0U;
            const uint32_t new_berm = berm > by ? berm - by : 0U;
            texel = (texel & ~((kByte << kPressShift) | (kByte << kBermShift))) | (new_press << kPressShift) | (new_berm << kBermShift);
            changed = true;
        }
        return changed;
    });
}

void TrailField::totals(uint64_t* press, uint64_t* berm, uint64_t* active) const
{
    *press = 0;
    *berm = 0;
    *active = 0;
    read_tiles([&](const surface_trail::Tile& tile) {
        for (const uint32_t texel : tile.texels) {
            *press += (texel >> kPressShift) & kByte;
            *berm += (texel >> kBermShift) & kByte;
            *active += (texel >> kActiveShift) & kByte;
        }
    });
}

float layer_height(uint32_t texel, float depth, float cover)
{
    const float press = static_cast<float>((texel >> kPressShift) & kByte) / kByteMax;
    const float berm = static_cast<float>((texel >> kBermShift) & kByte) / kByteMax;
    const float active = static_cast<float>((texel >> kActiveShift) & kByte) / kByteMax;
    return depth * cover * ((active * (1.0F - press)) + (kBermHeight * berm * (1.0F - press)));
}

std::array<float, 3> spray_place(const SprayParticle& particle, float age)
{
    return {core::at(particle.at, 0) + (core::at(particle.velocity, 0) * age),
            core::at(particle.at, 1) + (core::at(particle.velocity, 1) * age) - (kHalf * kSprayGravity * age * age),
            core::at(particle.at, 2) + (core::at(particle.velocity, 2) * age)};
}

// --- the state --------------------------------------------------------------------------------------------------------

void set_sky_map(std::shared_ptr<const SkyMap> map)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    own.map = std::move(map);
    clear_session(own);
}

std::shared_ptr<const SkyMap> sky_map()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.map;
}

void step(std::span<const Presser> pressers, double seconds, bool snowing)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    if (!own.settings.on || own.map == nullptr || !(seconds > 0.0) || !std::isfinite(seconds)) {
        return;
    }
    own.clock += seconds;
    ++own.counts.steps;
    for (const Presser& presser : pressers) {
        const Ground ground = ground_at(*own.map, core::at(presser.at, 0), core::at(presser.at, 2));
        auto [found, made] = own.memory.try_emplace(presser.id);
        Memory& memory = found->second;
        const float dx = core::at(presser.at, 0) - core::at(memory.last, 0);
        const float dz = core::at(presser.at, 2) - core::at(memory.last, 2);
        const bool jumped = (dx * dx) + (dz * dz) > kTeleport * kTeleport;
        if (!made && !jumped) {
            if (presser.kind == PresserKind::kWheel) {
                press_wheel(own, presser, memory, ground);
            } else {
                press_character(own, presser, memory, ground, seconds);
            }
        } else {
            memory = Memory{};
            memory.heading = presser.facing;
        }
        memory.last = presser.at;
        memory.seen = own.clock;
    }
    std::erase_if(own.memory, [&own](const auto& entry) { return own.clock - entry.second.seen > kForgetSeconds; });
    if (snowing) {
        own.fill_carry += seconds / kFillSeconds * static_cast<double>(kByteMax);
        const auto steps = static_cast<uint32_t>(own.fill_carry);
        if (steps > 0) {
            own.fill_carry -= static_cast<double>(steps);
            own.field.fill(steps);
            own.counts.fills += steps;
        }
    }
}

void read_trails(const std::function<void(const TrailField& field)>& reader)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    reader(own.field);
}

std::vector<SprayParticle> live_spray()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    std::vector<SprayParticle> live;
    live.reserve(own.spray.size());
    for (const SprayParticle& particle : own.spray) {
        if (own.clock - static_cast<double>(particle.born) < static_cast<double>(particle.life)) {
            live.push_back(particle);
        }
    }
    return live;
}

double clock()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.clock;
}

Counts counts()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.counts;
}

void reset()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    clear_session(own);
}

} // namespace render::snow
