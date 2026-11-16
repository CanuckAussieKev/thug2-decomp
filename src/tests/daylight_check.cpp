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
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_light_field.h"
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
                   std::abs(light.shadow - 1.0F) < 1e-3F && light.exposure == 1.0F && light.lamps == 0.0F &&
                   light.stars == 0.0F && !light.moonlit && same_direction(light.light, sun) &&
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
        const double sky = std::get<1>(light.sky);
        sky_darkening = sky_darkening && sky <= last_sky + 1e-6;
        last_sky = sky;
        if (!light.moonlit) {
            darkening = darkening && light.direct <= last_direct + 1e-6;
            last_direct = light.direct;
        }
    }
    expect(darkening, "noon to dusk: the sun's light never brightens");
    expect(sky_darkening, "noon to midnight: the sky's light never brightens (the night's floor taken in)");
    const rt::DayLight noon = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kNoon}, day));
    const rt::DayLight dusk = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kDusk}, day));
    const rt::DayLight dawn = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kDawn}, day));
    const rt::DayLight midnight = rt::day_light_at(day, rt::preset_hours({.preset = rt::TodPreset::kNight}, day));
    expect(noon.lamps == 0.0F && noon.stars == 0.0F && noon.direct > 0.9F && !noon.moonlit,
           "noon: sunlit, no lamps, no stars (direct " + number(noon.direct) + ")");
    expect(std::abs(rt::heading_of(dusk.sun).elevation - rt::kDuskElevation) < 1e-3 &&
               std::abs(rt::heading_of(dawn.sun).elevation - rt::kDuskElevation) < 1e-3 && dusk.hours > 12.0 &&
               dawn.hours < 12.0,
           "dusk and dawn: the sun 3 degrees up, setting and rising (" + number(dusk.hours) + ", " + number(dawn.hours) + ")");
    expect(dusk.lamps > 0.0F && dusk.lamps < 1.0F && dusk.direct < noon.direct && dusk.stars == 0.0F &&
               std::get<0>(dusk.direct_tint) > std::get<2>(dusk.direct_tint),
           "dusk: the lamps coming on (" + number(dusk.lamps) + "), the sun weaker and warm, no stars yet");
    expect(midnight.moonlit && midnight.lamps == 1.0F && midnight.stars == 1.0F && midnight.moon_disc > 0.9F &&
               midnight.direct > 0.05F && midnight.direct <= rt::kMoonLight &&
               std::get<2>(midnight.direct_tint) > std::get<0>(midnight.direct_tint) &&
               rt::heading_of(midnight.light).elevation >= rt::kLowestLight - 1e-3,
           "midnight: moonlit (direct " + number(midnight.direct) + ", blue), lamps and stars on");
    const double night_sky = std::get<1>(midnight.sky);
    expect(night_sky > 0.05 && night_sky < 0.3 && midnight.exposure > 1.0F && midnight.exposure <= rt::kMostExposure,
           "midnight: dark but lit (sky " + number(night_sky) + "), the exposure brought up (" +
               number(midnight.exposure) + ")");
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
    expect(std::get<1>(night_surface) > 0.08F && std::get<1>(night_surface) < 0.3F &&
               std::get<2>(night_surface) > std::get<0>(night_surface),
           "the bounce at midnight: dark and blue, as the relit level's bounce (" + number(std::get<1>(night_surface)) +
               ")");
    // (the original's own night - its night state's level colour, 50 65 75 of 128 - is the brighter one the objects stood
    // out by)
    expect(std::get<1>(night_surface) < 65.0F / 128.0F, "the bounce at midnight: darker than the original's night");

    // (the one lighting state) the direct light's colour is its strength x tint; the sun sharp, the moon soft
    for (const rt::DayLight* light : {&own, &midnight}) {
        bool product = true;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            product = product && std::abs(core::at(light->light_colour, ch) -
                                          (light->direct * core::at(light->direct_tint, ch))) < 1e-6F;
        }
        expect(product, "the lighting state: light_colour = direct x direct_tint");
    }
    expect(own.light_radius == rt::kSunRadiusDegrees && !own.moonlit, "the lighting state: the sun's radius by day");
    expect(midnight.moonlit && midnight.light_radius == rt::kMoonRadiusDegrees &&
               midnight.light_radius > 3.0F * own.light_radius,
           "the lighting state: the moon's light much softer than the sun's");
    expect(std::get<1>(midnight.light_colour) < 0.3F, "the lighting state: the moon weak (" +
                                                          number(std::get<1>(midnight.light_colour)) + ")");
    const rt::CloudLight none = rt::cloud_light(rt::DayLight{});
    expect(none.sun == 1.0F && none.moon_glow == rt::DayRgb{}, "cloud_light without the clock: the sun whole, no glow");
    const rt::CloudLight at_own = rt::cloud_light(own);
    expect(at_own.sun == 1.0F && at_own.moon_glow == rt::DayRgb{}, "cloud_light at the level's own time: the sun whole");
    const rt::CloudLight night = rt::cloud_light(midnight);
    const float night_sky = std::get<2>(midnight.night_sky);
    expect(night.sun == 0.0F && std::get<2>(night.moon_glow) > 0.0F &&
               std::get<2>(night.moon_glow) <= night_sky * rt::kCloudMoonGlow + 1e-6F,
           "cloud_light at midnight: no sun, the full moon's glow on them (" + number(std::get<2>(night.moon_glow)) +
               ")");
    // continuous from the afternoon into the night (a step of 3 minutes)
    double worst = 0.0;
    rt::CloudLight last = rt::cloud_light(rt::day_light_at(day, 15.0));
    for (double hours = 15.05; hours <= 23.99; hours += 0.05) {
        const rt::CloudLight now = rt::cloud_light(rt::day_light_at(day, hours));
        // (the sun's share as it is - 0 to 1 -, the moon's glow against its whole)
        worst = std::max({worst, static_cast<double>(std::abs(now.sun - last.sun)),
                          static_cast<double>(std::abs(std::get<2>(now.moon_glow) - std::get<2>(last.moon_glow)) /
                                              std::max(std::get<2>(night.moon_glow), 1e-6F))});
        last = now;
    }
    // (the moon's disc comes in over the sun's first degrees down)
    expect(worst < 0.25, "cloud_light: no jump from the afternoon into the night (worst step " + number(worst) + ")");
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
    const uint32_t lit = rt::scaled_colour(0x80406080U, {0.5F, 1.0F, 4.0F}, true);
    expect(lit == 0x80FF6040U, "scaled_colour: each channel by its ratio, held to a byte (" + std::to_string(lit) + ")");
}

