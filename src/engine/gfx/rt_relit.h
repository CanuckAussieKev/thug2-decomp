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
// The light the model doesn't explain told across meshes (relit_light_across): the neighbours of other meshes within
// kRelitLocalReach, facing within kRelitLocalFacing (cosine) of the vertex, their weights (facing^2 x (1 - (d /
// reach)^2)^2) at least kRelitLocalLeastWeight in all; their difference from the model (a factor on the light) taken
// where it is over kRelitLocalOver times the vertex's own or under its inverse.
inline constexpr float kRelitLocalReach = 160.0F;
inline constexpr float kRelitLocalFacing = 0.5F;
inline constexpr float kRelitLocalLeastWeight = 0.5F;
inline constexpr float kRelitLocalOver = 1.25F;
// (a bake with no sun - night, indoors) the daylight a sunless level is lit by when the clock brings the sun: the
// median of the 63 sunlit bakes' fitted sun and sky (light units, at their own suns - tints' median 1 in every level), the
// lighting state's reference day (rt_daylight.h: its light over a sun kDefaultSunElevation up) times these
inline constexpr Rgbf kRelitDaySun{0.36F, 0.33F, 0.26F};
inline constexpr Rgbf kRelitDaySky{0.57F, 0.58F, 0.61F};
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
    bool keeps_baked_sun = false; // (added by the scene's sun) "sun": "level" - lit by its baked sun under rt_sun_source=game
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
    // (lighting round A) the bake shows no sun (night, indoors: rt_sun.h's fit found none) - its light the sky, the
    // bounce and the lamps; `sun` is then kRelitDaySun, the daylight the clock's sun brings (never at the level's own
    // time), and day_sky kRelitDaySky
    bool sunless = false;
    Rgbf day_sky{}; // the sky's light at the lighting state's reference day (the bake's `sky` where it has a sun)
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
// sun 0): the sky, the bounce and (fit_relit_lamps) the lamps alone; valid when they light it. The model's sun is then
// the daylight the clock brings (kRelitDaySun, day_sky kRelitDaySky).
[[nodiscard]] RelitModel fit_relit_model(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light,
                                         const Vec3& toward, bool sunless = false);
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
// (the median tint 1). c is the bake's: what is taken out of the tints - the relit lamps shine L itself.
inline constexpr std::size_t kRelitLampEdges = 64;
void fit_relit_lamps(std::span<const RelitMesh> meshes, std::span<const RelitVertices> light, RelitModel* model);
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

// How the world is lit now (render_pc/rt_relit_world: the lighting state's - rt_daylight.h DayLight -, or the level's
// own light while the clock is off).
struct RelitLighting {
    Vec3 toward{};                     // the sun or the moon (the vertices' `sun` traced toward it)
    Rgbf sun_colour{1.0F, 1.0F, 1.0F}; // the model's a times this (the sky model's colour, without the clock)
    // the light now over the level's own (the clock's: DayLight's light_colour and sky; 1 without it - but a sunless
    // level's direct light is 0 at its own time: its bake has no sun)
    Rgbf direct_scale{1.0F, 1.0F, 1.0F};
    Rgbf sky_scale{1.0F, 1.0F, 1.0F};
    bool day = false;   // the clock's light: a sunless level's sky is its day_sky (the reference day's) x sky_scale
    float lamps = 0.0F; // the lamps' light L times this - the lamps on (0 - 1) and not lit per pixel by the lamps pass
};
// The model's light at a vertex: a S N.L + b K + g B + L x lamps (S from its `sun` and normal toward `lighting.toward`).
[[nodiscard]] Rgbf relit_light(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// ... in its parts: the direct light (a x N.L x S), the indirect (b K + g B) and the lamps' (L x lamps) - what the
// light stream and the light field (rt_light_field.h) keep.
struct RelitParts {
    Rgbf direct{};
    Rgbf indirect{};
    Rgbf lamps{};
};
[[nodiscard]] RelitParts relit_parts(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// The colour the world draws a vertex with (A8R8G8B8, 255 alpha; light x 128): its tint x its light, held to the
// colour's range - the light stream's colour word (rt_cel.h), which the draws read in place of the vertex colour.
[[nodiscard]] uint32_t relit_colour(const RelitModel& model, const RelitVertex& vertex, const RelitLighting& lighting);
// A baked colour's channels as light (red, green, blue; 128: 1).
[[nodiscard]] Rgbf colour_light(uint32_t argb);

} // namespace render::rt
