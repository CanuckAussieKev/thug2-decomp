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
#include <numbers>
#include <string>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_daylight.h"
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

int main()
{
    tests::no_error_dialogs();
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
