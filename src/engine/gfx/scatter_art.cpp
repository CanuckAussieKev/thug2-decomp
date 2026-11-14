// NOT ORIGINAL - decals and clutter: the decals' atlas and the clutter's meshes, made by code (scatter.h) - nothing
// fetched, nothing third-party. Every texel and vertex comes from integer hashes: the same bytes every time.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/scatter.h"

namespace render::scatter {

namespace {

constexpr float kTau = 6.28318530718F;
constexpr float kUnit = 1.0F / 4294967296.0F;
constexpr float kHalf = 0.5F;
constexpr std::size_t kTexel = 4;
constexpr float kEdgeFade = 0.04F; // (of a tile: its art fades to nothing at its border)

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

float hash_unit(uint32_t a, uint32_t b, uint32_t c)
{
    constexpr uint32_t kB = 0x9E3779B9U;
    constexpr uint32_t kC = 0x85EBCA6BU;
    return static_cast<float>(mix(a ^ mix(b * kB) ^ mix(c * kC + 1U))) * kUnit;
}

float smooth(float low, float high, float value)
{
    const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

// Value noise over a wrapping lattice of `period` cells (0 - 1), seeded.
float value_noise(float x, float y, uint32_t seed, int period)
{
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float tx = x - fx;
    const float ty = y - fy;
    const auto wrap = [period](float cell) {
        const int c = static_cast<int>(cell) % period;
        return static_cast<uint32_t>(c < 0 ? c + period : c);
    };
    const uint32_t x0 = wrap(fx);
    const uint32_t x1 = wrap(fx + 1.0F);
    const uint32_t y0 = wrap(fy);
    const uint32_t y1 = wrap(fy + 1.0F);
    const float sx = tx * tx * (3.0F - (2.0F * tx));
    const float sy = ty * ty * (3.0F - (2.0F * ty));
    const float a = hash_unit(seed, x0, y0);
    const float b = hash_unit(seed, x1, y0);
    const float c = hash_unit(seed, x0, y1);
    const float d = hash_unit(seed, x1, y1);
    return (a + ((b - a) * sx)) + (((c + ((d - c) * sx)) - (a + ((b - a) * sx))) * sy);
}

// Fractal noise (0 - 1 about): `octaves` of value noise from `base` cells across the tile.
float fbm(float u, float v, uint32_t seed, int base, int octaves)
{
    float sum = 0.0F;
    float weight = kHalf;
    float total = 0.0F;
    int period = base;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += weight * value_noise(u * static_cast<float>(period), v * static_cast<float>(period),
                                    seed + static_cast<uint32_t>(octave) * 101U, period);
        total += weight;
        weight *= kHalf;
        period *= 2;
    }
    return sum / total;
}

using Rgb = std::array<float, 3>;

// A tile being drawn: each texel's multiplier (1: untouched).
class ArtTile {
public:
    ArtTile() : texels_(static_cast<std::size_t>(kDecalTile) * kDecalTile, Rgb{1.0F, 1.0F, 1.0F}) {}
    Rgb& at(uint32_t x, uint32_t y) { return core::at(texels_, (static_cast<std::size_t>(y) * kDecalTile) + x); }
    // Darken a texel by `amount` (0 - 1) toward `colour` (the multiplier at full).
    void darken(uint32_t x, uint32_t y, float amount, const Rgb& colour)
    {
        Rgb& texel = at(x, y);
        const float a = std::clamp(amount, 0.0F, 1.0F);
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(texel, c) *= 1.0F - (a * (1.0F - core::at(colour, c)));
        }
    }
    void into_atlas(std::vector<uint8_t>* atlas, uint32_t column, uint32_t row) const
    {
        for (uint32_t y = 0; y < kDecalTile; ++y) {
            for (uint32_t x = 0; x < kDecalTile; ++x) {
                const float u = (static_cast<float>(x) + kHalf) / static_cast<float>(kDecalTile);
                const float v = (static_cast<float>(y) + kHalf) / static_cast<float>(kDecalTile);
                const float edge = smooth(0.0F, kEdgeFade, std::min({u, v, 1.0F - u, 1.0F - v}));
                const Rgb& texel = core::at(texels_, (static_cast<std::size_t>(y) * kDecalTile) + x);
                const std::size_t at =
                    ((((static_cast<std::size_t>(row) * kDecalTile) + y) * kAtlasWidth) + (column * kDecalTile) + x) *
                    kTexel;
                for (std::size_t c = 0; c < 3; ++c) {
                    core::at(*atlas, at + c) =
                        static_cast<uint8_t>(std::lround(std::clamp(core::at(texel, c), 0.0F, 1.0F) * 255.0F));
                }
                core::at(*atlas, at + 3) = static_cast<uint8_t>(std::lround(edge * 255.0F));
            }
        }
    }

private:
    std::vector<Rgb> texels_;
};

template <typename Paint>
void each_texel(Paint paint)
{
    for (uint32_t y = 0; y < kDecalTile; ++y) {
        for (uint32_t x = 0; x < kDecalTile; ++x) {
            paint(x, y, (static_cast<float>(x) + kHalf) / static_cast<float>(kDecalTile),
                  (static_cast<float>(y) + kHalf) / static_cast<float>(kDecalTile));
        }
    }
}

// A segment of a crack (tile units 0 - 1 across, 0 - `aspect` along) and its width (texels).
struct Segment {
    std::array<float, 2> a;
    std::array<float, 2> b;
    float width;
};

float segment_distance(const Segment& s, float x, float y)
{
    const float dx = core::at(s.b, 0) - core::at(s.a, 0);
    const float dy = core::at(s.b, 1) - core::at(s.a, 1);
    const float span = (dx * dx) + (dy * dy);
    float t = 0.0F;
    if (span > 1.0e-9F) {
        t = std::clamp((((x - core::at(s.a, 0)) * dx) + ((y - core::at(s.a, 1)) * dy)) / span, 0.0F, 1.0F);
    }
    const float px = core::at(s.a, 0) + (t * dx) - x;
    const float py = core::at(s.a, 1) + (t * dy) - y;
    return std::sqrt((px * px) + (py * py));
}

// A wandering line from (x, y) heading `angle`, `steps` steps of `step`, its width tapering from `width`.
void wander(std::vector<Segment>* out, float x, float y, float angle, int steps, float step, float width, uint32_t seed)
{
    for (int k = 0; k < steps; ++k) {
        constexpr float kWobble = 0.9F;
        angle += (hash_unit(seed, static_cast<uint32_t>(k), 7U) - kHalf) * kWobble;
        const float nx = x + (std::sin(angle) * step);
        const float ny = y + (std::cos(angle) * step);
        const float taper = 1.0F - (0.7F * static_cast<float>(k) / static_cast<float>(steps));
        out->push_back({.a = {x, y}, .b = {nx, ny}, .width = width * taper});
        x = nx;
        y = ny;
    }
}

constexpr float kTexels = static_cast<float>(kDecalTile);

void paint_crack(ArtTile* tile, uint32_t seed)
{
    constexpr float kAspect = 1.9F; // (along / across, the sizes' middle)
    std::vector<Segment> segments;
    const float start = 0.35F + (0.3F * hash_unit(seed, 1U, 0U));
    wander(&segments, start, 0.06F * kAspect, (hash_unit(seed, 2U, 0U) - kHalf) * 0.4F, 18, 0.05F * kAspect,
           2.2F + hash_unit(seed, 3U, 0U), seed);
    const auto branches = 2U + static_cast<uint32_t>(hash_unit(seed, 4U, 0U) * 3.0F);
    for (uint32_t b = 0; b < branches; ++b) {
        const Segment& from = core::at(segments, 3 + static_cast<std::size_t>(hash_unit(seed, 5U, b) * 12.0F));
        const float side = hash_unit(seed, 6U, b) < kHalf ? -1.0F : 1.0F;
        wander(&segments, core::at(from.b, 0), core::at(from.b, 1), side * (0.6F + (0.6F * hash_unit(seed, 8U, b))),
               5 + static_cast<int>(hash_unit(seed, 9U, b) * 6.0F), 0.04F * kAspect, from.width * 0.6F,
               seed + 31U * (b + 1U));
    }
    const Rgb dark{0.16F, 0.15F, 0.14F};
    const Rgb dirt{0.62F, 0.6F, 0.57F};
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        const float along = v * kAspect;
        float nearest = 1.0e9F;
        float width = 1.0F;
        for (const Segment& s : segments) {
            const float d = segment_distance(s, u, along) * kTexels;
            if (d < nearest) {
                nearest = d;
                width = s.width;
            }
        }
        const float grain = fbm(u, v, seed + 17U, 8, 3);
        tile->darken(x, y, smooth(width + 1.2F, width * 0.4F, nearest) * (0.75F + (0.25F * grain)), dark);
        tile->darken(x, y, 0.35F * std::exp(-nearest / 5.0F) * grain, dirt);
    });
}

