// NOT ORIGINAL - rain: the weather's rain as one physical state (rain.h; docs/design/vulkan.md "Rain").
#include "engine/gfx/rain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"

namespace render::rain {

namespace {

double relief_level_of(double stored);
double extinction_of(double rate);

constexpr double kSecondsPerHour = 3600.0;
constexpr double kMillimetresPerMetre = 1000.0;
constexpr int kIntegrationSteps = 2000; // (the distribution's integrals: Simpson's rule over [least, most])

// The integral of f(D) N(D) dD over [least, kMostDiameter] (D in mm).
template <typename F>
double integrate(double rate, double least, F f)
{
    const double a = std::max(least, kLeastDiameter);
    const double b = kMostDiameter;
    if (!(b > a) || !(rate > 0.0)) {
        return 0.0;
    }
    const double h = (b - a) / kIntegrationSteps;
    double sum = 0.0;
    for (int i = 0; i <= kIntegrationSteps; ++i) {
        const double d = a + (h * i);
        const double weight = (i == 0 || i == kIntegrationSteps) ? 1.0 : ((i % 2) == 1 ? 4.0 : 2.0);
        sum += weight * f(d) * size_density(rate, d);
    }
    return sum * h / 3.0;
}

// Unpolarised Fresnel reflectance going from index n1 into n2 at `cosine` of incidence (1: total internal reflection).
double fresnel(double n1, double n2, double cosine)
{
    const double sine_in = std::sqrt(std::max(1.0 - (cosine * cosine), 0.0));
    const double sine_out = n1 / n2 * sine_in;
    if (sine_out >= 1.0) {
        return 1.0;
    }
    const double cos_out = std::sqrt(1.0 - (sine_out * sine_out));
    const double rs = ((n1 * cosine) - (n2 * cos_out)) / ((n1 * cosine) + (n2 * cos_out));
    const double rp = ((n2 * cosine) - (n1 * cos_out)) / ((n2 * cosine) + (n1 * cos_out));
    return 0.5 * ((rs * rs) + (rp * rp));
}

// The hemispherical (cosine-weighted) average of fresnel(n1, n2): the integral of F 2 cos sin over the hemisphere.
double hemispherical(double n1, double n2)
{
    constexpr int kSteps = 4096;
    double sum = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const double theta = (i + 0.5) / kSteps * (std::numbers::pi / 2.0);
        sum += fresnel(n1, n2, std::cos(theta)) * 2.0 * std::cos(theta) * std::sin(theta);
    }
    return sum * (std::numbers::pi / 2.0) / kSteps;
}

// --- the relief's noise (relief_at; the GLSL twin's rain_relief) -----------------------------------------------------
uint32_t mix_bits(uint32_t x)
{
    constexpr uint32_t kFirst = 0x7FEB352DU;
    constexpr uint32_t kSecond = 0x846CA68BU;
    x ^= x >> 16U;
    x *= kFirst;
    x ^= x >> 15U;
    x *= kSecond;
    x ^= x >> 16U;
    return x;
}

float lattice(int32_t x, int32_t z)
{
    constexpr uint32_t kX = 0x8DA6B343U;
    constexpr uint32_t kZ = 0xD8163841U;
    constexpr float kScale = 16777216.0F;
    return static_cast<float>(mix_bits((static_cast<uint32_t>(x) * kX) ^ (static_cast<uint32_t>(z) * kZ)) >> 8U) /
           kScale;
}

float value_noise(float x, float z)
{
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const auto ix = static_cast<int32_t>(fx);
    const auto iz = static_cast<int32_t>(fz);
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0F - (2.0F * tx));
    tz = tz * tz * (3.0F - (2.0F * tz));
    const float a = lattice(ix, iz) + ((lattice(ix + 1, iz) - lattice(ix, iz)) * tx);
    const float b = lattice(ix, iz + 1) + ((lattice(ix + 1, iz + 1) - lattice(ix, iz + 1)) * tx);
    return a + ((b - a) * tz);
}

// The relief's distribution, measured once: sorted samples of relief_at (raw units) and their deviation.
struct ReliefStats {
    std::vector<float> sorted;
    std::vector<double> prefix; // prefix[k]: the sum of the k smallest
    double deviation = 1.0;
    double raw_slope = 0.0; // the relief's root-mean-square slope (raw units a world unit)
};

