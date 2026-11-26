#pragma once

#include <array>
#include <cstdint>

#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"

// NOT ORIGINAL - the volumetric light's model (docs/design/vulkan.md "Volumetric fog and light shafts"; the Vulkan
// backend's gpu_vulkan/vulkan_rt_fog draws it, vulkan_rt_sky_pass makes the clouds' shadow map it reads). API-neutral
// and pure but for medium_light(), which reads the frame's published state.
//
// One light lights the air: the scene's sun (rt_scene.h scene_sun - the one every effect lights by) and, while the clock
// lights the level (its origin kClock), the clock's state for it (rt_daylight.h DayLight: the sun by day, the moon at
// night - its colour and strength over the level's own sun's, its angular size). Without the clock the light is the
// level's own: colour 1, a point (as the shafts were before the clock). The air itself is the atmosphere's (changed by
// the physical air): the weather's aerosol and the air's own molecules (rt_sky.h Aerosol, kRayleigh) - the same
// coefficients the sky is made of - with what falls through it (rain, snow); the device's march and the checks'
// processor march are the same model.
namespace render::rt {

// The light is the relit level's: the air is lit by a sun only where the relit mode relights the level (rt_scene.h
// SunRelight::relights - its offline data read, the mode on, the level not opted out), so the air's light is the light
// the level's surfaces show. A level without relit data has none: its fog is its own look (the game's colour whole, the
// lamps' light in it), no sunlit air. (The console's `rt sun` - an override, an experiment - lights the air whatever
// the level.)
struct MediumLight {
    bool have = false;      // a light (none indoors, on a level the relit mode doesn't relight, or before its sun is read)
    bool clock = false;     // the clock's light (DayLight): its colour and size are used
    bool moonlit = false;   // the moon
    std::array<float, 3> toward{};                // normalised
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F}; // the light now over the level's own sun's (display-referred)
    float radius_degrees = 0.0F; // its angular radius - how soft the shafts' edges are: the clock's (DayLight::
                                 // light_radius - the sun's, the larger moon's); 0, a point, without the clock
    double sun_elevation = 0.0;                   // degrees: the sun's (not the moon's)
};
// The light from a scene sun, the relit mode's word on the level and a clock state (pure: the checks').
[[nodiscard]] MediumLight medium_light_from(const SceneSun& scene, const SunRelight& relight, const DayLight& day);
// The frame's (scene_sun(), sun_relight(), day_light()).
[[nodiscard]] MediumLight medium_light();

inline constexpr double kUnitsPerMetre = 39.37; // (the world's units are inches)

// --- (changed by the physical air) what falls through the air: the weather's precipitation ---------------------------
// The haze, the mist and the air's molecules are the atmosphere's (rt_sky.h - the weather picked by rt_fog_air); falling
// rain and snow add a grey extinction near the ground (their drops and flakes scatter forward as the aerosol does),
// thinning by e every kPrecipitationHeight. (round W) The snow's is its flakes' (engine/gfx/snow.h extinction - Gunn and
// Marshall's flakes at the snowfall's rate: 740 m at the moderate 1 mm/h). (changed by the rain) The rain's is
// its drops' (engine/gfx/rain.h extinction - Marshall-Palmer's drops at the rate, geometric optics): [Graphics] rain's
// rate, or the game's own scripted rain taken as moderate (rain::kModerateRate: about 4.5 km).
inline constexpr double kPrecipitationHeight = 60.0; // metres
// What the weather puts in the air: render_pc publishes it each frame from the game's weather, the snow's and the rain's
// settings.
struct AirWeather {
    bool rain = false;       // the game's own rain (its weather scripts)
    bool snow = false;
    double rain_rate = 0.0;  // (added by the rain) [Graphics] rain: mm/h (0: none)
    bool operator==(const AirWeather&) const = default;
};
void set_air_weather(const AirWeather& weather);
[[nodiscard]] AirWeather air_weather();
// The precipitation's extinction per metre at the ground (0: none).
[[nodiscard]] double precipitation_extinction(const AirWeather& weather);