// A blob's mask (0 - 1): a disc of `radius` (tile units) roughened by noise.
float blob(float u, float v, uint32_t seed, float radius, float rough, float soft)
{
    const float du = u - kHalf;
    const float dv = v - kHalf;
    const float d = std::sqrt((du * du) + (dv * dv));
    const float edge = radius * (1.0F + (rough * (fbm(u, v, seed, 4, 4) - kHalf) * 2.0F));
    return smooth(edge, edge - soft, d);
}

void paint_stain(ArtTile* tile, uint32_t seed)
{
    const Rgb brown{0.62F, 0.57F, 0.5F};
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        // (a soft, uneven blot: darker in patches, its edge a faint tide line)
        const float mask = blob(u, v, seed, 0.34F, 0.7F, 0.2F);
        const float inside = fbm(u, v, seed + 5U, 5, 4);
        const float patches = smooth(0.35F, 0.75F, fbm(u, v, seed + 11U, 3, 3));
        const float ring = blob(u, v, seed, 0.34F, 0.7F, 0.03F) - blob(u, v, seed, 0.31F, 0.7F, 0.04F);
        tile->darken(x, y, (mask * ((0.25F * inside) + (0.45F * patches))) + (0.2F * std::max(ring, 0.0F)), brown);
    });
}

