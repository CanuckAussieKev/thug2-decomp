// NOT ORIGINAL - the sky and the sun's colour from an atmosphere (rt_sky.h).
#include "engine/gfx/rt_sky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
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

// The densities at a point (Rayleigh's, Mie's: 1 at the ground; the ozone's: 1 at kOzoneCentre) - or, summed along a
// ray, their density-weighted lengths.
struct Densities {
    double rayleigh = 0.0;
    double mie = 0.0;
    double ozone = 0.0;
    void add(const Densities& other, double length)
    {
        rayleigh += other.rayleigh * length;
        mie += other.mie * length;
        ozone += other.ozone * length;
    }
};
Densities plus(const Densities& one, const Densities& other, double length)
{
    Densities out = one;
    out.add(other, length);
    return out;
}
Densities densities_at(const Dir& point, const Aerosol& air)
{
    const double height = std::max(0.0, altitude(point));
    return {.rayleigh = std::exp(-height / kRayleighHeight),
            .mie = aerosol_density(air, height),
            .ozone = std::max(0.0, 1.0 - (std::abs(height - kOzoneCentre) / kOzoneHalfWidth))};
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

Rgb extinction_of(const Densities& depth, const Aerosol& air)
{
    Rgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) =
            (core::at(kRayleigh, channel) * depth.rayleigh) + (core::at(air.extinction, channel) * depth.mie) +
            (core::at(kOzoneAbsorption, channel) * depth.ozone);
    }
    return out;
}

Rgb transmittance_of(const Rgb& tau)
{
    return {std::exp(-core::at(tau, 0)), std::exp(-core::at(tau, 1)), std::exp(-core::at(tau, 2))};
}

// The density-weighted lengths from a point to the atmosphere's top along `direction` (false: the ground is in the way).
bool depth_to_top(const Ray& ray, uint32_t steps, Densities* depth, const Aerosol& air)
{
    bool ground = false;
    const double length = ray_length(ray, &ground);
    *depth = {};
    if (ground) {
        return false;
    }
    for (uint32_t index = 0; index < steps; ++index) {
        const Segment part = segment(length, {.index = index, .count = steps});
        const Densities here = densities_at(along(ray, kHalf * (part.start + part.end)), air);
        depth->add(here, part.end - part.start);
    }
    return true;
}

Dir viewer() { return {0.0, kGroundRadius + kViewerAltitude, 0.0}; }

double luma(const Rgb& colour) { return dot3(colour, kLuma); }

} // namespace

Dir sun_direction_at(double elevation) { return {std::cos(elevation), std::sin(elevation), 0.0}; }

Rgb sun_transmittance(double elevation, uint32_t steps) { return sun_transmittance(elevation, steps, aerosol()); }

Rgb sun_transmittance(double elevation, uint32_t steps, const Aerosol& air)
{
    Densities depth;
    if (!depth_to_top({.origin = viewer(), .direction = sun_direction_at(elevation)}, std::max(1U, steps), &depth, air)) {
        return {0.0, 0.0, 0.0};
    }
    return transmittance_of(extinction_of(depth, air));
}

Rgb sky_radiance(const Dir& view, const Dir& sun, const SkySteps& steps)
{
    return sky_radiance(view, sun, steps, aerosol());
}

Rgb sky_radiance(const Dir& view, const Dir& sun, const SkySteps& steps, const Aerosol& air)
{
    const Ray ray{.origin = viewer(), .direction = view};
    bool ground = false;
    const double length = ray_length(ray, &ground);
    const double mu = dot3(view, sun);
    const double rayleigh_phase_here = rayleigh_phase(mu);
    const double mie_phase = aerosol_phase(mu, air.g);
    Rgb sum{};
    Densities travelled; // from the viewer to the segment's start
    const uint32_t segments = std::max(1U, steps.view);
    for (uint32_t index = 0; index < segments; ++index) {
        const Segment part = segment(length, {.index = index, .count = segments});
        const double span = part.end - part.start;
        const Dir point = along(ray, kHalf * (part.start + part.end));
        const Densities here = densities_at(point, air);
        const Densities to_here = plus(travelled, here, kHalf * span);
        Densities to_sun;
        if (depth_to_top({.origin = point, .direction = sun}, std::max(1U, steps.light), &to_sun, air)) {
            const Rgb kept = transmittance_of(extinction_of(plus(to_here, to_sun, 1.0), air));
            for (std::size_t channel = 0; channel < sum.size(); ++channel) {
                const double scattered = (core::at(kRayleigh, channel) * here.rayleigh * rayleigh_phase_here) +
                                         (core::at(air.scattering, channel) * here.mie * mie_phase);
                core::at(sum, channel) += core::at(kept, channel) * scattered * span;
            }
        }
        travelled.add(here, span);
    }
    return sum;
}

