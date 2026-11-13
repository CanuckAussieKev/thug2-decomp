// NOT ORIGINAL - the sky and the sun's colour from an atmosphere (rt_sky.h).
#include "engine/gfx/rt_sky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "base/core/checked_index.h"

namespace render::rt {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kRayleighPhase = 3.0 / (16.0 * kPi);
constexpr double kMiePhase = 3.0 / (8.0 * kPi);
constexpr double kHalf = 0.5;
constexpr double kMiePower = 1.5;
constexpr Rgb kLuma{0.2126, 0.7152, 0.0722};

struct Ray {
    Dir origin{}; // from the planet's centre
    Dir direction{};
};

double dot3(const Dir& one, const Dir& other)
{
    return (core::at(one, 0) * core::at(other, 0)) + (core::at(one, 1) * core::at(other, 1)) +
           (core::at(one, 2) * core::at(other, 2));
}

Dir along(const Ray& ray, double distance)
{
    return {core::at(ray.origin, 0) + (core::at(ray.direction, 0) * distance),
            core::at(ray.origin, 1) + (core::at(ray.direction, 1) * distance),
            core::at(ray.origin, 2) + (core::at(ray.direction, 2) * distance)};
}

// How far the ray goes before it leaves the atmosphere's top or meets the ground (whichever is first; `ground` says
// which).
double ray_length(const Ray& ray, bool* ground)
{
    const double radius = std::sqrt(dot3(ray.origin, ray.origin));
    const double mu = dot3(ray.origin, ray.direction) / radius;
    const double base = (radius * radius) * ((mu * mu) - 1.0);
    *ground = false;
    if (mu < 0.0) {
        const double inside = base + (kGroundRadius * kGroundRadius);
        if (inside >= 0.0) {
            *ground = true;
            return std::max(0.0, (-radius * mu) - std::sqrt(inside));
        }
    }
    return std::max(0.0, (-radius * mu) + std::sqrt(std::max(0.0, base + (kTopRadius * kTopRadius))));
}

double altitude(const Dir& point) { return std::sqrt(dot3(point, point)) - kGroundRadius; }

// The densities at a point (Rayleigh's, Mie's: 1 at the ground).
struct Densities {
    double rayleigh = 0.0;
    double mie = 0.0;
};
Densities densities_at(const Dir& point)
{
    const double height = std::max(0.0, altitude(point));
    return {.rayleigh = std::exp(-height / kRayleighHeight), .mie = std::exp(-height / kMieHeight)};
}

// One of `count` segments along a ray `length` long: its start and end (lengths growing with the square of the
// distance).
struct Segment {
    double start = 0.0;
    double end = 0.0;
};
struct Split {
    uint32_t index = 0;
    uint32_t count = 1;
};
Segment segment(double length, Split split)
{
    const double from = static_cast<double>(split.index) / static_cast<double>(split.count);
    const double to = static_cast<double>(split.index + 1) / static_cast<double>(split.count);
    return {.start = length * from * from, .end = length * to * to};
}

Rgb extinction_of(const Densities& depth)
{
    Rgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) =
            (core::at(kRayleigh, channel) * depth.rayleigh) + ((kMieScattering / kMieAlbedo) * depth.mie);
    }
    return out;
}

Rgb transmittance_of(const Rgb& tau)
{
    return {std::exp(-core::at(tau, 0)), std::exp(-core::at(tau, 1)), std::exp(-core::at(tau, 2))};
}

// The density-weighted lengths from a point to the atmosphere's top along `direction` (false: the ground is in the way).
bool depth_to_top(const Ray& ray, uint32_t steps, Densities* depth)
{
    bool ground = false;
    const double length = ray_length(ray, &ground);
    *depth = {};
    if (ground) {
        return false;
    }
    for (uint32_t index = 0; index < steps; ++index) {
        const Segment part = segment(length, {.index = index, .count = steps});
        const Densities here = densities_at(along(ray, kHalf * (part.start + part.end)));
        depth->rayleigh += here.rayleigh * (part.end - part.start);
        depth->mie += here.mie * (part.end - part.start);
    }
    return true;
}

Dir viewer() { return {0.0, kGroundRadius + kViewerAltitude, 0.0}; }

double luma(const Rgb& colour) { return dot3(colour, kLuma); }

} // namespace

