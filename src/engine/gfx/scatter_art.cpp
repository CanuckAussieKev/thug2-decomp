// NOT ORIGINAL - decals and clutter: the decals' atlas and the clutter's meshes, made by code (scatter.h) - nothing
// fetched, nothing third-party. Every texel and vertex comes from integer hashes: the same bytes every time. Each kind's
// numbers are in its own table below (the art's knobs).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/scatter.h"

namespace render::scatter {

namespace {

constexpr float kTau = 6.28318530718F;
constexpr float kUnit = 1.0F / 4294967296.0F;
constexpr float kHalf = 0.5F;
constexpr float kTwo = 2.0F;
constexpr float kThree = 3.0F;
constexpr std::size_t kTexel = 4;
constexpr float kEdgeFade = 0.04F; // (of a tile: its art fades to nothing at its border)
constexpr float kByteScale = 255.0F;
constexpr float kTexels = static_cast<float>(kDecalTile);
constexpr float kFar = 1.0e9F;
constexpr float kTinySpan = 1.0e-9F;
constexpr uint32_t kOctaveSeedStep = 101U;

using Rgb = std::array<float, 3>;
using Vec2 = std::array<float, 2>;
using V3 = std::array<float, 3>;

constexpr uint32_t mix(uint32_t value)
{
    constexpr uint32_t kFirst = 0x7FEB352DU;
    constexpr uint32_t kSecond = 0x846CA68BU;
    constexpr uint32_t kShiftA = 16;
    constexpr uint32_t kShiftB = 15;
    value ^= value >> kShiftA;
    value *= kFirst;
    value ^= value >> kShiftB;
    value *= kSecond;
    value ^= value >> kShiftA;
    return value;
}

// A hash of a seed and two numbers as 0 - 1.
struct HashKey {
    uint32_t seed = 0;
    uint32_t what = 0;
    uint32_t which = 0;
};
float hash_unit(const HashKey& key)
{
    constexpr uint32_t kWhat = 0x9E3779B9U;
    constexpr uint32_t kWhich = 0x85EBCA6BU;
    return static_cast<float>(mix(key.seed ^ mix(key.what * kWhat) ^ mix((key.which * kWhich) + 1U))) * kUnit;
}
float hash_unit(uint32_t seed, uint32_t what, uint32_t which)
{
    return hash_unit(HashKey{.seed = seed, .what = what, .which = which});
}

float smooth(float low, float high, float value)
{
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return along * along * (kThree - (kTwo * along));
}

float x_of(const Vec2& at) { return core::at(at, 0); }
float y_of(const Vec2& at) { return core::at(at, 1); }

// A noise's lattice: its seed and how many cells across the tile (it wraps there).
struct Lattice {
    uint32_t seed = 0;
    int period = 1;
};

// Value noise at `at` (lattice units) over a wrapping lattice (0 - 1).
float value_noise(const Vec2& at, const Lattice& lattice)
{
    const float fx = std::floor(x_of(at));
    const float fy = std::floor(y_of(at));
    const float tx = x_of(at) - fx;
    const float ty = y_of(at) - fy;
    const int period = lattice.period;
    const auto wrap = [period](float cell) {
        const int index = static_cast<int>(cell) % period;
        return static_cast<uint32_t>(index < 0 ? index + period : index);
    };
    const uint32_t x0 = wrap(fx);
    const uint32_t x1 = wrap(fx + 1.0F);
    const uint32_t y0 = wrap(fy);
    const uint32_t y1 = wrap(fy + 1.0F);
    const float sx = tx * tx * (kThree - (kTwo * tx));
    const float sy = ty * ty * (kThree - (kTwo * ty));
    const float corner00 = hash_unit(lattice.seed, x0, y0);
    const float corner10 = hash_unit(lattice.seed, x1, y0);
    const float corner01 = hash_unit(lattice.seed, x0, y1);
    const float corner11 = hash_unit(lattice.seed, x1, y1);
    const float low = corner00 + ((corner10 - corner00) * sx);
    const float high = corner01 + ((corner11 - corner01) * sx);
    return low + ((high - low) * sy);
}

// Fractal noise's make: its seed, the cells across its first octave, how many octaves.
struct Fractal {
    uint32_t seed = 0;
    int base = 1;
    int octaves = 1;
};

// Fractal noise (0 - 1 about) at `at` (tile units).
float fbm(const Vec2& at, const Fractal& fractal)
{
    float sum = 0.0F;
    float weight = kHalf;
    float total = 0.0F;
    int period = fractal.base;
    for (int octave = 0; octave < fractal.octaves; ++octave) {
        sum += weight * value_noise({x_of(at) * static_cast<float>(period), y_of(at) * static_cast<float>(period)},
                                    {.seed = fractal.seed + (static_cast<uint32_t>(octave) * kOctaveSeedStep),
                                     .period = period});
        total += weight;
        weight *= kHalf;
        period *= 2;
    }
    return sum / total;
}

// A texel being painted: where in the tile (texels) and its centre (0 - 1).
struct Texel {
    uint32_t column = 0;
    uint32_t row = 0;
    Vec2 uv{};
};

// A tile being drawn: each texel's multiplier (1: untouched).
class ArtTile {
public:
    ArtTile() : texels_(static_cast<std::size_t>(kDecalTile) * kDecalTile, Rgb{1.0F, 1.0F, 1.0F}) {}
    // Darken a texel by `amount` (0 - 1) toward `colour` (the multiplier at full).
    void darken(const Texel& texel, float amount, const Rgb& colour)
    {
        Rgb& here = core::at(texels_, (static_cast<std::size_t>(texel.row) * kDecalTile) + texel.column);
        const float share = std::clamp(amount, 0.0F, 1.0F);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            core::at(here, channel) *= 1.0F - (share * (1.0F - core::at(colour, channel)));
        }
    }
    // Into the atlas's cell (`cell`: column, row).
    void into_atlas(std::vector<uint8_t>* atlas, const std::array<uint32_t, 2>& cell) const
    {
        for (uint32_t row = 0; row < kDecalTile; ++row) {
            for (uint32_t column = 0; column < kDecalTile; ++column) {
                const float u = (static_cast<float>(column) + kHalf) / kTexels;
                const float v = (static_cast<float>(row) + kHalf) / kTexels;
                const float edge = smooth(0.0F, kEdgeFade, std::min({u, v, 1.0F - u, 1.0F - v}));
                const Rgb& texel = core::at(texels_, (static_cast<std::size_t>(row) * kDecalTile) + column);
                const std::size_t line = (static_cast<std::size_t>(core::at(cell, 1)) * kDecalTile) + row;
                const std::size_t across = (static_cast<std::size_t>(core::at(cell, 0)) * kDecalTile) + column;
                const std::size_t at = ((line * kAtlasWidth) + across) * kTexel;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    core::at(*atlas, at + channel) = static_cast<uint8_t>(
                        std::lround(std::clamp(core::at(texel, channel), 0.0F, 1.0F) * kByteScale));
                }
                core::at(*atlas, at + 3) = static_cast<uint8_t>(std::lround(edge * kByteScale));
            }
        }
    }

private:
    std::vector<Rgb> texels_;
};

