#pragma once

// NOT ORIGINAL - the sun a level's lighting was baked with, found from the bake (ray tracing, R4's sun: the shadow pass
// traces toward it - docs/design/vulkan.md "The baked sun").
//
// The game's directional lights light the skater and the objects; they are the same two in every level (toward
// (0.75, 0.5, 0.43) and (-0.79, 0.5, -0.37) - the light manager's defaults), so they say nothing about where a level's
// sun is. The level's own lighting is in its world meshes' vertex colours: sunlit ground bright, the shadows the
// buildings cast in it dark. So the sun is the direction whose shadows best explain those colours:
//   - samples: points on the world's upward-facing triangles (|normal.y| >= 0.7), each with its vertex colours'
//     brightness (the bake);
//   - for each candidate direction over the sky (a coarse set, then a finer one round the best), a ray from every sample
//     toward it through the level's opaque world (instance mask 0x01): blocked or not;
//   - the direction's score: the correlation of "not blocked" with the brightness (point-biserial). The best one is the
//     baked sun when its score reaches kMinConfidence; below it the bake shows no sun - a level lit by its lamps
//     (indoors), where sun shadows would only darken what the original lit.
// It runs in a new world's first frame, before anything is drawn (rt_record.cpp), a fraction of a second of ray queries.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "engine/gfx/rt_scene.h"

