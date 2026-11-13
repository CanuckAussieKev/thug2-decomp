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

// --- multiple scattering (rt_sky.h) --------------------------------------------------------------------------------

namespace {

// sqrt(top^2 - ground^2): the transmittance table's longest horizontal distance
double horizon_span() { return std::sqrt((kTopRadius * kTopRadius) - (kGroundRadius * kGroundRadius)); }

// a texel centre's 0 - 1 coordinate back to 0 - 1 over the table's whole range (and the other way)
double unit_from_texel(double u, uint32_t size)
{
    const double texel = 1.0 / static_cast<double>(size);
    return std::clamp((u - (kHalf * texel)) / (1.0 - texel), 0.0, 1.0);
}
double texel_from_unit(double x, uint32_t size)
{
    const double texel = 1.0 / static_cast<double>(size);
    return (kHalf * texel) + (std::clamp(x, 0.0, 1.0) * (1.0 - texel));
}

Dir from_radius_cosine(const RadiusCosine& point) { return {0.0, point.radius, 0.0}; }
Dir direction_of_cosine(double cosine)
{
    const double c = std::clamp(cosine, -1.0, 1.0);
    return {std::sqrt(std::max(0.0, 1.0 - (c * c))), c, 0.0};
}

Rgb scattering_of(const Densities& here)
{
    Rgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) = (core::at(kRayleigh, channel) * here.rayleigh) + (kMieScattering * here.mie);
    }
    return out;
}

// the sun's transmittance from a point (0 when the ground is in the way)
Rgb sun_seen_from(const Dir& point, const Dir& sun, uint32_t light_steps)
{
    Densities depth;
    if (!depth_to_top({.origin = point, .direction = sun}, std::max(1U, light_steps), &depth)) {
        return {0.0, 0.0, 0.0};
    }
    return transmittance_of(extinction_of(depth));
}

double length3(const Dir& v) { return std::sqrt(dot3(v, v)); }

constexpr double kFourPi = 4.0 * kPi;

} // namespace

RadiusCosine transmittance_lut_point(double u, double v)
{
    const double x_mu = unit_from_texel(u, kTransmittanceWidth);
    const double x_r = unit_from_texel(v, kTransmittanceHeight);
    const double span = horizon_span();
    const double rho = span * x_r;
    const double radius = std::sqrt((rho * rho) + (kGroundRadius * kGroundRadius));
    const double d_min = kTopRadius - radius;
    const double d_max = rho + span;
    const double d = d_min + (x_mu * (d_max - d_min));
    const double cosine = d <= 0.0 ? 1.0 : ((span * span) - (rho * rho) - (d * d)) / (2.0 * radius * d);
    return {.radius = radius, .cosine = std::clamp(cosine, -1.0, 1.0)};
}

std::array<double, 2> transmittance_lut_uv(const RadiusCosine& point)
{
    const double span = horizon_span();
    const double r = point.radius;
    const double rho = std::sqrt(std::max(0.0, (r * r) - (kGroundRadius * kGroundRadius)));
    const double mu = point.cosine;
    const double d = std::max(0.0, (-r * mu) + std::sqrt(std::max(0.0, ((r * r) * ((mu * mu) - 1.0)) + (kTopRadius * kTopRadius))));
    const double d_min = kTopRadius - r;
    const double d_max = rho + span;
    const double x_mu = d_max > d_min ? (d - d_min) / (d_max - d_min) : 0.0;
    return {texel_from_unit(x_mu, kTransmittanceWidth), texel_from_unit(rho / span, kTransmittanceHeight)};
}

Rgb transmittance_to_top(const RadiusCosine& point, uint32_t steps)
{
    const Ray ray{.origin = from_radius_cosine(point), .direction = direction_of_cosine(point.cosine)};
    const double r = point.radius;
    const double mu = point.cosine;
    const double length =
        std::max(0.0, (-r * mu) + std::sqrt(std::max(0.0, ((r * r) * ((mu * mu) - 1.0)) + (kTopRadius * kTopRadius))));
    const uint32_t count = std::max(1U, steps);
    const double span = length / static_cast<double>(count);
    Densities depth;
    for (uint32_t index = 0; index < count; ++index) {
        const Densities here = densities_at(along(ray, (static_cast<double>(index) + kHalf) * span));
        depth.rayleigh += here.rayleigh * span;
        depth.mie += here.mie * span;
    }
    return transmittance_of(extinction_of(depth));
}

