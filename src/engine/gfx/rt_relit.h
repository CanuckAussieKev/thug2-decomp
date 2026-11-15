#pragma once

// NOT ORIGINAL - the relit mode ([Graphics] rt_relit; docs/design/vulkan.md "Relit: the bake taken apart, offline"): each
// level's baked vertex lighting separated from the artists' colours, so the world can be lit again by the ray-traced
// sun, sky and lamps.
//
// The decomposition is made offline, once a level, by the relit bake (render_pc/relit_bake.h: `thug2-decomp.exe
// -relit-bake <level>`; tools/game/relit_bake.ts for every level) and kept in the level's relit data (relit_data.h);
// the game only reads it. Of "the analysis" below only the sun's and the bounce's traces run in the game - traced
// again as the light moves (trace_relit_sun, trace_relit_sky: the real-time half).
//
// THUG2 bakes a level's light into its world meshes' vertex colours: what a vertex shows is (roughly) the artists'
// colour times the light that reached it, v = tint x L. Neither is stored apart. The bake works out the light itself,
// with rays, from what a light model needs at each vertex:
//   - S: the sun's direct light - the share of the sun rays toward the level's baked sun (rt_sun.h; jittered over a
//     disc kRelitSunRadiusDegrees across: a soft edge) that reach the sky, times N.L;
//   - K: the sky's light - the share of the sky seen over the vertex's hemisphere (cosine-weighted rays that escape);
//   - B: the light bounced to it - the same rays' hits on the world, each bringing the baked colour where it lands
//     (that surface's own tint x light: what it shows), averaged over all the rays (a miss brings none);
//   - L: the level's lamps' light - the emitters and point lights the lamps pass shines (rt_lights.h: their colour,
//     intensity and falloff), each toward the vertex as it faces it, where a shadow ray reaches it (trace_relit_lamps).
// The level's light model is then E = a S + b K + g B + c L per channel (red, green, blue): a, b, g found by a robust
// fit over its mesh edges (two neighbours' colours' ratio is their light's; non-negative; each edge's error relative
// to its own light; reweighted toward the median error, so lamps and unusually coloured surfaces count little);
// meshes of one colour all over (painted, not lit by the bake) are left out; an edge across a traced shadow's border
// counts only where the bake's colours step down into it too; and the sun's strength against the rest is then set by
// which way faces turn (rt_relit.cpp facing_calibration). c, the lamps', is fitted after them over the edges along
// which the lamps' light changes (fit_relit_lamps) - a lamp's light is a term of its own, not hidden in the tints: the
// lamps pass replaces exactly that term.
// With E known, each vertex's tint is v / E (per channel; E no lower than kRelitFloorShare of the level's mean). Where E
// is small next to the level's median the tint is taken from its neighbours instead (their tints averaged over the
// mesh's edges, in logarithms, weighted by their confidence, smoothed kRelitSmoothPasses times). Tints are clamped to
// kRelitMaxTint. The light the model can't explain - a vertex brighter than tint x E by more than kRelitLampLow times -
// is what the bake adds over the model, in full from kRelitLampHigh times on: v - tint x E. It is taken apart by what
// it is (relit_split_residual):
//   - the bake's sun where the tracer saw none, or saw less than the bake shows: on a face toward the bake's sun, the
//     part with the sun's colour that is at least kRelitMissedLeast of the sun's whole light there becomes
//     `sun_missed`, more of the sun's visibility toward the bake's sun. It is the sun's light: it follows the direct
//     light now and only toward the bake's own sun (relit_missed_trust);
//   - the rest - glows and the artists' own lights the lamps' list doesn't have - stays the `residual`: constant
//     through the day.
// And the bake's sunlight is never left in a tint (relit_sun_in_tint): a vertex facing the bake's sun whose tint comes
// out far brighter than its surface's sunlit tints, by the sun's colour, had the bake's sun where the rays saw a
// caster - that light becomes direct light, which a shadow can fall on.
// A lamp's light the list doesn't have is told within a mesh, then across meshes (relit_local_across). Finally one
// factor (the level's `scale`, kept within kRelitScaleRange) makes the relit level's mean brightness at the bake's own
// sun the bake's.
//
// A vertex relit: scale x tint x (E' + c L) + residual (the level's own light - relit_own_light - left out with
// rt_relit_residual=0), E' the model's light with the sun where it is now (its rays traced again when it moves) and the
// time of day's scales; blended with the bake by `strength`. At the bake's own sun the relit vertex is the bake again.
//
// A level whose bake shows no sun, or whose fit finds no sunlight, has data saying so: it keeps the original game's
// lighting. So does a level without data (the game says which build step makes it).
//
// API-neutral: the rays go through RayTracing::probe_many (the Vulkan Gpu's, or a check's). render_pc/rt_relit_world
// reads the level's data and writes the colours into the vertex buffers the game draws from - nothing in the shaders
// changes.

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

