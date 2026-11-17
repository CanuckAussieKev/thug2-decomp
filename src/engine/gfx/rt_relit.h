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
// The bake's light at a vertex is then modelled as E = a S + b K + g B + c L per channel (red, green, blue): a, b, g
// found by a robust fit over its mesh edges (two neighbours' colours' ratio is their light's; non-negative; each edge's
// error relative to its own light; reweighted toward the median error, so lamps and unusually coloured surfaces count
// little); meshes of one colour all over (painted, not lit by the bake) are left out; an edge across a traced shadow's
// border counts only where the bake's colours step down into it too; and the sun's strength against the rest is then
// set by which way faces turn (rt_relit.cpp facing_calibration). c, the lamps', is fitted after them over the edges
// along which the lamps' light changes (fit_relit_lamps) - so the bake's lamp light is taken out of the tints.
// The model describes the bake's lighting only to take it out: what the data keeps of each vertex is its true surface
// colour - its albedo, the `tint` (lighting round A, 2026-11-16: the user's "all lighting hacks should be removed ...
// handled via physics"). The tint is v / E (per channel; E no lower than kRelitFloorShare of the level's mean) where the
// bake agrees with the model; where it doesn't, the difference is light, never colour:
//   - a vertex brighter than its neighbourhood's tint x E by more than kRelitLampLow times (lamps the list doesn't
//     hold, the bake's sun where the rays saw a caster) or darker by as much (the bake's shadows where the rays saw the
//     sun - a fence post's, a picnic table's, a kiosk's -, darkening the bake painted into corners) takes its
//     neighbourhood's tint (wholly from kRelitLampHigh times on); the neighbourhood's own tint is made without such
//     vertices (local_tints' exclusion, kLampRounds rounds);
//   - a mesh the bake lit or shaded whole (a recessed door under a lamp, a small patch in a baked shadow) shows no such
//     difference within itself: the same difference found on its neighbours of other meshes facing the same way
//     (relit_light_across) is taken out of it;
//   - the light the difference was is dropped. Nothing of the bake's lighting is kept: no residual, no missed sun, no
//     level-wide scale - the lamps' light comes from the lamps (the data's list: position, colour x intensity, reach),
//     the sun's visibility from the rays.
// Where E is small next to the level's median the tint is taken from its neighbours too (their tints averaged over the
// mesh's edges, in logarithms, weighted by their confidence, smoothed kRelitSmoothPasses times). Tints are clamped to
// kRelitMaxTint (1: the texture as painted - the surface's albedo is the texture's colour x the tint).
//
// A vertex relit: tint x (a' S N.L + b' K + g B' + L x the lamps on), a', b' the model's sun and sky at the light now
// (the lighting state's: rt_daylight.h - the atmosphere's light over the level's own), S traced again toward the light
// now, B' the bounce of the relit light (hit colours: the relit ones), L the lamps' light (the same lamps the lamps pass
// shines, in the same units) while the lamps are on and the pass doesn't light them itself.
//
// A level whose bake shows no sun, or whose fit finds no sunlight, has data saying so: it keeps the original game's
// lighting. So does a level without data (the game says which build step makes it).
//
// API-neutral: the rays go through RayTracing::probe_many (the Vulkan Gpu's, or a check's). render_pc/rt_relit_world
// reads the level's data and writes the relit light into a store of its own (the light stream, rt_cel.h: a buffer per
// mesh beside the game's) that the world's draws read in place of the vertex colour - the game's vertex buffers are
// never written.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sun.h"