template <typename Paint>
void each_texel(Paint paint)
{
    for (uint32_t row = 0; row < kDecalTile; ++row) {
        for (uint32_t column = 0; column < kDecalTile; ++column) {
            paint(Texel{
                .column = column,
                .row = row,
                .uv = {(static_cast<float>(column) + kHalf) / kTexels, (static_cast<float>(row) + kHalf) / kTexels}});
        }
    }
}

// --- cracks --------------------------------------------------------------------------------------------------------

// A segment of a crack (tile units 0 - 1 across, 0 - the aspect along) and its width (texels).
struct Segment {
    Vec2 from{};
    Vec2 to{};
    float width = 0.0F;
};

float segment_distance(const Segment& segment, const Vec2& at)
{
    const float dx = x_of(segment.to) - x_of(segment.from);
    const float dy = y_of(segment.to) - y_of(segment.from);
    const float span = (dx * dx) + (dy * dy);
    float along = 0.0F;
    if (span > kTinySpan) {
        along = std::clamp((((x_of(at) - x_of(segment.from)) * dx) + ((y_of(at) - y_of(segment.from)) * dy)) / span,
                           0.0F, 1.0F);
    }
    const float px = x_of(segment.from) + (along * dx) - x_of(at);
    const float py = y_of(segment.from) + (along * dy) - y_of(at);
    return std::sqrt((px * px) + (py * py));
}

// A wandering line: where from, heading, steps and their length, its width (tapering), its seed.
struct Path {
    Vec2 from{};
    float angle = 0.0F;
    int steps = 0;
    float step = 0.0F;
    float width = 0.0F;
    uint32_t seed = 0;
};

constexpr float kWobble = 0.9F;     // a crack's turn a step (radians, at most half each way)
constexpr float kTaper = 0.7F;      // its width lost along it
constexpr uint32_t kWobbleKey = 7U; // (the hash's)

void wander(std::vector<Segment>* out, const Path& path)
{
    Vec2 at = path.from;
    float angle = path.angle;
    for (int k = 0; k < path.steps; ++k) {
        angle += (hash_unit(path.seed, static_cast<uint32_t>(k), kWobbleKey) - kHalf) * kWobble;
        const Vec2 next{x_of(at) + (std::sin(angle) * path.step), y_of(at) + (std::cos(angle) * path.step)};
        const float taper = 1.0F - (kTaper * static_cast<float>(k) / static_cast<float>(path.steps));
        out->push_back({.from = at, .to = next, .width = path.width * taper});
        at = next;
    }
}

struct CrackArt {
    float aspect{};    // along / across (the sizes' middle)
    float start_low{}; // where across the main line starts (and how far that varies)
    float start_spread{};
    float start_along{};    // ... and along (tile units)
    float heading_spread{}; // its heading (radians, whole spread)
    int steps{};            // its steps and their length (tile units, x the aspect)
    float step{};
    float width{};          // its width (texels; + up to 1)
    float least_branches{}; // branches: this many and up to `branch_spread` more
    float branch_spread{};
    std::size_t branch_first{}; // they start from these segments
    float branch_choice{};
    float branch_turn{}; // turned off it this much and up to as much again (radians)
    int branch_steps{};  // their steps (and up to `branch_step_spread` more), length, width share
    float branch_step_spread{};
    float branch_step{};
    float branch_width{};
    uint32_t branch_seed{};
    Rgb dark{}; // the crack's colour, and its dirty halo's
    Rgb dirt{};
    float edge{}; // the line's soft edge (texels) and its core's share of the width
    float core{};
    float grain_low{}; // the grain's share of the darkness
    float grain_high{};
    Fractal grain{};
    float halo{}; // the halo's darkness and how fast it falls (texels)
    float halo_fall{};
};
constexpr CrackArt kCrack{.aspect = 1.9F,
                          .start_low = 0.35F,
                          .start_spread = 0.3F,
                          .start_along = 0.06F,
                          .heading_spread = 0.4F,
                          .steps = 18,
                          .step = 0.05F,
                          .width = 2.2F,
                          .least_branches = 2.0F,
                          .branch_spread = 3.0F,
                          .branch_first = 3,
                          .branch_choice = 12.0F,
                          .branch_turn = 0.6F,
                          .branch_steps = 5,
                          .branch_step_spread = 6.0F,
                          .branch_step = 0.04F,
                          .branch_width = 0.6F,
                          .branch_seed = 31U,
                          .dark = {0.16F, 0.15F, 0.14F},
                          .dirt = {0.62F, 0.6F, 0.57F},
                          .edge = 1.2F,
                          .core = 0.4F,
                          .grain_low = 0.75F,
                          .grain_high = 0.25F,
                          .grain = {.seed = 17U, .base = 8, .octaves = 3},
                          .halo = 0.35F,
                          .halo_fall = 5.0F};