Dir sun_direction_at(double elevation) { return {std::cos(elevation), std::sin(elevation), 0.0}; }

Rgb sun_transmittance(double elevation, uint32_t steps)
{
    Densities depth;
    if (!depth_to_top({.origin = viewer(), .direction = sun_direction_at(elevation)}, std::max(1U, steps), &depth)) {
        return {0.0, 0.0, 0.0};
    }
    return transmittance_of(extinction_of(depth));
}

Rgb sky_radiance(const Dir& view, const Dir& sun, const SkySteps& steps)
{
    const Ray ray{.origin = viewer(), .direction = view};
    bool ground = false;
    const double length = ray_length(ray, &ground);
    const double mu = dot3(view, sun);
    const double rayleigh_phase = kRayleighPhase * (1.0 + (mu * mu));
    const double asymmetry = kMieG;
    const double mie_phase = kMiePhase * ((1.0 - (asymmetry * asymmetry)) * (1.0 + (mu * mu))) /
                             ((2.0 + (asymmetry * asymmetry)) *
                              std::pow(1.0 + (asymmetry * asymmetry) - (2.0 * asymmetry * mu), kMiePower));
    Rgb sum{};
    Densities travelled; // from the viewer to the segment's start
    const uint32_t segments = std::max(1U, steps.view);
    for (uint32_t index = 0; index < segments; ++index) {
        const Segment part = segment(length, {.index = index, .count = segments});
        const double span = part.end - part.start;
        const Dir point = along(ray, kHalf * (part.start + part.end));
        const Densities here = densities_at(point);
        const Densities to_here{.rayleigh = travelled.rayleigh + (kHalf * here.rayleigh * span),
                                .mie = travelled.mie + (kHalf * here.mie * span)};
        Densities to_sun;
        if (depth_to_top({.origin = point, .direction = sun}, std::max(1U, steps.light), &to_sun)) {
            const Rgb kept = transmittance_of(
                extinction_of({.rayleigh = to_here.rayleigh + to_sun.rayleigh, .mie = to_here.mie + to_sun.mie}));
            for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                const double scattered = (core::at(kRayleigh, channel) * here.rayleigh * rayleigh_phase) +
                                         (kMieScattering * here.mie * mie_phase);
                core::at(sum, channel) += core::at(kept, channel) * scattered * span;
            }
        }
        travelled.rayleigh += here.rayleigh * span;
        travelled.mie += here.mie * span;
    }
    return sum;
}

double sky_exposure()
{
    static const double kExposure = [] {
        const Rgb zenith = sky_radiance({0.0, 1.0, 0.0}, sun_direction_at(kExposureElevation), kReferenceSteps);
        const double value = luma(zenith);
        return value > 0.0 ? kZenithTarget / value : 1.0;
    }();
    return kExposure;
}

std::array<float, 3> sun_colour_at(double elevation, uint32_t steps)
{
    const Rgb here = sun_transmittance(elevation, steps);
    const Rgb overhead = sun_transmittance(kPi / 2.0, steps);
    Rgb shown{};
    double most = 0.0;
    for (std::size_t channel = 0; channel < shown.size(); ++channel) {
        const double relative =
            core::at(overhead, channel) > 0.0 ? core::at(here, channel) / core::at(overhead, channel) : 0.0;
        core::at(shown, channel) = std::pow(std::max(relative, 0.0), 1.0 / kDisplayGamma);
        most = std::max(most, core::at(shown, channel));
    }
    if (!(most > 0.0)) {
        return {0.0F, 0.0F, 0.0F};
    }
    return {static_cast<float>(core::at(shown, 0) / most), static_cast<float>(core::at(shown, 1) / most),
            static_cast<float>(core::at(shown, 2) / most)};
}

Dir sky_lut_direction(const SkyLutEntry& entry)
{
    const double across = (static_cast<double>(entry.column) + kHalf) / static_cast<double>(kSkyLutWidth);
    const double down = (static_cast<double>(entry.row) + kHalf) / static_cast<double>(kSkyLutHeight);
    const double azimuth = across * kPi;
    const double elevation = kSkyLutLowest + ((down * down) * ((kPi / 2.0) - kSkyLutLowest));
    return {std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth)};
}

} // namespace render::rt