namespace render::rt {

using Rgbf = std::array<float, 3>;

inline constexpr uint32_t kRelitSkyRays = 24;     // (the game's: the bounce traced again as the sun moves)
inline constexpr uint32_t kRelitMaxSkyRays = 256;
inline constexpr uint32_t kRelitSunRays = 4;      // (the game's: the sun traced again as it moves)
inline constexpr uint32_t kRelitMaxSunRays = 64;
// (lighting round A2 - the bounce judged by energy) The bounce passes the game makes for each light
// (render_pc/rt_relit_world.cpp rebounce): each pass reads the colours the last one left, so after n passes the world
// holds up to n + 1 orders of interreflection - the series a path tracer sums (with albedos under 1 the orders past the
// third add a few percent).
inline constexpr uint32_t kRelitBouncePasses = 3;
inline constexpr uint32_t kRelitBakeSkyRays = 128; // the relit bake's (offline: no time limit)
inline constexpr uint32_t kRelitBakeSunRays = 32;
inline constexpr float kRelitSunRadiusDegrees = kSunRadiusDegrees; // (round S: the sun's own - rt_daylight.h)
inline constexpr float kRelitOffset = 2.0F;       // the rays start this far off the surface along its normal
// (lighting round A3 - the concave seam) ... and at most this far into the vertex's own triangles (RelitVertex::inward):
// a vertex on a mesh's edge - a floor's at a wall's foot - started its rays on the wall's plane, and those toward the
// wall crossed it at distance 0 (no hit) and saw a sky that isn't there (the furnace room's edge vertices: 0.5 - 0.78).
// Moved into its own surface it starts off every neighbouring plane, so they hit the wall. An inner vertex (its
// triangles all round it) isn't moved: their centres' mean is the vertex itself.
inline constexpr float kRelitInward = 2.0F;
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
// The light the model doesn't explain told across meshes (relit_light_across): the neighbours of other meshes within
// kRelitLocalReach, facing within kRelitLocalFacing (cosine) of the vertex, their weights (facing^2 x (1 - (d /
// reach)^2)^2) at least kRelitLocalLeastWeight in all; their difference from the model (a factor on the light) taken
// where it is over kRelitLocalOver times the vertex's own or under its inverse.
inline constexpr float kRelitLocalReach = 160.0F;
inline constexpr float kRelitLocalFacing = 0.5F;
inline constexpr float kRelitLocalLeastWeight = 0.5F;
inline constexpr float kRelitLocalOver = 1.25F;
inline constexpr float kRelitPerByte = 1.0F / 128.0F; // a colour byte as light: 128 is 1 (the passes double it)
inline constexpr float kRelitMaxValue = 255.0F / 128.0F;

struct RelitSettings {
    bool on = false;
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
    // (added by the level's compass) "north": the world heading (degrees, round from +z toward +x) of the level's north -
    // the time of day's sun path turned by it (rt_daylight.h NativeInputs::north); not set: north follows the bake's sun
    bool north_set = false;
    float north_heading = 0.0F;
};
// The levels a document lists: {"levels": {"<name>": {"relit": false, "sun": "level"}, ...}} (other keys - the retired
// "strength" among them - ignored; "sun": "game" - the default - undoes a "level" listed before).
// False when it isn't such a document (`out` keeps what was read before the error).
bool parse_relit_levels(std::string_view json, std::vector<RelitLevel>* out);
// The built-in list (src/engine/gfx/rt_relit_levels.json).
[[nodiscard]] std::string_view built_in_relit_levels();
// The list in use: the built-in one, then thug2-decomp-relit.json beside the exe - read once, at the first ask (the
// time of day's clock asks when a level arrives - its north -, render_pc's relit mode for the rest).
[[nodiscard]] const std::vector<RelitLevel>& relit_levels();
// A level's entry (its scene's checksum: the last entry naming it wins), or null.
[[nodiscard]] const RelitLevel* relit_level_entry(uint32_t scene);

// --- the analysis (the relit bake's, offline; the sun's and the bounce's traces also the game's real-time half) ------

// A mesh of the world as the analysis reads it (world coordinates - the level's meshes are drawn untransformed).
struct RelitMesh {
    MeshId id = 0; // the RayTracing's (a hit on it is looked up by it); 0: rays can't find it
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;     // one per vertex, or empty: made from the triangles (area-weighted)
    std::vector<uint32_t> colours; // the baked colours, A8R8G8B8 (128: 1)
    // (round L) the light each vertex shows (linear, the global unit - tint x light + its emission; relit_shown) for the
    // bounce's rays: what a ray hitting it brings back in place of the colours; empty: the colours'. Physical, not
    // pre-exposed and not 8 bits: a night's light is 10^-7 of the unit.
    std::vector<Rgbf> light;
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
    // (the bake's evidence) The triangles as the level's file has them, before the game's own geometry passes at load
    // (smoothing, building relief, the water's grid - render_pc/mesh_smoothing.h original_strip; their original
    // vertices are still first): the bake's light is on these - a vertex those passes made carries only its corners'
    // colours blended, not the bake's light, beside its own traced light (a relief side turned from the wall with the
    // wall's colour said "no sun": BO, BA). What the fit reads (edges, terms, the faces' turning); empty: `triangles`.
    // `baked_vertices`: how many of the vertices they use (the first); 0: all. Not in the data's content key.
    std::vector<std::array<uint32_t, 3>> baked_triangles;
    uint32_t baked_vertices = 0;
};
// (the bake's evidence) Whether a mesh's vertex is one the bake's light is on (RelitMesh::baked_vertices).
[[nodiscard]] inline bool relit_baked_vertex(const RelitMesh& mesh, std::size_t vertex)
{
    return mesh.baked_vertices == 0 || vertex < mesh.baked_vertices;
}

// What the analysis keeps for each vertex (the level's data holds it): its surface colour, and what its light is made
// of - the shares of the sun and sky it sees, the light bounced to it and the lamps' light reaching it (geometry and
// the lamps' data, never the bake's light).
struct RelitVertex {
    Vec3 normal{};
    float sun = 0.0F; // the share of the sun's rays that reached the sky (toward the sun the vertex was traced for)
    float sky = 0.0F; // K
    Rgbf bounce{};    // B (the data's: the bounce of the relit light at the bake's sun - relit_bake)
    Rgbf tint{};      // the surface's colour, its albedo (x the texture's): none of the bake's light in it
    float sun_baked = 0.0F;  // S toward the bake's own sun (kept when `sun` is traced again for another light)
    // (the offline relit data) L: the level's lamps' light reaching it, per channel (trace_relit_lamps: their colour x
    // intensity, falloff, facing, shadow rays) - lit with it while the lamps are on
    Rgbf lamp{};
    bool used = false;      // a corner of some triangle (an unused vertex keeps its colour)
    // (lighting round A3 - not kept in the data: made again with the normal) the way into the vertex's own surface - its
    // triangles' centres' area-weighted mean less the vertex, at most kRelitInward long: its rays' start is moved by it
    // (relit_ray_origin)
    Vec3 inward{};
    // (lighting round A3 - not kept in the data: the game's, from the lamps' switches) the radiance the vertex sends of
    // its own - a lamp's own surface (relit_data.h RelitEmitting: its light / pi x how much it is on), added to what it
    // reflects (relit_colour)
    // (round L) the lamp's surface luminance (relit_data.h RelitLamp::luminance) in the unit, in its colour, x how much it
    // is on - physical; `emit_masked`: a lamp drawn in a texture - shown on its lamp texels alone (rt_cel.h flags)
    Rgbf emit{};
    bool emit_masked = false;
};
using RelitVertices = std::vector<RelitVertex>;

// The level's light model.
struct RelitModel {
    bool valid = false;
    Vec3 toward{}; // the sun it was fitted with (the bake's)
    Rgbf sun{};    // a
    Rgbf sky{};    // b
    Rgbf bounce{}; // g
    Rgbf lamps{};  // c - the bake's lamp light (c x L: taken out of the tints; the relit lamps are L as the lamps shine)
    // (lighting round A) the bake shows no sun (night, indoors: the relit fit found none) - its light the sky, the
    // bounce and the lamps; the clock's sun brings daylight from the physical sky (rt::DayLight), like every level's
    bool sunless = false;
    // (lighting round A2 - one light unit) relit_to_unit's: the fit's bounce coefficient g (luma) - the tints x it are
    // albedos, the bake's light / it in the global unit (the game's colours are radiance in that unit: a white
    // diffuser under the reference day shows 1). Once the data is made, sun / sky are the level's own-time light in the
    // global unit (rt_daylight.h: the physical sun per N.L - none in a sunless bake - and sky) and bounce / lamps 1.
    float albedo_scale = 1.0F;
    // (reported) the bake's own daylight on a level surface toward its sun (a S + b, / g) over the physical day's there:
    // how far its artists' light is from the physical sky's (1: the same; 0: a sunless bake)
    float daylight_ratio = 0.0F;
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
    std::array<std::size_t, 3> lamp_votes{}; // (round A2) the lamps' solve: the edges it was told by per channel
    Rgbf lamp_median{};                      // ... and the solved lamps' median intensity over their found brightness
    std::size_t lamps_solved = 0;            // the lamps the bake's edges told (the rest: their own surfaces' light)
    std::size_t lamps_dark = 0;              // ... of them told dark (no light of theirs in the bake)
    std::size_t lamps_zero = 0;              // (round A3) ... of those solved to no light at all (the rest under the noise)
    std::size_t lamps_off_in_bake = 0;       // (round A3) ... and those whose solved light its own switch says was off
    float lamp_noise = 0.0F;                 // (round A3) the noise a lamp's share is told above: the unlit edges' median
    std::size_t lamps_implausible = 0;       // (round A3) ... and those whose light leaves its pool's surfaces darker than
                                             // any the level's lamps don't reach (a solve run off: its size untold)
    Rgbf lamp_high{};                        // ... the lit ones' 95th percentile over their own surfaces' light
    // (reported) the vertices whose bake differs from the model by kRelitLampLow or more: brighter (lamps the list
    // doesn't hold, the bake's sun where the rays saw a caster) and darker (the bake's shadows where the rays saw the
    // sun, its darkened corners) - that light dropped, their tints their surroundings'; and those told across meshes
    std::size_t brighter = 0;
    std::size_t darker = 0;
    std::size_t across = 0;
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
// (lighting round A3) Where a vertex's rays start: kRelitOffset off its surface along its normal and `inward` into it
// (every ray of the analysis - sky, bounce, sun, lamps - and the game's traces again).
[[nodiscard]] Vec3 relit_ray_origin(const Vec3& position, const RelitVertex& vertex);
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
void trace_relit_sky(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RayTracing& tracer, std::vector<RelitVertices>* light, RelitStats* stats, RelitAsk* ask = nullptr);
// Only the sun's share toward `toward`, for the meshes `wanted` says.
void trace_relit_sun(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const Vec3& toward,
                     const RelitParams& params, RayTracing& tracer, std::vector<RelitVertices>* light,
                     RelitStats* stats, RelitAsk* ask = nullptr);
// The model fitted to the traced light and the baked colours. Not valid when too few vertices are lit or the sun's
// term comes out as nothing.
// An edge across a traced shadow's border counts only where the bake's colours step down into the traced shade too.
// (lighting round A) `sunless`: the bake shows no sun (rt_sun.h's fit found none - a night or indoor bake, its vertices'
// sun 0): the sky, the bounce and (fit_relit_lamps) the lamps alone; valid when they light it (its sun 0).
[[nodiscard]] RelitModel fit_relit_model(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light,
                                         const Vec3& toward, bool sunless = false);
// (lighting round A2 - the baked sun picked by the relit fit) How well a model explains the bake: over every mesh edge
// of two lit vertices (one-colour meshes left out: the same edges for every model of the level) the edge's error
// |y_i E_j - y_j E_i| / (y_i E_j + y_j E_i) per channel - its colours' ratio against its light's -, luma-weighted, the
// mean (the sun's evidence is in a minority of the edges). Models fitted toward different suns are told apart by it
// (the edge set doesn't change with the sun).
[[nodiscard]] float relit_edge_error(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light,
                                     const RelitModel& model);
// The sun's share of the model's light on an open level surface (luma: a sin(e) over a sin(e) + b) - the baked sun's
// confidence when the relit fit picks it (0 for a sunless model).
[[nodiscard]] float relit_sun_share(const RelitModel& model);
// The candidates are told apart by how much their sun improves the fit (RelitModel::sun_needed: the edges' objective
// without the sun over with it - the edges across the candidate's traced shadows count only where the bake steps down
// there too, so the right sun keeps the most of them; the mean edge error, relit_edge_error, favours the weakest sun and
// can't). A sun is in the bake when the best one's fit is at least kRelitMinSunGain times worse without it (measured,
// round A2: SE - Pro Skater, evening - 1.006, kyoto 1.008, practice 1.000; the outdoor afternoon levels from z_ho's 1.015
// and fl / vc / thps3_la's 1.026 - 1.029 up to vn's 2.27).
// Its sun's share of an open surface's light (relit_sun_share) is reported as the confidence: a bake's own a against
// b - Boston's 0.27, CA's 0.14 (its artists' sun weak), the physical clear sky's 0.6 - 0.85 - is the bake's art, not a test.
inline constexpr float kRelitMinSunGain = 1.01F;
// ... and only in a level whose own data allows a sun: not a night state (its bake's directional light is the moon's),
// not roofed - its upward faces' median sky share (their traced K) at least kRelitIndoorSky (the rebake over 98 levels:
// with the gain alone the skate shops and warehouses - floors seeing 0.00 - 0.05 of the sky - "found" low suns, a term
// that fits their lamps' pools; the outdoor levels' floors see 0.26 - 0.94).
inline constexpr float kRelitIndoorSky = 0.15F;
// (the offline relit data) A lamp as the lamps pass shines it (rt_lights.h Emitter: an emitter found in the level or a
// point light).
struct RelitLamp {
    Vec3 position{};
    float size = 0.0F; // a shadow ray stops this short of it
    // its intensity per channel: x lamp_geometry the light it sends (1 right at a point or a panel). (Round A2) Solved
    // from the bake by solve_relit_lamps, then in the global light unit; before, the emitter's displayed brightness.
    Rgbf light{};
    float core = 0.0F; // the shape's core (a disc of its area's radius; a tube's half thickness) and its reach
    float reach = 0.0F;
    // (lighting round A2) its shape (rt_lights.h LampShape: point / panel / line, a panel's normal or a line's axis, a
    // line's half length) - an area or line light a path tracer samples
    EmitterShape shape = EmitterShape::kPoint;
    Vec3 axis{0.0F, 1.0F, 0.0F};
    float half_length = 0.0F;
    EmitterKind kind = EmitterKind::kGlow; // a found emitter's kind, or kPoint: the level's own point light
    // (reported, a path tracer's) its light in physical units (relit_lamp_photometry): the luminous intensity toward its
    // brightest side (cd), the flux it sends (lm) and its colour's correlated colour temperature (K; 0 - none: too far
    // from a black body's or dark)
    float candela = 0.0F;
    float lumens = 0.0F;
    float kelvin = 0.0F;
    bool solved = false; // its intensity told by the bake's edges (else its own surface's light)
    // (round L - lamps by physics, engine/gfx/lamp_fixtures.h) where its light comes from (RelitLampSource), its kind of
    // fixture, its luminous area (m^2: its emissive triangles' lit share) and that surface's luminance (cd/m^2 - its
    // flux / (pi area); what its own surface shows: RelitVertex::emit)
    uint8_t source = 0;
    uint8_t fixture = 0;
    float area = 0.0F;
    float luminance = 0.0F;
    // (lighting round A3) its switch (rt_lights.h lamp_on): the open sky's light (luma, the global unit) under which the
    // daylight round it is under a photocell's turn-on - relit_lamp_switches; kLampAlwaysOn where its surfaces see no
    // sky, 0 where it lights nothing (not known)
    float switch_sky = 0.0F;
};
[[nodiscard]] LampShape lamp_shape(const RelitLamp& lamp);
// (round L) Where a lamp's light comes from.
enum class RelitLampSource : uint8_t {
    kNone = 0,        // no light (a candidate the bake doesn't confirm)
    kSolved = 1,      // told by the bake's edges, its surface luminance within its kind's range
    kModel = 2,       // the bake can't tell it (no edges, off in the bake by its own switch): its fixture's model
    kImplausible = 3, // solved, but its surface luminance outside its kind's range - a failed measurement: the model
    kOwnSurface = 4,  // a point light (the level's own): its own displayed light - no surface to model
    kNotAFixture = 5, // a glow or candidate whose luminous area is no one fixture's of its kind (or none: a card with no
                      // saturated core - a halo or haze, not drawn round a lamp): no light
};
[[nodiscard]] const char* relit_lamp_source_name(RelitLampSource source);
// (round L) What the bake knew of a lamp's light (solve_relit_lamps' verdict, before the model).
enum class RelitLampTold : uint8_t {
    kSolved = 0,    // told, plausible by the bake's own tests
    kNoEdges = 1,   // not told in every channel - no edges reach it
    kOffInBake = 2, // its own switch had it off at the bake's time (a street lamp in an afternoon bake)
    kDark = 3,      // told, on in the bake, its light under the fit's noise: no light of its own the bake can see
    kRunOff = 4,    // told, but its pool darker than any unlit surface or not a light's colour: A3's implausible
};
// (round L) Each lamp's light where the bake can't tell it, or told it implausibly: its fixture (lamp_fixtures.h
// fixture_of, from its colour, shape and whether it is roofed) x its luminous area (`areas`, m^2: its emissive
// triangles x their lit share) - the flux pi L A of its kind's luminance L, put into the unit's intensity by its shape
// (relit_lamp_photometry's inverse); its colour its own where it is a light's (not saturated) or a sign's / neon's,
// else its kind's colour temperature. A solved lamp whose surface luminance (its flux / (pi A)) is outside its kind's
// range falls back to the model. `told`: solve_relit_lamps' verdicts. A candidate (a bright surface or texture) gets the
// model only off in the bake and only as a luminaire - not a sign, a window or a saturated colour, and outdoors (an
// indoor light is on in every bake: the bake shows whether it lights). Each lamp's source, fixture, area and luminance
// set; its photometry made (relit_lamp_photometry).
// (Round L, the College acceptance case) A glow the bake didn't tell no longer shines by its own surface's displayed
// radiance over its whole card (A3's lower bound): sch's magenta card at the origin, core 172 units, gave 46 million lm
// and blew the plaza magenta. A glow with a luminous core of a fixture's size takes its kind's model (dark in the bake
// too: under the fit's noise its size is untold); with none, or one no fixture of its kind has, it is no lamp.
struct LampModelCounts {
    std::array<std::size_t, 6> by_source{};
    std::array<std::size_t, 8> by_fixture{};
};
// `own_colours`: each lamp's colour as found (its glow's or surface's, before the solve).
LampModelCounts relit_lamp_models(std::span<RelitLamp> lamps, std::span<const RelitLampTold> told,
                                  std::span<const float> areas, std::span<const Rgbf> own_colours, double unit_lux);
// The unit's intensity (lamp_geometry's 1 x this) that sends `lumens` by the lamp's shape (relit_lamp_photometry's
// inverse; 0 without a core).
[[nodiscard]] float relit_lamp_intensity_for(const RelitLamp& lamp, double lumens, double unit_lux);
// (lighting round A2) A vertex's lamps: each one's lamp_geometry x its shadow ray's visibility (per unit of intensity)
// - what solve_relit_lamps fits and what L is made of. One mesh's: `first` (one more than its vertices) indexes `links`.
struct RelitLampLink {
    uint32_t lamp = 0;
    float weight = 0.0F;
};
struct RelitMeshLinks {
    std::vector<uint32_t> first;
    std::vector<RelitLampLink> links;
};
using RelitLampLinks = std::vector<RelitMeshLinks>;
// L at the vertices of the meshes `wanted` says (their normals made): each lamp within its reach that the vertex faces,
// its light x lamp_geometry, where a shadow ray from the vertex reaches it (toward the shape's point - a line's nearest;
// the world's opaque geometry and cut-outs by their alpha, as the other rays). `links`, when given: every mesh's links
// (the meshes not wanted left empty).
void trace_relit_lamps(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted,
                       std::span<const RelitLamp> lamps, RayTracing& tracer, std::vector<RelitVertices>* light,
                       RelitStats* stats, RelitLampLinks* links = nullptr);
// (lighting round A2 - lamps by their light, not a level's c) Each lamp's intensity, per channel, from the bake once the
// sun, sky and bounce are fitted: over the mesh edges of two lit vertices with lamps (one-colour meshes left out), a
// shared surface colour makes y_i (E_j + sum_l I_l G_jl) = y_j (E_i + sum_l I_l G_il) - linear in the intensities; their
// non-negative least squares (each edge relative to its own light, reweighted toward the median error kLampRounds
// times; projected coordinate descent). A lamp the bake doesn't tell shines by its own surface (a glow's): a Lambertian
// emitter of the radiance its glow shows (the game's colours are radiance in the unit) - pi x it at the surface
// (lamp_geometry's 1). Then each vertex's L again (relit_lamp_light), and the model's lamps' coefficient 1.
inline constexpr std::size_t kRelitLampEdges = 64;
inline constexpr float kRelitLambertian = 3.14159265F; // (a Lambertian surface's exitance over its radiance)
// (lighting round A3 - no "dark level", no lamps-on guess for the bake) Every lamp is solved, in every bake; the bake
// tells a lamp's light when it has kRelitLampEdges edges in every channel AND its solved light is a share of the light
// where it lights at least the fit's own noise (RelitModel::error, the median edge's relative residual - a lamp under
// it can't be told from the fit's error: a street lamp in a sunlit bake) - and, for a candidate (no artist's mark), its
// own switch (rt_lights.h lamp_on, the game's rule, against the bake's own sky light b) says it was on: a light under the
// daylight where it lights would have been off, the fit's residue, not a lamp's. Told with light: its intensity solved (it was
// on in the bake). Otherwise - off in the bake, swamped, or no edges - a glow or a point light (the artists' mark of a
// lamp) shines by its own surface; a candidate (rt_lights.h candidate_kind: a bright surface or texture) gives no light
// - brightness alone is no lamp.
// `bake_time`: the bake's own time's light (the global unit's lux, its physical open sky's light - luma - and the open
// sky's lamps then) - a candidate's switch at it; unit_lux 0: not checked.
struct BakeTime {
    double unit_lux = 0.0;
    float sky = 0.0F;
    float lamps = 0.0F;
};
void solve_relit_lamps(std::span<const RelitMesh> meshes, std::span<RelitVertices> light, const RelitLampLinks& links,
                       std::span<RelitLamp> lamps, RelitModel* model, const BakeTime& bake_time = {},
                       std::vector<RelitLampTold>* told = nullptr);
// Each vertex's L from its links and the lamps' intensities (building relief's made vertices from their sources) - with
// all lamps on, or (`on`, one a lamp) each x how much it is on.
void relit_lamp_light(std::span<const RelitMesh> meshes, std::span<RelitVertices> light, const RelitLampLinks& links,
                      std::span<const RelitLamp> lamps, std::span<const float> on = {});
void relit_mesh_lamp_light(const RelitMesh& mesh, RelitVertices* light, const RelitMeshLinks& links,
                           std::span<const RelitLamp> lamps, std::span<const float> on = {});
// (lighting round A3) Each lamp's switch (RelitLamp::switch_sky): a photocell's - it turns a lamp on when the daylight
// on it falls under kPhotocellLux (ANSI C136.10's turn-on, 1.5 foot-candles: a standard street-lighting photocontrol,
// a physical constant, not a level's or a lamp's value). The daylight where the lamp is: the open sky's light x the sky
// share K of the surfaces it lights (each alike: the area round it). So the switch is the open sky's light (the global
// unit; `unit_lux` its lux) at which that reaches kPhotocellLux. A lamp whose surfaces are roofed - their sky share under
// kRelitIndoorSky, the rule that tells a roofed level's floors - is an interior light: kLampAlwaysOn (a room is lit
// while it is used, whatever its windows let in; a photocell is a street light's). Not the lamp's own brightness: the
// lamps solved from a night bake carry its art's exposure (the night's sky display-referred, ~10^3 x a real night's),
// and by their own light some outshone noon.
inline constexpr float kRelitNoSky = 1e-3F;
inline constexpr float kPhotocellLux = 16.0F;
void relit_lamp_switches(std::span<const RelitVertices> light, const RelitLampLinks& links, std::span<RelitLamp> lamps,
                         double unit_lux);
// (lighting round A2 - one light unit for every level) The decomposition in the global unit, from energy alone: the
// light bounced to a vertex, B, is the radiance its surroundings show - in the bake's own colours, so in the game's unit
// whatever the bake's exposure - and a surface's light takes it whole (one bounce adds the surroundings' radiance x the
// cosine-weighted share that reaches it: coefficient 1). The fit's g (per channel) is then the factor between its tints
// and albedos: tints x g (held to kRelitMaxTint), each vertex's L and the solved lamps' intensities / g (a lamp shining
// by its own surface is in the unit already: relit_lamp_light makes L again after), the model's sun and sky / g (the
// bake's own light in the unit), its bounce and lamps 1, `albedo_scale` g's luma. The same rule for a sunlit, a night and an
// indoor bake (no exposure guessed, no corpus value); the bake's own sun and sky are then replaced by the physical ones.
void relit_to_unit(RelitModel* model, std::span<RelitVertices> light, std::span<RelitLamp> lamps);
// (lighting round A2) A lamp's photometry from its intensity in the global light unit (`unit_lux`: the illuminance of
// the unit's 1 - rt_sky.h kSolarIlluminance x pi / sky_exposure; the world's units inches): candela, lumens, kelvin.
void relit_lamp_photometry(RelitLamp* lamp, double unit_lux);
// The tints - the surfaces' colours - of the meshes `wanted` says (the header's "The model describes the bake's
// lighting only to take it out"): each vertex's colour over the model's light at the bake where they agree, its
// surroundings' tint where the bake's light differs from the model's by kRelitLampLow or more (that light dropped),
// then the differences told across meshes (relit_light_across). Reported in the model's brighter / darker / across.
void decompose_relit(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted, const RelitParams& params,
                     RelitModel* model, std::vector<RelitVertices>* light);
// (decompose_relit's last step) The bake's light the model doesn't explain told across meshes: a lamp or a baked shadow
// is found within a mesh only where it changes along it, so a mesh it lights or shades whole - a recessed door under a
// lamp, a small patch inside a shadow - still has it in its tint. Each vertex of the meshes `wanted` says whose own
// light agrees with the model takes its neighbours' on other meshes (within kRelitLocalReach, facing the same way -
// kRelitLocalFacing - weighted by both): where they differ from the model by kRelitLocalOver or more, either way, its
// tint is divided by their difference (their factor on the light). `differs`: each mesh's vertices' factor (the bake's
// light over the model's; 1 where they agree) - the evidence. Returns the vertices changed.
std::size_t relit_light_across(std::span<const RelitMesh> meshes, std::span<const uint8_t> wanted,
                               std::span<const std::vector<float>> differs, std::span<RelitVertices> light);

// How the world is lit now, in the global light unit (lighting round A2: rt_daylight.h DayLight::sun_light / sky_light
// while the clock lights the level; the level's own - relit_own_lighting - while it doesn't). One unit for every level:
// the tints are albedos, the light the physical sky's and sun's (no level's own a, b; no scale toward its bake).
struct RelitLighting {
    Vec3 toward{};  // the sun or the moon (the vertices' `sun` traced toward it)
    Rgbf sun{};     // the direct light per unit of N.L
    Rgbf sky{};     // the sky's light on a level surface (x the vertex's K)
    float lamps = 0.0F; // the lamps' light L times this - the lamps on (0 - 1) and not lit per pixel by the lamps pass
};
// The level's own lighting (tod=level: nothing changed): toward its baked sun, the model's own-time light (RelitModel's
// sun and sky - none from the sun in a bake without one).
[[nodiscard]] RelitLighting relit_own_lighting(const RelitModel& model, float lamps);
// The light at a vertex: sun S N.L + sky K + B + L x lamps (S from its `sun` and normal toward `lighting.toward`; the
// bounce B the light the surroundings send - its coefficient 1: B is radiance in the same unit, the model's fitted g only
// described the bake).
[[nodiscard]] Rgbf relit_light(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// ... in its parts: the direct light (a x N.L x S), the indirect (b K + g B) and the lamps' (L x lamps) - what the
// light stream and the light field (rt_light_field.h) keep.
struct RelitParts {
    Rgbf direct{};
    Rgbf indirect{};
    Rgbf lamps{};
};
[[nodiscard]] RelitParts relit_parts(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// (round L) The light a vertex reflects (linear, the global unit, physical): its tint x its light - what the bounce's rays
// bring back from it (its emission is a lamp's: that light is the lamps' L already - counted once). And the light it
// shows: that + its own emission.
[[nodiscard]] Rgbf relit_reflected(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
[[nodiscard]] Rgbf relit_shown(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// The light the world draws a vertex with, as the light store's word (engine/gfx/light_store.h: RGB9E5, physical) - the
// light stream's colour word (rt_cel.h), which the draws read in place of the vertex colour (x the frame's pre-exposure).
[[nodiscard]] uint32_t relit_colour(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// A baked colour's channels as light (red, green, blue; 128: 1).
[[nodiscard]] Rgbf colour_light(uint32_t argb);

} // namespace render::rt
