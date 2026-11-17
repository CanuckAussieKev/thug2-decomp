// NOT ORIGINAL - thug2_daylight_check.exe: the time of day's maths (engine/gfx/rt_daylight.h) without the game - the
// sun's path against an independent rotation of the celestial sphere and known places (noon, the equinox's sunrise and
// sunset), its inverse (the time at an elevation), each level's own day (the path's sun at the level's time is the
// level's sun), the moon, the light at the times of a day (the level's own at its time, night dark but lit by the moon,
// the lamps and stars only at night), the original's states blended without a jump, and the settings' text. Exit code 0
// when every check passes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_clouds.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_sky.h"
#include "tests/no_error_dialogs.h"

namespace {

namespace rt = render::rt;

constexpr double kPi = std::numbers::pi;
constexpr double kRadians = kPi / 180.0;
constexpr double kDegrees = 180.0 / kPi;
constexpr double kAngleTolerance = 1e-6;   // degrees: the path against the rotation
constexpr double kDirectionTolerance = 1e-4; // a direction's components (floats)

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const std::string& what)
{
    if (!ok) {
        ++failures();
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("ok: %s\n", what.c_str());
    }
}

std::string number(double value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.4f", value);
    return text.data();
}

bool same_direction(const rt::DayVec& a, const rt::DayVec& b, double tolerance = kDirectionTolerance)
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(static_cast<double>(core::at(a, i)) - static_cast<double>(core::at(b, i))) > tolerance) {
            return false;
        }
    }
    return true;
}

double angle_between(double a, double b)
{
    const double d = std::fmod(std::abs(a - b), 360.0);
    return std::min(d, 360.0 - d);
}

// The sun's place by rotating the celestial sphere: the sun at hour angle h and declination d as a vector in the
// equatorial frame (x toward the meridian's south point at the equator, y west, z the pole), turned about the west axis
// by 90 - latitude into the horizon frame (x south, y west, z up). Independent of rt_daylight's formulae.
rt::SkyPlace rotated_place(double latitude, double declination, double hours)
{
    const double h = (hours - 12.0) * 15.0 * kRadians;
    const double d = declination * kRadians;
    const double colatitude = (90.0 - latitude) * kRadians;
    const std::array<double, 3> equatorial{std::cos(d) * std::cos(h), std::cos(d) * std::sin(h), std::sin(d)};
    // (about y: z toward x by the colatitude)
    const double south = (std::get<0>(equatorial) * std::cos(colatitude)) - (std::get<2>(equatorial) * std::sin(colatitude));
    const double west = std::get<1>(equatorial);
    const double up = (std::get<0>(equatorial) * std::sin(colatitude)) + (std::get<2>(equatorial) * std::cos(colatitude));
    const double elevation = std::asin(std::clamp(up, -1.0, 1.0)) * kDegrees;
    // (from north through east: north is -south, east is -west)
    double azimuth = std::atan2(-west, -south) * kDegrees;
    azimuth = azimuth < 0.0 ? azimuth + 360.0 : azimuth;
    return {.elevation = elevation, .azimuth = azimuth};
}

void check_path()
{
    // the path against the rotation, every quarter hour, several latitudes and dates
    double worst = 0.0;
    for (const double latitude : {0.0, 20.0, 34.0, 51.5, -33.9}) {
        for (const double declination : {-23.44, 0.0, 15.0, 23.44}) {
            const rt::SunPath path{.latitude = latitude, .declination = declination};
            for (int quarter = 0; quarter < 96; ++quarter) {
                const double hours = quarter * 0.25;
                const rt::SkyPlace got = rt::sun_place(path, hours);
                const rt::SkyPlace want = rotated_place(latitude, declination, hours);
                worst = std::max(worst, std::abs(got.elevation - want.elevation));
                if (std::abs(want.elevation) < 89.9) {
                    worst = std::max(worst, angle_between(got.azimuth, want.azimuth));
                }
            }
        }
    }
    expect(worst < kAngleTolerance, "the sun's path against a rotation of the celestial sphere (worst " + number(worst) +
                                        " degrees, 1920 places)");
    // known places
    const rt::SunPath here{.latitude = 34.0, .declination = 15.0};
    const rt::SkyPlace noon = rt::sun_place(here, 12.0);
    expect(std::abs(noon.elevation - 71.0) < 1e-9 && std::abs(noon.azimuth - 180.0) < 1e-9,
           "noon at 34 N, declination 15: 71 degrees up, due south (" + number(noon.elevation) + ", " +
               number(noon.azimuth) + ")");
    const rt::SunPath equinox{.latitude = 0.0, .declination = 0.0};
    const rt::SkyPlace rising = rt::sun_place(equinox, 6.0);
    const rt::SkyPlace setting = rt::sun_place(equinox, 18.0);
    expect(std::abs(rising.elevation) < 1e-9 && std::abs(rising.azimuth - 90.0) < 1e-9 &&
               std::abs(setting.elevation) < 1e-9 && std::abs(setting.azimuth - 270.0) < 1e-9,
           "the equator at the equinox: the sun rises due east at 6:00, sets due west at 18:00");
    const rt::SkyPlace afternoon = rt::sun_place({.latitude = 34.0, .declination = 0.0}, 15.0);
    expect(std::abs(afternoon.elevation - 35.889) < 1e-3 && std::abs(afternoon.azimuth - 240.79) < 0.02,
           "34 N at the equinox, 15:00: 35.889 degrees up, azimuth 240.79 (" + number(afternoon.elevation) + ", " +
               number(afternoon.azimuth) + ")");
    // the inverse
    double inverse = 0.0;
    for (const double elevation : {-6.0, 0.0, 3.0, 20.0, 52.0, 70.0}) {
        for (const bool after : {false, true}) {
            const double hours = rt::hours_at_elevation(here, elevation, after);
            inverse = std::max(inverse, std::abs(rt::sun_place(here, hours).elevation - elevation));
            expect(after ? hours > 12.0 : hours < 12.0,
                   "the time at " + number(elevation) + " degrees on its side of noon (" + number(hours) + ")");
        }
    }
    expect(inverse < 1e-9, "the time at an elevation gives that elevation (worst " + number(inverse) + ")");
    expect(rt::hours_at_elevation(here, 80.0, true) < 0.0, "never 80 degrees up there: none");
    // the moon: full, opposite - highest at midnight, as high as the sun's noon with the declination turned round
    const rt::SkyPlace moon = rt::moon_place(here, 0.0);
    expect(std::abs(moon.elevation - (90.0 - 34.0 - 15.0)) < 1e-9 && std::abs(moon.azimuth - 180.0) < 1e-9,
           "the full moon at midnight: due south, " + number(moon.elevation) + " degrees up");
    expect(rt::moon_place(here, 12.0).elevation < 0.0, "the full moon down at noon");
}