double sky_exposure()
{
    static const double kExposure = [] {
        const GroundLight day = ground_light(kReferenceElevation, aerosol_for(Weather::kClear));
        const double total = day.direct + day.sky;
        return total > 0.0 ? kPi / total : 1.0;
    }();
    return kExposure;
}

double unit_illuminance() { return kSolarIlluminance * kPi / sky_exposure(); }

double sun_solid_angle() { return 2.0 * kPi * (1.0 - std::cos(kSunAngularRadius)); }

Rgb sun_disc_radiance(double elevation)
{
    constexpr uint32_t kSteps = 64;
    const Rgb through = sun_transmittance(elevation, kSteps);
    const double scale = sky_exposure() / (sun_solid_angle() * (1.0 - (kSunLimbDarkening / 3.0)));
    return {core::at(through, 0) * scale, core::at(through, 1) * scale, core::at(through, 2) * scale};
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
double unit_from_texel(double source, uint32_t size)
{
    const double texel = 1.0 / static_cast<double>(size);
    return std::clamp((source - (kHalf * texel)) / (1.0 - texel), 0.0, 1.0);
}
double texel_from_unit(double source, uint32_t size)
{
    const double texel = 1.0 / static_cast<double>(size);
    return (kHalf * texel) + (std::clamp(source, 0.0, 1.0) * (1.0 - texel));
}

Dir from_radius_cosine(const RadiusCosine& point) { return {0.0, point.radius, 0.0}; }
Dir direction_of_cosine(double cosine)
{
    const double clamped = std::clamp(cosine, -1.0, 1.0);
    return {std::sqrt(std::max(0.0, 1.0 - (clamped * clamped))), clamped, 0.0};
}

Rgb scattering_of(const Densities& here, const Aerosol& air)
{
    Rgb out{};
    for (std::size_t channel = 0; channel < out.size(); ++channel) {
        core::at(out, channel) =
            (core::at(kRayleigh, channel) * here.rayleigh) + (core::at(air.scattering, channel) * here.mie);
    }
    return out;
}

// the sun's transmittance from a point (0 when the ground is in the way)
Rgb sun_seen_from(const Dir& point, const Dir& sun, uint32_t light_steps, const Aerosol& air)
{
    Densities depth;
    if (!depth_to_top({.origin = point, .direction = sun}, std::max(1U, light_steps), &depth, air)) {
        return {0.0, 0.0, 0.0};
    }
    return transmittance_of(extinction_of(depth, air));
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
    const double to_top = d_min + (x_mu * (d_max - d_min));
    const double cosine =
        to_top <= 0.0 ? 1.0 : ((span * span) - (rho * rho) - (to_top * to_top)) / (2.0 * radius * to_top);
    return {.radius = radius, .cosine = std::clamp(cosine, -1.0, 1.0)};
}

std::array<double, 2> transmittance_lut_uv(const RadiusCosine& point)
{
    const double span = horizon_span();
    const double radius = point.radius;
    const double rho = std::sqrt(std::max(0.0, (radius * radius) - (kGroundRadius * kGroundRadius)));
    const double mu = point.cosine;
    const double to_top = std::max(
        0.0, (-radius * mu) + std::sqrt(std::max(0.0, ((radius * radius) * ((mu * mu) - 1.0)) + (kTopRadius * kTopRadius))));
    const double d_min = kTopRadius - radius;
    const double d_max = rho + span;
    const double x_mu = d_max > d_min ? (to_top - d_min) / (d_max - d_min) : 0.0;
    return {texel_from_unit(x_mu, kTransmittanceWidth), texel_from_unit(rho / span, kTransmittanceHeight)};
}

Rgb transmittance_to_top(const RadiusCosine& point, uint32_t steps)
{
    const Aerosol air = aerosol();
    const Ray ray{.origin = from_radius_cosine(point), .direction = direction_of_cosine(point.cosine)};
    const double radius = point.radius;
    const double mu = point.cosine;
    const double length = std::max(
        0.0, (-radius * mu) + std::sqrt(std::max(0.0, ((radius * radius) * ((mu * mu) - 1.0)) + (kTopRadius * kTopRadius))));
    const uint32_t count = std::max(1U, steps);
    const double span = length / static_cast<double>(count);
    Densities depth;
    for (uint32_t index = 0; index < count; ++index) {
        const Densities here = densities_at(along(ray, (static_cast<double>(index) + kHalf) * span), air);
        depth.add(here, span);
    }
    return transmittance_of(extinction_of(depth, air));
}

Rgb multiple_scattering(const AtmospherePoint& place, const MultipleSteps& steps_given)
{
    return multiple_scattering(place, steps_given, aerosol());
}

Rgb multiple_scattering(const AtmospherePoint& place, const MultipleSteps& steps_given, const Aerosol& air)
{
    const double altitude = place.altitude;
    const double cos_sun = place.cos_sun;
    const uint32_t steps = steps_given.steps;
    const uint32_t light_steps = steps_given.light;
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
                const Densities here = densities_at(point, air);
                const Rgb sigma_s = scattering_of(here, air);
                const Rgb sigma_t = extinction_of(here, air);
                const Rgb sun_t = sun_seen_from(point, sun, light_steps, air);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const double extinction = core::at(sigma_t, channel);
                    const double kept = std::exp(-extinction * span);
                    // (the integral of exp(-extinction s) over the step)
                    const double integral = extinction > 0.0 ? (1.0 - kept) / extinction : span;
                    core::at(second, channel) += core::at(throughput, channel) * core::at(sigma_s, channel) *
                                                 core::at(sun_t, channel) * (1.0 / kFourPi) * integral;
                    core::at(transfer, channel) += core::at(throughput, channel) * core::at(sigma_s, channel) * integral;
                    core::at(throughput, channel) *= kept;
                }
            }
            if (ground) {
                const Dir at_ground = along(ray, length);
                const double up = dot3(at_ground, sun) / std::max(length3(at_ground), 1.0);
                const Rgb sun_t = sun_seen_from(at_ground, sun, light_steps, air);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    core::at(second, channel) += core::at(throughput, channel) * core::at(sun_t, channel) *
                                                 std::max(up, 0.0) * kGroundAlbedo / kPi;
                }
            }
        }
    }
    const double directions = static_cast<double>(side) * static_cast<double>(side);
    Rgb psi{};
    constexpr double kLeastRemaining = 1e-6;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double l2 = core::at(second, channel) / directions;
        const double back = core::at(transfer, channel) / directions;
        core::at(psi, channel) = l2 / std::max(1.0 - back, kLeastRemaining);
    }
    return psi;
}