const ReliefStats& relief_stats()
{
    static const ReliefStats stats = [] {
        ReliefStats made;
        constexpr int kSide = 256;
        constexpr float kStep = 7.31F; // world units between samples (not a divisor of the lattice: no aliasing)
        made.sorted.reserve(static_cast<std::size_t>(kSide) * kSide);
        double sum = 0.0;
        double squares = 0.0;
        for (int z = 0; z < kSide; ++z) {
            for (int x = 0; x < kSide; ++x) {
                const float r =
                    relief_at((static_cast<float>(x) * kStep) + 0.37F, (static_cast<float>(z) * kStep * 1.07F) + 0.71F);
                made.sorted.push_back(r);
                sum += r;
                squares += static_cast<double>(r) * r;
            }
        }
        const auto count = static_cast<double>(made.sorted.size());
        const double mean = sum / count;
        made.deviation = std::sqrt(std::max((squares / count) - (mean * mean), 1.0e-12));
        // (its slope: central differences a tenth of a unit apart at a share of the samples)
        constexpr float kStep2 = 0.1F;
        double slopes = 0.0;
        int counted = 0;
        for (int z = 0; z < kSide; z += 4) {
            for (int x = 0; x < kSide; x += 4) {
                const float px = (static_cast<float>(x) * kStep) + 0.37F;
                const float pz = (static_cast<float>(z) * kStep * 1.07F) + 0.71F;
                const double gx = (relief_at(px + kStep2, pz) - relief_at(px - kStep2, pz)) / (2.0 * kStep2);
                const double gz = (relief_at(px, pz + kStep2) - relief_at(px, pz - kStep2)) / (2.0 * kStep2);
                slopes += (gx * gx) + (gz * gz);
                ++counted;
            }
        }
        made.raw_slope = std::sqrt(slopes / std::max(counted, 1));
        std::ranges::sort(made.sorted);
        made.prefix.resize(made.sorted.size() + 1, 0.0);
        for (std::size_t i = 0; i < made.sorted.size(); ++i) {
            core::at(made.prefix, i + 1) = core::at(made.prefix, i) + static_cast<double>(core::at(made.sorted, i));
        }
        return made;
    }();
    return stats;
}

// The mean water depth (in raw relief units) over level ground whose water stands at `level`.
double water_over(const ReliefStats& stats, double level)
{
    // (the samples under the level, k of them: k x level less their sum)
    const auto under = std::ranges::lower_bound(stats.sorted, static_cast<float>(level));
    const auto k = static_cast<std::size_t>(under - stats.sorted.begin());
    return ((static_cast<double>(k) * level) - core::at(stats.prefix, k)) / static_cast<double>(stats.sorted.size());
}

// --- the state --------------------------------------------------------------------------------------------------------
struct State {
    std::mutex mutex;
    Settings settings;
    double clock = 0.0;
    double stored = 0.0;
    bool session = false; // the store set for the level session's start (it has been raining - or not - already)
};

State& state()
{
    static State own;
    return own;
}

} // namespace

// --- the settings ---------------------------------------------------------------------------------------------------

void set_settings(const Settings& settings)
{
    Settings clean = settings;
    clean.rate = std::isfinite(clean.rate) ? std::clamp(clean.rate, 0.0, kMostRate) : 0.0;
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    own.settings = clean;
}

Settings settings()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.settings;
}

// --- the drops --------------------------------------------------------------------------------------------------------

double size_slope(double rate)
{
    return rate > 0.0 ? kMarshallPalmerScale * std::pow(rate, kMarshallPalmerPower) : 0.0;
}

double size_density(double rate, double diameter)
{
    if (!(rate > 0.0) || diameter < kLeastDiameter || diameter > kMostDiameter) {
        return 0.0;
    }
    return kMarshallPalmerN0 * std::exp(-size_slope(rate) * diameter);
}

double number_density(double rate) { return number_density_over(rate, kLeastDiameter); }

double number_density_over(double rate, double least)
{
    if (!(rate > 0.0)) {
        return 0.0;
    }
    const double slope = size_slope(rate);
    const double a = std::max(least, kLeastDiameter);
    return kMarshallPalmerN0 / slope * (std::exp(-slope * a) - std::exp(-slope * kMostDiameter));
}