// (added by lighting round 4) The level's own lights as a light of their own (rt_light_field.h local_at): a corner where
// a lamp lights a wall facing +x (0.6 on it), the ground a little (0.15), the wall's back nothing; a thing standing there
// takes the lamp's light as a light from +x - its side facing the lamp lit about as the wall facing it, its back side
// barely; the level's scale in; none where the field has no local light.
void check_local_light()
{
    auto field = std::make_shared<rt::LightField>();
    constexpr float kCell = rt::kLightFieldCell;
    const auto part = [](float local, const rt::FieldPoint& normal) {
        return rt::LightParts{.direct_now = {},
                              .indirect_now = {0.1F, 0.1F, 0.1F},
                              .direct_own = {},
                              .indirect_own = {0.4F, 0.4F, 0.4F},
                              .local = {local, local, local},
                              .normal = normal};
    };
    for (int i = 0; i < 8; ++i) {
        const float step = static_cast<float>(i) * 4.0F;
        field->add({(0.5F * kCell) + step, 20.0F + step, 0.5F * kCell}, part(0.6F, {1.0F, 0.0F, 0.0F}));
        field->add({(0.5F * kCell) - step, 20.0F + step, 0.5F * kCell}, part(0.0F, {-1.0F, 0.0F, 0.0F}));
        field->add({(0.5F * kCell) + step, 1.0F, (0.5F * kCell) + step}, part(0.15F, {0.0F, 1.0F, 0.0F}));
    }
    const rt::FieldPoint here{0.5F * kCell, 0.5F * kCell, 0.5F * kCell};
    const rt::LocalLight lamp = field->local_at(here);
    const auto on_face = [&](const rt::LocalLight& light, const rt::FieldPoint& normal) {
        const float facing = std::max(0.0F, (std::get<0>(normal) * std::get<0>(light.toward)) +
                                                (std::get<1>(normal) * std::get<1>(light.toward)) +
                                                (std::get<2>(normal) * std::get<2>(light.toward)));
        return std::get<1>(light.ambient) + (std::get<1>(light.directional) * facing);
    };
    const float toward_lamp = on_face(lamp, {1.0F, 0.0F, 0.0F});
    const float away = on_face(lamp, {-1.0F, 0.0F, 0.0F});
    expect(std::get<0>(lamp.toward) > 0.9F && std::abs(toward_lamp - 0.6F) < 0.15F && away < 0.2F,
           "the lamp's light as a light from +x (" + number(std::get<0>(lamp.toward)) + "): a side facing it " +
               number(toward_lamp) + " (the wall's 0.6), the back " + number(away));
    field->set_local_scale(1.2F);
    const float scaled = on_face(field->local_at(here), {1.0F, 0.0F, 0.0F});
    expect(std::abs(scaled - (1.2F * toward_lamp)) < 1e-4F, "the level's scale in it (" + number(scaled) + ")");
    auto dark = std::make_shared<rt::LightField>();
    dark->add(here, part(0.0F, {0.0F, 1.0F, 0.0F}));
    expect(!dark->local_at(here).any() && !rt::local_light_at(here).any(), "no local light: none (and none without a field)");
}

// (added by lighting round 4) The exposure adapting to the level's light: none by day, a dim night brought part of the
// way up and never past kMostExposure, a dusk between; over the frames toward it, there in the end.
void check_exposure()
{
    expect(rt::adapted_exposure(1.0F) == 1.0F && rt::adapted_exposure(1.4F) == 1.0F,
           "by day (the level's own light or more): 1");
    expect(std::abs(rt::adapted_exposure(0.5F) - 1.4142F) < 1e-3F,
           "half the light: " + number(rt::adapted_exposure(0.5F)));
    expect(rt::adapted_exposure(0.1F) == rt::kMostExposure && rt::adapted_exposure(0.0F) == rt::kMostExposure,
           "a dark night: held at " + number(rt::kMostExposure));
    float now = 1.0F;
    for (int frame = 0; frame < 30; ++frame) {
        now = rt::adapt_exposure(now, 2.0F, 1.0 / 60.0);
    }
    const float half_second = now;
    for (int frame = 0; frame < 1200; ++frame) {
        now = rt::adapt_exposure(now, 2.0F, 1.0 / 60.0);
    }
    expect(half_second > 1.2F && half_second < 1.4F && now == 2.0F,
           "adapting: " + number(half_second) + " after half a second, there after twenty");
}

int main()
{
    tests::no_error_dialogs();
    check_exposure();
    check_light_field();
    check_local_light();
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
