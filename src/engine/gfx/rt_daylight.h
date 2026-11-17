#pragma once

// NOT ORIGINAL - the time of day ([Graphics] tod, tod_cycle, tod_cycle_minutes, tod_levels; docs/design/vulkan.md
// "Time of day"): a clock that moves the sun (and the moon) along a real path over each level, and what the light
// is at that time - the sun's and the sky's strength and colour beside the level's own time, the lamps, the stars, the
// exposure, and which of the original game's four time-of-day states (engine/gfx/tod_manager.h) it is closest to.
//
// The path: the sun's place for a latitude and a declination (the date) at a solar time - the standard formulae
// (elevation from sin e = sin(lat) sin(dec) + cos(lat) cos(dec) cos(h), h the hour angle, 15 degrees an hour from noon;
// the azimuth from north through east) - turned so that the level's north lies at `north_heading` (the world's heading,
// round from +z toward +x as rt_sun.h toward_heading). The moon: east of the sun by its age (moon_elongation - full:
// opposite the sun, up all night, highest at midnight).
//
// Each level has its own day (fit_native_day): the time at which the path's sun is the level's own - the sun its
// lighting was baked with (rt_sun.h's fit), else the sun the game shows, else 45 degrees up - on the side of noon the
// level's own time-of-day state says (the original's afternoon: after noon). So tod=level's look is the path's sun at
// that time, and every other time moves the sun from there. The latitude is kTodLatitude unless the level's sun is
// higher than the path's noon there (then just enough lower).
//
// The light (day_light_at; round S - physical): in the global unit (rt_sky.h ground_light_table: 1 the light on a
// level surface under the reference day) -
//   - the sun's direct light: its transmittance through the atmosphere x the share of its disc above the horizon;
//   - the moon's: the sun's light reflected by the moon at its phase (moon_irradiance) through the same atmosphere;
//   - the sky's: the atmosphere's for the sun and for the moon as sources, and the night sky's own (kNightSkyIlluminance);
//   - the clouds (rt_clouds.h cloud_layer_light, the weather's cover): the direct light their columns let through, the
//     rest scattered down as the deck's diffuse light;
//   - the lamps by their photocells (the light on the ground), the stars, the moon's disc and the night sky's radiance
//     for the drawn sky; the display-referred ratios over the level's own light (the game's colours: r^(1 / 2.2)).
// What is shown is the camera's (gpu_vulkan/vulkan_hdr.h: its meter adapts as an eye, night stays dark).
// API-neutral and pure but for the published state (set_day_light / day_light: what render_pc's clock - engine/gfx/
// day_clock.h - works out each frame, and what every effect reads).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/gfx/rt_sky.h"