inline constexpr uint32_t kRelitSkyRays = 24;     // (the game's: the bounce traced again as the sun moves)
inline constexpr uint32_t kRelitMaxSkyRays = 256;
inline constexpr uint32_t kRelitSunRays = 4;      // (the game's: the sun traced again as it moves)
inline constexpr uint32_t kRelitMaxSunRays = 64;
inline constexpr uint32_t kRelitBakeSkyRays = 128; // the relit bake's (offline: no time limit)
inline constexpr uint32_t kRelitBakeSunRays = 32;
inline constexpr float kRelitSunRadiusDegrees = 1.5F;
inline constexpr float kRelitOffset = 2.0F;       // the rays start this far off the surface along its normal
inline constexpr float kRelitRayLength = 1.0e6F;  // (further than any level)
inline constexpr uint32_t kRelitWorldMask = 0x05; // the level geometry: the opaque (0x01) and the cut-outs (0x04) -
                                                 // the TLAS's instance masks; the cut-outs by their alpha (ProbeRequest::cutouts)
// The fit's sun (luma) at least this: the bake shows sunlight (the model valid).
inline constexpr float kRelitMinSunLight = 0.05F;
inline constexpr float kRelitFloorShare = 0.05F;
inline constexpr float kRelitConfidence = 0.35F;
inline constexpr uint32_t kRelitSmoothPasses = 4;
inline constexpr float kRelitMaxTint = 2.0F;
inline constexpr float kRelitLampLow = 1.2F;
inline constexpr float kRelitLampHigh = 1.8F;
inline constexpr float kRelitFacing = 0.05F;      // a face toward the bake's sun (N.L above this) can have missed sun
inline constexpr float kRelitMissedLeast = 0.6F;  // the sun-coloured excess at least this share of the sun's light
inline constexpr float kRelitMostMissed = 2.0F;   // sun_missed at most this (the bake's sun up to twice the fit's where the
                                                  // rays missed it; more is the level's own light - lamps facing the sun)
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
    bool residual = true;  // the level's own light (the lamps' term and the residual) kept
    // (changed by the scene's sun: the sun the world is relit by is rt_scene.h relit_target_sun - [Graphics]
    // rt_sun_source, every effect's; rt_relit_sun is retired)
    // the rays the game traces again as the light moves (the bake's own: kRelitBake*, kept in the level's data)
    uint32_t sky_rays = kRelitSkyRays;
    uint32_t sun_rays = kRelitSunRays;
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

// --- the analysis (the relit bake's, offline; the sun's and the bounce's traces also the game's real-time half) ------

// A mesh of the world as the analysis reads it (world coordinates - the level's meshes are drawn untransformed).
struct RelitMesh {
    MeshId id = 0; // the RayTracing's (a hit on it is looked up by it); 0: rays can't find it
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;     // one per vertex, or empty: made from the triangles (area-weighted)
    std::vector<uint32_t> colours; // the baked colours, A8R8G8B8 (128: 1)
    // Its triangles as the RayTracing numbers them (a hit's primitive): a strip's odd ones turned back round, so all
    // wind the same way; a degenerate one (two corners the same) kept in its place.
    std::vector<std::array<uint32_t, 3>> triangles;
    // (added by building relief - engine/gfx/facade_relief.h) A vertex made from others: the relief's points lie on a
    // flat wall's triangle (moved off it), so their traced light is that triangle's corners' blended - the wall's light
    // at that point, as without relief - never traced from the relief's own corners (a point at the foot of a sill's
    // side or in a recess's corner would see half its rays blocked, and its light spread over the whole piece: per
    // vertex the relief's own shadows can't be drawn - the shadow pass draws them per pixel). One per vertex, or empty.
    struct Source {
        std::array<uint32_t, 3> from{};
        std::array<float, 3> weight{};
        uint8_t count = 0; // 0: the vertex's own (traced)
    };
    std::vector<Source> sources;
};