double diameter_at(double rate, double u)
{
    const double slope = size_slope(rate);
    if (!(slope > 0.0)) {
        return kLeastDiameter;
    }
    const double span = 1.0 - std::exp(-slope * (kMostDiameter - kLeastDiameter));
    const double share = std::clamp(u, 0.0, 1.0) * span;
    return std::min(kLeastDiameter - (std::log(1.0 - share) / slope), kMostDiameter);
}

double median_volume_diameter(double rate)
{
    constexpr double kMedianVolume = 3.67; // (the exponential's: half the water in drops smaller than 3.67 / Lambda)
    const double slope = size_slope(rate);
    return slope > 0.0 ? kMedianVolume / slope : 0.0;
}

double fall_speed(double diameter)
{
    constexpr double kTop = 9.65;
    constexpr double kSpan = 10.3;
    constexpr double kShape = 0.6;
    return std::max(kTop - (kSpan * std::exp(-kShape * diameter)), 0.0);
}

double rate_from_drops(double rate)
{
    // R = 6 pi 1e-4 x the integral of D^3 v(D) N(D) dD (mm/h; D in mm, v in m/s, N in m^-3 mm^-1)
    constexpr double kRateFactor = 6.0 * std::numbers::pi * 1.0e-4;
    return kRateFactor * integrate(rate, kLeastDiameter, [](double d) { return d * d * d * fall_speed(d); });
}

// (the integrals at the last rate asked kept: the frame's passes and the fog ask every frame)
struct RateCache {
    double rate = -1.0;
    double least = -1.0;
    double value = 0.0;
};

double extinction(double rate)
{
    thread_local RateCache cache;
    if (cache.rate == rate) {
        return cache.value;
    }
    cache.rate = rate;
    cache.value = extinction_of(rate);
    return cache.value;
}

namespace {
double extinction_of(double rate)
{
    // 2 (the extinction efficiency) x pi/4 D^2 (mm^2) N (m^-3 mm^-1) dD: mm^2 m^-3 = 1e-6 per metre
    constexpr double kMillimetreSquared = 1.0e-6;
    return 2.0 * (std::numbers::pi / 4.0) * kMillimetreSquared *
           integrate(rate, kLeastDiameter, [](double d) { return d * d; });
}
} // namespace

double impact_rate(double rate, double least)
{
    thread_local std::array<RateCache, 2> cache{};
    for (RateCache& kept : cache) {
        if (kept.rate == rate && kept.least == least) {
            return kept.value;
        }
    }
    std::swap(core::at(cache, 0), core::at(cache, 1));
    core::at(cache, 0) = {.rate = rate, .least = least, .value = integrate(rate, least, [](double d) { return fall_speed(d); })};
    return core::at(cache, 0).value;
}

// --- the wind on them -------------------------------------------------------------------------------------------------

double wind_at(double wind_10m, double height)
{
    return wind::profile(wind_10m, height);
}

Fall fall(double rate, double wind_10m, const std::array<double, 2>& downwind)
{
    Fall made;
    const double speed = fall_speed(std::max(median_volume_diameter(rate), kLeastDiameter)); // m/s down
    const double across = wind_at(wind_10m, kDropHeight);                                    // m/s along the wind
    const double length = std::sqrt((speed * speed) + (across * across));
    if (!(length > 0.0)) {
        return made;
    }
    made.direction = {core::at(downwind, 0) * across / length, -speed / length,
                      core::at(downwind, 1) * across / length};
    made.speed = length * kUnitsPerMetre;
    made.slant = speed > 0.0 ? std::array<double, 2>{core::at(downwind, 0) * across / speed,
                                                     core::at(downwind, 1) * across / speed}
                             : std::array<double, 2>{};
    return made;
}

// --- the sky ----------------------------------------------------------------------------------------------------------

double cloud_cover(double rate) { return rate > 0.0 ? 1.0 : 0.0; }

// --- the water on the ground ------------------------------------------------------------------------------------------

double film_thickness(double rate, double sine, double stored)
{
    const double kept = std::max(stored, 0.0);
    if (!(sine > 1.0e-4)) {
        return kept;
    }
    // the rain's supply q (m/s) over a plane kFlowLength long: h = (3 mu q L / (rho g sin))^(1/3)
    const double supply = std::max(rate, 0.0) / kMillimetresPerMetre / kSecondsPerHour;
    const double metres = std::cbrt(3.0 * kWaterViscosity * supply * kFlowLength / (kWaterDensity * kGravity * sine));
    return std::min(metres * kMillimetresPerMetre, kept);
}