// The hash keys of a crack's choices.
constexpr uint32_t kCrackStart = 1;
constexpr uint32_t kCrackHeading = 2;
constexpr uint32_t kCrackWidth = 3;
constexpr uint32_t kCrackBranches = 4;
constexpr uint32_t kBranchFrom = 5;
constexpr uint32_t kBranchSide = 6;
constexpr uint32_t kBranchTurn = 8;
constexpr uint32_t kBranchSteps = 9;

void paint_crack(ArtTile* tile, uint32_t seed)
{
    const CrackArt art = kCrack;
    std::vector<Segment> segments;
    wander(&segments, {.from = {art.start_low + (art.start_spread * hash_unit(seed, kCrackStart, 0U)),
                                art.start_along * art.aspect},
                       .angle = (hash_unit(seed, kCrackHeading, 0U) - kHalf) * art.heading_spread,
                       .steps = art.steps,
                       .step = art.step * art.aspect,
                       .width = art.width + hash_unit(seed, kCrackWidth, 0U),
                       .seed = seed});
    const auto branches = static_cast<uint32_t>(art.least_branches) +
                          static_cast<uint32_t>(hash_unit(seed, kCrackBranches, 0U) * art.branch_spread);
    for (uint32_t branch = 0; branch < branches; ++branch) {
        const Segment& from =
            core::at(segments, art.branch_first +
                                   static_cast<std::size_t>(hash_unit(seed, kBranchFrom, branch) * art.branch_choice));
        const float side = hash_unit(seed, kBranchSide, branch) < kHalf ? -1.0F : 1.0F;
        wander(&segments, {.from = from.to,
                           .angle = side * (art.branch_turn + (art.branch_turn * hash_unit(seed, kBranchTurn, branch))),
                           .steps = art.branch_steps +
                                    static_cast<int>(hash_unit(seed, kBranchSteps, branch) * art.branch_step_spread),
                           .step = art.branch_step * art.aspect,
                           .width = from.width * art.branch_width,
                           .seed = seed + (art.branch_seed * (branch + 1U))});
    }
    each_texel([&](const Texel& texel) {
        const Vec2 at{x_of(texel.uv), y_of(texel.uv) * art.aspect};
        float nearest = kFar;
        float width = 1.0F;
        for (const Segment& segment : segments) {
            const float apart = segment_distance(segment, at) * kTexels;
            if (apart < nearest) {
                nearest = apart;
                width = segment.width;
            }
        }
        const float grain =
            fbm(texel.uv, {.seed = seed + art.grain.seed, .base = art.grain.base, .octaves = art.grain.octaves});
        tile->darken(texel,
                     smooth(width + art.edge, width * art.core, nearest) * (art.grain_low + (art.grain_high * grain)),
                     art.dark);
        tile->darken(texel, art.halo * std::exp(-nearest / art.halo_fall) * grain, art.dirt);
    });
}

// --- blots ---------------------------------------------------------------------------------------------------------

// A blob: a disc of `radius` (tile units) at the tile's centre, its edge roughened by noise (`rough` of the radius) and
// softened over `soft`.
struct Blob {
    uint32_t seed = 0;
    float radius = 0.0F;
    float rough = 0.0F;
    float soft = 0.0F;
};
constexpr Fractal kBlobNoise{.seed = 0, .base = 4, .octaves = 4};

float blob(const Vec2& at, const Blob& shape)
{
    const float du = x_of(at) - kHalf;
    const float dv = y_of(at) - kHalf;
    const float apart = std::sqrt((du * du) + (dv * dv));
    const float wobble = fbm(at, {.seed = shape.seed, .base = kBlobNoise.base, .octaves = kBlobNoise.octaves}) - kHalf;
    const float edge = shape.radius * (1.0F + (shape.rough * wobble * kTwo));
    return smooth(edge, edge - shape.soft, apart);
}