// The air near the ground as one layer (the level's own fog's march - its game fog's light is display-referred, the
// air in it a single exponential layer): the aerosol's extinction at the ground (green) and the height it thins over
// (the mist's where there is one, else the haze's), the precipitation's - per world unit, heights in world units.
struct StreetAir {
    double extinction = 0.0; // at its ground
    double height = 0.0;
    double snow = 0.0;       // (the precipitation: rain or snow)
    double snow_height = 0.0;
};
[[nodiscard]] StreetAir street_air(const Aerosol& aerosol, const AirWeather& weather);

// (round UNITS - one physical unit for every level's fog) The level's own fog as a participating medium. The original
// blends a surface toward the fog colour linearly from `start` to `end` (the game's SetFogDistance: end = start x 4, or
// start + 7200 with distance clipping): at `end` a thing is gone - the end is the fog's visibility. The physical medium
// with that visibility is an even one of extinction sigma_t = kKoschmieder / (end - start) per world unit
// (Koschmieder's law: the distance at which a black object's contrast falls to 2 %; rt_sky.h's visibilities are the
// same rule), beginning `start` from the eye (Unreal's exponential height fog's Start Distance - a world medium has no
// distance from the camera; the game's fog-free near range is kept as engines keep it). It is the weather's aerosol made
// denser: its single-scattering albedo, phase and light are the aerosol's (rt_sky.h Aerosol - mist and haze droplets),
// lit by what lights the level - the sun or the moon, the open sky, every lamp - in the global unit. The game's fog
// colour is then not an input: it was the light this medium sends the eye under the original's lighting, which the
// physical medium makes from the lights (the census, tools/game/light_units.ts, sets the two side by side).
// THUG2's night fog (start 8000, end 32 000 units) is a visibility of 610 m - a mist, 50 - 60 times the extinction of a
// clear night's air (40 km): physically lamps in it have halos (Koschmieder's mist in street light); its day haze the
// same density lit by the sun.
[[nodiscard]] double level_fog_extinction(float start, float end); // per world unit (0: no fog)
// The medium's single-scattering albedo per channel: the aerosol's scattering over its extinction (1 where it has none).
[[nodiscard]] std::array<float, 3> aerosol_albedo(const Aerosol& aerosol);

// The air's phase function x 4 pi (1 on average over the sphere): the aerosol's - Cornette-Shanks with its g (rt_sky.h
// aerosol_phase), the one phase the sky is made with.
[[nodiscard]] double henyey_greenstein(double cosine, double g); // x 4 pi
[[nodiscard]] double medium_phase(double cosine, double g);

// --- (added by the god rays) the forward peak's shadows, seen on the screen: [Graphics] rt_fog_god_rays -------------
// Looking toward a low sun through branches, past a tower, the air shows streaks radiating from the sun: the light its
// forward lobe scatters toward the eye, cut by everything between the eye's lines of sight and the sun. That light
// comes from within a few degrees of the sun, and its shadows are as fine as the branches seen against the sky - finer
// than the march's quarter-resolution texels and its steps. So the shadowing of the light the phase gathers near the
// light's way - its peak, the aerosol's phase over its value kGodRayConeDegrees from the light - is resolved at the
// screen's resolution instead (changed by the physical air: an estimate of the same integral, nothing added):
//   - the march (vulkan_rt_fog step 1) scatters the light with march_sun_phase - the phase without the peak - and adds
//     up, beside it, the peak's light unshadowed per unit of the peak's phase (its potential: the aerosol's light per
//     unit of light x what reaches the eye, x the clouds' shadow - the geometry's shadows are the screen's);
//   - the screen (steps 2a / 2b, half the viewport's resolution): along the line from each pixel toward the light's place
//     on the screen, the share of it that is sky (the depth's far value: cut-outs' gaps are sky) and the mean distance of
//     what stands on it - in three radial passes of kGodRayTaps taps each (kGodRayTaps^3 points: the same every frame);
//   - every point of the view ray toward the light projects onto that same line (epipolar), so what stands on it
//     shadows the air between it and the eye: open = 1 - (1 - sky share) x the share of the peak's air lying before
//     what stands there (god_ray_open; air_share_before - the medium's own: a uniform air's light within d of the eye,
//     (1 - e^(-sigma d)) / (1 - T));
//   - the composite adds peak(view) x potential x open x the light. open is at most 1: unshadowed the light is the
//     march's own peak, moved, not made (the checks' energy case); rt_fog_god_rays is on or off.
inline constexpr double kGodRayConeDegrees = 30.0;
inline constexpr int kGodRayTaps = 10; // per radial pass (three passes)
// The peak (x 4 pi): max(medium_phase(cosine, g) - medium_phase(cos kGodRayConeDegrees, g), 0).
[[nodiscard]] double god_ray_peak(double cosine, double g);
// The phase the march scatters the sun's or moon's light with: medium_phase, less the peak while the god rays take it.
[[nodiscard]] double march_sun_phase(double cosine, double g, bool god_rays);
// What of the peak's light gets through along a line toward the light: its sky share and the share of the peak's air
// lying before what stands on it.
[[nodiscard]] double god_ray_open(double sky_share, double air_before);
// The share of a uniform medium's light along a view ray that lies within `distance` of the eye: extinction `sigma` (per
// world unit), the whole ray's transmittance `transmittance`.
[[nodiscard]] double air_share_before(double distance, double sigma, double transmittance);