float relief_at(float x, float z)
{
    constexpr float kOctave1 = 2.13F;
    constexpr float kOctave2 = 4.37F;
    constexpr float kMean = 0.875F; // (1 + 0.5 + 0.25) x 0.5
    const auto scale = static_cast<float>(1.0 / (kReliefLength * kUnitsPerMetre));
    const float px = x * scale;
    const float pz = z * scale;
    return value_noise(px, pz) + (0.5F * value_noise((px * kOctave1) + 17.1F, (pz * kOctave1) + 5.3F)) +
           (0.25F * value_noise((px * kOctave2) + 3.7F, (pz * kOctave2) + 29.9F)) - kMean;
}

double relief_level(double stored)
{
    // (the last answer kept: the stored water changes slowly, every frame's blocks ask)
    thread_local double last_stored = -1.0;
    thread_local double last_level = 0.0;
    if (stored == last_stored) {
        return last_level;
    }
    last_stored = stored;
    last_level = relief_level_of(stored);
    return last_level;
}

namespace {
double relief_level_of(double stored)
{
    const ReliefStats& stats = relief_stats();
    // raw relief units a millimetre: kReliefDeviation mm is the relief's own deviation
    const double raw_per_mm = stats.deviation / kReliefDeviation;
    const double wanted = std::max(stored, 0.0) * raw_per_mm;
    double low = static_cast<double>(stats.sorted.front()) - 1.0;
    double high = static_cast<double>(stats.sorted.back()) + 8.0;
    if (!(wanted > 0.0)) {
        return low;
    }
    constexpr int kHalvings = 48;
    for (int i = 0; i < kHalvings; ++i) {
        const double middle = 0.5 * (low + high);
        (water_over(stats, middle) < wanted ? low : high) = middle;
    }
    return 0.5 * (low + high);
}
} // namespace

double relief_slope()
{
    const ReliefStats& stats = relief_stats();
    const double mm_per_raw = kReliefDeviation / stats.deviation;
    return stats.raw_slope * mm_per_raw / (kMillimetresPerMetre / kUnitsPerMetre);
}

double flat_share(double film)
{
    return std::clamp(std::max(film, 0.0) / (2.0 * kTextureDepth), 0.0, 1.0);
}

double puddle_cover(double stored)
{
    const ReliefStats& stats = relief_stats();
    const double level = relief_level(stored);
    const auto under = std::ranges::lower_bound(stats.sorted, static_cast<float>(level));
    return static_cast<double>(under - stats.sorted.begin()) / static_cast<double>(stats.sorted.size());
}

Ground ground_of(double stored, double rate)
{
    Ground ground;
    ground.stored = std::clamp(stored, 0.0, kDepressionStorage);
    ground.pores = std::clamp(ground.stored / kPoreStorage, 0.0, 1.0);
    ground.level = relief_level(ground.stored);
    ground.film_rate = std::max(rate, 0.0);
    return ground;
}

// --- the wet material -------------------------------------------------------------------------------------------------

double water_f0()
{
    const double r = (kWaterIndex - 1.0) / (kWaterIndex + 1.0);
    return r * r;
}

double water_outside_reflectance()
{
    static const double value = hemispherical(1.0, kWaterIndex);
    return value;
}

double water_inside_reflectance()
{
    static const double value = hemispherical(kWaterIndex, 1.0);
    return value;
}

double wet_albedo(double albedo)
{
    const double a = std::clamp(albedo, 0.0, 1.0);
    const double outside = water_outside_reflectance();
    const double inside = water_inside_reflectance();
    return (1.0 - outside) * a * (1.0 - inside) / (1.0 - (a * inside));
}

Material wet_material(const Material& dry, const Wetness& wet)
{
    Material out = dry;
    const double water = std::clamp(std::max({wet.pores * dry.porosity, wet.film, wet.puddle}), 0.0, 1.0);
    for (std::size_t c = 0; c < 3; ++c) {
        const double a = core::at(dry.albedo, c);
        core::at(out.albedo, c) = a + ((wet_albedo(a) - a) * water);
    }
    const double cover = std::clamp(std::max(wet.flat, wet.puddle), 0.0, 1.0);
    out.roughness = dry.roughness + ((kWaterRoughness - dry.roughness) * cover);
    out.f0 = dry.f0 + ((water_f0() - dry.f0) * cover);
    return out;
}

