#pragma once

// NOT ORIGINAL - the time of day ([Graphics] tod, tod_cycle, tod_cycle_minutes, tod_levels; docs/design/vulkan.md
// "Time of day"): a clock that moves the sun (and a full moon) along a real path over each level, and what the light
// is at that time - the sun's and the sky's strength and colour beside the level's own time, the lamps, the stars, the
// exposure, and which of the original game's four time-of-day states (engine/gfx/tod_manager.h) it is closest to.
//
// The path: the sun's place for a latitude and a declination (the date) at a solar time - the standard formulae
// (elevation from sin e = sin(lat) sin(dec) + cos(lat) cos(dec) cos(h), h the hour angle, 15 degrees an hour from noon;
// the azimuth from north through east) - turned so that the level's north lies at `north_heading` (the world's heading,
// round from +z toward +x as rt_sun.h toward_heading). The moon: full, opposite the sun (its hour angle 12 hours on, its
// declination the sun's negated) - up all night, highest at midnight.
//
// Each level has its own day (fit_native_day): the time at which the path's sun is the level's own - the sun its
// lighting was baked with (rt_sun.h's fit), else the sun the game shows, else 45 degrees up - on the side of noon the
// level's own time-of-day state says (the original's afternoon: after noon). So tod=level's look is the path's sun at
// that time, and every other time moves the sun from there. The latitude is kTodLatitude unless the level's sun is
// higher than the path's noon there (then just enough lower).
//
// The light (day_light_at): relative to the level's own time, display-referred (what the game's colours and the bake
// are: a linear ratio r shows as r ^ (1 / 2.2) - so night is dim, not black):
//   - direct: the sun's transmittance through the atmosphere (rt_sky.h; its luminance over the level's sun's), gone
//     below the horizon; at night the moon (kMoonLight, kMoonTint) once the sun is far enough down;
//   - sky: the sky's light on a level surface (rt_sky.h's single scattering over the upper hemisphere, cosine-weighted),
//     over the level's sun's; at night kNightSkyLight (the moonlit sky's blue);
//   - the lamps on from a little before sunset to a little after sunrise, the stars once it is dark, the moon's disc,
//     the night sky's own light added to the drawn sky, and an exposure that brings night up partly (HDR only).
// API-neutral and pure but for the published state (set_day_light / day_light: what render_pc's clock - engine/gfx/
// day_clock.h - works out each frame, and what every effect reads).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
struct TodSettings {
    TodChoice choice;
    bool cycle = false;                         // tod_cycle=on: the clock runs
    float cycle_minutes = kDefaultCycleMinutes; // minutes of game time for a whole day (the simulation's steps)
    std::vector<TodLevel> levels;
    // (a network session - the settings model's limit, engine/config/settings_model.h limits) the clock lights the level
    // but never sets the level's own time-of-day states: their scripts make and remove objects every player shares
    bool lighting_only = false;
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
[[nodiscard]] SkyPlace moon_place(const SunPath& path, double hours);
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
inline constexpr double kSunGoneLow = -0.5;   // the sun's direct light from 0 here ...
inline constexpr double kSunGoneHigh = 2.0;   // ... to its transmittance's here
inline constexpr double kMoonFrom = -4.0;     // the sun lower than this: the moon lights the scene
inline constexpr double kMoonFull = -12.0;    // its light whole from here down
inline constexpr float kMoonLight = 0.22F;    // the moon's direct light (display-referred, the level's sun 1)
inline constexpr DayRgb kMoonTint{0.62F, 0.74F, 1.0F};
// The light's angular radius (degrees) - how soft the shadows it casts are (the relit level's sun rays are jittered over
// it; the shadow pass's softness follows it): the sun's disc softened as the bake's shadows are, the moon's light much
// softer - weak and diffuse, its shadows blurred edges, never the hard wedges of the bake's sun.
inline constexpr float kSunRadiusDegrees = 1.5F;
inline constexpr float kMoonRadiusDegrees = 6.0F;
// the night sky's light (display-referred): with the moon about the original's own night (its night state's level
// colour, 50 65 75 of 128 - tod_manager.qb), so its night lights and skater fit it
inline constexpr DayRgb kNightSkyLight{0.14F, 0.18F, 0.26F};
inline constexpr double kNightFrom = 0.0;     // the night's floor from 0 here (the sun setting) ...
inline constexpr double kNightFull = -4.0;    // ... to whole here (while the twilight is still brighter)
// the night sky's radiance overhead (linear, in the sky's unit - rt_sky.h; changed by the physical air: no longer tuned
// for an exposure - the camera's meter exposes it with the rest of the night)
inline constexpr DayRgb kNightSky{0.0018F, 0.0029F, 0.0062F};
inline constexpr double kLampsOn = 6.0;       // the lamps start to come on with the sun this high ...
inline constexpr double kLampsFull = -4.0;    // ... and are whole here
inline constexpr double kStarsFrom = -6.0;    // the stars from none here ...
inline constexpr double kStarsFull = -16.0;   // ... to all here
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
    DayVec sun{};             // toward the sun (below the horizon at night)
    DayVec moon{};            // toward the moon
    DayVec light{};           // toward what lights the scene - the sun, or the moon at night - where it is (lighting round
                              // A: no longer held up; under the horizon its light is gone)
    DayVec native{};          // the level's own sun (the drawn sky is matched to the level for it)
    bool moonlit = false;     // `light` is the moon
    float direct = 1.0F;      // the direct light's strength over the level's own sun's (display-referred)
    DayRgb direct_tint{1.0F, 1.0F, 1.0F}; // its colour over the level's own sun's (the moon's blue at night)
    DayRgb light_colour{1.0F, 1.0F, 1.0F}; // direct x direct_tint: the direct light now over the level's own sun's
    float light_radius = kSunRadiusDegrees; // the direct light's angular radius (degrees): how soft its shadows are
    DayRgb sky{1.0F, 1.0F, 1.0F};         // the sky's light over the level's own (display-referred)
    DayRgb bounce{1.0F, 1.0F, 1.0F};      // the light bounced off the surroundings over the level's own: the direct
                                          // light's and the sky's in the level's own shares (direct_share)
    float direct_share = kDirectShare;    // (added by the physical air) the direct light's share of the level's own
                                          // light - the model's at its own sun (direct_share_at)
    // (lighting round A2 - one light unit for every level) the light now in the global unit (rt_sky.h ground_light_table:
    // linear, 1 the light on a level surface under the reference day - a white diffuser's radiance; what relit's tints,
    // the albedos, are lit by): the direct light per unit of N.L (the sun's transmitted light, or the moon's at night) and
    // the sky's on a level surface (the atmosphere's every order, the night's floor added)
    DayRgb sun_light{};
    DayRgb sky_light{};
    float shadow = 1.0F;      // the sun's shadows' strength over the level's own (their share of the light)
    float lamps = 0.0F;       // 0 - 1: the lamps (rt_lights) on
    float stars = 0.0F;       // 0 - 1
    float moon_disc = 0.0F;   // 0 - 1: the moon drawn
    DayRgb night_sky{};       // added to the drawn sky (linear radiance in the sky's unit, overhead - not pre-exposed:
                              // the camera's meter exposes it with everything else)
    std::array<float, 4> stars_pole{}; // xyz toward the celestial pole; w the stars' turn about it (radians)
    TodBlend blend;           // the original's states
    uint64_t serial = 0;      // (set_day_light numbers each change)
};
// The light at `hours` on the level's day.
[[nodiscard]] DayLight day_light_at(const NativeDay& native, double hours);
// (lighting round A2 - one light unit for every level) The sun's and the sky's light in the global unit (DayLight's
// sun_light / sky_light) with the sun `elevation` degrees up in the frame's air (rt_sky.h aerosol(); a table a degree,
// made again when the weather changes) - none from the sun under the horizon; no night floor, no moon.
struct AbsoluteLight {
    DayRgb sun{};
    DayRgb sky{};
};
[[nodiscard]] AbsoluteLight absolute_light_at(double elevation);
// The night's light in the global unit (art values, as the moon's and the night sky's display-referred ones they come
// from - kMoonLight, kNightSkyLight: still not physical, docs/design/vulkan.md): the full moon's direct light per unit
// of N.L, and the night sky's on a level surface.
[[nodiscard]] DayRgb moon_light_absolute();
[[nodiscard]] DayRgb night_sky_absolute();
// (lighting round A2 - one direct share) The direct light's share of an open level surface's light now - what the sun's
// visibility takes from a pixel lit by the game's lights (the skater, the objects: no light store of their own; the
// shadows pass): with the clock its light's share now in the global unit (sun_light x the light's sine over that plus
// sky_light - the moon's at night), without it the model's at
// the level's own sun (`own_sun`: whether its bake has one, `own_elevation` its degrees up) - 0 in a bake without a sun.
// The one source: the light field's own measured share is gone.
[[nodiscard]] float direct_share_now(const DayLight& day, bool own_sun, double own_elevation);
// (added by the snow) An overcast sky over that light, `share` 0 - 1 of a full winter overcast (day_clock applies it to
// the state it makes - snow::overcast_share): the cloud deck keeps kOvercastDirect of the direct light at full and
// widens its radius toward kOvercastRadiusDegrees (soft shadows), the sky's light turns grey (kOvercastGrey of the way to
// its own luminance) and gains kOvercastScatter of what the deck took from the direct light (in the level's shares); the
// stars and the moon's disc go behind it; the bounce, the shadows' strength and the model's exposure follow. Nothing
// while the light is off or the share 0.
inline constexpr float kOvercastDirect = 0.3F;
inline constexpr float kOvercastRadiusDegrees = 16.0F;
inline constexpr float kOvercastGrey = 0.75F;
inline constexpr float kOvercastScatter = 0.5F;
void apply_overcast(DayLight* light, float share);
// The drawn clouds' light at the clock's time (the Vulkan sky pass's). Their sky light is the drawn sky's own, read from
// its table (rt_clouds.h cloud_sky_light: the sky as it is now, the night sky's light in it); the sun's light on them
// is the atmosphere's transmittance at the layer (reddened when low) x `sun` (gone once the sun is kCloudSunGone under
// the horizon - a cloud high up keeps the sunset's light a little after the ground has lost it); with the full moon up
// `moon_glow` (night_sky x kCloudMoonGlow x the moon's disc) is added on their tops and bases: at night a cloud is a dim
// silhouette barely lighter than the sky round it, not a daylit one. 1 / none while `on` is false; continuous over
// the dusk.
inline constexpr double kCloudSunGone = -6.0;  // degrees: the sun's light on the clouds from none here ...
inline constexpr double kCloudSunWhole = 0.0;  // ... to all here
inline constexpr float kCloudMoonGlow = 0.5F;  // (x night_sky x moon_disc)
struct CloudLight {
    float sun = 1.0F;
    DayRgb moon_glow{}; // exposed linear, added to the sky's light on them
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