// (added by the distinct shafts; changed by the physical air) The air's sun map: where the air round the eye is in the
// sun, made each frame from the light's side - rays traced down the light's way through a square of texels
// (rt_fog_quality's: 512 / 1024 / 1536 a side) across the plane square to it, round the eye's place in it (snapped to
// whole texels: shadows don't swim as the camera moves), each texel the distance to the first thing its ray hits (the
// cut-outs by their alpha - the one lighting path's test). A point of the air is lit where it lies before that distance;
// a texel's penumbra is the light's disc's. Every step of the march inside the square reads it instead of a ray each
// other step - the near air's shadows sharp. It is how the visibility term is sampled, not part of the medium: it
// covers kAirMapReach round the eye, beyond it the march traces rays. The rays start kAirMapLift world units up the
// light's way.
inline constexpr double kAirMapReach = 100.0; // metres
inline constexpr double kAirMapLift = 100000.0; // world units (the fog's light rays' reach)
[[nodiscard]] double air_map_half(); // its half side in world units

// The clouds' shadow map (vulkan_rt_sky_pass makes it, the fog's march and the clouds' shadows on the ground read it):
// the light's transmittance through the cloud layer, kCloudMapSize texels a side over 2 x kCloudMapHalf metres of the
// cloud base's plane, centred where the camera's way toward the light meets it (snapped to whole texels: the map doesn't
// swim as the camera moves), kCloudMapSamples densities along the light through the layer. A point at `at` reads the
// texel where its way toward the light meets the base. The light no lower than kCloudMapLeastRise (its height's sine);
// the shadows fade in between kCloudMapFadeFrom and kCloudMapFadeTo of it (a low light's shadows lie too far along).
inline constexpr uint32_t kCloudMapSize = 256;
inline constexpr double kCloudMapHalf = 1600.0; // metres
inline constexpr uint32_t kCloudMapSamples = 8;
inline constexpr double kCloudMapLeastRise = 0.08;
inline constexpr double kCloudMapFadeFrom = 0.02;
inline constexpr double kCloudMapFadeTo = 0.15;
[[nodiscard]] double cloud_map_fade(double rise);
// Where a point's way toward the light meets the cloud base (world x, z): `base_y` the base's world height.
[[nodiscard]] std::array<double, 2> cloud_map_place(const std::array<double, 3>& at,
                                                    const std::array<double, 3>& toward, double base_y);