// What the analysis keeps for each vertex (the cache holds it): its light's parts and its colour's.
struct RelitVertex {
    Vec3 normal{};
    float sun = 0.0F; // the share of the sun's rays that reached the sky (toward the sun the vertex was traced for)
    float sky = 0.0F; // K
    Rgbf bounce{};    // B
    // (added by lighting round 4) the part of B that is the level's own light where its rays land (their residual): a
    // lamp-lit wall's light bounced into a recess - constant through the day, as the residual is (traced with the
    // bounce once the residuals are known: trace_relit_sky's `local`)
    Rgbf bounce_local{};
    Rgbf tint{};
    Rgbf residual{};        // the level's own light here the lamps' term doesn't explain (glows): constant through the day
    float sun_missed = 0.0F; // the bake's sun the tracer missed (visibility toward the bake's sun, over S)
    float sun_baked = 0.0F;  // S toward the bake's own sun (kept when `sun` is traced again for another light)
    // (the offline relit data) L: the level's lamps' light reaching it, per channel (trace_relit_lamps) - the model's
    // c x L is its lamps' term
    Rgbf lamp{};
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
    Rgbf lamps{};  // c (the offline relit data: the lamps' term - 0 where the level has no lamps or the fit found none)
    float scale = 1.0F;
    float error = 0.0F;       // the fit's median residual (relative: an edge's light ratio against its colours')
    std::size_t vertices = 0; // that the fit used
    float sun_needed =
        0.0F; // the fit's objective without the sun over with it (reported: no test of a sun - rt_sun.h's
              // fit is)
    // (reported, not kept in the cache) the rays' shadow edges - a vertex in the traced sun beside one in its shade,
    // facing the same way: how many, and the medians of their colours' ratio in the bake and of their light's in the
    // model (the sun's strength told from the shadows alone: the model's far under the bake's - a sun fitted too weak)
    std::size_t shadow_edges = 0;
    std::size_t shadow_edges_kept = 0;
    float sun_calibration = 1.0F; // the factor the faces' turning put on the edges' sun (rt_relit.cpp facing_calibration) // ... of them with the bake's step: those the fit is told by
    std::size_t one_colour_meshes = 0;
    float facing_baked = 0.0F; // the faces toward the sun (in the rays' sun) over those turned from it: the bake's ...
    float facing_model = 0.0F; // ... and the model's (medians' ratios) // meshes left out of the fit: one colour all over (painted, not lit by the bake)
    float shadow_baked = 0.0F;
    float shadow_model = 0.0F;
    // (added by the light energy audit; reported) channels the edges gave no sun, started from the others' (fit_relit_model)
    std::size_t dead_channels = 0;
    // (the offline relit data; reported) the lamps' fit: its edges' votes per channel and their weighted median before it
    // is held at 0; the vertices the bake's sunlight was taken out of the tint at (relit_sun_in_tint)
    std::array<std::size_t, 3> lamp_votes{};
    Rgbf lamp_median{};
    std::size_t sun_in_tint = 0;
};

struct RelitParams {
    uint32_t sky_rays = kRelitSkyRays;
    uint32_t sun_rays = kRelitSunRays;
    float sun_radius_degrees = kRelitSunRadiusDegrees;
    float detail = 1.0F;
    // (the real-time half) trace_relit_sky traces the bounce alone: the sky's share K kept as the level's data has it
    // (the bake's many rays - the sky doesn't move)
    bool keep_sky = false;
};

struct RelitStats {
    std::size_t vertices = 0;
    std::size_t rays = 0;
};

// The vertices' normals (where the mesh gives none: its triangles', area-weighted, turned by `facing` - +1 or -1, the
// world's winding: relit_facing) and which vertices are used.
void relit_normals(const RelitMesh& mesh, float facing, RelitVertices* out);
// (added by building relief) The meshes' made vertices (RelitMesh::sources) given their sources' traced light - the
// sun's, the sky's and the bounce's shares - blended (their own normals, colours and the rest kept). The tracing does it
// last; also for light read back (the cache).
void relit_derive(const RelitMesh& mesh, RelitVertices* light);
// The world's winding: +1 when its triangles' (b - a) x (c - a) points out of their front, -1 when into it - found
// from the upward-facing area (floors outnumber ceilings): the sign that turns most of it up.
[[nodiscard]] float relit_facing(std::span<const RelitMesh> meshes);
// The sky and bounce of the meshes `wanted` says (all when empty) traced (normals made first), and their sun toward
// `toward`: rays through `tracer`, a hit's colour read from the mesh among `meshes` it lands on.
void trace_relit_light(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                       const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                       RelitStats* stats);
// (added for the relit mode's moving light - render_pc/rt_relit_world.cpp) A trace in two halves, so the device's work
// is never waited for: asked (`ask` not answered: the rays recorded into it, nothing traced, the light left as it is),
// then - once the tracer has the answers to `requests` (RayTracing::probe_later / probe_ready) in `answers` and
// `answered` set - the same call again with the same meshes in the same order: the answers taken into the light.
// `spans`, when given, are the vertices asked (`wanted` then unused): a part of a mesh, so a frame's share of the work
// needn't be a whole mesh.
struct RelitSpan {
    std::size_t mesh = 0;
    std::size_t first = 0; // its first vertex ...
    std::size_t end = 0;   // ... and the one after its last
};
struct RelitRun {
    std::size_t mesh = 0;
    std::size_t vertex = 0;
    uint32_t first = 0; // its first ray among the requests
    uint32_t count = 0;
};
struct RelitAsk {
    std::vector<RelitSpan> spans;
    std::vector<ProbeRequest> requests;
    std::vector<RelitRun> runs;
    std::vector<ProbeResult> answers;
    bool answered = false;
};
// Only the sky's share and the bounce (the vertices' normals made already), for the meshes `wanted` says - again after
// the sun moved, the meshes' colours then the relit ones: the light the sun now puts on the surfaces round a vertex
// bounced to it (one bounce of the new light; the sky's share comes out the same).
// `local`: each mesh's vertices' own light (their residual), when known - the bounce's part from it (bounce_local).
void trace_relit_sky(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RayTracing& tracer, std::vector<RelitVertices>* light, RelitStats* stats,
                     std::span<const std::vector<Rgbf>> local = {}, RelitAsk* ask = nullptr);