struct StainArt {
    Rgb colour{};
    Blob body{};       // (seeds: offsets from the tile's)
    Blob ring_outer{}; // the tide line: the outer blob less the inner
    Blob ring_inner{};
    Fractal inside{};
    Fractal patches{};
    float inside_weight{};
    float patch_low{};
    float patch_high{};
    float patch_weight{};
    float ring_weight{};
};
constexpr StainArt kStain{.colour = {0.62F, 0.57F, 0.5F},
                          .body = {.seed = 0, .radius = 0.34F, .rough = 0.7F, .soft = 0.2F},
                          .ring_outer = {.seed = 0, .radius = 0.34F, .rough = 0.7F, .soft = 0.03F},
                          .ring_inner = {.seed = 0, .radius = 0.31F, .rough = 0.7F, .soft = 0.04F},
                          .inside = {.seed = 5U, .base = 5, .octaves = 4},
                          .patches = {.seed = 11U, .base = 3, .octaves = 3},
                          .inside_weight = 0.25F,
                          .patch_low = 0.35F,
                          .patch_high = 0.75F,
                          .patch_weight = 0.45F,
                          .ring_weight = 0.2F};

Blob seeded(const Blob& shape, uint32_t seed)
{
    Blob out = shape;
    out.seed += seed;
    return out;
}
Fractal seeded(const Fractal& fractal, uint32_t seed)
{
    Fractal out = fractal;
    out.seed += seed;
    return out;
}

void paint_stain(ArtTile* tile, uint32_t seed)
{
    const StainArt art = kStain;
    each_texel([&](const Texel& texel) {
        // (a soft, uneven blot: darker in patches, its edge a faint tide line)
        const float mask = blob(texel.uv, seeded(art.body, seed));
        const float inside = fbm(texel.uv, seeded(art.inside, seed));
        const float patches = smooth(art.patch_low, art.patch_high, fbm(texel.uv, seeded(art.patches, seed)));
        const float ring = blob(texel.uv, seeded(art.ring_outer, seed)) - blob(texel.uv, seeded(art.ring_inner, seed));
        tile->darken(texel,
                     (mask * ((art.inside_weight * inside) + (art.patch_weight * patches))) +
                         (art.ring_weight * std::max(ring, 0.0F)),
                     art.colour);
    });
}

struct OilArt {
    Rgb colour{};
    Rgb sheen_colour{};
    Blob body{};
    Blob sheen{};
    Fractal inside{};
    float body_low{};
    float body_high{};
    float sheen_weight{};
    float least_drops{};
    float drop_spread{};
    float drop_out{}; // the drops' distance from the centre (tile units) and its spread
    float drop_out_spread{};
    float drop_radius{}; // their radius and its spread
    float drop_radius_spread{};
    float drop_core{}; // their core's share of the radius
    float drop_dark{};
};
constexpr OilArt kOil{.colour = {0.2F, 0.2F, 0.23F},
                      .sheen_colour = {0.75F, 0.72F, 0.8F},
                      .body = {.seed = 0, .radius = 0.28F, .rough = 0.35F, .soft = 0.1F},
                      .sheen = {.seed = 3U, .radius = 0.4F, .rough = 0.4F, .soft = 0.15F},
                      .inside = {.seed = 9U, .base = 4, .octaves = 3},
                      .body_low = 0.7F,
                      .body_high = 0.3F,
                      .sheen_weight = 0.4F,
                      .least_drops = 3.0F,
                      .drop_spread = 5.0F,
                      .drop_out = 0.3F,
                      .drop_out_spread = 0.15F,
                      .drop_radius = 0.015F,
                      .drop_radius_spread = 0.025F,
                      .drop_core = 0.5F,
                      .drop_dark = 0.8F};

// The hash keys of spots' and drips' choices.
constexpr uint32_t kDropCount = 1;
constexpr uint32_t kDropAngle = 2;
constexpr uint32_t kDropOut = 3;
constexpr uint32_t kDropRadius = 4;
constexpr uint32_t kDropDark = 5;

void paint_oil(ArtTile* tile, uint32_t seed)
{
    const OilArt art = kOil;
    const auto drops = static_cast<uint32_t>(art.least_drops) +
                       static_cast<uint32_t>(hash_unit(seed, kDropCount, 0U) * art.drop_spread);
    each_texel([&](const Texel& texel) {
        const float mask = blob(texel.uv, seeded(art.body, seed));
        const float inside = fbm(texel.uv, seeded(art.inside, seed));
        tile->darken(texel, mask * (art.body_low + (art.body_high * inside)), art.colour);
        tile->darken(texel, blob(texel.uv, seeded(art.sheen, seed)) * art.sheen_weight, art.sheen_colour);
        for (uint32_t k = 0; k < drops; ++k) {
            const float angle = hash_unit(seed, kDropAngle, k) * kTau;
            const float out = art.drop_out + (art.drop_out_spread * hash_unit(seed, kDropOut, k));
            const float cx = kHalf + (std::cos(angle) * out);
            const float cy = kHalf + (std::sin(angle) * out);
            const float radius = art.drop_radius + (art.drop_radius_spread * hash_unit(seed, kDropRadius, k));
            const float du = x_of(texel.uv) - cx;
            const float dv = y_of(texel.uv) - cy;
            const float apart = std::sqrt((du * du) + (dv * dv));
            tile->darken(texel, smooth(radius, radius * art.drop_core, apart) * art.drop_dark, art.colour);
        }
    });
}

