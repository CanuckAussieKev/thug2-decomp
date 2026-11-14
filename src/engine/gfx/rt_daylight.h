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
    float cycle_minutes = kDefaultCycleMinutes; // real minutes for a whole day
    std::vector<TodLevel> levels;
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
struct NativeInputs {
    DayVec sun{};
    bool have_sun = false;
    bool from_bake = false;
    TodState state = TodState::kAfternoon;
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
inline constexpr double kLowestLight = 6.0;   // degrees: the light's direction (relit, the shadows) held this high
inline constexpr double kSunGoneLow = -0.5;   // the sun's direct light from 0 here ...
inline constexpr double kSunGoneHigh = 2.0;   // ... to its transmittance's here
inline constexpr double kMoonFrom = -4.0;     // the sun lower than this: the moon lights the scene
inline constexpr double kMoonFull = -12.0;    // its light whole from here down
inline constexpr float kMoonLight = 0.22F;    // the moon's direct light (display-referred, the level's sun 1)
inline constexpr DayRgb kMoonTint{0.62F, 0.74F, 1.0F};
// the night sky's light (display-referred): with the moon about the original's own night (its night state's level
// colour, 50 65 75 of 128 - tod_manager.qb), so its night lights and skater fit it
inline constexpr DayRgb kNightSkyLight{0.14F, 0.18F, 0.26F};
inline constexpr double kNightFrom = 0.0;     // the night's floor from 0 here (the sun setting) ...
inline constexpr double kNightFull = -4.0;    // ... to whole here (while the twilight is still brighter)
inline constexpr DayRgb kNightSky{0.0028F, 0.0045F, 0.0095F}; // the night sky's radiance overhead (exposed linear)
inline constexpr double kLampsOn = 6.0;       // the lamps start to come on with the sun this high ...
inline constexpr double kLampsFull = -4.0;    // ... and are whole here
inline constexpr double kStarsFrom = -6.0;    // the stars from none here ...
inline constexpr double kStarsFull = -16.0;   // ... to all here
inline constexpr float kDirectShare = 0.65F;  // the share of the level's own light that is the sun's (the shadows')
inline constexpr float kMostExposure = 1.3F;  // the exposure's factor at most (night)
inline constexpr double kExposurePower = 0.35;
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
    DayVec light{};           // toward what lights the scene - the sun, or the moon at night -, at least kLowestLight up
    DayVec native{};          // the level's own sun (the drawn sky is matched to the level for it)
    bool moonlit = false;     // `light` is the moon
    float direct = 1.0F;      // the direct light's strength over the level's own sun's (display-referred)
    DayRgb direct_tint{1.0F, 1.0F, 1.0F}; // its colour over the level's own sun's (the moon's blue at night)
    DayRgb sky{1.0F, 1.0F, 1.0F};         // the sky's light over the level's own (display-referred)
    float shadow = 1.0F;      // the sun's shadows' strength over the level's own (their share of the light)
    float lamps = 0.0F;       // 0 - 1: the lamps (rt_lights) on
    float stars = 0.0F;       // 0 - 1
    float moon_disc = 0.0F;   // 0 - 1: the moon drawn
    float exposure = 1.0F;    // the HDR exposure's factor
    DayRgb night_sky{};       // added to the drawn sky (exposed linear, overhead)
    std::array<float, 4> stars_pole{}; // xyz toward the celestial pole; w the stars' turn about it (radians)
    TodBlend blend;           // the original's states
    uint64_t serial = 0;      // (set_day_light numbers each change)
};
// The light at `hours` on the level's day.
[[nodiscard]] DayLight day_light_at(const NativeDay& native, double hours);
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