// Only the sun's share toward `toward`, for the meshes `wanted` says.
void trace_relit_sun(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                     const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                     RelitStats* stats, RelitAsk* ask = nullptr);
// The model fitted to the traced light and the baked colours. Not valid when too few vertices are lit or the sun's
// term comes out as nothing.
// An edge across a traced shadow's border counts only where the bake's colours step down into the traced shade too.
[[nodiscard]] RelitModel fit_relit_model(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light,
                                         const Vec3& toward);
// (the offline relit data) A lamp as the lamps pass shines it (rt_lights.h Emitter: an emitter found in the level or a
// point light), its colour times its intensity.
struct RelitLamp {
    Vec3 position{};
    float size = 0.0F; // a shadow ray stops this short of it
    Rgbf light{};      // colour x intensity
    float core = 0.0F; // light_falloff's core and reach
    float reach = 0.0F;
};
// L at the vertices of the meshes `wanted` says (their normals made): each lamp within its reach that the vertex faces,
// its light x light_falloff x the cosine toward it, where a shadow ray from the vertex reaches it (the world's opaque
// geometry and cut-outs by their alpha, as the other rays).
void trace_relit_lamps(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted,
                       std::span<const RelitLamp> lamps, RayTracing& tracer, std::vector<RelitVertices>* light,
                       RelitStats* stats);
// The lamps' c, per channel, once a, b and g are fitted: over the mesh edges along which the lamps' light changes,
// each edge's c the one that makes its two colours' ratio their light's, weighted by how much the lamps' light changes
// there; their weighted median (none under 0; 0 with fewer than kRelitLampEdges edges). Then each channel's scale again
// (the median tint 1).
inline constexpr std::size_t kRelitLampEdges = 64;
void fit_relit_lamps(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, RelitModel* model);
// (decompose_relit's step after the residual's split) The bake's sunlight left in a tint taken out: a vertex facing the
// bake's sun, partly or wholly in the rays' shade, whose tint is more than kRelitSunInTintOver times its surface's
// sunlit tint (its mesh's vertices in the rays' sun, else the level's) by the sun's colour - the bake's sun reached it
// where the rays saw a caster - gets the sun its surface's tint needs as sun_missed (at most what its shade lacks of
// the whole sun), and its tint over that light. Its light is then direct light, which a shadow can fall on (the
// skater's too).
inline constexpr float kRelitSunInTintOver = 1.5F;
inline constexpr float kRelitSunInTintColour = 0.97F; // the excess's colour against the sun's (cosine) at least this
inline constexpr float kRelitSunInTintLeast = 0.2F;   // the missed sun found at least this (of the whole sun)
// Returns how many vertices it gave the sun.
std::size_t relit_sun_in_tint(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted,
                              const RelitModel& model, std::span<RelitVertices> light);
// The level's own light at a vertex - its lamps' term and its residual (scale x tint x c x L + residual): what the
// lamps pass's light is held against, what the light field keeps as the local light. Constant through the day.
[[nodiscard]] Rgbf relit_own_light(const RelitModel& model, const RelitVertex& vertex);
// The tints and residuals of the meshes `wanted` says, and (with `rescale`) the model's scale.
void decompose_relit(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RelitModel* model, std::vector<RelitVertices>* light, bool rescale);

// A vertex's residual taken apart (decompose_relit's last step): the bake's sun the tracer missed into sun_missed, the
// rest kept. `model` the level's (its sun a and toward), `scale` the factor its colours are relit with.
void relit_split_residual(const RelitModel& model, float scale, RelitVertex* vertex);
// A vertex's local light - the level's own lights (lamps, glows) that reach it, per unit of its tint: its own light
// (relit_own_light) over the model's scale x its tint (what the light field keeps, rt_light_field.h).
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