rt::brdf::Material wet_brdf_material(const rt::brdf::Material& dry, double porosity, const Wetness& wet)
{
    rt::brdf::Material out = dry;
    const Material changed =
        wet_material({.albedo = {core::at(dry.albedo, 0), core::at(dry.albedo, 1), core::at(dry.albedo, 2)},
                      .roughness = dry.roughness,
                      .f0 = (core::at(dry.f0, 0) + core::at(dry.f0, 1) + core::at(dry.f0, 2)) / 3.0,
                      .porosity = porosity},
                     wet);
    const double cover = std::clamp(std::max(wet.flat, wet.puddle), 0.0, 1.0);
    for (std::size_t c = 0; c < 3; ++c) {
        core::at(out.albedo, c) = static_cast<float>(core::at(changed.albedo, c));
        core::at(out.f0, c) = static_cast<float>(core::at(dry.f0, c) + ((water_f0() - core::at(dry.f0, c)) * cover));
    }
    out.roughness = static_cast<float>(changed.roughness);
    return out;
}

double wet_reflectance(double albedo, double water_share)
{
    const double a = std::clamp(albedo, 0.0, 1.0);
    const double share = std::clamp(water_share, 0.0, 1.0);
    const double f0 = water_f0();
    const double inside = water_inside_reflectance();
    const double under =
        (1.0 - f0) * a * (1.0 - inside) / (1.0 - (a * inside)); // (the light from straight above enters
                                                                // past F(0); what returns escapes by 1 - ri)
    return (share * (f0 + under)) + ((1.0 - share) * a);
}