void check_native()
{
    struct Baked {
        const char* name;
        double heading;
        double elevation;
    };
    // (the baked suns of BO, z_dt, sch and the highest the fit allows)
    constexpr std::array<Baked, 4> kBaked{{{"BO", -75.0, 52.0}, {"z_dt", 44.0, 53.0}, {"sch", -45.0, 36.0},
                                           {"overhead", 10.0, 84.0}}};
    for (const Baked& baked : kBaked) {
        const double h = baked.heading * kRadians;
        const double e = baked.elevation * kRadians;
        const rt::DayVec sun{static_cast<float>(std::sin(h) * std::cos(e)), static_cast<float>(std::sin(e)),
                             static_cast<float>(std::cos(h) * std::cos(e))};
        const rt::NativeDay day =
            rt::fit_native_day({.sun = sun, .have_sun = true, .from_bake = true, .state = rt::TodState::kAfternoon});
        const rt::DayVec at = rt::toward_place(day.path, rt::sun_place(day.path, day.hours));
        expect(same_direction(at, sun) && same_direction(day.toward, sun) && day.hours > 12.0 && day.from_bake,
               std::string(baked.name) + ": the path's sun at the level's time (" + number(day.hours) +
                   ") is its baked sun, after noon (latitude " + number(day.path.latitude) + ")");
        // the level's light at its own time is the level's
        const rt::DayLight light = rt::day_light_at(day, day.hours);
        expect(std::abs(light.direct - 1.0F) < 1e-3F && std::abs(std::get<1>(light.sky) - 1.0F) < 1e-3F &&
                   std::abs(light.shadow - 1.0F) < 1e-3F && light.lamps == 0.0F && !light.moonlit &&
                   same_direction(light.light, sun) &&
                   light.blend == rt::TodBlend{},
               std::string(baked.name) + ": at its own time the light is the level's (direct " + number(light.direct) +
                   ", sky " + number(std::get<1>(light.sky)) + ", the afternoon state)");
        // the sun goes west after it: its heading turns the way the path's azimuth does
        const rt::SkyPlace now = rt::heading_of(at);
        const rt::SkyPlace later = rt::heading_of(rt::toward_place(day.path, rt::sun_place(day.path, day.hours + 0.5)));
        const double turned = std::fmod(later.azimuth - now.azimuth + 360.0, 360.0);
        expect(turned > 0.0 && turned < 90.0 && later.elevation < now.elevation,
               std::string(baked.name) + ": half an hour later lower and turned on (" + number(turned) + " degrees)");
    }
    expect(rt::fit_native_day({.state = rt::TodState::kAfternoon}).path.latitude == rt::kTodLatitude,
           "no sun: 45 degrees up at the default latitude");
    const rt::NativeDay night = rt::fit_native_day({.state = rt::TodState::kNight});
    expect(night.hours == rt::kNightHours, "a level whose own state is night: its time midnight");
    // (added by the level's compass) a level whose data gives its north: the path turned there - its own time the same
    // (its sun as high), its sun's heading north + the path's azimuth; every time of day turned with it
    const rt::DayVec college_sun{-0.5788F, 0.5691F, 0.5840F};
    const rt::NativeDay fitted =
        rt::fit_native_day({.sun = college_sun, .have_sun = true, .from_bake = true, .state = rt::TodState::kAfternoon});
    constexpr double kCollegeNorth = 105.6;
    const rt::NativeDay oriented = rt::fit_native_day({.sun = college_sun,
                                                       .have_sun = true,
                                                       .from_bake = true,
                                                       .state = rt::TodState::kAfternoon,
                                                       .north_set = true,
                                                       .north_heading = kCollegeNorth});
    const rt::SkyPlace own_place = rt::heading_of(oriented.toward);
    const rt::SkyPlace path_place = rt::sun_place(oriented.path, oriented.hours);
    const double own_turn = std::remainder(own_place.azimuth - (kCollegeNorth + path_place.azimuth), 360.0);
    expect(std::abs(oriented.path.north_heading - kCollegeNorth) < 1e-9 && std::abs(oriented.hours - fitted.hours) < 1e-9 &&
               std::abs(own_place.elevation - rt::heading_of(college_sun).elevation) < 0.05 && std::abs(own_turn) < 0.05,
           "a level's north from its data: the path turned there (own time " + number(oriented.hours) +
               " as without it, its sun as high, heading " + number(own_place.azimuth) + ")");
    constexpr double kMorning = 6.0 + (10.0 / 60.0);
    const rt::SkyPlace morning = rt::heading_of(rt::day_light_at(oriented, kMorning).sun);
    expect(std::abs(std::remainder(morning.azimuth - (-175.6), 360.0)) < 0.5 && std::abs(morning.elevation - 10.3) < 0.3,
           "College at 06:10 with its north at 105.6: the sun behind the clock tower seen from the plaza (heading " +
               number(morning.azimuth) + ", " + number(morning.elevation) + " degrees up)");
}