void paint_oil(ArtTile* tile, uint32_t seed)
{
    const Rgb oil{0.2F, 0.2F, 0.23F};
    const Rgb sheen{0.75F, 0.72F, 0.8F};
    const auto drops = 3U + static_cast<uint32_t>(hash_unit(seed, 1U, 0U) * 5.0F);
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        const float mask = blob(u, v, seed, 0.28F, 0.35F, 0.1F);
        const float inside = fbm(u, v, seed + 9U, 4, 3);
        tile->darken(x, y, mask * (0.7F + (0.3F * inside)), oil);
        tile->darken(x, y, blob(u, v, seed + 3U, 0.4F, 0.4F, 0.15F) * 0.4F, sheen);
        for (uint32_t k = 0; k < drops; ++k) {
            const float angle = hash_unit(seed, 2U, k) * kTau;
            const float out = 0.3F + (0.15F * hash_unit(seed, 3U, k));
            const float cx = kHalf + (std::cos(angle) * out);
            const float cy = kHalf + (std::sin(angle) * out);
            const float r = 0.015F + (0.025F * hash_unit(seed, 4U, k));
            const float d = std::sqrt(((u - cx) * (u - cx)) + ((v - cy) * (v - cy)));
            tile->darken(x, y, smooth(r, r * 0.5F, d) * 0.8F, oil);
        }
    });
}