double porosity_of(double roughness)
{
    constexpr double kSmooth = 0.3;
    constexpr double kRough = 0.6;
    const double t = std::clamp((roughness - kSmooth) / (kRough - kSmooth), 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

// --- the rings ---------------------------------------------------------------------------------------------------------

Ripples ripples_of(double rate)
{
    Ripples ripples;
    const double strikes = impact_rate(rate, kRippleLeast); // a square metre a second
    if (!(strikes > 0.0)) {
        return ripples;
    }
    // each layer's cell one ring a life: layers / (cell^2 x life) = strikes; a ring runs out to at most half a cell in its
    // life (so the four cells nearest a point hold every ring over it): life = cell / (2 speed), at most kRippleLife -
    // cell^3 = 2 layers speed / strikes
    double metres = std::cbrt(2.0 * static_cast<double>(ripples.layers) * kRippleSpeed / strikes);
    ripples.life = std::min(metres / (2.0 * kRippleSpeed), kRippleLife);
    metres = std::sqrt(static_cast<double>(ripples.layers) / (strikes * ripples.life));
    ripples.cell = metres * kUnitsPerMetre;
    return ripples;
}

// --- the GLSL twin --------------------------------------------------------------------------------------------------

const char* ripple_glsl()
{
    // (mix_bits' twin; the rings: no block read - the water's shader takes them too). (round MIST) Guarded: every ray pass
    // has it (vulkan_rt_scene.h cutout_rays_glsl - the water's surface), and some add it again for their own use.
    return R"(
#ifndef RAIN_RIPPLE_GLSL
#define RAIN_RIPPLE_GLSL
uint rain_mix(uint x)
{
    x ^= x >> 16u;
    x *= 0x7FEB352Du;
    x ^= x >> 15u;
    x *= 0x846CA68Bu;
    x ^= x >> 16u;
    return x;
}
// the rings' slope at a place (x, z units): `ripple` x the cell (units), y the layers, z the clock (s), w the life (s)
vec2 rain_ripple(vec2 xz, vec4 ripple)
{
    vec2 slope = vec2(0.0);
    if (ripple.x <= 0.0) return slope;
    for (int l = 0; l < 4; ++l) {
        if (float(l) >= ripple.y) break;
        vec2 p = xz / ripple.x + vec2(float(l) * 0.37, float(l) * 0.61);
        vec2 base = floor(p - 0.5); // (the four cells nearest: a ring reaches at most half a cell)
        for (int j = 0; j <= 1; ++j) {
            for (int i = 0; i <= 1; ++i) {
                ivec2 c = ivec2(base) + ivec2(i, j);
                uint h = rain_mix((uint(c.x) * 0x9E3779B1u) ^ (uint(c.y) * 0x85EBCA77u) ^ (uint(l) * 0xC2B2AE3Du));
                float phase = float(h >> 8u) / 16777216.0;
                float cycles = ripple.z / ripple.w + phase;
                float age = fract(cycles);
                uint g = rain_mix(h ^ (uint(int(floor(cycles))) * 0x27D4EB2Fu));
                vec2 centre = vec2(c) + vec2(float(g & 0xFFFFu), float(g >> 16u)) / 65536.0;
                vec2 d = (p - centre) * ripple.x;
                float r = length(d);
                float x = (r - RAIN_RIPPLE_SPEED * age * ripple.w) / RAIN_RIPPLE_WIDTH;
                float dh = RAIN_RIPPLE_SLOPE * (1.0 - age) * -2.0 * x * exp(-x * x) / 0.8578; // (2x e^-x^2 peaks at 0.8578)
                slope += r > 1.0e-4 ? dh * d / r : vec2(0.0);
            }
        }
    }
    return slope;
}
#endif
)";
}

const char* surface_glsl()
{
    // (relief_at, lattice, film_thickness, wet_albedo's twins; `rain` the uniform block; after ripple_glsl)
    return R"(
float rain_lattice(ivec2 c) { return float(rain_mix((uint(c.x) * 0x8DA6B343u) ^ (uint(c.y) * 0xD8163841u)) >> 8u) / 16777216.0; }
float rain_value(vec2 p)
{
    vec2 f = floor(p);
    ivec2 i = ivec2(f);
    vec2 t = p - f;
    t = t * t * (3.0 - 2.0 * t);
    float a = mix(rain_lattice(i), rain_lattice(i + ivec2(1, 0)), t.x);
    float b = mix(rain_lattice(i + ivec2(0, 1)), rain_lattice(i + ivec2(1, 1)), t.x);
    return mix(a, b, t.y);
}
float rain_relief(vec2 xz)
{
    vec2 p = xz * RAIN_RELIEF_SCALE;
    return rain_value(p) + 0.5 * rain_value(p * 2.13 + vec2(17.1, 5.3)) + 0.25 * rain_value(p * 4.37 + vec2(3.7, 29.9)) - 0.875;
}
// the steady film (mm) on a slope `sine` under `rate` mm/h, at most `stored` (film_thickness's twin)
float rain_film(float rate, float sine, float stored)
{
    if (sine <= 1.0e-4) return stored;
    float supply = max(rate, 0.0) / 3.6e6;
    return min(pow(3.0 * RAIN_VISCOSITY * supply * RAIN_FLOW_LENGTH / (RAIN_DENSITY * RAIN_GRAVITY * sine), 1.0 / 3.0) * 1000.0, stored);
}
// Lekner and Dorf's darkening of a linear albedo under water (wet_albedo's twin)
vec3 rain_wet_albedo(vec3 a)
{
    a = clamp(a, 0.0, 1.0);
    return (1.0 - RAIN_OUTSIDE) * a * (1.0 - RAIN_INSIDE) / (1.0 - a * RAIN_INSIDE);
}
float rain_porosity(float roughness) { return smoothstep(0.3, 0.6, roughness); }
// The water at a surface point `world` (units) facing `n` that the rain reaches by `reached` (0 - 1: the precipitation
// occlusion map's) and strikes by `flux` (its face against the fall, over level ground's): x the pores filled, y the
// film's share (a whole film from RAIN_FILM_WHOLE mm), z the standing water's share (the relief under the level), w the
// water's depth there (mm).
vec4 rain_wetness(vec3 world, vec3 n, float reached, float flux)
{
    float wet = reached * clamp(flux, 0.0, 1.0);
    if (wet <= 0.0) return vec4(0.0);
    float stored = rain.ground.x * wet;
    float sine = sqrt(max(1.0 - n.y * n.y, 0.0));
    float pores = clamp(stored / RAIN_PORE_STORAGE, 0.0, 1.0);
    // (on level ground the film drains over the relief's own slopes between the puddles)
    float film_mm = rain_film(rain.ground.w * wet, max(sine, RAIN_RELIEF_SLOPE), stored);
    float film = clamp(film_mm / RAIN_FILM_WHOLE, 0.0, 1.0);
    float puddle = 0.0;
    if (n.y > 0.9 && rain.ground.z > -7.0) {
        // (a slope lowers the water it can hold over its relief: by its fall over half the relief's length)
        float level = rain.ground.z - sine * RAIN_SLOPE_LOSS;
        float r = rain_relief(world.xz);
        puddle = smoothstep(r - RAIN_PUDDLE_EDGE, r + RAIN_PUDDLE_EDGE, level) * reached;
    }
    // the water's flat share: the puddles, or the film where it rises over the surface's texture
    float flat_share = max(puddle, clamp(film_mm / (2.0 * RAIN_TEXTURE_DEPTH), 0.0, 1.0));
    return vec4(pores, film, puddle, flat_share);
}
)";
}