MultipleTable multiple_table(const MultipleSteps& steps)
{
    MultipleTable table(static_cast<std::size_t>(kMultipleWidth) * kMultipleHeight);
    for (uint32_t row = 0; row < kMultipleHeight; ++row) {
        const double altitude = (static_cast<double>(row) + kHalf) / kMultipleHeight * (kTopRadius - kGroundRadius);
        for (uint32_t column = 0; column < kMultipleWidth; ++column) {
            const double cos_sun = (((static_cast<double>(column) + kHalf) / kMultipleWidth) * 2.0) - 1.0;
            core::at(table, (static_cast<std::size_t>(row) * kMultipleWidth) + column) =
                multiple_scattering({.altitude = altitude, .cos_sun = cos_sun}, steps);
        }
    }
    return table;
}

Rgb multiple_from_table(const MultipleTable& table, const AtmospherePoint& place)
{
    const double altitude = place.altitude;
    const double cos_sun = place.cos_sun;
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
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double top = (core::at(at(x0, y0), channel) * (1.0 - fx)) + (core::at(at(x1, y0), channel) * fx);
        const double bottom = (core::at(at(x0, y1), channel) * (1.0 - fx)) + (core::at(at(x1, y1), channel) * fx);
        core::at(out, channel) = (top * (1.0 - fy)) + (bottom * fy);
    }
    return out;
}