namespace render::rt {

// Three floats as a vector (the fit's and the samplers' arithmetic).
using Vec3 = std::array<float, 3>;
[[nodiscard]] constexpr float x_of(const Vec3& v) { return std::get<0>(v); }
[[nodiscard]] constexpr float y_of(const Vec3& v) { return std::get<1>(v); }
[[nodiscard]] constexpr float z_of(const Vec3& v) { return std::get<2>(v); }
[[nodiscard]] constexpr Vec3 plus(const Vec3& a, const Vec3& b) { return {x_of(a) + x_of(b), y_of(a) + y_of(b), z_of(a) + z_of(b)}; }
[[nodiscard]] constexpr Vec3 minus(const Vec3& a, const Vec3& b) { return {x_of(a) - x_of(b), y_of(a) - y_of(b), z_of(a) - z_of(b)}; }
[[nodiscard]] constexpr Vec3 scaled(const Vec3& v, float by) { return {x_of(v) * by, y_of(v) * by, z_of(v) * by}; }
[[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b)
{
    return (x_of(a) * x_of(b)) + (y_of(a) * y_of(b)) + (z_of(a) * z_of(b));
}
[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {(y_of(a) * z_of(b)) - (z_of(a) * y_of(b)), (z_of(a) * x_of(b)) - (x_of(a) * z_of(b)),
            (x_of(a) * y_of(b)) - (y_of(a) * x_of(b))};
}

struct BakeSample {
    std::array<float, 3> position{}; // a point on the triangle (its centre)
    std::array<float, 3> normal{};   // the triangle's, turned up
    float brightness = 0.0F;         // its baked vertex colours' luma (0 - 255)
    // (lighting round A) the mesh it lies on (any number the same for one mesh's samples): sun_score also judges each
    // sample against its own mesh's others - a level painted in many colours (Skatopia's yards, dirt and grass) hides its
    // sun's shadows from a score over the whole level's brightness, not from one within each surface
    uint32_t group = 0;
};

// The best direction's score at least this: the bake shows a sun (the outdoor levels tried score 0.38 - 0.54, the
// Foundry - indoors - 0.07 - 0.24).
inline constexpr float kMinConfidence = 0.3F;
inline constexpr float kMinElevationDegrees = 2.0F; // (lighting round A: from 8 - an evening bake's low sun, Skatopia's, lay under it)
// (added by the sun's glare) BakedSun::ground: this quantile of the samples' heights (the floor, not a rooftop).
inline constexpr double kGroundShare = 0.1;
// (added by the street air) BakedSun::street: this quantile of the heights of the samples facing up (normal y at least
// kStreetUp) - the street level of a level whose lowest floor lies under it (Berlin's plaza over its sunken streets).
inline constexpr double kStreetShare = 0.5;
inline constexpr float kStreetUp = 0.85F;

// The correlation between "lit" (the direction not blocked) and the samples' brightness: -1 - 1, 0 when either group is
// under 3% of the samples (no shadow to tell by). (lighting round A) The larger of two: over the samples' brightness as it
// is, and over each one's brightness against its own mesh's (the logarithm, its mesh's mean taken off - a mesh of at
// least kGroupLeast samples; the rest against the level's) - the surfaces' own colours taken out, their light left.
inline constexpr std::size_t kGroupLeast = 8;
[[nodiscard]] float sun_score(std::span<const BakeSample> samples, std::span<const uint8_t> lit);

// Directions over the sky (toward it): `count` spread evenly from kMinElevationDegrees to straight up.
[[nodiscard]] std::vector<std::array<float, 3>> sky_directions(std::size_t count);
// `count` directions within `degrees` of `centre` (above the minimum elevation), the centre first.
struct Cone {
    Vec3 centre{};
    float degrees = 0.0F;
    std::size_t count = 0;
};
[[nodiscard]] std::vector<Vec3> directions_round(const Cone& cone);

// (lighting round A2) BakedSun::candidates: the best and this many of the coarse set's next best, each at least
// kCandidateApartDegrees from those before it - the relit bake's fit picks among them (a correlation of "lit" with the
// brightness misses a sun whose shadows fall on many-coloured ground: Skatopia's evening).
inline constexpr std::size_t kSunCandidates = 12;
inline constexpr float kCandidateApartDegrees = 8.0F;
// ... and a low sun's ring: these headings round at each of these elevations (degrees)
inline constexpr std::size_t kCandidateRingHeadings = 8;
inline constexpr std::array<float, 3> kCandidateRingElevations{6.0F, 13.0F, 22.0F};
struct SunFit {
    BakedSun sun;            // (scene left 0: the caller numbers it)
    std::size_t samples = 0;
    std::size_t rays = 0;
    std::array<std::array<float, 4>, 3> runners_up{}; // the coarse set's next best: toward x, y, z and the score
};
// The fit, tracing through `tracer` (probe_many against its current scene).
[[nodiscard]] SunFit fit_baked_sun(std::span<const BakeSample> samples, RayTracing& tracer);

// --- (added by the scene's sun) which sun lights the scene: rt_scene.h scene_sun's rules ----------------------------
// A game sun lower than this (degrees) lights with the baked sun instead: its shadows would cross the whole level.
inline constexpr float kMinGameSunDegrees = 10.0F;
struct SunInputs {
    SunChoice choice;
    bool override_set = false; // the console's `rt sun`
    Vec3 override_toward{};
    bool game_set = false; // the sun the game shows
    Vec3 game_toward{};
    uint64_t baked_scene = 0; // the baked sun's fit (0: not made yet)
    bool baked_found = false;
    Vec3 baked_toward{};
    SunRelight relight;
    bool clock_set = false; // (added by the time of day) the clock lights the level (rt_daylight.h DayLight::on)
    Vec3 clock_toward{};    // its light: the sun, the moon at night
};
// The scene's sun (serial left 0): the override; with no baked sun fitted yet kWaiting (kFixed doesn't wait); a bake
// that shows no sun kNone (kFixed lights anyway); kLevel the baked sun; kGame the game's where it is shown, at least
// kMinGameSunDegrees up, the level relit and its entry not asking for the bake - else the baked sun; kFixed its own.
// (added by the time of day) kClock - the clock's light - before kGame and kLevel wherever the level is relit (its
// entry's "sun": "level" is about the game's sun, not the clock's); not relit yet, the baked sun.
// A direction that can't be normalised counts as none.
[[nodiscard]] SceneSun resolve_scene_sun(const SunInputs& in);
// [Graphics] rt_sun_source's text: "game", "level" (or "bake"), "x,y,z" (toward the sun) or "heading,elevation"
// (degrees: heading round from +z toward +x, as the logs give the baked sun's). False: none of those (`out` unchanged).
bool parse_sun_source(std::string_view text, SunChoice* out);
// A direction from a heading and an elevation in degrees (heading 0: +z, 90: +x).
struct SunAngles {
    float heading_degrees = 0.0F;
    float elevation_degrees = 0.0F;
};
[[nodiscard]] Vec3 toward_heading(const SunAngles& angles);

} // namespace render::rt