const char* glsl_constants()
{
    static const std::string text = [] {
        const ReliefStats& stats = relief_stats();
        const double mm_per_raw = kReliefDeviation / stats.deviation;
        const auto define = [](std::string* out, const char* name, double value) {
            *out += text::format("#define %s %.9g\n", name, value);
        };
        std::string out;
        define(&out, "RAIN_RELIEF_SCALE", 1.0 / (kReliefLength * kUnitsPerMetre));
        define(&out, "RAIN_VISCOSITY", kWaterViscosity);
        define(&out, "RAIN_FLOW_LENGTH", kFlowLength);
        define(&out, "RAIN_DENSITY", kWaterDensity);
        define(&out, "RAIN_GRAVITY", kGravity);
        define(&out, "RAIN_OUTSIDE", water_outside_reflectance());
        define(&out, "RAIN_INSIDE", water_inside_reflectance());
        define(&out, "RAIN_PORE_STORAGE", kPoreStorage);
        define(&out, "RAIN_FILM_WHOLE", kFilmWhole);
        define(&out, "RAIN_MM_PER_RELIEF", mm_per_raw);
        // a slope's fall over half the relief's length, in raw relief units a unit of sine
        define(&out, "RAIN_SLOPE_LOSS", 0.5 * kReliefLength * kMillimetresPerMetre / mm_per_raw);
        constexpr double kShore = 0.1; // mm of relief: a puddle's edge, where the water thins to nothing
        define(&out, "RAIN_PUDDLE_EDGE", kShore / mm_per_raw);
        define(&out, "RAIN_WATER_F0", water_f0());
        define(&out, "RAIN_RELIEF_SLOPE", relief_slope());
        define(&out, "RAIN_TEXTURE_DEPTH", kTextureDepth);
        define(&out, "RAIN_RIPPLE_SPEED", kRippleSpeed * kUnitsPerMetre);
        define(&out, "RAIN_RIPPLE_WIDTH", kRippleWidth * kUnitsPerMetre);
        define(&out, "RAIN_RIPPLE_SLOPE", kRippleSlope);
        define(&out, "RAIN_RIPPLE_LIFE", kRippleLife);
        define(&out, "RAIN_RIPPLE_LAYERS", kRippleLayers);
        define(&out, "RAIN_WATER_ROUGHNESS", kWaterRoughness);
        return out;
    }();
    return text.c_str();
}

// --- the state, stepped -----------------------------------------------------------------------------------------------

void step(double seconds)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    const double rate = own.settings.rate;
    if (!own.session) {
        // (a level entered while it rains has been rained on already: its ground as long rain leaves it)
        own.session = true;
        own.stored = rate > 0.0 ? kDepressionStorage : 0.0;
    }
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return;
    }
    own.clock += seconds;
    const double change = (rate > 0.0 ? rate : -kEvaporation) * seconds / kSecondsPerHour;
    own.stored = std::clamp(own.stored + change, 0.0, kDepressionStorage);
}

double clock()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.clock;
}

double stored()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    return own.stored;
}

void set_stored(double millimetres)
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    own.stored = std::clamp(millimetres, 0.0, kDepressionStorage);
    own.session = true;
}

void reset()
{
    State& own = state();
    const std::scoped_lock lock(own.mutex);
    own.clock = 0.0;
    own.stored = 0.0;
    own.session = false;
}

} // namespace render::rain