Rgb sky_radiance_ms(const Dir& view, const Dir& sun, const SkySteps& steps, const MultipleTable& table)
{
    return sky_radiance_ms(view, sun, steps, table, aerosol());
}

Rgb sky_radiance_ms(const Dir& view, const Dir& sun, const SkySteps& steps, const MultipleTable& table,
                    const Aerosol& air)
{
    const Ray ray{.origin = viewer(), .direction = view};
    bool ground = false;
    const double length = ray_length(ray, &ground);
    const double mu = dot3(view, sun);
    const double rayleigh_phase_here = rayleigh_phase(mu);
    const double mie_phase = aerosol_phase(mu, air.g);
    Rgb sum{};
    Densities travelled;
    const uint32_t segments = std::max(1U, steps.view);
    for (uint32_t index = 0; index < segments; ++index) {
        const Segment part = segment(length, {.index = index, .count = segments});
        const double span = part.end - part.start;
        const Dir point = along(ray, kHalf * (part.start + part.end));
        const Densities here = densities_at(point, air);
        const Densities to_here = plus(travelled, here, kHalf * span);
        const Rgb seen = transmittance_of(extinction_of(to_here, air));
        const Rgb sun_t = sun_seen_from(point, sun, steps.light, air);
        const double radius = length3(point);
        const Rgb psi = multiple_from_table(table, {.altitude = radius - kGroundRadius, .cos_sun = dot3(point, sun) / radius});
        for (std::size_t channel = 0; channel < sum.size(); ++channel) {
            const double rayleigh_s = core::at(kRayleigh, channel) * here.rayleigh;
            const double mie_s = core::at(air.scattering, channel) * here.mie;
            const double single =
                core::at(sun_t, channel) * ((rayleigh_s * rayleigh_phase_here) + (mie_s * mie_phase));
            const double multiple = (rayleigh_s + mie_s) * core::at(psi, channel);
            core::at(sum, channel) += core::at(seen, channel) * (single + multiple) * span;
        }
        travelled.add(here, span);
    }
    if (ground) {
        const Dir at_ground = along(ray, length);
        const Rgb seen = transmittance_of(extinction_of(travelled, air));
        const Rgb sun_t = sun_seen_from(at_ground, sun, steps.light, air);
        const double up = dot3(at_ground, sun) / std::max(length3(at_ground), 1.0);
        for (std::size_t channel = 0; channel < sum.size(); ++channel) {
            core::at(sum, channel) +=
                core::at(seen, channel) * core::at(sun_t, channel) * std::max(up, 0.0) * kGroundAlbedo / kPi;
        }
    }
    return sum;
}