Rgb multiple_scattering(double altitude, double cos_sun, uint32_t steps, uint32_t light_steps)
{
    const Dir origin{0.0, kGroundRadius + std::max(altitude, 0.0), 0.0};
    const Dir sun = direction_of_cosine(cos_sun);
    const uint32_t side = kMultipleDirectionsSide;
    const uint32_t count = std::max(1U, steps);
    Rgb second{};
    Rgb transfer{};
    for (uint32_t i = 0; i < side; ++i) {
        const double cos_theta = 1.0 - (2.0 * (static_cast<double>(i) + kHalf) / static_cast<double>(side));
        const double sin_theta = std::sqrt(std::max(0.0, 1.0 - (cos_theta * cos_theta)));
        for (uint32_t j = 0; j < side; ++j) {
            const double phi = 2.0 * kPi * (static_cast<double>(j) + kHalf) / static_cast<double>(side);
            const Ray ray{.origin = origin, .direction = {sin_theta * std::cos(phi), cos_theta, sin_theta * std::sin(phi)}};
            bool ground = false;
            const double length = ray_length(ray, &ground);
            const double span = length / static_cast<double>(count);
            Rgb throughput{1.0, 1.0, 1.0};
            for (uint32_t k = 0; k < count; ++k) {
                const Dir point = along(ray, (static_cast<double>(k) + kHalf) * span);
                const Densities here = densities_at(point);
                const Rgb sigma_s = scattering_of(here);
                const Rgb sigma_t = extinction_of(here);
                const Rgb sun_t = sun_seen_from(point, sun, light_steps);
                for (std::size_t c = 0; c < 3; ++c) {
                    const double t = core::at(sigma_t, c);
                    const double kept = std::exp(-t * span);
                    const double integral = t > 0.0 ? (1.0 - kept) / t : span; // integral of exp(-t s) over the step
                    core::at(second, c) += core::at(throughput, c) * core::at(sigma_s, c) * core::at(sun_t, c) *
                                           (1.0 / kFourPi) * integral;
                    core::at(transfer, c) += core::at(throughput, c) * core::at(sigma_s, c) * integral;
                    core::at(throughput, c) *= kept;
                }
            }
            if (ground) {
                const Dir at_ground = along(ray, length);
                const double up = dot3(at_ground, sun) / std::max(length3(at_ground), 1.0);
                const Rgb sun_t = sun_seen_from(at_ground, sun, light_steps);
                for (std::size_t c = 0; c < 3; ++c) {
                    core::at(second, c) +=
                        core::at(throughput, c) * core::at(sun_t, c) * std::max(up, 0.0) * kGroundAlbedo / kPi;
                }
            }
        }
    }
    const double directions = static_cast<double>(side) * static_cast<double>(side);
    Rgb psi{};
    for (std::size_t c = 0; c < 3; ++c) {
        const double l2 = core::at(second, c) / directions;
        const double f = core::at(transfer, c) / directions;
        core::at(psi, c) = l2 / std::max(1.0 - f, 1e-6);
    }
    return psi;
}

MultipleTable multiple_table(uint32_t steps, uint32_t light_steps)
{
    MultipleTable table(static_cast<std::size_t>(kMultipleWidth) * kMultipleHeight);
    for (uint32_t row = 0; row < kMultipleHeight; ++row) {
        const double altitude = (static_cast<double>(row) + kHalf) / kMultipleHeight * (kTopRadius - kGroundRadius);
        for (uint32_t column = 0; column < kMultipleWidth; ++column) {
            const double cos_sun = (((static_cast<double>(column) + kHalf) / kMultipleWidth) * 2.0) - 1.0;
            core::at(table, (static_cast<std::size_t>(row) * kMultipleWidth) + column) =
                multiple_scattering(altitude, cos_sun, steps, light_steps);
        }
    }
    return table;
}