struct TyreArt {
    Rgb colour{};
    float bend{};  // the skid's curve (at most half each way, tile units at its ends)
    float start{}; // where it builds up (tile units along) and how much that varies
    float start_spread{};
    float build{}; // over how far it builds up, and where it fades out from
    float fade_from{};
    float half_width{}; // the band's half width (tile units) and its soft edge
    float edge{};
    float wobble{}; // the width's wobble along it
    Fractal wobble_noise{};
    float streaks_low{}; // streaks along it: the tread's ribs dragged
    float streaks_high{};
    float streak_cells{};
    uint32_t streak_seed{};
    float patch_low{}; // patchy along it
    float patch_high{};
    float patch_squash{};
    Fractal patches{};
    float darkness{};
};
constexpr TyreArt kTyre{.colour = {0.14F, 0.14F, 0.14F},
                        .bend = 0.16F,
                        .start = 0.05F,
                        .start_spread = 0.2F,
                        .build = 0.2F,
                        .fade_from = 0.6F,
                        .half_width = 0.34F,
                        .edge = 0.1F,
                        .wobble = 0.15F,
                        .wobble_noise = {.seed = 3U, .base = 6, .octaves = 3},
                        .streaks_low = 0.65F,
                        .streaks_high = 0.35F,
                        .streak_cells = 48.0F,
                        .streak_seed = 5U,
                        .patch_low = 0.5F,
                        .patch_high = 0.5F,
                        .patch_squash = 0.3F,
                        .patches = {.seed = 7U, .base = 4, .octaves = 4},
                        .darkness = 0.8F};

constexpr float kCurveScale = 4.0F; // ((v - 0.5)^2 x this: 1 at the ends)

void paint_tyre(ArtTile* tile, uint32_t seed)
{
    const TyreArt art = kTyre;
    const float bend = (hash_unit(seed, 1U, 0U) - kHalf) * art.bend;
    const float start = art.start + (art.start_spread * hash_unit(seed, 2U, 0U)); // (a skid builds up, then fades)
    each_texel([&](const Texel& texel) {
        const float u = x_of(texel.uv);
        const float v = y_of(texel.uv);
        const float centre = kHalf + (bend * (v - kHalf) * (v - kHalf) * kCurveScale);
        const float wobble = fbm({0.0F, v}, seeded(art.wobble_noise, seed)) - kHalf;
        const float across = std::abs(u - centre) * (1.0F + (art.wobble * wobble));
        const float band = smooth(art.half_width, art.half_width - art.edge, across);
        const float ends = smooth(start, start + art.build, v) * smooth(1.0F, art.fade_from, v);
        const float streaks =
            art.streaks_low + (art.streaks_high * value_noise({u * art.streak_cells, kHalf},
                                                              {.seed = seed + art.streak_seed,
                                                               .period = static_cast<int>(art.streak_cells)}));
        const float patchy =
            art.patch_low + (art.patch_high * fbm({u * art.patch_squash, v}, seeded(art.patches, seed)));
        tile->darken(texel, band * ends * streaks * patchy * art.darkness, art.colour);
    });
}

struct GumArt {
    Rgb colour{};
    float least_spots{};
    float spot_spread{};
    float reach{};  // how far from the centre they lie (tile units)
    float radius{}; // a spot's radius (texels) and its spread
    float radius_spread{};
    float dark{}; // its darkness and its spread
    float dark_spread{};
    float lumpy{}; // its outline's wobble (share of the radius) and lobes
    float lobes{};
    float core{}; // its core's share of the radius, its smear's reach and darkness
    float smear{};
    float smear_dark{};
};
constexpr GumArt kGum{.colour = {0.4F, 0.4F, 0.38F},
                      .least_spots = 5.0F,
                      .spot_spread = 10.0F,
                      .reach = 0.38F,
                      .radius = 4.0F,
                      .radius_spread = 6.0F,
                      .dark = 0.45F,
                      .dark_spread = 0.3F,
                      .lumpy = 0.08F,
                      .lobes = 5.0F,
                      .core = 0.7F,
                      .smear = 1.5F,
                      .smear_dark = 0.15F};

void paint_gum(ArtTile* tile, uint32_t seed)
{
    const GumArt art = kGum;
    const auto spots = static_cast<uint32_t>(art.least_spots) +
                       static_cast<uint32_t>(hash_unit(seed, kDropCount, 0U) * art.spot_spread);
    struct Spot {
        Vec2 at;
        float radius;
        float dark;
    };
    std::vector<Spot> list;
    for (uint32_t k = 0; k < spots; ++k) {
        const float angle = hash_unit(seed, kDropAngle, k) * kTau;
        const float out = art.reach * std::sqrt(hash_unit(seed, kDropOut, k));
        list.push_back({.at = {kHalf + (std::cos(angle) * out), kHalf + (std::sin(angle) * out)},
                        .radius = (art.radius + (art.radius_spread * hash_unit(seed, kDropRadius, k))) / kTexels,
                        .dark = art.dark + (art.dark_spread * hash_unit(seed, kDropDark, k))});
    }
    each_texel([&](const Texel& texel) {
        for (std::size_t k = 0; k < list.size(); ++k) {
            const Spot& spot = core::at(list, k);
            const float du = x_of(texel.uv) - x_of(spot.at);
            const float dv = y_of(texel.uv) - y_of(spot.at);
            const float angle = std::atan2(dv, du);
            const float radius =
                spot.radius * (1.0F + (art.lumpy * std::sin((angle * art.lobes) + static_cast<float>(k))));
            const float apart = std::sqrt((du * du) + (dv * dv));
            tile->darken(texel, smooth(radius, radius * art.core, apart) * spot.dark, art.colour);
            tile->darken(texel, smooth(radius * art.smear, radius, apart) * art.smear_dark, art.colour);
        }
    });
}