void paint_tyre(ArtTile* tile, uint32_t seed)
{
    const Rgb rubber{0.14F, 0.14F, 0.14F};
    const float bend = (hash_unit(seed, 1U, 0U) - kHalf) * 0.16F;
    const float start = 0.05F + (0.2F * hash_unit(seed, 2U, 0U)); // (a skid builds up, then fades out)
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        const float centre = kHalf + (bend * (v - kHalf) * (v - kHalf) * 4.0F);
        const float across = std::abs(u - centre) * (1.0F + (0.15F * (fbm(0.0F, v, seed + 3U, 6, 3) - kHalf)));
        const float band = smooth(0.34F, 0.24F, across);
        const float ends = smooth(start, start + 0.2F, v) * smooth(1.0F, 0.6F, v);
        // (streaks along the skid: the tread's ribs dragged; patchy along it)
        const float streaks = 0.65F + (0.35F * value_noise(u * 48.0F, 0.5F, seed + 5U, 48));
        const float patchy = 0.5F + (0.5F * fbm(u * 0.3F, v, seed + 7U, 4, 4));
        tile->darken(x, y, band * ends * streaks * patchy * 0.8F, rubber);
    });
}

void paint_gum(ArtTile* tile, uint32_t seed)
{
    const Rgb gum{0.4F, 0.4F, 0.38F};
    const auto spots = 5U + static_cast<uint32_t>(hash_unit(seed, 1U, 0U) * 10.0F);
    struct Spot {
        float x, y, r, dark;
    };
    std::vector<Spot> list;
    for (uint32_t k = 0; k < spots; ++k) {
        const float angle = hash_unit(seed, 2U, k) * kTau;
        const float out = 0.38F * std::sqrt(hash_unit(seed, 3U, k));
        list.push_back({.x = kHalf + (std::cos(angle) * out),
                        .y = kHalf + (std::sin(angle) * out),
                        .r = (4.0F + (6.0F * hash_unit(seed, 4U, k))) / kTexels,
                        .dark = 0.45F + (0.3F * hash_unit(seed, 5U, k))});
    }
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        for (std::size_t k = 0; k < list.size(); ++k) {
            const Spot& s = core::at(list, k);
            const float du = u - s.x;
            const float dv = v - s.y;
            const float angle = std::atan2(dv, du);
            const float r = s.r * (1.0F + (0.08F * std::sin((angle * 5.0F) + static_cast<float>(k))));
            const float d = std::sqrt((du * du) + (dv * dv));
            tile->darken(x, y, smooth(r, r * 0.7F, d) * s.dark, gum);
            tile->darken(x, y, smooth(r * 1.5F, r, d) * 0.15F, gum);
        }
    });
}

void paint_wet(ArtTile* tile, uint32_t seed)
{
    const Rgb wet{0.58F, 0.6F, 0.64F};
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        const float mask = blob(u, v, seed, 0.38F, 0.5F, 0.06F);
        const float deep = blob(u, v, seed + 2U, 0.25F, 0.6F, 0.1F);
        tile->darken(x, y, (mask * 0.85F) + (deep * 0.3F), wet);
    });
}

void paint_grime(ArtTile* tile, uint32_t seed)
{
    const Rgb grime{0.5F, 0.47F, 0.4F};
    const auto drips = 6U + static_cast<uint32_t>(hash_unit(seed, 1U, 0U) * 8.0F);
    each_texel([&](uint32_t x, uint32_t y, float u, float v) {
        const float sides = smooth(0.0F, 0.2F, u) * smooth(1.0F, 0.8F, u);
        const float body = std::pow(1.0F - v, 1.6F) * (0.55F + (0.45F * fbm(u, v * 0.5F, seed + 3U, 6, 4)));
        float amount = body * sides;
        for (uint32_t k = 0; k < drips; ++k) {
            const float at = 0.1F + (0.8F * hash_unit(seed, 2U, k));
            const float top = 0.4F + (0.5F * hash_unit(seed, 3U, k));
            const float width = (1.5F + (2.5F * hash_unit(seed, 4U, k))) / kTexels;
            const float wobble = 0.01F * std::sin((v * 20.0F) + static_cast<float>(k));
            const float streak = smooth(width, width * 0.3F, std::abs(u - at - wobble)) * smooth(top, top * 0.6F, v);
            amount = std::max(amount, streak * 0.45F);
        }
        tile->darken(x, y, amount, grime);
    });
}

using Painter = void (*)(ArtTile*, uint32_t);
constexpr std::array<Painter, kDecalKinds> kPainters{&paint_crack, &paint_stain, &paint_oil,  &paint_tyre,
                                                     &paint_gum,   &paint_wet,   &paint_grime};