void check_objects_and_clouds(const rt::NativeDay& day, const rt::DayLight& midnight);
double luma_of(const rt::DayRgb& c)
{
    return (0.2126 * static_cast<double>(std::get<0>(c))) + (0.7152 * static_cast<double>(std::get<1>(c))) +
           (0.0722 * static_cast<double>(std::get<2>(c)));
}

void check_light()
{
    const rt::NativeDay day = rt::fit_native_day(
        {.sun = {0.3214F, 0.7660F, 0.5567F}, .have_sun = true, .from_bake = true, .state = rt::TodState::kAfternoon});
    // from noon to midnight: the direct light and the sky darken; the lamps and the stars only come with the night
    double last_sky = 2.0;
    double last_direct = 2.0;
    bool darkening = true;
    bool sky_darkening = true;
    for (double hours = 12.0; hours <= 23.99; hours += 0.05) {
        const rt::DayLight light = rt::day_light_at(day, hours);
        // (the sky's light never brightens into the night: the night's floor comes in while the twilight is still
        // brighter than it)
        // (round S: once the twilight is gone the rising moon's sky lights the night - the check ends there)
        const double sky = std::get<1>(light.sky);
        if (rt::heading_of(light.sun).elevation < -10.0) {
            break;
        }
        sky_darkening = sky_darkening && sky <= last_sky + 1e-6;
        if (sky > last_sky + 1e-6) {
            std::printf("  (the sky's light rises at %.2f h: %.5f -> %.5f, the sun %.2f up)\n", hours, last_sky, sky,
                        rt::heading_of(light.sun).elevation);
        }
        last_sky = sky;
        if (!light.moonlit) {
            darkening = darkening && light.direct <= last_direct + 1e-6;
            last_direct = light.direct;
        }
    }
    expect(darkening, "noon to dusk: the sun's light never brightens");
    expect(sky_darkening, "noon to the twilight's end: the sky's light never brightens");
    const rt::DayLight noon = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kNoon}, day));
    const rt::DayLight dusk = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kDusk}, day));
    const rt::DayLight dawn = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kDawn}, day));
    const rt::DayLight midnight = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kNight}, day));
    expect(noon.lamps == 0.0F && noon.direct > 0.9F && !noon.moonlit,
           "noon: sunlit, no lamps (direct " + number(noon.direct) + ")");
    expect(std::abs(rt::heading_of(dusk.sun).elevation - rt::kDuskElevation) < 1e-3 &&
               std::abs(rt::heading_of(dawn.sun).elevation - rt::kDuskElevation) < 1e-3 && dusk.hours > 12.0 &&
               dawn.hours < 12.0,
           "dusk and dawn: the sun 3 degrees up, setting and rising (" + number(dusk.hours) + ", " + number(dawn.hours) + ")");
    expect(dusk.direct < noon.direct && std::get<0>(dusk.direct_tint) > std::get<2>(dusk.direct_tint) &&
               dusk.illuminance_lux > static_cast<float>(rt::kLampsOnLux) && dusk.lamps == 0.0F,
           "dusk: the sun weaker and warm, " + number(dusk.illuminance_lux) + " lux - the lamps' photocells still off");
    // (round S) the physical moon: the full moon's light on the ground, its colour the sun's reflected (not blue)
    const double unit = rt::unit_illuminance();
    const double moon_sine = std::max(0.0F, std::get<1>(midnight.light));
    const double moon_lux = luma_of(midnight.sun_light) * moon_sine * unit;
    expect(midnight.moonlit && midnight.lamps == 1.0F && midnight.stars == 1.0F && midnight.moon_disc == 1.0F &&
               moon_lux > 0.1 && moon_lux < 0.3 &&
               (std::get<0>(midnight.light) * std::get<0>(midnight.moon)) + (std::get<1>(midnight.light) * std::get<1>(midnight.moon)) + (std::get<2>(midnight.light) * std::get<2>(midnight.moon)) > 0.9999F,
           "midnight: moonlit - the full moon " + number(moon_lux) + " lux on the ground (0.1 - 0.3), lamps on");
    expect(std::get<0>(midnight.direct_tint) >= std::get<2>(midnight.direct_tint) &&
               std::get<0>(rt::moon_irradiance(rt::kMoonFullAge)) > std::get<2>(rt::moon_irradiance(rt::kMoonFullAge)),
           "midnight: the moonlight not blue - the sun's light reflected, a little redder (tint " +
               number(std::get<0>(midnight.direct_tint)) + " / " + number(std::get<2>(midnight.direct_tint)) + ")");
    const double ratio = 1.0 / luma_of(rt::moon_irradiance(rt::kMoonFullAge));
    expect(ratio > 3.0e5 && ratio < 6.0e5, "the sun : the full moon at the top " + number(ratio) + " (about 4 x 10^5)");
    const double quarter = rt::moon_phase_law(90.0);
    expect(rt::moon_phase_law(0.0) == 1.0 && quarter > 0.06 && quarter < 0.12 && rt::moon_phase_law(180.0) < 1e-3,
           "the moon's phase law: 1 full, " + number(quarter) + " at a quarter, nothing new");
    const rt::DayLight new_moon = rt::day_light_at(day, rt::kNightHours, {.moon_age = 0.0});
    expect(!new_moon.moonlit && new_moon.illuminance_lux > 0.001F && new_moon.illuminance_lux < 0.004F,
           "a new moon's midnight: the night sky's own light alone, " + number(new_moon.illuminance_lux) + " lux");
    expect(midnight.illuminance_lux > 10.0F * new_moon.illuminance_lux,
           "the full moon's night " + number(midnight.illuminance_lux) + " lux, far brighter than a new moon's");
    // (round S) the clouds dim the light: the direct light by the deck's beam, the rest its diffuse light
    for (const float cover : {0.5F, 1.0F}) {
        const rt::DayLight cloudy = rt::day_light_at(day, noon.hours, {.cloud_cover = cover});
        const rt::CloudLayerLight layer =
            rt::cloud_layer_light(cover, std::sin(rt::heading_of(noon.sun).elevation * std::numbers::pi / 180.0));
        const double kept = luma_of(cloudy.sun_light) / luma_of(noon.sun_light);
        expect(std::abs(kept - layer.beam) < 1e-3 && std::abs(cloudy.cloud_beam - layer.beam) < 1e-4 &&
                   cloudy.illuminance_lux < noon.illuminance_lux && cloudy.illuminance_lux > 0.05F * noon.illuminance_lux &&
                   luma_of(cloudy.sky_light) > 0.0,
               "clouds " + number(cover) + ": the direct light x " + number(kept) + " (the deck's beam " +
                   number(layer.beam) + ", optical depth " + number(layer.mean_depth) + "), the ground " +
                   number(cloudy.illuminance_lux / noon.illuminance_lux) + " of the clear day's");
        if (cover == 1.0F) {
            expect(layer.beam < 0.01 && cloudy.illuminance_lux < 0.5F * noon.illuminance_lux,
                   "overcast: no sun through the deck (" + number(layer.beam) + "), the light " +
                       number(cloudy.illuminance_lux) + " lux, under half the clear day's");
        }
    }
    // (round S) the pre-exposure: night held near the day's values, divided out by the meter
    rt::DayLight exposed = midnight;
    rt::pre_expose(&exposed);
    rt::DayLight day_exposed = noon;
    rt::pre_expose(&day_exposed);
    expect(day_exposed.pre_exposure == 1.0F && exposed.pre_exposure > 1000.0F &&
               exposed.pre_exposure <= static_cast<float>(rt::kMostPreExposure) &&
               std::abs(luma_of(exposed.sun_light) / luma_of(midnight.sun_light) - exposed.pre_exposure) <
                   1e-3 * exposed.pre_exposure,
           "the pre-exposure: 1 by day, " + number(exposed.pre_exposure) + " at midnight (the light x it)");
    expect(midnight.blend == rt::TodBlend{.from = rt::TodState::kNight, .to = rt::TodState::kNight},
           "midnight: the original's night state");
    // the original's states blended without a jump: their weights along the elevation, both sides of noon
    for (const bool after : {false, true}) {
        double worst = 0.0;
        std::array<double, rt::kTodStates> last{};
        bool first = true;
        for (double elevation = 40.0; elevation >= -20.0; elevation -= 0.05) {
            const rt::TodBlend blend = rt::tod_blend(elevation, after);
            std::array<double, rt::kTodStates> weights{};
            core::at(weights, static_cast<std::size_t>(blend.from)) += 1.0 - blend.fraction;
            core::at(weights, static_cast<std::size_t>(blend.to)) += blend.fraction;
            if (!first) {
                for (std::size_t i = 0; i < weights.size(); ++i) {
                    worst = std::max(worst, std::abs(core::at(weights, i) - core::at(last, i)));
                }
            }
            last = weights;
            first = false;
        }
        expect(worst < 0.02, std::string(after ? "evening" : "morning") +
                                 ": the original's states' weights change smoothly (worst step " + number(worst) + ")");
    }
    // the sky's light: brighter the higher the sun, none deep in the night
    bool rising = true;
    for (double elevation = -20.0; elevation < 90.0; elevation += 1.0) {
        rising = rising && rt::sky_light_at(elevation + 1.0) >= rt::sky_light_at(elevation);
    }
    expect(rising && std::abs(rt::sky_light_at(90.0) - 1.0) < 1e-12 && rt::sky_light_at(-20.0) < 1e-3,
           "the sky's light rises with the sun (overhead 1, " + number(rt::sky_light_at(-20.0)) + " at -20)");
    expect(rt::sun_light_at(-5.0) < 1e-6 && rt::sun_light_at(10.0) < rt::sun_light_at(50.0),
           "the sun's light: none below the horizon, more the higher it is");
    check_objects_and_clouds(day, midnight);
}