// --- (round MEDIUM) the air as a medium: one definition for the fog's froxels and the reference path tracer ----------
// The fog pass (vulkan_rt_fog) decides each frame which air the view has - the atmosphere's air (aerial perspective: the
// air's molecules, the weather's aerosol, its mist, what falls), the street air near the ground (the weather's aerosol
// as one layer, what falls), or none - and describes it as up to kMediumLayers exponential layers: each a scattering and
// an extinction per world unit (rgb) at its ground's height, thinning by e every 1 / inverse_height above it, even below
// it, scattering by Rayleigh's phase (the molecules) or the aerosol's (Cornette-Shanks with g: haze, mist, drops,
// flakes). Its froxels march that medium; the path tracer (`pt ref`) traces paths through the same one
// (vulkan_path_reference.cpp: its optical depths in closed form, the in-scattering by next-event estimation at a point
// picked along each ray, the scattered paths by free-flight sampling). Where its media end:
//   - `reach`: no ray's air goes farther from its origin (the froxels' grid's far - the aerial air's kFogAerialFar, the
//     haze's kFogHazeEnd; past it the level's world is seen through what is before it);
//   - `sky_holds_ray`: a ray that leaves the level has no air - the sky's radiance there is the sky-view table's, which
//     holds the whole ray through the atmosphere (the aerial air with the atmosphere drawn); else a ray to the sky goes
//     through `reach` of the air first. The sun's and the sky's light are those at the level's ground (the atmosphere's
//     transmittance in them), so a light ray toward them takes the same rule.
// Not a medium here (left out of the reference, logged): the level's own fog drawn from the eye's distances (the
// original's `start`, a camera-relative near range - no world medium has one) and a level without physical light (its
// fog is the game's colour, a look, not light).
inline constexpr std::size_t kMediumLayers = 4;
enum class MediumPhase : uint32_t { kRayleigh = 0, kAerosol = 1 };
struct MediumLayer {
    std::array<float, 3> scattering{}; // per world unit at its ground
    std::array<float, 3> extinction{};
    float inverse_height = 0.0F;       // per world unit (0: even)
    float ground = 0.0F;               // world y
    MediumPhase phase = MediumPhase::kAerosol;
};
struct AirMedium {
    uint32_t layers = 0; // (0: no medium)
    std::array<MediumLayer, kMediumLayers> layer{};
    float g = 0.0F;             // the aerosol's asymmetry (Cornette-Shanks)
    float reach = 0.0F;         // world units
    bool sky_holds_ray = false;
    const char* none = nullptr; // why there is no medium (the log's)
};
// The optical depth of a layer along a ray from height `u0` over its ground, going up `dy` per unit, between s0 and s1
// (its density 1 at the ground) - the GLSL's medium_layer_depth.
[[nodiscard]] double medium_layer_depth(double u0, double dy, double s0, double s1, double inverse_height);

// (GLSL, after nothing) the layers' density and optical depth in closed form, the phases per steradian.
inline constexpr const char* kMediumGlsl = R"(
// a layer's density at height u over its ground: e^(-u / H) above it, 1 below
float medium_layer_density(float u, float inv_h) { return exp(-max(u, 0.0) * inv_h); }
// a layer's optical depth (its density 1 at its ground) along y = u0 + dy s, s in [s0, s1], in closed form
float medium_layer_depth(float u0, float dy, float s0, float s1, float inv_h)
{
    float lo = s0; // [lo, hi]: the part above the ground
    float hi = s1;
    float below = 0.0;
    if (inv_h > 0.0) {
        if (abs(dy) < 1e-12) {
            if (u0 <= 0.0) {
                below = s1 - s0;
                hi = lo;
            }
        } else {
            float cut = clamp(-u0 / dy, s0, s1); // where the ray crosses the ground's height
            if (dy > 0.0) {
                below = cut - s0;
                lo = cut;
            } else {
                below = s1 - cut;
                hi = cut;
            }
        }
    }
    float depth = below;
    if (hi > lo) {
        float k = dy * inv_h;
        float e_lo = exp(-max(u0 + dy * lo, 0.0) * inv_h);
        float e_hi = exp(-max(u0 + dy * hi, 0.0) * inv_h);
        if (abs(k) * (hi - lo) < 1e-4) {
            depth += exp(-max(u0 + dy * 0.5 * (lo + hi), 0.0) * inv_h) * (hi - lo);
        } else {
            depth += (e_lo - e_hi) / k;
        }
    }
    return depth;
}
// Rayleigh's phase and the aerosol's (Cornette-Shanks), per steradian (rt_sky.h)
float medium_rayleigh_phase(float c) { return 0.0596831037 * (1.0 + c * c); }
float medium_aerosol_phase(float c, float g)
{
    float k = max(1.0 + g * g - 2.0 * g * c, 1e-6);
    return 0.119366207 * (1.0 - g * g) * (1.0 + c * c) / ((2.0 + g * g) * k * sqrt(k));
}
)";