Dir sky_view_direction(const std::array<double, 2>& uv)
{
    const double u = core::at(uv, 0);
    const double v = core::at(uv, 1);
    const double azimuth = kPi * u * u;
    const double signed_root = (2.0 * v) - 1.0;
    const double elevation = (signed_root < 0.0 ? -1.0 : 1.0) * signed_root * signed_root * (kPi / 2.0);
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

// --- (added by the physical air) the weather's aerosol -----------------------------------------------------------------

namespace {

constexpr double kGreenWavelength = 550.0; // nm
constexpr Rgb kWavelengths{680.0, 550.0, 440.0};

struct AerosolKind {
    double visibility = kClearVisibility; // metres: the haze's layer alone
    double mist_visibility = 0.0;         // metres, at the ground with the mist (0: none)
    double angstrom = 1.3;
    double albedo = 0.9;
    double g = 0.8;
};

Aerosol aerosol_of(const AerosolKind& kind)
{
    const double air_green = core::at(kRayleigh, 1);
    const double haze = std::max(kKoschmieder / kind.visibility - air_green, 0.0);
    Aerosol out;
    out.height = kHazeHeight;
    out.mist_height = kMistHeight;
    out.g = kind.g;
    if (kind.mist_visibility > 0.0 && haze > 0.0) {
        const double whole = kKoschmieder / kind.mist_visibility - air_green;
        out.mist = std::max(whole - haze, 0.0) / haze;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double shape = std::pow(core::at(kWavelengths, channel) / kGreenWavelength, -kind.angstrom);
        core::at(out.extinction, channel) = haze * shape;
        core::at(out.scattering, channel) = haze * shape * kind.albedo;
    }
    return out;
}

struct AerosolSlot {
    std::mutex mutex;
    Weather weather = Weather::kClear;
    Aerosol value = aerosol_for(Weather::kClear);
};
AerosolSlot& aerosol_slot()
{
    static AerosolSlot slot;
    return slot;
}

} // namespace

Aerosol aerosol_for(Weather weather)
{
    constexpr double kHazyAngstrom = 1.0;
    constexpr double kMistAngstrom = 0.3;
    constexpr double kMistAlbedo = 0.97;
    switch (weather) {
    case Weather::kHazy:
        return aerosol_of({.visibility = kHazyVisibility, .angstrom = kHazyAngstrom});
    case Weather::kMisty:
        return aerosol_of({.visibility = kClearVisibility,
                           .mist_visibility = kMistyVisibility,
                           .angstrom = kMistAngstrom,
                           .albedo = kMistAlbedo});
    case Weather::kClear:
        break;
    }
    return aerosol_of({});
}

Weather parse_weather(const char* text)
{
    const auto is = [text](const char* word) { return text != nullptr && std::strcmp(text, word) == 0; };
    if (is("hazy") || is("1") || is("cinematic")) {
        return Weather::kHazy;
    }
    if (is("misty") || is("2")) {
        return Weather::kMisty;
    }
    return Weather::kClear;
}

const char* weather_name(Weather weather)
{
    switch (weather) {
    case Weather::kClear:
        return "clear";
    case Weather::kHazy:
        return "hazy";
    case Weather::kMisty:
        return "misty";
    }
    return "?";
}

void set_weather(Weather weather)
{
    AerosolSlot& slot = aerosol_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.weather = weather;
    slot.value = aerosol_for(weather);
}

Weather weather()
{
    AerosolSlot& slot = aerosol_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.weather;
}

void set_aerosol(const Aerosol& aerosol)
{
    AerosolSlot& slot = aerosol_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.value = aerosol;
}

Aerosol aerosol()
{
    AerosolSlot& slot = aerosol_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.value;
}

double aerosol_density(const Aerosol& aerosol, double height)
{
    const double h = std::max(height, 0.0);
    double density = std::exp(-h / aerosol.height);
    if (aerosol.mist > 0.0 && aerosol.mist_height > 0.0) {
        density += aerosol.mist * std::exp(-h / aerosol.mist_height);
    }
    return density;
}

double aerosol_phase(double cosine, double g)
{
    const double mu = std::clamp(cosine, -1.0, 1.0);
    return kMiePhase * ((1.0 - (g * g)) * (1.0 + (mu * mu))) /
           ((2.0 + (g * g)) * std::pow(std::max(1.0 + (g * g) - (2.0 * g * mu), 1e-9), kMiePower));
}

double rayleigh_phase(double cosine)
{
    const double mu = std::clamp(cosine, -1.0, 1.0);
    return kRayleighPhase * (1.0 + (mu * mu));
}

GroundLight ground_light(double elevation, const Aerosol& air)
{
    GroundLight out;
    constexpr uint32_t kLightSteps = 64;
    if (elevation > 0.0) {
        out.direct = luma(sun_transmittance(elevation, kLightSteps, air)) * std::sin(elevation);
    }
    // the sky's: every order (Psi from a table of this air), cosine-weighted over the upper hemisphere (both sides of
    // the sun's plane)
    constexpr MultipleSteps kTableSteps{.steps = 10, .light = 8};
    MultipleTable table(static_cast<std::size_t>(kMultipleWidth) * kMultipleHeight);
    for (uint32_t row = 0; row < kMultipleHeight; ++row) {
        const double altitude = (static_cast<double>(row) + kHalf) / kMultipleHeight * (kTopRadius - kGroundRadius);
        for (uint32_t column = 0; column < kMultipleWidth; ++column) {
            const double cos_sun = (((static_cast<double>(column) + kHalf) / kMultipleWidth) * 2.0) - 1.0;
            core::at(table, (static_cast<std::size_t>(row) * kMultipleWidth) + column) =
                multiple_scattering({.altitude = altitude, .cos_sun = cos_sun}, kTableSteps, air);
        }
    }
    constexpr std::size_t kRings = 8;
    constexpr std::size_t kAzimuths = 8;
    constexpr SkySteps kViewSteps{.view = 32, .light = 16};
    const Dir sun = sun_direction_at(elevation);
    const double ring = (kPi / 2.0) / static_cast<double>(kRings);
    const double across = kPi / static_cast<double>(kAzimuths);
    for (std::size_t r = 0; r < kRings; ++r) {
        const double e = (static_cast<double>(r) + kHalf) * ring;
        for (std::size_t a = 0; a < kAzimuths; ++a) {
            const double azimuth = (static_cast<double>(a) + kHalf) * across;
            const Dir view{std::cos(e) * std::cos(azimuth), std::sin(e), std::cos(e) * std::sin(azimuth)};
            out.sky += 2.0 * luma(sky_radiance_ms(view, sun, kViewSteps, table, air)) * std::sin(e) * std::cos(e) *
                       ring * across;
        }
    }
    return out;
}

std::vector<GroundLightRgb> ground_light_table(double lowest, std::size_t count, const Aerosol& air)
{
    constexpr uint32_t kLightSteps = 64;
    constexpr MultipleSteps kTableSteps{.steps = 10, .light = 8};
    MultipleTable table(static_cast<std::size_t>(kMultipleWidth) * kMultipleHeight);
    for (uint32_t row = 0; row < kMultipleHeight; ++row) {
        const double altitude = (static_cast<double>(row) + kHalf) / kMultipleHeight * (kTopRadius - kGroundRadius);
        for (uint32_t column = 0; column < kMultipleWidth; ++column) {
            const double cos_sun = (((static_cast<double>(column) + kHalf) / kMultipleWidth) * 2.0) - 1.0;
            core::at(table, (static_cast<std::size_t>(row) * kMultipleWidth) + column) =
                multiple_scattering({.altitude = altitude, .cos_sun = cos_sun}, kTableSteps, air);
        }
    }
    constexpr std::size_t kRings = 8;
    constexpr std::size_t kAzimuths = 8;
    constexpr SkySteps kViewSteps{.view = 32, .light = 16};
    const double ring = (kPi / 2.0) / static_cast<double>(kRings);
    const double across = kPi / static_cast<double>(kAzimuths);
    const double unit = sky_exposure() / kPi;
    constexpr double kRadiansPerDegree = kPi / 180.0;
    std::vector<GroundLightRgb> out(count);
    for (std::size_t entry = 0; entry < count; ++entry) {
        const double elevation = (lowest + static_cast<double>(entry)) * kRadiansPerDegree;
        GroundLightRgb& here = core::at(out, entry);
        if (elevation > 0.0) {
            const Rgb through = sun_transmittance(elevation, kLightSteps, air);
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(here.sun, ch) = core::at(through, ch) * unit;
            }
        }
        const Dir sun = sun_direction_at(elevation);
        for (std::size_t r = 0; r < kRings; ++r) {
            const double e = (static_cast<double>(r) + kHalf) * ring;
            for (std::size_t a = 0; a < kAzimuths; ++a) {
                const double azimuth = (static_cast<double>(a) + kHalf) * across;
                const Dir view{std::cos(e) * std::cos(azimuth), std::sin(e), std::cos(e) * std::sin(azimuth)};
                const Rgb radiance = sky_radiance_ms(view, sun, kViewSteps, table, air);
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    core::at(here.sky, ch) +=
                        2.0 * core::at(radiance, ch) * std::sin(e) * std::cos(e) * ring * across * unit;
                }
            }
        }
    }
    return out;
}

} // namespace render::rt