struct WetArt {
    Rgb colour{};
    Blob body{};
    Blob deep{};
    float body_weight{};
    float deep_weight{};
};
constexpr WetArt kWet{.colour = {0.58F, 0.6F, 0.64F},
                      .body = {.seed = 0, .radius = 0.38F, .rough = 0.5F, .soft = 0.06F},
                      .deep = {.seed = 2U, .radius = 0.25F, .rough = 0.6F, .soft = 0.1F},
                      .body_weight = 0.85F,
                      .deep_weight = 0.3F};

void paint_wet(ArtTile* tile, uint32_t seed)
{
    const WetArt art = kWet;
    each_texel([&](const Texel& texel) {
        const float mask = blob(texel.uv, seeded(art.body, seed));
        const float deep = blob(texel.uv, seeded(art.deep, seed));
        tile->darken(texel, (mask * art.body_weight) + (deep * art.deep_weight), art.colour);
    });
}

struct GrimeArt {
    Rgb colour{};
    float least_drips{};
    float drip_spread{};
    float sides{}; // its sides' fade (tile units)
    float fall{};  // how fast it fades up (a power)
    float body_low{};
    float body_high{};
    float noise_squash{};
    Fractal body{};
    float drip_from{}; // where the drips are across (from, and over how much)
    float drip_across{};
    float drip_top{}; // how far up they reach (from, and up to as much more)
    float drip_top_spread{};
    float drip_width{}; // their width (texels) and its spread
    float drip_width_spread{};
    float drip_wobble{}; // their wobble (tile units) and its waves along them
    float drip_waves{};
    float drip_core{}; // their core's share of the width, their top's soft share
    float drip_soft_top{};
    float drip_dark{};
};
constexpr GrimeArt kGrime{.colour = {0.5F, 0.47F, 0.4F},
                          .least_drips = 6.0F,
                          .drip_spread = 8.0F,
                          .sides = 0.2F,
                          .fall = 1.6F,
                          .body_low = 0.55F,
                          .body_high = 0.45F,
                          .noise_squash = 0.5F,
                          .body = {.seed = 3U, .base = 6, .octaves = 4},
                          .drip_from = 0.1F,
                          .drip_across = 0.8F,
                          .drip_top = 0.4F,
                          .drip_top_spread = 0.5F,
                          .drip_width = 1.5F,
                          .drip_width_spread = 2.5F,
                          .drip_wobble = 0.01F,
                          .drip_waves = 20.0F,
                          .drip_core = 0.3F,
                          .drip_soft_top = 0.6F,
                          .drip_dark = 0.45F};

void paint_grime(ArtTile* tile, uint32_t seed)
{
    const GrimeArt art = kGrime;
    const auto drips = static_cast<uint32_t>(art.least_drips) +
                       static_cast<uint32_t>(hash_unit(seed, kDropCount, 0U) * art.drip_spread);
    each_texel([&](const Texel& texel) {
        const float u = x_of(texel.uv);
        const float v = y_of(texel.uv);
        const float sides = smooth(0.0F, art.sides, u) * smooth(1.0F, 1.0F - art.sides, u);
        const float body = std::pow(1.0F - v, art.fall) *
                           (art.body_low + (art.body_high * fbm({u, v * art.noise_squash}, seeded(art.body, seed))));
        float amount = body * sides;
        for (uint32_t k = 0; k < drips; ++k) {
            const float at = art.drip_from + (art.drip_across * hash_unit(seed, kDropAngle, k));
            const float top = art.drip_top + (art.drip_top_spread * hash_unit(seed, kDropOut, k));
            const float width = (art.drip_width + (art.drip_width_spread * hash_unit(seed, kDropRadius, k))) / kTexels;
            const float wobble = art.drip_wobble * std::sin((v * art.drip_waves) + static_cast<float>(k));
            const float streak = smooth(width, width * art.drip_core, std::abs(u - at - wobble)) *
                                 smooth(top, top * art.drip_soft_top, v);
            amount = std::max(amount, streak * art.drip_dark);
        }
        tile->darken(texel, amount, art.colour);
    });
}

using Painter = void (*)(ArtTile*, uint32_t);
constexpr std::array<Painter, kDecalKinds> kPainters{&paint_crack, &paint_stain, &paint_oil,  &paint_tyre,
                                                     &paint_gum,   &paint_wet,   &paint_grime};

// --- the meshes ---------------------------------------------------------------------------------------------------

// A triangle of a mesh: its corners, its shade (the mesh's own light) and its part (1: a second colour).
struct MeshTriangle {
    V3 first{};
    V3 second{};
    V3 third{};
    float shade = 1.0F;
    float part = 0.0F;
};

V3 difference(const V3& one, const V3& two)
{
    return {core::at(one, 0) - core::at(two, 0), core::at(one, 1) - core::at(two, 1),
            core::at(one, 2) - core::at(two, 2)};
}