// --- the meshes ---------------------------------------------------------------------------------------------------

using V3 = std::array<float, 3>;

class MeshBuilder {
public:
    explicit MeshBuilder(std::vector<MeshVertex>* out) : out_(out), first_(out->size()) {}
    void triangle(const V3& a, const V3& b, const V3& c, float shade = 1.0F, float part = 0.0F)
    {
        const V3 ab{core::at(b, 0) - core::at(a, 0), core::at(b, 1) - core::at(a, 1), core::at(b, 2) - core::at(a, 2)};
        const V3 ac{core::at(c, 0) - core::at(a, 0), core::at(c, 1) - core::at(a, 1), core::at(c, 2) - core::at(a, 2)};
        V3 n{(core::at(ab, 1) * core::at(ac, 2)) - (core::at(ab, 2) * core::at(ac, 1)),
             (core::at(ab, 2) * core::at(ac, 0)) - (core::at(ab, 0) * core::at(ac, 2)),
             (core::at(ab, 0) * core::at(ac, 1)) - (core::at(ab, 1) * core::at(ac, 0))};
        const float l = std::sqrt((core::at(n, 0) * core::at(n, 0)) + (core::at(n, 1) * core::at(n, 1)) +
                                  (core::at(n, 2) * core::at(n, 2)));
        if (l > 0.0F) {
            n = {core::at(n, 0) / l, core::at(n, 1) / l, core::at(n, 2) / l};
        }
        if (core::at(n, 1) < 0.0F) {
            n = {-core::at(n, 0), -core::at(n, 1), -core::at(n, 2)}; // (two-sided: the upward side's light)
        }
        for (const V3* p : {&a, &b, &c}) {
            out_->push_back({.position = {core::at(*p, 0), core::at(*p, 1), core::at(*p, 2), shade},
                             .normal = {core::at(n, 0), core::at(n, 1), core::at(n, 2), part}});
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

// A leaf (length 1 along z, its tip at +z): a diamond curled up at the sides, a midrib.
void leaf(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    const V3 tip{0.0F, 0.06F, kHalf};
    const V3 stem{0.0F, 0.03F, -kHalf};
    const V3 left{-0.26F, 0.12F, 0.02F};
    const V3 right{0.26F, 0.12F, 0.02F};
    const V3 mid{0.0F, 0.02F, 0.0F};
    mesh.triangle(stem, mid, left, 0.9F);
    mesh.triangle(stem, right, mid, 0.9F);
    mesh.triangle(mid, tip, left, 1.0F);
    mesh.triangle(mid, right, tip, 1.0F);
    // the stalk
    const V3 end{0.0F, 0.02F, -0.7F};
    mesh.triangle(stem, end, {0.02F, 0.03F, -kHalf}, 0.7F);
    mesh.finish();
}

// A paper scrap (1 x 0.8): a crumpled 3 x 3 sheet.
void paper(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    constexpr std::array<float, 9> kHeights{0.02F, 0.12F, 0.03F, 0.1F, 0.18F, 0.06F, 0.01F, 0.09F, 0.04F};
    const auto at = [&](std::size_t i, std::size_t j) {
        return V3{(static_cast<float>(i) * kHalf) - kHalf, core::at(kHeights, (j * 3) + i),
                  ((static_cast<float>(j) * kHalf) - kHalf) * 0.8F};
    };
    for (std::size_t j = 0; j < 2; ++j) {
        for (std::size_t i = 0; i < 2; ++i) {
            const float shade = 0.85F + (0.15F * static_cast<float>((i + j) % 2));
            mesh.triangle(at(i, j), at(i, j + 1), at(i + 1, j), shade);
            mesh.triangle(at(i + 1, j), at(i, j + 1), at(i + 1, j + 1), shade * 0.95F);
        }
    }
    mesh.finish();
}

// A can (length 1 along x, radius 0.27) lying on its side: six sides (the label) and one end.
void can(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    constexpr int kSides = 6;
    constexpr float kRadius = 0.27F;
    const auto ring = [&](int k, float x) {
        const float angle = kTau * static_cast<float>(k) / static_cast<float>(kSides);
        return V3{x, kRadius + (kRadius * std::cos(angle)), kRadius * std::sin(angle)};
    };
    for (int k = 0; k < kSides; ++k) {
        const V3 a = ring(k, -kHalf);
        const V3 b = ring(k + 1, -kHalf);
        const V3 c = ring(k, kHalf);
        const V3 d = ring(k + 1, kHalf);
        mesh.triangle(a, c, b, 1.0F, 1.0F);
        mesh.triangle(b, c, d, 1.0F, 1.0F);
    }
    const V3 centre{kHalf, kRadius, 0.0F};
    for (int k = 0; k < 4; ++k) {
        mesh.triangle(centre, ring(k * 2 % kSides, kHalf), ring((k * 2 + 2) % kSides, kHalf), 0.8F, 0.0F);
    }
    mesh.finish();
}

// A weed (height 1): seven blades leaning out, four leaves lying round its foot.
void weed(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    constexpr int kBlades = 7;
    for (int k = 0; k < kBlades; ++k) {
        const float angle = kTau * static_cast<float>(k) / static_cast<float>(kBlades) + (0.3F * static_cast<float>(k % 2));
        const float tall = 0.55F + (0.45F * hash_unit(17U, static_cast<uint32_t>(k), 0U));
        const float lean = 0.25F + (0.25F * hash_unit(17U, static_cast<uint32_t>(k), 1U));
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        constexpr float kWidth = 0.06F;
        mesh.triangle({-s * kWidth, 0.0F, c * kWidth}, {s * kWidth, 0.0F, -c * kWidth},
                      {c * lean, tall, s * lean}, 0.8F + (0.2F * tall));
    }
    for (int k = 0; k < 4; ++k) {
        const float angle = (kTau * static_cast<float>(k) / 4.0F) + 0.4F;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        constexpr float kLength = 0.45F;
        constexpr float kWidth = 0.1F;
        mesh.triangle({-s * kWidth, 0.03F, c * kWidth}, {s * kWidth, 0.03F, -c * kWidth},
                      {c * kLength, 0.08F, s * kLength}, 0.75F);
    }
    mesh.finish();
}

// A pebble (about 1 across, 0.55 tall): an octahedron, its corners pushed about.
void pebble(std::vector<MeshVertex>* out)
{
    MeshBuilder mesh(out);
    const V3 top{0.05F, 0.55F, -0.04F};
    const V3 bottom{0.0F, 0.0F, 0.0F};
    const std::array<V3, 4> waist{{{kHalf, 0.22F, 0.05F}, {0.04F, 0.25F, 0.46F}, {-0.47F, 0.2F, -0.03F}, {0.0F, 0.24F, -0.52F}}};
    for (std::size_t k = 0; k < 4; ++k) {
        const V3& a = core::at(waist, k);
        const V3& b = core::at(waist, (k + 1) % 4);
        mesh.triangle(a, top, b, 1.0F);
        mesh.triangle(b, bottom, a, 0.6F);
    }
    mesh.finish();
}

} // namespace

std::vector<uint8_t> make_decal_atlas()
{
    std::vector<uint8_t> atlas(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * kTexel, 0);
    for (uint32_t kind = 0; kind < kDecalKinds; ++kind) {
        for (uint32_t variant = 0; variant < kDecalVariants; ++variant) {
            ArtTile tile;
            constexpr uint32_t kKindStride = 1000;
            core::at(kPainters, kind)(&tile, (kind * kKindStride) + (variant * 7U) + 1U);
            tile.into_atlas(&atlas, variant, kind);
        }
    }
    return atlas;
}

std::vector<MeshVertex> make_meshes()
{
    std::vector<MeshVertex> meshes;
    meshes.reserve(kItemKinds * kMeshVertices);
    leaf(&meshes);
    paper(&meshes);
    can(&meshes);
    weed(&meshes);
    pebble(&meshes);
    return meshes;
}

} // namespace render::scatter