// --- (round MEDIUM) the water's body as a medium: one definition for the water's shader and the path tracer -------------
// The water's shader (water_shader.cpp) has always taken what is under the surface through an absorbing body - its
// extinction from the water's colour and how far one sees into it (its `through`) - and the light the body scatters back
// as deep water's colour (its `deep`: a diffuse reflectance of the water's hue, x the light the surface gets). The path
// tracer's water (round LW / POOL: an interface - Fresnel's share mirrored, the texture's coverage the body's own colour)
// now has the same body below the interface: Beer-Lambert's extinction water_extinction, and scattering (an even phase)
// whose single-scattering albedo gives a deep body that reflectance - Kubelka-Munk's two-stream relation for a
// semi-infinite medium, R = (1 - sqrt(1 - a)) / (1 + sqrt(1 - a)), so a = 1 - ((1 - R) / (1 + R))^2. The surface is the
// plane it was entered through (a path's next-event rays from under the water cross that much of it).
// (GLSL, after nothing)
inline constexpr const char* kWaterMediumGlsl = R"(
// water's extinction per world unit for a body of hue `hue` (its brightest channel 1) seen `clarity` units into
vec3 water_extinction(vec3 hue, float clarity) { return (3.0 / max(clarity, 1.0)) * (0.2 + 1.6 * (1.0 - hue)); }
// deep water's diffuse reflectance per unit of the body's albedo's luminance (its scattered light over a white
// diffuser's under the same light, x that luminance)
vec3 water_deep_reflectance(vec3 hue)
{
    vec3 h2 = hue * hue; // (deep water's colour more saturated than the texture's mean, which has its highlights)
    return h2 * (0.65 / max(dot(h2, vec3(0.2126, 0.7152, 0.0722)), 0.05));
}
// the body's single-scattering albedo for a deep reflectance R (Kubelka-Munk's two-stream, semi-infinite)
vec3 water_scattering_albedo(vec3 reflectance)
{
    vec3 r = clamp(reflectance, vec3(0.0), vec3(0.999));
    vec3 q = (1.0 - r) / (1.0 + r);
    return 1.0 - q * q;
}
)";
// --- (round MEDIUM) the open sky's light in the air indoors -----------------------------------------------------------
// The air's light from the sky - the sky's own scattered light and every higher order (Psi at the ground for the
// atmosphere's air; the sky's light on an even upper hemisphere for the street air) - is the open air's: the air sees
// the whole sky. Where geometry closes part of it (a hall, a street canyon), what reaches the air toward the eye is the
// share of that light coming in through what is open, weighed by the medium's phase toward the eye: the in-scattering
// sigma_s Integral p(w . d) L(w) V(w) dw over the open air's sigma_s Integral p(w . d) L(w) dw (the sky taken as even
// over the upper hemisphere, as the open air's term takes it). Before (round FIXTURE's finding) the share was the open
// share alone, every open direction counted alike: Training's hall under its roof openings, the eye looking across, got
// the light the forward-scattering aerosol (g 0.8) sends ahead as if it came from in front - 60 x the reference's air
// (round MEDIUM, `pt ref` with the medium). The froxels sample the upper hemisphere evenly; a sample's weight is
// 4 pi p(w . d) / its mean over the upper hemisphere for the view's elevation (Rayleigh's: 1 for every elevation - the
// mean of (w . d)^2 over a hemisphere is 1 / 3; the aerosol's: open_sky_phase_mean, tabled over d.y).
inline constexpr std::size_t kSkyPhaseTable = 32; // the view's elevation's sine from -1 to 1
// The mean of 4 pi x the aerosol's phase (Cornette-Shanks, g) over the upper hemisphere's directions w for a view
// direction of elevation sine `view_y`: (1 / 2 pi) Integral_upper 4 pi p(w . d) dw.
[[nodiscard]] double open_sky_phase_mean(double view_y, double g);
[[nodiscard]] std::array<float, kSkyPhaseTable> open_sky_phase_table(double g);

// The processor's twins (the checks').
[[nodiscard]] std::array<float, 3> water_extinction(const std::array<float, 3>& hue, float clarity);
[[nodiscard]] std::array<float, 3> water_deep_reflectance(const std::array<float, 3>& hue);
[[nodiscard]] std::array<float, 3> water_scattering_albedo(const std::array<float, 3>& reflectance);

} // namespace render::rt