namespace render::rt {

using DayVec = std::array<float, 3>;
using DayRgb = std::array<float, 3>;

// --- the settings ----------------------------------------------------------------------------------------------------
// [Graphics] tod: level (the default - the level as it is, nothing changed), dawn, morning, noon, afternoon, dusk,
// night, or a clock time HH:MM (0:00 - 23:59, solar time).
enum class TodPreset : uint8_t { kLevel, kDawn, kMorning, kNoon, kAfternoon, kDusk, kNight, kClock };
struct TodChoice {
    TodPreset preset = TodPreset::kLevel;
    float hours = 0.0F; // kClock's (0 - 24)
    bool operator==(const TodChoice&) const = default;
};
// False: not one of those (`out` unchanged).
bool parse_tod(std::string_view text, TodChoice* out);
// The choice as the ini writes it ("level", "dusk", "07:30").
[[nodiscard]] std::string tod_text(const TodChoice& choice);
// [Graphics] tod_levels: per-level choices, "<scene>:<tod>" separated by commas (NO:night,AU:dusk); a level's scene as
// LoadScene names it, any case. False when an entry isn't one (those before it kept in `out`).
struct TodLevel {
    std::string level;
    TodChoice choice;
};
bool parse_tod_levels(std::string_view text, std::vector<TodLevel>* out);
inline constexpr float kDefaultCycleMinutes = 24.0F;
inline constexpr float kLeastCycleMinutes = 1.0F;
inline constexpr float kMostCycleMinutes = 1440.0F;
// (round S) the moon's age (days since the new moon) when the clock starts: [Graphics] tod_moon (new, first, full, last
// or a number of days); a running clock ages it a day each day it runs
inline constexpr double kSynodicMonth = 29.530589; // days
inline constexpr double kMoonFullAge = 0.5 * kSynodicMonth;
// False: not one of those (`out` unchanged).
bool parse_moon_age(std::string_view text, double* out);
struct TodSettings {
    TodChoice choice;
    bool cycle = false;                         // tod_cycle=on: the clock runs
    float cycle_minutes = kDefaultCycleMinutes; // minutes of game time for a whole day (the simulation's steps)
    std::vector<TodLevel> levels;
    // (a network session - the settings model's limit, engine/config/settings_model.h limits) the clock lights the level
    // but never sets the level's own time-of-day states: their scripts make and remove objects every player shares
    bool lighting_only = false;
    double moon_age = kMoonFullAge; // (round S) tod_moon
};
void set_tod_settings(const TodSettings& settings);
[[nodiscard]] TodSettings tod_settings();
[[nodiscard]] uint64_t tod_settings_serial(); // a new number each time they are set
// Whether the settings can turn the clock on in some level (anything but tod=level, no cycle and no level listed): the
// relit mode is wanted then (renderer_choice turns it on, as rt_sun_source does).
[[nodiscard]] bool tod_may_run(const TodSettings& settings);
// The choice for a level (its scene's checksum - checksums::hash_from_text of the name): its tod_levels entry, else tod.
using TodNameHash = uint32_t (*)(const char* name);
[[nodiscard]] TodChoice tod_choice_for(const TodSettings& settings, uint32_t level, TodNameHash hash);

// --- the sun's path ----------------------------------------------------------------------------------------------------
inline constexpr double kTodLatitude = 34.0;    // degrees north (a southern Californian city's)
inline constexpr double kTodDeclination = 15.0; // degrees (early May / early August)
inline constexpr double kHoursPerDay = 24.0;
inline constexpr double kNoonHours = 12.0;
inline constexpr double kDegreesPerHour = 15.0;
struct SunPath {
    double latitude = kTodLatitude;       // degrees
    double declination = kTodDeclination; // degrees
    double north_heading = 0.0;           // the world heading of north (degrees, round from +z toward +x)
};
struct SkyPlace {
    double elevation = 0.0; // degrees above the horizon
    double azimuth = 0.0;   // degrees from north through east
};
[[nodiscard]] SkyPlace sun_place(const SunPath& path, double hours);
// (round S) the moon `age` days old (days since the new moon - DayLight::moon_age; the full moon without it)
[[nodiscard]] SkyPlace moon_place(const SunPath& path, double hours, double age = kMoonFullAge);
// A place's direction in the level (toward it, normalised, y up).
[[nodiscard]] DayVec toward_place(const SunPath& path, const SkyPlace& place);
// The time (hours, 0 - 24) at which the sun is `elevation` degrees up, before noon (`afternoon` false) or after; a
// negative number when it never is.
[[nodiscard]] double hours_at_elevation(const SunPath& path, double elevation, bool afternoon);
// A direction's heading (degrees, round from +z toward +x) and elevation (degrees).
[[nodiscard]] SkyPlace heading_of(const DayVec& toward); // (azimuth: the heading)

// The original's four states (tod_manager.h), in the order of the day.
enum class TodState : uint8_t { kMorning, kAfternoon, kEvening, kNight };
inline constexpr std::size_t kTodStates = 4;
// The level's own day: its path, and the time its own look is at.
struct NativeDay {
    SunPath path;
    double hours = 0.0;    // the level's own time
    DayVec toward{};       // the sun then (the level's own; toward_place(path, sun_place(path, hours)))
    TodState state = TodState::kAfternoon;
    bool from_bake = false; // the level's baked sun (rt_sun.h); else the game's or the default
};
// The level's day from its sun (toward it: the baked sun, or the game's; `have_sun` false: none - 45 degrees up toward
// +z) and its own state: afternoon / morning - the time the path's sun is that high on that side of noon; evening -
// the sun kDuskElevation up after noon; night - midnight.
inline constexpr double kDefaultSunElevation = 45.0;
inline constexpr double kHighestMargin = 2.0; // a sun too high for kTodLatitude: the latitude this much under the fit
// (added by the level's compass) A level whose north is given (its data - rt_relit_levels.json's "north",
// rt_relit.h relit_level_entry, read by the clock when the level arrives): the path turned so north lies there instead; the level's own time still the time its sun is that
// high on that side of noon, so its own light is unchanged, and its own sun the path's then - the bake's direction
// only where the given north is the one the fit would find. Every effect follows (they all read DayLight).
struct NativeInputs {
    DayVec sun{};
    bool have_sun = false;
    bool from_bake = false;
    TodState state = TodState::kAfternoon;
    bool north_set = false;
    double north_heading = 0.0; // degrees, round from +z toward +x
};
[[nodiscard]] NativeDay fit_native_day(const NativeInputs& in);
// A preset's time in that day: dawn and dusk when the sun is kDuskElevation up (rising, setting), morning 9:00, noon,
// afternoon 15:30, night midnight, a clock time its own, level the level's.
inline constexpr double kDuskElevation = 3.0;
inline constexpr double kMorningHours = 9.0;
inline constexpr double kAfternoonHours = 15.5;
inline constexpr double kNightHours = 0.0;
[[nodiscard]] double preset_hours(const TodChoice& choice, const NativeDay& native);

// --- the light at a time -----------------------------------------------------------------------------------------------
// (round S - the light sources and the sky's state: every value below is physical; the art values of the night -
// kMoonLight, kMoonTint, kNightSkyLight, kNightSky, kCloudMoonGlow -, the sun's and the moon's softened sizes, the
// sunset's ramps and snow's own overcast are gone)
//
// The lights' sizes: their angular radii (rt_sky.h kSunAngularRadius, kMoonAngularRadius) in degrees - what the drawn
// discs, relit's jittered sun rays and the shadows pass's cone read. A soft overcast shadow comes from the sky's light
// outweighing the sun's, not from a wider sun.
inline constexpr double kRadiansToDegrees = 57.295779513082321;
inline constexpr float kSunRadiusDegrees = static_cast<float>(kSunAngularRadius * kRadiansToDegrees);
inline constexpr float kMoonRadiusDegrees = static_cast<float>(kMoonAngularRadius * kRadiansToDegrees);
// The share of a disc of angular radius `radius` (degrees) above the horizon with its centre `elevation` degrees up -
// how a light sets (the transmittance does the rest; no ramp).
[[nodiscard]] double disc_above(double elevation, double radius);

// The moon. Its phase by its age (days since the new moon; the synodic month): the elongation from the sun 360 x age /
// kSynodicMonth degrees (180: full), its place on the sky that far east of the sun along the path (its hour angle the
// sun's less the elongation, its declination the sun's x cos(elongation) - the moon's orbit taken in the ecliptic), its
// phase angle (sun - moon - earth) 180 - the elongation. Its light at the top of the atmosphere, per unit of the sun's
// irradiance there: the geometric albedo x (its radius over its distance)^2 x the phase law, per band -
//   - kMoonAlbedo: the visible geometric albedo 0.12 (V), redder than grey by the moon's colour indices over the sun's
//     (B-V 0.92 vs 0.65, V-R 0.55 vs 0.36: x 0.78 at the blue band, x 1.19 at the red) - the sun's light reflected,
//     slightly redder than sunlight, never blue;
//   - the phase law: the lunar magnitude's dependence on the phase angle a (degrees; Allen's, as Krisciunas and
//     Schaefer 1991 use it): 10^(-0.4 (0.026 |a| + 4e-9 a^4)) - 1 at full (the opposition surge in the albedo), 0.09 at
//     the quarters, nothing new.
// The full moon is then 128 000 lux x 0.12 x 2.04e-5 = 0.31 lux at the top, about 0.25 lux overhead on the ground (the
// sun : full moon about 4 x 10^5); through the same atmosphere as the sun's light (its transmittance at the moon's
// elevation, its sky light the atmosphere's for a source there).
inline constexpr std::array<double, 3> kMoonAlbedo{0.143, 0.120, 0.094}; // geometric, at 680 / 550 / 440 nm
[[nodiscard]] double moon_elongation(double age_days);          // degrees, 0 - 360 (180 full)
[[nodiscard]] double moon_phase_law(double phase_angle_degrees); // 1 at full
[[nodiscard]] DayRgb moon_irradiance(double age_days);          // per unit of the sun's, at the top of the atmosphere
// The night sky's own light (clear, moonless): the airglow, the stars and the zodiacal light - about 0.002 lux on the
// ground (the airglow's green and red lines: a little warm of white), above any cloud (the airglow is 90 km up).
inline constexpr double kNightSkyIlluminance = 0.002; // lux
inline constexpr DayRgb kNightSkyColour{1.05F, 1.0F, 0.85F}; // (normalised to luma 1)
// The lamps by their photocells: street lights switch on as the light on the ground falls through kLampsOnLux (the
// first ones) to kLampsFullLux (all) - ANSI C136.10's turn-on levels; the clouds' dusk turns them on as the sun's does.
inline constexpr double kLampsOnLux = 50.0;
inline constexpr double kLampsFullLux = 10.0;
// The frame's pre-exposure (the published state only - pre_expose): the light is scaled by 1 / the light on a level
// surface now (the unit's, 1 under the reference day), at least 1 and at most kMostPreExposure, so a moonlit night is
// held in the linear frame's 16-bit floats near the day's values (a moonlit wall at 1e-6 of the unit would be a few
// denormal steps); every pass that adds light the state doesn't carry (the sky's tables, the sun's and the moon's discs,
// the clouds' light, the lamps, the air) multiplies by it (frame_pre_exposure) and the camera's meter divides it out -
// what is shown is the physical light (docs/design/vulkan.md round S).
inline constexpr double kMostPreExposure = 65536.0;

// the share of the level's own light that is the sun's (the shadows') without the clock - with it the model's at the
// level's own sun (direct_share_at; changed by the physical air: one number for every level was the old way)
inline constexpr float kDirectShare = 0.65F;
// The share of a level surface's light that is the sun's direct light with the sun `elevation` degrees up: its
// transmittance x sine over that and the whole sky's (rt_sky.h's model, the frame's air; single scattering).
[[nodiscard]] float direct_share_at(double elevation);
// (The exposure is the camera's alone - gpu_vulkan/vulkan_hdr.h's meter; the clock's own exposure was retired by the
// physical air.)
// The original's states blended by the sun's elevation: afternoon above kStateDay, evening (after noon) / morning
// (before) at kStateLow, night below kStateNight - smoothly between.
inline constexpr double kStateDay = 20.0;
inline constexpr double kStateLow = 4.0;
inline constexpr double kStateNight = -8.0;
struct TodBlend {
    TodState from = TodState::kAfternoon;
    TodState to = TodState::kAfternoon;
    float fraction = 0.0F; // 0: `from`, 1: `to`
    bool operator==(const TodBlend&) const = default;
};
[[nodiscard]] TodBlend tod_blend(double sun_elevation, bool afternoon);

struct DayLight {
    bool on = false;          // the clock lights the level (false: tod=level - nothing changes)
    double hours = 0.0;       // the clock (solar time)
    double moon_age = kMoonFullAge; // (round S) days since the new moon
    DayVec sun{};             // toward the sun (below the horizon at night)
    DayVec moon{};            // toward the moon
    DayVec light{};           // toward what lights the scene - the sun, or the moon at night - where it is (lighting round
                              // A: no longer held up; under the horizon its light is gone)
    DayVec native{};          // the level's own sun (the drawn sky is matched to the level for it)
    bool moonlit = false;     // `light` is the moon (its direct light more than the sun's)
    // (relative to the level's own sun's light - the light now over the level's own, display-referred: r^(1 / 2.2) of
    // the linear ratio r, pre-exposed as the rest)
    float direct = 1.0F;      // the direct light's strength
    DayRgb direct_tint{1.0F, 1.0F, 1.0F}; // its colour (the ratio's channels over its luma)
    DayRgb light_colour{1.0F, 1.0F, 1.0F}; // direct x direct_tint
    float light_radius = kSunRadiusDegrees; // the direct light's angular radius (degrees): its disc's (round S)
    DayRgb sky{1.0F, 1.0F, 1.0F};         // the sky's light (per channel)
    DayRgb bounce{1.0F, 1.0F, 1.0F};      // the light bounced off the surroundings: the direct light's and the sky's in
                                          // the level's own shares (direct_share)
    float direct_share = kDirectShare;    // (added by the physical air) the direct light's share of the level's own
                                          // light - the model's at its own sun (direct_share_at)
    // (lighting round A2 - one light unit for every level) the light now in the global unit (rt_sky.h ground_light_table:
    // linear, 1 the light on a level surface under the reference day - a white diffuser's radiance; what relit's tints,
    // the albedos, are lit by): the direct light per unit of N.L (the sun's or the moon's, through the clouds) and the
    // light on a level surface from the sky (the sun's and the moon's sky, the clouds' diffuse light, the night sky's)
    DayRgb sun_light{};
    DayRgb sky_light{};
    float shadow = 1.0F;      // the sun's shadows' strength over the level's own (their share of the light)
    float lamps = 0.0F;       // 0 - 1: the lamps (rt_lights) on - their photocells (kLampsOnLux)
    float stars = 0.0F;       // 0 - 1: the stars drawn (1 while the clock runs: the sky's own light hides them by day)
    float moon_disc = 0.0F;   // 0 - 1: the moon drawn
    DayRgb moon_radiance{};   // (round S) the moon's disc's mean radiance (linear, the sky's unit; its transmittance at
                              // its elevation, its phase's light spread over the whole disc - the textured, phased disc
                              // is to come)
    DayRgb night_sky{};       // added to the drawn sky (linear radiance in the sky's unit, overhead): the moonlit sky's
                              // and the night sky's own light
    std::array<float, 4> stars_pole{}; // xyz toward the celestial pole; w the stars' turn about it (radians)
    TodBlend blend;           // the original's states
    // (round S - the clouds) the cloud cover the light was made with, the share of the direct light the clouds keep
    // (their mean over the level: the clouds' shadow map gives each point's over this), and the sky's light kept
    float cloud_cover = 0.0F;
    float cloud_beam = 1.0F;
    float cloud_sky = 1.0F;
    // (round S) the light on a level surface now (lux, physical, not pre-exposed) and the frame's pre-exposure (1 until
    // pre_expose)
    float illuminance_lux = 0.0F;
    float pre_exposure = 1.0F;
    uint64_t serial = 0;      // (set_day_light numbers each change)
};
// The light at `hours` on the level's day with the moon `moon_age` days old and the clouds `cloud_cover` (0 - 1:
// rt_clouds.h cloud_coverage) - physical, not pre-exposed.
struct DayWeather {
    double moon_age = kMoonFullAge;
    float cloud_cover = 0.0F;
};
[[nodiscard]] DayLight day_light_at(const NativeDay& native, double hours, const DayWeather& weather);
[[nodiscard]] DayLight day_light_at(const NativeDay& native, double hours);
// The state pre-exposed (what day_clock publishes): its light (sun_light, sky_light, night_sky, moon_radiance and the
// display-referred ratios) x its pre_exposure (1 / the light on a level surface, kMostPreExposure at most).
void pre_expose(DayLight* light);
// The published state's pre-exposure (1 while the clock is off) - what the passes that add light of their own multiply
// by, and the meter divides out.
[[nodiscard]] float frame_pre_exposure();
// (lighting round A2 - one light unit for every level) The sun's and the sky's light in the global unit (DayLight's
// sun_light / sky_light) with the sun `elevation` degrees up in the frame's air (rt_sky.h aerosol(); a table a degree,
// made again when the weather changes) - the sun's direct light x the share of its disc above the horizon; clear sky.
struct AbsoluteLight {
    DayRgb sun{};
    DayRgb sky{};
};
[[nodiscard]] AbsoluteLight absolute_light_at(double elevation);
// (lighting round A2 - one direct share) The direct light's share of an open level surface's light now - what the sun's
// visibility takes from a pixel lit by the game's lights (the skater, the objects: no light store of their own; the
// shadows pass): with the clock its light's share now in the global unit (sun_light x the light's sine over that plus
// sky_light - the moon's at night), without it the model's at
// the level's own sun (`own_sun`: whether its bake has one, `own_elevation` its degrees up) - 0 in a bake without a sun.
// The one source: the light field's own measured share is gone.
[[nodiscard]] float direct_share_now(const DayLight& day, bool own_sun, double own_elevation);
// The drawn clouds' light (the Vulkan sky pass's): the light that lights them - the sun, or the moon at night (the
// lighting state's light) - its direction and its irradiance at the top of the atmosphere per unit of the sun's (the
// moon's phase and colour; pre-exposed); the sky pass takes its transmittance down to the layer (none where the ground
// is in the way). Without the clock: the sky pass's own sun, 1.
struct CloudLight {
    bool clock = false;
    DayVec toward{};
    DayRgb light{1.0F, 1.0F, 1.0F};
};
[[nodiscard]] CloudLight cloud_light(const DayLight& light);
// The sky's light on a level surface with the sun `elevation` degrees up, linear, relative to the sun overhead's
// (rt_sky.h's single scattering, cosine-weighted over the upper hemisphere; a table made once, read between degrees).
[[nodiscard]] double sky_light_at(double elevation);
// The sun's direct light (luminance of its transmittance, linear, the top of the atmosphere's 1).
[[nodiscard]] double sun_light_at(double elevation);

// What the frame's clock gives (day_clock), read by every effect; `on` false while tod=level (and before the clock's
// first frame). A new serial each time it changes.
void set_day_light(const DayLight& light);
[[nodiscard]] DayLight day_light();

} // namespace render::rt
