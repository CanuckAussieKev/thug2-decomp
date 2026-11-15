#pragma once

// NOT ORIGINAL - the relit mode ([Graphics] rt_relit; docs/design/vulkan.md "Relit: the bake taken apart"): each level's
// baked vertex lighting separated from the artists' colours, so the world can be lit again by the ray-traced sun and
// sky.
//
// THUG2 bakes a level's light into its world meshes' vertex colours: what a vertex shows is (roughly) the artists'
// colour times the light that reached it, v = tint x L. Neither is stored apart. The relit mode works out L itself, with
// rays, from what a light model needs at each vertex:
//   - S: the sun's direct light - the share of kRelitSunRays rays toward the level's baked sun (rt_sun.h; jittered
//     over a disc kRelitSunRadiusDegrees across: a soft edge) that reach the sky, times N.L;
//   - K: the sky's light - the share of the sky seen over the vertex's hemisphere (sky_rays cosine-weighted rays that
//     escape; the ambient occlusion of the vertex);
//   - B: the light bounced to it - the same rays' hits on the world, each bringing the baked colour where it lands
//     (that surface's own tint x light: what it shows), averaged over all the rays (a miss brings none).
// The level's light model is then E = a S + b K + g B per channel (red, green, blue): a, b, g found by a robust
// least-squares fit over its mesh edges (two neighbours' colours' ratio is their light's; non-negative; each edge's
// error relative to its own light, so no term is favoured for being small; reweighted toward the median error, so
// lamps and unusually coloured surfaces count little). The fit's a is the sun's colour and strength as the bake has
// it, b the sky's, g how much the bounce counts.
// With E known, each vertex's tint is v / E (per channel; E no lower than kRelitFloorShare of the level's mean, so a
// vertex the model says is in darkness doesn't get a huge tint). Where E is small next to the level's median (a vertex
// the model can't vouch for: deep in shade) the tint is taken from its neighbours instead - their tints averaged
// over the mesh's edges (in logarithms, weighted by their confidence E / (E + kRelitConfidence x median E), smoothed
// kRelitSmoothPasses times) - and `detail` (0 - 1) says how much of each vertex's own tint stays where it can be
// vouched for (1: all - the bake's own detail kept; 0: none - every tint its neighbourhood's, the most relighting).
// Tints are clamped to kRelitMaxTint. The light the model can't explain - a vertex brighter than tint x E by more than
// kRelitLampLow times - is what the bake adds over the model, in full from kRelitLampHigh times on (smoothstep between):
// v - tint x E. It is taken apart by what it is (relit_split_residual):
//   - the bake's sun where the tracer saw none, or saw less than the bake shows (a caster the rays don't meet as the
//     bake did, the bake's sun stronger there than the level's fit): on a face toward the bake's sun, the part with the
//     sun's colour (a x tint per channel) that is at least kRelitMissedLeast of the sun's whole light there - the sun
//     reaching the vertex, not a dim glow - becomes `sun_missed`, more of the sun's visibility toward the bake's sun
//     (S + sun_missed, at most kRelitMostMissed). It is the sun's light: it follows the direct light now (its strength
//     and colour - the moon's at night) and only toward the bake's own sun (relit_missed_trust: whole within
//     kRelitTrustInner degrees of it, none past kRelitTrustOuter) - toward any other light the traced S alone says
//     what reaches the vertex, so the bake's sun shadows never show as hard wedges at another time;
//   - the rest - lamps, glows, the artists' own lights: light of its own colour, or too weak to be the sun - stays the
//     `residual`: the level's own emitters, whose light doesn't change with the time of day (only the exposure brings
//     them up at night). Occlusion the bake has beyond the traced sky stays in the tint (a darker surface under every
//     light).
// A lamp's light is told within a mesh, against the mesh's own neighbourhood; then across meshes (relit_local_across):
// a mesh the lamp lights whole takes its neighbours' local light, so the same lamp's light is the residual on every
// surface it reaches.
// Finally one factor (the level's `scale`, kept within kRelitScaleRange) makes the relit level's mean brightness at the
// bake's own sun the bake's.
//
// A vertex relit: scale x tint x E' + residual (the residual left out with rt_relit_residual=0), E' the model's light
// (its sun term a x N.L x (S + trust x sun_missed))
// with the sun where it is now (its rays traced again when it moves: rt_scene.h scene_sun - the console's `rt sun`,
// [Graphics] rt_sun_source=game or x,y,z) and its colour (a's) turned by the sky model's when one is on (rt_sky_model: the sun's colour at its new
// elevation over the colour at the bake's - the bake's own colour kept at the bake's sun); blended with the bake by
// `strength`. At the bake's own sun the relit vertex is the bake again (within the clamps, exactly with detail 1 and
// the residual kept) - the decomposition loses nothing; what changes is what a moved or recoloured sun does to it.
//
// A level whose bake shows no sun (BakedSun::found false: the Foundry, indoors - lit by its lamps) has no sun term to
// fit: it keeps its bake (relit_model's `valid` false). So does a level listed off in rt_relit_levels.json (or the
// user's thug2-decomp-relit.json beside the exe), whose entries can also set the level's strength and keep its baked
// sun under rt_sun_source=game ("sun": "level").
//
// API-neutral: the rays go through RayTracing::probe_many (the Vulkan Gpu's, or a check's). render_pc/rt_relit_world
// gathers the world's meshes, keeps the results (cached on disk) and writes the colours into the vertex buffers the game
// draws from - nothing in the shaders changes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"