// (added for the night's objects and clouds) What isn't relit darkens as the level does; the clouds at night are lit by
// the night sky alone, and nothing jumps over the dusk.
void check_objects_and_clouds(const rt::NativeDay& day, const rt::DayLight& midnight)
{
    const rt::DayLight own = rt::day_light_at(day, day.hours);
    const rt::DayRgb own_surface = own.bounce;
    const rt::DayRgb night_surface = midnight.bounce;
    const rt::DayRgb off = rt::DayLight{}.bounce;
    expect(off == rt::DayRgb{1.0F, 1.0F, 1.0F}, "the lighting state's bounce without the clock: 1");
    expect(std::abs(std::get<1>(own_surface) - 1.0F) < 1e-3F, "the bounce at the level's own time: 1 (" +
                                                                    number(std::get<1>(own_surface)) + ")");
    expect(std::get<1>(night_surface) > 0.0F && std::get<1>(night_surface) < 0.05F,
           "the bounce at midnight: the moon's - physical, far darker than the original's night (" +
               number(std::get<1>(night_surface)) + ")");

    // (the one lighting state) the direct light's colour is its strength x tint; the sun sharp, the moon soft
    for (const rt::DayLight* light : {&own, &midnight}) {
        bool product = true;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            product = product && std::abs(core::at(light->light_colour, ch) -
                                          (light->direct * core::at(light->direct_tint, ch))) < 1e-6F;
        }
        expect(product, "the lighting state: light_colour = direct x direct_tint");
    }
    // (round S - one sun and one moon) their real sizes, from the one place
    expect(own.light_radius == rt::kSunRadiusDegrees && !own.moonlit && std::abs(rt::kSunRadiusDegrees - 0.2667F) < 1e-3F,
           "the lighting state: the sun's real radius by day (" + number(rt::kSunRadiusDegrees) + " degrees)");
    expect(midnight.moonlit && midnight.light_radius == rt::kMoonRadiusDegrees &&
               std::abs(rt::kMoonRadiusDegrees - 0.259F) < 2e-3F,
           "the lighting state: the moon's real radius at night (" + number(rt::kMoonRadiusDegrees) + " degrees)");
    expect(std::get<1>(midnight.light_colour) < 0.3F, "the lighting state: the moon weak (" +
                                                          number(std::get<1>(midnight.light_colour)) + ")");
    const rt::CloudLight none = rt::cloud_light(rt::DayLight{});
    expect(!none.clock && none.light == rt::DayRgb{1.0F, 1.0F, 1.0F}, "cloud_light without the clock: the sky pass's sun");
    const rt::CloudLight at_own = rt::cloud_light(own);
    expect(at_own.clock && at_own.light == rt::DayRgb{1.0F, 1.0F, 1.0F} && same_direction(at_own.toward, own.sun),
           "cloud_light at the level's own time: the sun whole");
    const rt::CloudLight night = rt::cloud_light(midnight);
    expect(same_direction(night.toward, midnight.moon) && std::get<1>(night.light) > 0.0F &&
               std::get<1>(night.light) < 1e-5F,
           "cloud_light at midnight: the moon's light (" + number(std::get<1>(night.light)) + " of the sun's)");
}