class MeshBuilder {
public:
    explicit MeshBuilder(std::vector<MeshVertex>* out) : out_(out), first_(out->size()) {}
    void triangle(const MeshTriangle& made)
    {
        const V3 ab = difference(made.second, made.first);
        const V3 ac = difference(made.third, made.first);
        V3 normal{(core::at(ab, 1) * core::at(ac, 2)) - (core::at(ab, 2) * core::at(ac, 1)),
                  (core::at(ab, 2) * core::at(ac, 0)) - (core::at(ab, 0) * core::at(ac, 2)),
                  (core::at(ab, 0) * core::at(ac, 1)) - (core::at(ab, 1) * core::at(ac, 0))};
        const float size =
            std::sqrt((core::at(normal, 0) * core::at(normal, 0)) + (core::at(normal, 1) * core::at(normal, 1)) +
                      (core::at(normal, 2) * core::at(normal, 2)));
        if (size > 0.0F) {
            normal = {core::at(normal, 0) / size, core::at(normal, 1) / size, core::at(normal, 2) / size};
        }
        if (core::at(normal, 1) < 0.0F) {
            normal = {-core::at(normal, 0), -core::at(normal, 1),
                      -core::at(normal, 2)}; // (two-sided: the upward light)
        }
        for (const V3* corner : {&made.first, &made.second, &made.third}) {
            out_->push_back({.position = {core::at(*corner, 0), core::at(*corner, 1), core::at(*corner, 2), made.shade},
                             .normal = {core::at(normal, 0), core::at(normal, 1), core::at(normal, 2), made.part}});
        }
    }
    void finish()
    {
        while (out_->size() < first_ + kMeshVertices) {
            out_->push_back({});
        }
        out_->resize(first_ + kMeshVertices);
    }

private:
    std::vector<MeshVertex>* out_;
    std::size_t first_;
};

void add_all(MeshBuilder* mesh, std::span<const MeshTriangle> triangles)
{
    for (const MeshTriangle& made : triangles) {
        mesh->triangle(made);
    }
    mesh->finish();
}

// A leaf (length 1 along z, its tip at +z): a diamond curled up at the sides, a midrib, a stalk.
constexpr V3 kLeafTip{0.0F, 0.06F, kHalf};
constexpr V3 kLeafStem{0.0F, 0.03F, -kHalf};
constexpr V3 kLeafLeft{-0.26F, 0.12F, 0.02F};
constexpr V3 kLeafRight{0.26F, 0.12F, 0.02F};
constexpr V3 kLeafMiddle{0.0F, 0.02F, 0.0F};
constexpr std::array<MeshTriangle, 5> kLeaf{{
    {.first = kLeafStem, .second = kLeafMiddle, .third = kLeafLeft, .shade = 0.9F, .part = 0.0F},
    {.first = kLeafStem, .second = kLeafRight, .third = kLeafMiddle, .shade = 0.9F, .part = 0.0F},
    {.first = kLeafMiddle, .second = kLeafTip, .third = kLeafLeft, .shade = 1.0F, .part = 0.0F},
    {.first = kLeafMiddle, .second = kLeafRight, .third = kLeafTip, .shade = 1.0F, .part = 0.0F},
    {.first = kLeafStem, .second = {0.0F, 0.02F, -0.7F}, .third = {0.02F, 0.03F, -kHalf}, .shade = 0.7F, .part = 0.0F},
}};

// A paper scrap (1 x 0.8): a crumpled 3 x 3 sheet.
constexpr std::array<float, 9> kPaperHeights{0.02F, 0.12F, 0.03F, 0.1F, 0.18F, 0.06F, 0.01F, 0.09F, 0.04F};
constexpr float kPaperDepth = 0.8F;
constexpr float kPaperShade = 0.85F;
constexpr float kPaperShadeStep = 0.15F;
constexpr float kPaperBackShade = 0.95F;

void paper(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    const auto corner = [](std::size_t across, std::size_t deep) {
        return V3{(static_cast<float>(across) * kHalf) - kHalf, core::at(kPaperHeights, (deep * 3) + across),
                  ((static_cast<float>(deep) * kHalf) - kHalf) * kPaperDepth};
    };
    for (std::size_t deep = 0; deep < 2; ++deep) {
        for (std::size_t across = 0; across < 2; ++across) {
            const float shade = kPaperShade + (kPaperShadeStep * static_cast<float>((across + deep) % 2));
            mesh.triangle({.first = corner(across, deep),
                           .second = corner(across, deep + 1),
                           .third = corner(across + 1, deep),
                           .shade = shade,
                           .part = 0.0F});
            mesh.triangle({.first = corner(across + 1, deep),
                           .second = corner(across, deep + 1),
                           .third = corner(across + 1, deep + 1),
                           .shade = shade * kPaperBackShade,
                           .part = 0.0F});
        }
    }
    mesh.finish();
}

// A can (length 1 along x, radius 0.27) lying on its side: six sides (the label, part 1) and one end.
constexpr int kCanSides = 6;
constexpr float kCanRadius = 0.27F;
constexpr int kCanEndTriangles = 4;
constexpr float kCanEndShade = 0.8F;

void can(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    const auto ring = [](int side, float x) {
        const float angle = kTau * static_cast<float>(side) / static_cast<float>(kCanSides);
        return V3{x, kCanRadius + (kCanRadius * std::cos(angle)), kCanRadius * std::sin(angle)};
    };
    for (int k = 0; k < kCanSides; ++k) {
        const V3 near_one = ring(k, -kHalf);
        const V3 near_two = ring(k + 1, -kHalf);
        const V3 far_one = ring(k, kHalf);
        const V3 far_two = ring(k + 1, kHalf);
        mesh.triangle({.first = near_one, .second = far_one, .third = near_two, .shade = 1.0F, .part = 1.0F});
        mesh.triangle({.first = near_two, .second = far_one, .third = far_two, .shade = 1.0F, .part = 1.0F});
    }
    const V3 centre{kHalf, kCanRadius, 0.0F};
    for (int k = 0; k < kCanEndTriangles; ++k) {
        mesh.triangle({.first = centre,
                       .second = ring((k * 2) % kCanSides, kHalf),
                       .third = ring(((k * 2) + 2) % kCanSides, kHalf),
                       .shade = kCanEndShade,
                       .part = 0.0F});
    }
    mesh.finish();
}

