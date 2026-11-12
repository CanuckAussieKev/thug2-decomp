// NOT ORIGINAL - the sky and the sun's colour from an atmosphere (rt_sky.h).
#include "engine/gfx/rt_sky.h"

#include <algorithm>
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
constexpr Rgb kLuma{0.2126, 0.7152, 0.0722};

struct Ray {
    Dir origin{}; // from the planet's centre
    Dir direction{};
};

double dot3(const Dir& a, const Dir& b)
{
    return (core::at(a, 0) * core::at(b, 0)) + (core::at(a, 1) * core::at(b, 1)) + (core::at(a, 2) * core::at(b, 2));
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
    const double r = std::sqrt(dot3(ray.origin, ray.origin));
    const double mu = dot3(ray.origin, ray.direction) / r;
    const double base = (r * r) * ((mu * mu) - 1.0);
    *ground = false;
    if (mu < 0.0) {
        const double inside = base + (kGroundRadius * kGroundRadius);
        if (inside >= 0.0) {
            *ground = true;
            return std::max(0.0, (-r * mu) - std::sqrt(inside));
        }
    }
    return std::max(0.0, (-r * mu) + std::sqrt(std::max(0.0, base + (kTopRadius * kTopRadius))));
}

double altitude(const Dir& point) { return std::sqrt(dot3(point, point)) - kGroundRadius; }

// The densities at a point (Rayleigh's, Mie's: 1 at the ground).
struct Densities {
    double rayleigh = 0.0;
    double mie = 0.0;
};
Densities densities_at(const Dir& point)
{
    const double h = std::max(0.0, altitude(point));
    return {.rayleigh = std::exp(-h / kRayleighHeight), .mie = std::exp(-h / kMieHeight)};
}

// Segment i of n along a ray `length` long: its start and end (lengths growing with the square of the distance).
struct Segment {
    double start = 0.0;
    double end = 0.0;
};
Segment segment(double length, uint32_t i, uint32_t n)
{
    const double a = static_cast<double>(i) / static_cast<double>(n);
    const double b = static_cast<double>(i + 1) / static_cast<double>(n);
    return {.start = length * a * a, .end = length * b * b};
}

Rgb extinction_of(const Densities& depth)
{
    Rgb out{};
    for (std::size_t c = 0; c < out.size(); ++c) {
        core::at(out, c) = (core::at(kRayleigh, c) * depth.rayleigh) + ((kMieScattering / kMieAlbedo) * depth.mie);
    }
    return out;
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
    for (uint32_t i = 0; i < steps; ++i) {
        const Segment s = segment(length, i, steps);
        const Densities here = densities_at(along(ray, kHalf * (s.start + s.end)));
        depth->rayleigh += here.rayleigh * (s.end - s.start);
        depth->mie += here.mie * (s.end - s.start);
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
    const Rgb tau = extinction_of(depth);
    return {std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2])};
}

Rgb sky_radiance(const Dir& view, const Dir& sun, const SkySteps& steps)
{
    const Ray ray{.origin = viewer(), .direction = view};
    bool ground = false;
    const double length = ray_length(ray, &ground);
    const double mu = dot3(view, sun);
    const double rayleigh_phase = kRayleighPhase * (1.0 + (mu * mu));
    const double g = kMieG;
    const double mie_phase = kMiePhase * ((1.0 - (g * g)) * (1.0 + (mu * mu))) /
                             ((2.0 + (g * g)) * std::pow(1.0 + (g * g) - (2.0 * g * mu), 1.5));
    Rgb sum{};
    Densities travelled; // from the viewer to the segment's start
    const uint32_t n = std::max(1U, steps.view);
    for (uint32_t i = 0; i < n; ++i) {
        const Segment s = segment(length, i, n);
        const double ds = s.end - s.start;
        const Dir point = along(ray, kHalf * (s.start + s.end));
        const Densities here = densities_at(point);
        const Densities to_here{.rayleigh = travelled.rayleigh + (kHalf * here.rayleigh * ds),
                                .mie = travelled.mie + (kHalf * here.mie * ds)};
        Densities to_sun;
        if (depth_to_top({.origin = point, .direction = sun}, std::max(1U, steps.light), &to_sun)) {
            const Rgb tau = extinction_of({.rayleigh = to_here.rayleigh + to_sun.rayleigh, .mie = to_here.mie + to_sun.mie});
            for (std::size_t c = 0; c < sum.size(); ++c) {
                const double scattered = (core::at(kRayleigh, c) * here.rayleigh * rayleigh_phase) +
                                         (kMieScattering * here.mie * mie_phase);
                core::at(sum, c) += std::exp(-core::at(tau, c)) * scattered * ds;
            }
        }
        travelled.rayleigh += here.rayleigh * ds;
        travelled.mie += here.mie * ds;
    }
    (void)ground;
    return sum;
}

double sky_exposure()
{
    static const double exposure = [] {
        const Rgb zenith = sky_radiance({0.0, 1.0, 0.0}, sun_direction_at(kExposureElevation), kReferenceSteps);
        const double value = luma(zenith);
        return value > 0.0 ? kZenithTarget / value : 1.0;
    }();
    return exposure;
}

std::array<float, 3> sun_colour_at(double elevation, uint32_t steps)
{
    const Rgb here = sun_transmittance(elevation, steps);
    const Rgb overhead = sun_transmittance(kPi / 2.0, steps);
    Rgb shown{};
    double most = 0.0;
    for (std::size_t c = 0; c < shown.size(); ++c) {
        const double relative = core::at(overhead, c) > 0.0 ? core::at(here, c) / core::at(overhead, c) : 0.0;
        core::at(shown, c) = std::pow(std::max(relative, 0.0), 1.0 / kDisplayGamma);
        most = std::max(most, core::at(shown, c));
    }
    if (!(most > 0.0)) {
        return {0.0F, 0.0F, 0.0F};
    }
    return {static_cast<float>(shown[0] / most), static_cast<float>(shown[1] / most), static_cast<float>(shown[2] / most)};
}

Dir sky_lut_direction(uint32_t column, uint32_t row)
{
    const double u = (static_cast<double>(column) + kHalf) / static_cast<double>(kSkyLutWidth);
    const double v = (static_cast<double>(row) + kHalf) / static_cast<double>(kSkyLutHeight);
    const double azimuth = u * kPi;
    const double elevation = kSkyLutLowest + ((v * v) * ((kPi / 2.0) - kSkyLutLowest));
    return {std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth)};
}

} // namespace render::rt