void check_settings()
{
    rt::TodChoice choice;
    expect(rt::parse_tod("level", &choice) && choice.preset == rt::TodPreset::kLevel, "tod=level");
    expect(rt::parse_tod(" Dusk ", &choice) && choice.preset == rt::TodPreset::kDusk, "tod=Dusk (any case)");
    expect(rt::parse_tod("07:30", &choice) && choice.preset == rt::TodPreset::kClock && choice.hours == 7.5F &&
               rt::tod_text(choice) == "07:30",
           "tod=07:30, written back the same");
    expect(rt::parse_tod("7:05", &choice) && choice.hours > 7.08F && choice.hours < 7.09F, "tod=7:05");
    const rt::TodChoice kept = choice;
    expect(!rt::parse_tod("24:00", &choice) && !rt::parse_tod("12:60", &choice) && !rt::parse_tod("12:5", &choice) &&
               !rt::parse_tod("midday", &choice) && !rt::parse_tod("", &choice) && choice == kept,
           "tod: anything else refused, the choice kept");
    std::vector<rt::TodLevel> levels;
    expect(rt::parse_tod_levels("NO:night, AU:dusk,", &levels) && levels.size() == 2 && levels.front().level == "NO" &&
               levels.front().choice.preset == rt::TodPreset::kNight && levels.back().choice.preset == rt::TodPreset::kDusk,
           "tod_levels=NO:night, AU:dusk");
    levels.clear();
    expect(!rt::parse_tod_levels("NO:night,AU", &levels) && levels.size() == 1, "tod_levels: a bad entry stops it");
    rt::TodSettings settings;
    expect(!rt::tod_may_run(settings), "the defaults: the clock never runs");
    settings.levels = {{.level = "NO", .choice = {.preset = rt::TodPreset::kNight}}};
    expect(rt::tod_may_run(settings), "a level's own time: it may");
    const auto hash = [](const char* name) { return std::string(name) == "NO" ? 7U : 9U; };
    expect(rt::tod_choice_for(settings, 7U, hash).preset == rt::TodPreset::kNight &&
               rt::tod_choice_for(settings, 8U, hash).preset == rt::TodPreset::kLevel,
           "a level listed gets its own, another the setting's");
    settings.cycle_minutes = 0.0F;
    rt::set_tod_settings(settings);
    expect(rt::tod_settings().cycle_minutes == rt::kLeastCycleMinutes, "a day at least a minute long");
    // the published light: numbered on change only
    rt::set_day_light({.on = true, .lamps = 1.0F});
    const uint64_t serial = rt::day_light().serial;
    rt::set_day_light({.on = true, .lamps = 1.0F});
    const uint64_t same = rt::day_light().serial;
    rt::set_day_light({});
    expect(same == serial && rt::day_light().serial == serial + 1 && !rt::day_light().on,
           "the published light numbered when it changes, not otherwise");
}

} // namespace