Rgb multiple_from_table(const MultipleTable& table, double altitude, double cos_sun)
{
    if (table.size() != static_cast<std::size_t>(kMultipleWidth) * kMultipleHeight) {
        return {};
    }
    const double x = std::clamp((((cos_sun * kHalf) + kHalf) * kMultipleWidth) - kHalf, 0.0, kMultipleWidth - 1.0);
    const double y = std::clamp(((altitude / (kTopRadius - kGroundRadius)) * kMultipleHeight) - kHalf, 0.0,
                                kMultipleHeight - 1.0);
    const auto x0 = static_cast<uint32_t>(x);
    const auto y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, kMultipleWidth - 1);
    const uint32_t y1 = std::min(y0 + 1, kMultipleHeight - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto at = [&table](uint32_t column, uint32_t row) -> const Rgb& {
        return core::at(table, (static_cast<std::size_t>(row) * kMultipleWidth) + column);
    };
    Rgb out{};
    for (std::size_t c = 0; c < 3; ++c) {
        const double top = (core::at(at(x0, y0), c) * (1.0 - fx)) + (core::at(at(x1, y0), c) * fx);
        const double bottom = (core::at(at(x0, y1), c) * (1.0 - fx)) + (core::at(at(x1, y1), c) * fx);
        core::at(out, c) = (top * (1.0 - fy)) + (bottom * fy);
    }
    return out;
}

Rgb sky_radiance_ms(const Dir& view, const Dir& sun, const SkySteps& steps, const MultipleTable& table)
{
    const Ray ray{.origin = viewer(), .direction = view};
    bool ground = false;
    const double length = ray_length(ray, &ground);
    const double mu = dot3(view, sun);
    const double rayleigh_phase = kRayleighPhase * (1.0 + (mu * mu));
    const double g = kMieG;
    const double mie_phase = kMiePhase * ((1.0 - (g * g)) * (1.0 + (mu * mu))) /
                             ((2.0 + (g * g)) * std::pow(1.0 + (g * g) - (2.0 * g * mu), kMiePower));
    Rgb sum{};
    Densities travelled;
    const uint32_t segments = std::max(1U, steps.view);
    for (uint32_t index = 0; index < segments; ++index) {
        const Segment part = segment(length, {.index = index, .count = segments});
        const double span = part.end - part.start;
        const Dir point = along(ray, kHalf * (part.start + part.end));
        const Densities here = densities_at(point);
        const Densities to_here{.rayleigh = travelled.rayleigh + (kHalf * here.rayleigh * span),
                                .mie = travelled.mie + (kHalf * here.mie * span)};
        const Rgb seen = transmittance_of(extinction_of(to_here));
        const Rgb sun_t = sun_seen_from(point, sun, steps.light);
        const double radius = length3(point);
        const Rgb psi = multiple_from_table(table, radius - kGroundRadius, dot3(point, sun) / radius);
        for (std::size_t channel = 0; channel < sum.size(); ++channel) {
            const double rayleigh_s = core::at(kRayleigh, channel) * here.rayleigh;
            const double mie_s = kMieScattering * here.mie;
            const double single = core::at(sun_t, channel) * ((rayleigh_s * rayleigh_phase) + (mie_s * mie_phase));
            const double multiple = (rayleigh_s + mie_s) * core::at(psi, channel);
            core::at(sum, channel) += core::at(seen, channel) * (single + multiple) * span;
        }
        travelled.rayleigh += here.rayleigh * span;
        travelled.mie += here.mie * span;
    }
    if (ground) {
        const Dir at_ground = along(ray, length);
        const Rgb seen = transmittance_of(extinction_of(travelled));
        const Rgb sun_t = sun_seen_from(at_ground, sun, steps.light);
        const double up = dot3(at_ground, sun) / std::max(length3(at_ground), 1.0);
        for (std::size_t channel = 0; channel < sum.size(); ++channel) {
            core::at(sum, channel) +=
                core::at(seen, channel) * core::at(sun_t, channel) * std::max(up, 0.0) * kGroundAlbedo / kPi;
        }
    }
    return sum;
}

Dir sky_view_direction(double u, double v)
{
    const double azimuth = kPi * u * u;
    const double s = (2.0 * v) - 1.0;
    const double elevation = (s < 0.0 ? -1.0 : 1.0) * s * s * (kPi / 2.0);
    return {std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth)};
}

std::array<double, 2> sky_view_uv(const Dir& view)
{
    const double flat = std::hypot(core::at(view, 0), core::at(view, 2));
    const double azimuth = flat > 1e-9 ? std::acos(std::clamp(core::at(view, 0) / flat, -1.0, 1.0)) : 0.0;
    const double elevation = std::asin(std::clamp(core::at(view, 1), -1.0, 1.0));
    const double v = kHalf + (kHalf * (elevation < 0.0 ? -1.0 : 1.0) * std::sqrt(std::abs(elevation) / (kPi / 2.0)));
    return {std::sqrt(azimuth / kPi), v};
}

} // namespace render::rt