namespace render::rt {

using Rgbf = std::array<float, 3>;

inline constexpr uint32_t kRelitSkyRays = 24;
inline constexpr uint32_t kRelitMaxSkyRays = 256;
inline constexpr uint32_t kRelitSunRays = 4;
inline constexpr uint32_t kRelitMaxSunRays = 64;
inline constexpr float kRelitSunRadiusDegrees = 1.5F;
inline constexpr float kRelitOffset = 2.0F;       // the rays start this far off the surface along its normal
inline constexpr float kRelitRayLength = 1.0e6F;  // (further than any level)
inline constexpr uint32_t kRelitWorldMask = 0x05; // the level geometry: the opaque (0x01) and the cut-outs (0x04) -
                                                 // the TLAS's instance masks; the cut-outs by their alpha (ProbeRequest::cutouts)
inline constexpr float kRelitFloorShare = 0.05F;
inline constexpr float kRelitConfidence = 0.35F;
inline constexpr uint32_t kRelitSmoothPasses = 4;
inline constexpr float kRelitMaxTint = 2.0F;
inline constexpr float kRelitLampLow = 1.2F;
inline constexpr float kRelitLampHigh = 1.8F;
inline constexpr float kRelitFacing = 0.05F;      // a face toward the bake's sun (N.L above this) can have missed sun
inline constexpr float kRelitMissedLeast = 0.6F;  // the sun-coloured excess at least this share of the sun's light
inline constexpr float kRelitMostMissed = 8.0F;   // sun_missed at most this (the bake's sun that much over the fit's: Berlin's ~5)
inline constexpr float kRelitTrustInner = 2.0F;   // degrees: sun_missed whole toward the bake's sun ...
inline constexpr float kRelitTrustOuter = 10.0F;  // ... and none this far from it
// The level's own lights across meshes (relit_local_across): the neighbours of other meshes within kRelitLocalReach,
// facing within kRelitLocalFacing (cosine) of the vertex, their weights (facing^2 x (1 - (d / reach)^2)^2) at least
// kRelitLocalLeastWeight in all; their local light taken where it is over kRelitLocalOver times the vertex's own and at
// least kRelitLocalLeast of the model's light there.
inline constexpr float kRelitLocalReach = 160.0F;
inline constexpr float kRelitLocalFacing = 0.5F;
inline constexpr float kRelitLocalLeastWeight = 0.5F;
inline constexpr float kRelitLocalOver = 1.25F;
inline constexpr float kRelitLocalLeast = 0.05F;
inline constexpr std::array<float, 2> kRelitScaleRange{0.7F, 1.4F};
inline constexpr float kRelitPerByte = 1.0F / 128.0F; // a colour byte as light: 128 is 1 (the passes double it)
inline constexpr float kRelitMaxValue = 255.0F / 128.0F;

struct RelitSettings {
    bool on = false;
    float strength = 1.0F; // 0: the bake as it is; 1: fully relit
    bool residual = true;  // the bake's light the model can't explain (lamps) kept
    float detail = 1.0F;   // how much of each vertex's own tint stays where the model vouches for it
    // (changed by the scene's sun: the sun the world is relit by is rt_scene.h relit_target_sun - [Graphics]
    // rt_sun_source, every effect's; rt_relit_sun is retired)
    uint32_t sky_rays = kRelitSkyRays;
    uint32_t sun_rays = kRelitSunRays;
    bool cache = true; // the analysis kept on disk (thug2-decomp-relit\ beside the exe)
};
void set_relit_settings(const RelitSettings& settings);
[[nodiscard]] RelitSettings relit_settings();
// The mode's state in a line (render_pc/rt_relit_world sets it; the console's `rt relit` prints it).
void set_relit_status(std::string text);
[[nodiscard]] std::string relit_status();
// (the console's `rt probe <across> <down>` - devtools reaches render_pc through it) What the picture shows at a point:
// render_pc/rt_record.h describe_screen_point, set by render_pc while ray tracing records frames; none: a line saying so.
using ScreenProbe = std::string (*)(float across, float down);
void set_screen_probe(ScreenProbe probe);
[[nodiscard]] std::string screen_probe(float across, float down);

// --- per-level choices (rt_relit_levels.json, built in; thug2-decomp-relit.json beside the exe adds to it) -----------
struct RelitLevel {
    std::string name;       // the level's scene, as LoadScene names it (z_dt, BO, foun...)
    bool off = false;       // "relit": false - the level keeps its bake
    float strength = -1.0F; // "strength": 0 - 1 (below 0: not set - the setting's)
    bool keeps_baked_sun = false; // (added by the scene's sun) "sun": "level" - lit by its baked sun under rt_sun_source=game
};
// The levels a document lists: {"levels": {"<name>": {"relit": false, "strength": 0.5, "sun": "level"}, ...}} (other
// keys ignored; "sun": "game" - the default - undoes a "level" listed before).
// False when it isn't such a document (`out` keeps what was read before the error).
bool parse_relit_levels(std::string_view json, std::vector<RelitLevel>* out);
// The built-in list (src/engine/gfx/rt_relit_levels.json).
[[nodiscard]] std::string_view built_in_relit_levels();

// --- the analysis ----------------------------------------------------------------------------------------------------

// A mesh of the world as the analysis reads it (world coordinates - the level's meshes are drawn untransformed).
struct RelitMesh {
    MeshId id = 0; // the RayTracing's (a hit on it is looked up by it); 0: rays can't find it
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;     // one per vertex, or empty: made from the triangles (area-weighted)
    std::vector<uint32_t> colours; // the baked colours, A8R8G8B8 (128: 1)
    // Its triangles as the RayTracing numbers them (a hit's primitive): a strip's odd ones turned back round, so all
    // wind the same way; a degenerate one (two corners the same) kept in its place.
    std::vector<std::array<uint32_t, 3>> triangles;
};

// What the analysis keeps for each vertex (the cache holds it): its light's parts and its colour's.
struct RelitVertex {
    Vec3 normal{};
    float sun = 0.0F; // the share of the sun's rays that reached the sky (toward the sun the vertex was traced for)
    float sky = 0.0F; // K
    Rgbf bounce{};    // B
    Rgbf tint{};
    Rgbf residual{};        // the level's own light here (lamps, glows): constant through the day
    float sun_missed = 0.0F; // the bake's sun the tracer missed (visibility toward the bake's sun, over S)
    float sun_baked = 0.0F;  // S toward the bake's own sun (kept when `sun` is traced again for another light)
    bool used = false;      // a corner of some triangle (an unused vertex keeps its colour)
};
using RelitVertices = std::vector<RelitVertex>;

// The level's light model.
struct RelitModel {
    bool valid = false;
    Vec3 toward{}; // the sun it was fitted with (the bake's)
    Rgbf sun{};    // a
    Rgbf sky{};    // b
    Rgbf bounce{}; // g
    float scale = 1.0F;
    float error = 0.0F;       // the fit's median residual (relative: an edge's light ratio against its colours')
    std::size_t vertices = 0; // that the fit used
    float sun_needed =
        0.0F; // the fit's objective without the sun over with it (reported: no test of a sun - rt_sun.h's
              // fit is)
};

struct RelitParams {
    uint32_t sky_rays = kRelitSkyRays;
    uint32_t sun_rays = kRelitSunRays;
    float sun_radius_degrees = kRelitSunRadiusDegrees;
    float detail = 1.0F;
};

struct RelitStats {
    std::size_t vertices = 0;
    std::size_t rays = 0;
};

// The vertices' normals (where the mesh gives none: its triangles', area-weighted, turned by `facing` - +1 or -1, the
// world's winding: relit_facing) and which vertices are used.
void relit_normals(const RelitMesh& mesh, float facing, RelitVertices* out);
// The world's winding: +1 when its triangles' (b - a) x (c - a) points out of their front, -1 when into it - found
// from the upward-facing area (floors outnumber ceilings): the sign that turns most of it up.
[[nodiscard]] float relit_facing(std::span<const RelitMesh> meshes);
// The sky and bounce of the meshes `wanted` says (all when empty) traced (normals made first), and their sun toward
// `toward`: rays through `tracer`, a hit's colour read from the mesh among `meshes` it lands on.
void trace_relit_light(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                       const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                       RelitStats* stats);
// Only the sky's share and the bounce (the vertices' normals made already), for the meshes `wanted` says - again after
// the sun moved, the meshes' colours then the relit ones: the light the sun now puts on the surfaces round a vertex
// bounced to it (one bounce of the new light; the sky's share comes out the same).
void trace_relit_sky(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RayTracing& tracer, std::vector<RelitVertices>* light, RelitStats* stats);
// Only the sun's share toward `toward`, for the meshes `wanted` says.
void trace_relit_sun(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                     const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                     RelitStats* stats);
// The model fitted to the traced light and the baked colours. Not valid when too few vertices are lit or the sun's
// term comes out as nothing.
[[nodiscard]] RelitModel fit_relit_model(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light,
                                         const Vec3& toward);
// The tints and residuals of the meshes `wanted` says, and (with `rescale`) the model's scale.
void decompose_relit(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RelitModel* model, std::vector<RelitVertices>* light, bool rescale);

// A vertex's residual taken apart (decompose_relit's last step): the bake's sun the tracer missed into sun_missed, the
// rest kept. `model` the level's (its sun a and toward), `scale` the factor its colours are relit with.
void relit_split_residual(const RelitModel& model, float scale, RelitVertex* vertex);
// A vertex's local light - the level's own lights (lamps, glows) that reach it, per unit of its tint: its residual over
// the model's scale x its tint (what the light field keeps, rt_light_field.h).
[[nodiscard]] Rgbf relit_local_light(const RelitModel& model, const RelitVertex& vertex);
// (decompose_relit's last step) The level's own lights told across meshes: a lamp's light is found as the residual
// only where it falls off within a mesh (the mesh's neighbourhood's tint against a vertex's own), so a mesh the lamp
// lights whole - a recessed door, a sign, a small facade piece - has it in its tint, and loses it with the sun at
// night while the wall beside it keeps it. So each vertex of the meshes `wanted` says takes the local light of its
// neighbours on other meshes (within kRelitLocalReach, facing the same way - kRelitLocalFacing - weighted by both)
// where that is more than its own (kRelitLocalOver; and some of its light, kRelitLocalLeast): its colour taken apart
// again over the model's light plus that local light - the tint the rest, the residual what the local light gives it
// (the bake exactly again). The same lamp's light is then the residual on every surface it reaches.
void relit_local_across(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitModel& model,
                        std::span<RelitVertices> light);
// How much of a vertex's sun_missed a light toward `toward` takes: 1 within kRelitTrustInner degrees of the bake's sun,
// 0 past kRelitTrustOuter (smoothly between).
[[nodiscard]] float relit_missed_trust(const RelitModel& model, const Vec3& toward);

// How the world is lit now.
struct RelitLighting {
    Vec3 toward{};                     // the sun (the vertices' `sun` traced toward it)
    Rgbf sun_colour{1.0F, 1.0F, 1.0F}; // the model's a times this
    float strength = 1.0F;
    bool residual = true;
    // (added by the time of day - rt_daylight.h) the light's strength at the clock's time over the level's own: the
    // sun's (or the moon's) a, the sky's b and the bounce's g times these (1: the level's own - its arithmetic as before)
    Rgbf direct_scale{1.0F, 1.0F, 1.0F};
    Rgbf sky_scale{1.0F, 1.0F, 1.0F};
    Rgbf bounce_scale{1.0F, 1.0F, 1.0F};
};
// The model's light at a vertex: a S + b K + g B (S from its `sun` and normal toward `lighting.toward`).
[[nodiscard]] Rgbf relit_light(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// ... in its parts: the direct light (a x N.L x (S + the missed sun)) and the indirect (b K + g B) - what the light
// field (rt_light_field.h) keeps.
struct RelitParts {
    Rgbf direct{};
    Rgbf indirect{};
};
[[nodiscard]] RelitParts relit_parts(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// The colour a vertex is drawn with (A8R8G8B8; the original's alpha kept): the bake blended with the relit colour.
[[nodiscard]] uint32_t relit_colour(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting,
                                    uint32_t original);
// A baked colour's channels as light (red, green, blue; 128: 1).
[[nodiscard]] Rgbf colour_light(uint32_t argb);

} // namespace render::rt