// (the one lighting path - engine/gfx/rt_light_field.h) The level's light where a thing is: a moonlit street's cell and a
// lamp-lit corner's give their own ratios (the lamp's light the same at both times, so a thing under it stays lit), a
// point between the two cells their mix, a point far from both the nearest's, the level's whole all of it; no field: 1.
void check_light_field()
{
    const rt::LightRatio none = rt::light_ratio_at({0.0F, 0.0F, 0.0F});
    expect(none == rt::LightRatio{}, "no light field: every ratio 1 (the original's look)");
    auto field = std::make_shared<rt::LightField>();
    // the street: lit by the sun at the bake (direct 0.5, indirect 0.5), by the moon now (0.05, 0.1)
    const rt::LightParts street{.direct_now = {0.05F, 0.05F, 0.05F},
                                .indirect_now = {0.1F, 0.1F, 0.1F},
                                .direct_own = {0.5F, 0.5F, 0.5F},
                                .indirect_own = {0.5F, 0.5F, 0.5F},
                                .local = {}};
    // the corner: in shade (indirect 0.4 at the bake, 0.08 now) under a lamp (0.6, its own light)
    const rt::LightParts corner{.direct_now = {},
                                .indirect_now = {0.08F, 0.08F, 0.08F},
                                .direct_own = {},
                                .indirect_own = {0.4F, 0.4F, 0.4F},
                                .local = {0.6F, 0.6F, 0.6F}};
    constexpr float kCell = rt::kLightFieldCell;
    for (int i = 0; i < 4; ++i) {
        field->add({(0.5F * kCell) + static_cast<float>(i), 10.0F, 0.5F * kCell}, street);
        field->add({(1.5F * kCell) + static_cast<float>(i), 10.0F, 0.5F * kCell}, corner);
    }
    const rt::LightRatio on_street = field->at({0.5F * kCell, 0.5F * kCell, 0.5F * kCell});
    expect(std::abs(std::get<1>(on_street.whole) - 0.15F) < 1e-4F &&
               std::abs(std::get<1>(on_street.indirect) - 0.2F) < 1e-4F,
           "the field on the moonlit street: whole " + number(std::get<1>(on_street.whole)) + " (0.15), indirect " +
               number(std::get<1>(on_street.indirect)) + " (0.2)");
    const rt::LightRatio under_lamp = field->at({1.5F * kCell, 0.5F * kCell, 0.5F * kCell});
    expect(std::abs(std::get<1>(under_lamp.whole) - 0.68F) < 1e-4F,
           "the field under the lamp: the lamp's own light kept - whole " + number(std::get<1>(under_lamp.whole)) +
               " (0.68)");
    expect(std::abs(std::get<1>(under_lamp.indirect) - 0.2F) < 1e-4F,
           "under the lamp the indirect ratio is the sky's and bounce's alone (the lamp a light of its own) - " +
               number(std::get<1>(under_lamp.indirect)) + " (0.2)");
    const rt::LightRatio between = field->at({1.0F * kCell, 0.5F * kCell, 0.5F * kCell});
    expect(std::get<1>(between.whole) > std::get<1>(on_street.whole) + 0.1F &&
               std::get<1>(between.whole) < std::get<1>(under_lamp.whole) - 0.1F,
           "between the two: their mix (" + number(std::get<1>(between.whole)) + ")");
    const rt::LightRatio far_off = field->at({-2.5F * kCell, 0.5F * kCell, 0.5F * kCell});
    expect(std::abs(std::get<1>(far_off.whole) - std::get<1>(on_street.whole)) < 1e-4F,
           "a point with no cell round it: the nearest cells' light (" + number(std::get<1>(far_off.whole)) + ")");
    const rt::LightRatio whole = field->whole();
    expect(std::abs(std::get<1>(whole.whole) - ((0.15F + 0.68F) / 2.0F)) < 1e-4F,
           "the level's whole: every vertex's light (" + number(std::get<1>(whole.whole)) + ")");
    rt::set_light_field(field);
    expect(rt::light_ratio_at({0.5F * kCell, 0.5F * kCell, 0.5F * kCell}) == on_street,
           "the published field answers light_ratio_at");
    rt::set_light_field(nullptr);
    // a colour lit: each channel by its ratio, held to a byte, the alpha kept (red low, as the tod manager's)
    // (round O: the ratio is linear light, the colour display-referred - each byte by the ratio's display factor)
    const uint32_t lit = rt::scaled_colour(0x80406080U, {0.5F, 1.0F, 4.0F}, true);
    const auto byte = [](float value, float ratio) {
        return static_cast<uint32_t>(std::lround(std::min(value * std::pow(ratio, 1.0F / 2.2F), 255.0F)));
    };
    const uint32_t want = 0x80000000U | (byte(64.0F, 4.0F) << 16) | (byte(96.0F, 1.0F) << 8) | byte(128.0F, 0.5F);
    expect(lit == want, "scaled_colour: each channel by its ratio's display factor, held to a byte (" + std::to_string(lit) +
                            ", " + std::to_string(want) + ")");
    const uint32_t bright = rt::scaled_colour(0x80406080U, {8.0F, 1.0F, 1.0F}, true);
    expect((bright & 0xFFU) == 0xFFU, "a ratio past a byte's room: held to 255 (no ratio cap before it)");
}