// A weed (height 1): seven blades leaning out, four leaves lying round its foot.
struct WeedArt {
    int blades{};
    float stagger{};    // every other blade turned this much more (radians)
    float least_tall{}; // a blade's height: this and up to `tall_spread` more
    float tall_spread{};
    float least_lean{}; // its lean out (units at the tip) and its spread
    float lean_spread{};
    float blade_width{};
    float blade_shade{}; // its shade: this + `blade_light` x its height
    float blade_light{};
    uint32_t seed{};
    int leaves{};
    float leaf_turn{}; // the leaves' turn (radians), their length and width, their heights
    float leaf_length{};
    float leaf_width{};
    float leaf_base{};
    float leaf_tip{};
    float leaf_shade{};
};
constexpr WeedArt kWeed{.blades = 7,
                        .stagger = 0.3F,
                        .least_tall = 0.55F,
                        .tall_spread = 0.45F,
                        .least_lean = 0.25F,
                        .lean_spread = 0.25F,
                        .blade_width = 0.06F,
                        .blade_shade = 0.8F,
                        .blade_light = 0.2F,
                        .seed = 17U,
                        .leaves = 4,
                        .leaf_turn = 0.4F,
                        .leaf_length = 0.45F,
                        .leaf_width = 0.1F,
                        .leaf_base = 0.03F,
                        .leaf_tip = 0.08F,
                        .leaf_shade = 0.75F};

void weed(std::vector<MeshVertex>* out)
{
    const WeedArt art = kWeed;
    MeshBuilder mesh(out);
    for (int k = 0; k < art.blades; ++k) {
        const float angle =
            (kTau * static_cast<float>(k) / static_cast<float>(art.blades)) + (art.stagger * static_cast<float>(k % 2));
        const float tall = art.least_tall + (art.tall_spread * hash_unit(art.seed, static_cast<uint32_t>(k), 0U));
        const float lean = art.least_lean + (art.lean_spread * hash_unit(art.seed, static_cast<uint32_t>(k), 1U));
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        mesh.triangle({.first = {-sine * art.blade_width, 0.0F, cosine * art.blade_width},
                       .second = {sine * art.blade_width, 0.0F, -cosine * art.blade_width},
                       .third = {cosine * lean, tall, sine * lean},
                       .shade = art.blade_shade + (art.blade_light * tall),
                       .part = 0.0F});
    }
    for (int k = 0; k < art.leaves; ++k) {
        const float angle = (kTau * static_cast<float>(k) / static_cast<float>(art.leaves)) + art.leaf_turn;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        mesh.triangle({.first = {-sine * art.leaf_width, art.leaf_base, cosine * art.leaf_width},
                       .second = {sine * art.leaf_width, art.leaf_base, -cosine * art.leaf_width},
                       .third = {cosine * art.leaf_length, art.leaf_tip, sine * art.leaf_length},
                       .shade = art.leaf_shade,
                       .part = 0.0F});
    }
    mesh.finish();
}

// A pebble (about 1 across, 0.55 tall): an octahedron, its corners pushed about.
constexpr V3 kPebbleTop{0.05F, 0.55F, -0.04F};
constexpr V3 kPebbleBottom{0.0F, 0.0F, 0.0F};
constexpr std::array<V3, 4> kPebbleWaist{
    {{kHalf, 0.22F, 0.05F}, {0.04F, 0.25F, 0.46F}, {-0.47F, 0.2F, -0.03F}, {0.0F, 0.24F, -0.52F}}};
constexpr float kPebbleUnderShade = 0.6F;

void pebble(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    for (std::size_t k = 0; k < kPebbleWaist.size(); ++k) {
        const V3& one = core::at(kPebbleWaist, k);
        const V3& two = core::at(kPebbleWaist, (k + 1) % kPebbleWaist.size());
        mesh.triangle({.first = one, .second = kPebbleTop, .third = two, .shade = 1.0F, .part = 0.0F});
        mesh.triangle({.first = two, .second = kPebbleBottom, .third = one, .shade = kPebbleUnderShade, .part = 0.0F});
    }
    mesh.finish();
}

constexpr uint32_t kKindStride = 1000; // (a tile's seed: its kind's x this + its variant's x kVariantStride + 1)
constexpr uint32_t kVariantStride = 7;

} // namespace

std::vector<uint8_t> make_decal_atlas()
{
    std::vector<uint8_t> atlas(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * kTexel, 0);
    for (uint32_t kind = 0; kind < kDecalKinds; ++kind) {
        for (uint32_t variant = 0; variant < kDecalVariants; ++variant) {
            ArtTile tile;
            core::at(kPainters, kind)(&tile, (kind * kKindStride) + (variant * kVariantStride) + 1U);
            tile.into_atlas(&atlas, {variant, kind});
        }
    }
    return atlas;
}

std::vector<MeshVertex> make_meshes()
{
    std::vector<MeshVertex> meshes;
    meshes.reserve(kItemKinds * kMeshVertices);
    {
        MeshBuilder leaf(&meshes);
        add_all(&leaf, kLeaf);
    }
    paper(&meshes);
    can(&meshes);
    weed(&meshes);
    pebble(&meshes);
    return meshes;
}

} // namespace render::scatter