// (round O - one light for everything, rt_light_field.h light_at) What a thing standing in the level receives is the light
// store's light there: the probes fitted over the vertices' normals, the sun, the lamps.
//  - a grey object and a grey wall at one place get one light: a corner whose walls face +x and -x, a floor facing up,
//    a ceiling facing down - each with its own sky, bounce and lamps' light (and the sun on the sunlit ones) - an object's
//    face turned as each surface takes that surface's light within 5% (each part, and the whole with the sun);
//  - where the vertices don't tell (an open plaza: every vertex facing up), the sky comes from above - a side face
//    takes half the up face's sky, a face turned down none; the bounce the same from every side;
//  - the device's lights (DeviceLights) give exactly the probe's light - c0 + k (n . toward) through Direct3D's
//    clamped lights -, the sun as the key; nothing of the game's lights is an input (device_lights takes the field's
//    light alone).
void check_point_light()
{
    constexpr float kCell = rt::kLightFieldCell;
    const rt::FieldPoint kSunToward{0.6F, 0.8F, 0.0F};
    const rt::FieldRgb kSun{1.2F, 1.1F, 0.9F};
    struct Surface {
        rt::FieldPoint normal;
        float sky = 0.0F;
        float bounce = 0.0F;
        float lamps = 0.0F;
    };
    const std::array<Surface, 4> surfaces{{{{1.0F, 0.0F, 0.0F}, 0.33F, 0.17F, 0.18F},
                                           {{-1.0F, 0.0F, 0.0F}, 0.29F, 0.11F, 0.02F},
                                           {{0.0F, 1.0F, 0.0F}, 0.62F, 0.08F, 0.15F},
                                           {{0.0F, -1.0F, 0.0F}, 0.00F, 0.20F, 0.05F}}};
    const auto dot = [](const rt::FieldPoint& a, const rt::FieldPoint& b) {
        return (std::get<0>(a) * std::get<0>(b)) + (std::get<1>(a) * std::get<1>(b)) + (std::get<2>(a) * std::get<2>(b));
    };
    auto field = std::make_shared<rt::LightField>();
    for (int i = 0; i < 16; ++i) {
        const float step = static_cast<float>(i) * 6.0F;
        for (const Surface& surface : surfaces) {
            const float nl = std::max(0.0F, dot(surface.normal, kSunToward));
            rt::LightParts parts;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                core::at(parts.sky_now, ch) = surface.sky;
                core::at(parts.indirect_now, ch) = surface.sky + surface.bounce;
                core::at(parts.direct_now, ch) = core::at(kSun, ch) * nl;
                core::at(parts.local, ch) = surface.lamps;
            }
            parts.normal = surface.normal;
            field->add({(0.5F * kCell) + step - 45.0F, 20.0F + step, (0.5F * kCell) - step + 45.0F}, parts);
        }
    }
    field->set_sun(kSunToward, kSun);
    const rt::FieldPoint here{0.5F * kCell, 0.5F * kCell, 0.5F * kCell};
    const rt::PointLight light = field->light_at(here);
    expect(light.valid, "a point in the field has a light");
    float worst = 0.0F;
    for (const Surface& surface : surfaces) {
        const float sky = std::get<1>(light.sky.at(surface.normal));
        const float bounce = std::get<1>(light.bounce.at(surface.normal));
        const float lamps = std::get<1>(light.lamps.at(surface.normal));
        const float whole = std::get<1>(light.on(surface.normal, 1.0F, 1.0F));
        const float wall = surface.sky + surface.bounce + surface.lamps +
                           (std::get<1>(kSun) * std::max(0.0F, dot(surface.normal, kSunToward)));
        const auto off = [](float got, float want) { return std::abs(got - want) / std::max(want, 0.05F); };
        worst = std::max({worst, off(sky, surface.sky), off(bounce, surface.bounce), off(lamps, surface.lamps),
                          off(whole, wall)});
        expect(off(sky, surface.sky) < 0.05F && off(bounce, surface.bounce) < 0.05F && off(lamps, surface.lamps) < 0.05F &&
                   off(whole, wall) < 0.05F,
               "a grey object's face turned as the wall facing " + number(std::get<0>(surface.normal)) + " " +
                   number(std::get<1>(surface.normal)) + " gets its light: sky " + number(sky) + " (" +
                   number(surface.sky) + "), bounce " + number(bounce) + " (" + number(surface.bounce) + "), lamps " +
                   number(lamps) + " (" + number(surface.lamps) + "), whole " + number(whole) + " (" + number(wall) + ")");
    }
    std::printf("daylight_check: the object against the walls - worst part %.4f off\n", static_cast<double>(worst));
    // the device's lights: the same light through Direct3D's clamped lights
    const rt::DeviceLights device = rt::device_lights(light, 0.5F);
    float device_worst = 0.0F;
    for (const rt::FieldPoint& normal : {rt::FieldPoint{1.0F, 0.0F, 0.0F}, rt::FieldPoint{0.0F, 0.6F, 0.8F},
                                         rt::FieldPoint{-0.48F, -0.6F, 0.64F}, rt::FieldPoint{0.0F, -1.0F, 0.0F}}) {
        const rt::FieldRgb want = light.on(normal, 0.25F, 0.5F);
        const rt::FieldRgb got = device.on(normal, 0.25F);
        for (std::size_t ch = 0; ch < 3; ++ch) {
            device_worst = std::max(device_worst, std::abs(core::at(got, ch) - core::at(want, ch)));
        }
    }
    expect(device_worst < 1e-4F + (2.0F * device.lost),
           "the device's lights give the probe's light and the sun (off by " + number(device_worst) + ", the part across " +
               "the lobe " + number(device.lost) + ")");
    // the open plaza: every vertex facing up, the sky 0.8 on them
    auto plaza = std::make_shared<rt::LightField>();
    for (int i = 0; i < 16; ++i) {
        rt::LightParts parts;
        parts.sky_now = {0.8F, 0.8F, 0.8F};
        parts.indirect_now = {0.9F, 0.9F, 0.9F};
        parts.normal = {0.0F, 1.0F, 0.0F};
        plaza->add({(0.5F * kCell) + static_cast<float>(i), 1.0F, 0.5F * kCell}, parts);
    }
    const rt::PointLight open = plaza->light_at(here);
    const float up = std::get<1>(open.sky.at({0.0F, 1.0F, 0.0F}));
    const float side = std::get<1>(open.sky.at({1.0F, 0.0F, 0.0F}));
    const float down = std::get<1>(open.sky.at({0.0F, -1.0F, 0.0F}));
    const float bounce_side = std::get<1>(open.bounce.at({1.0F, 0.0F, 0.0F}));
    expect(std::abs(up - 0.8F) < 0.01F && std::abs(side - 0.4F) < 0.01F && down < 0.01F &&
               std::abs(bounce_side - 0.1F) < 0.01F,
           "the open plaza: the sky from above - up " + number(up) + " (0.8), a side " + number(side) + " (0.4), down " +
               number(down) + " (0); the bounce from every side " + number(bounce_side) + " (0.1)");
    expect(!rt::point_light_at(here).valid, "no field: no light (the game's lights - the original's look)");
}

// (added by the physical air) The light's unit: a white diffuser under the reference day (the sun 45 degrees up in the
// clear weather's air) - its direct and sky light per unit of the sun's irradiance, the unit's factor, the zenith's
// radiance in it and the sky's horizon over its zenith a quarter turn from the sun (a clear sky's about 1.5 - 4).
void check_unit()
{
    const rt::Aerosol clear = rt::aerosol_for(rt::Weather::kClear);
    const rt::GroundLight day = rt::ground_light(rt::kReferenceElevation, clear);
    const double unit = rt::sky_exposure();
    const rt::MultipleTable table = rt::multiple_table({.steps = 10, .light = 8});
    const rt::Dir sun = rt::sun_direction_at(rt::kReferenceElevation);
    constexpr rt::SkySteps kSteps{.view = 64, .light = 32};
    const auto luma = [](const rt::Rgb& c) {
        return (0.2126 * std::get<0>(c)) + (0.7152 * std::get<1>(c)) + (0.0722 * std::get<2>(c));
    };
    const double zenith = luma(rt::sky_radiance_ms({0.0, 1.0, 0.0}, sun, kSteps, table)) * unit;
    const double horizon =
        luma(rt::sky_radiance_ms({0.0, std::sin(0.02), std::cos(0.02)}, sun, kSteps, table)) * unit;
    expect(day.direct > 0.4 && day.direct < 0.7 && day.sky > 0.05 && day.sky < 0.25 &&
               std::abs(unit - (std::numbers::pi / (day.direct + day.sky))) < 1e-9,
           "the unit's day: direct " + number(day.direct) + ", sky " + number(day.sky) + " of the sun's irradiance; x " +
               number(unit));
    expect(zenith > 0.03 && zenith < 0.3 && horizon / zenith > 1.0 && horizon / zenith < 6.0,
           "the reference day's zenith " + number(zenith) + " of white (a clear sky's 0.05 - 0.2), the horizon " +
               number(horizon / zenith) + " x it a quarter turn from the sun");
}

// (lighting round A2 - one light unit for every level) The light in the global unit: under the reference day (the sun
// 45 degrees up, the clear weather's air) a level surface's light - the sun's per N.L x the sine plus the sky's - is 1
// (a white diffuser shows white); noon brighter, a low sun dimmer, the sun gone under the horizon; the clock's DayLight
// carries the same (sun_light / sky_light) at its time, the moon's and the night's at midnight; and the shadows' one
// direct share (direct_share_now) is the clock's own share now, or the model's at the level's own sun without it.
void check_absolute()
{
    rt::set_aerosol(rt::aerosol_for(rt::Weather::kClear));
    const auto luma = [](const rt::DayRgb& c) {
        return (0.2126 * static_cast<double>(std::get<0>(c))) + (0.7152 * static_cast<double>(std::get<1>(c))) +
               (0.0722 * static_cast<double>(std::get<2>(c)));
    };
    const auto level_light = [&](double elevation) {
        const rt::AbsoluteLight at = rt::absolute_light_at(elevation);
        return (luma(at.sun) * std::max(0.0, std::sin(elevation * std::numbers::pi / 180.0))) + luma(at.sky);
    };
    const double reference = level_light(rt::kDefaultSunElevation);
    const double noon = level_light(75.0);
    const double low = level_light(5.0);
    const double gone = luma(rt::absolute_light_at(-5.0).sun);
    expect(std::abs(reference - 1.0) < 0.02 && noon > reference && low < 0.3 * reference && gone == 0.0,
           "the global unit: a level surface's light under the reference day " + number(reference) + " (1: white), at 75 "
               "degrees " + number(noon) + ", at 5 degrees " + number(low) + ", the sun under the horizon " + number(gone));
    rt::NativeDay native = rt::fit_native_day({.sun = {0.0F, 0.7071F, 0.7071F}, .have_sun = true, .from_bake = true});
    const rt::DayLight own = rt::day_light_at(native, native.hours);
    const rt::AbsoluteLight own_at = rt::absolute_light_at(rt::kDefaultSunElevation);
    const rt::DayLight midnight = rt::day_light_at(native, 0.0);
    expect(std::abs(luma(own.sun_light) - luma(own_at.sun)) < 0.02 * luma(own_at.sun) && midnight.moonlit &&
               luma(midnight.sun_light) > 0.0 && luma(midnight.sun_light) < 1e-5 * luma(own_at.sun) &&
               luma(midnight.sky_light) > 0.0 && luma(midnight.sky_light) < 1e-5 * luma(own_at.sky),
           "the clock's light in the global unit: at the level's own time its sun " + number(luma(own.sun_light)) +
               " (the table's " + number(luma(own_at.sun)) + "); at midnight the moon's " +
               number(luma(midnight.sun_light)) + ", the night sky's " + number(luma(midnight.sky_light)));
    // the one direct share
    rt::DayLight off;
    const float without = rt::direct_share_now(off, true, rt::kDefaultSunElevation);
    const float sunless = rt::direct_share_now(off, false, rt::kDefaultSunElevation);
    const float with = rt::direct_share_now(own, true, rt::kDefaultSunElevation);
    expect(std::abs(without - rt::direct_share_at(rt::kDefaultSunElevation)) < 1e-6F && sunless == 0.0F &&
               std::abs(with - without) < 0.02F,
           "one direct share: without the clock the model's at the level's sun " + number(without) + " (none without one: " +
               number(sunless) + "), with it the clock's now at the same sun " + number(with) + "; at midnight (the moon's) " +
               number(rt::direct_share_now(midnight, true, 45.0)));
}

int main()
{
    tests::no_error_dialogs();
    check_unit();
    check_absolute(); // (lighting round A2)
    check_light_field();
    check_point_light(); // (round O)
    check_path();
    check_native();
    check_light();
    check_settings();
    if (failures() != 0) {
        std::printf("%d check(s) failed\n", failures());
        return 1;
    }
    std::printf("daylight_check: all checks passed\n");
    return 0;
}
