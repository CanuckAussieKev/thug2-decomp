#pragma once

// NOT ORIGINAL - a level's relit data (docs/design/vulkan.md "Relit: the bake taken apart, offline"): the decomposition
// of the level's baked vertex lighting that the relit mode lights the world from, made offline by the relit bake
// (render_pc/relit_bake.h - `thug2-decomp.exe -relit-bake <level>`, driven for every level by tools/game/relit_bake.ts)
// and only read by the game: nothing of it is traced or fitted while the game runs.
//
// One file a level, thug2-decomp-relit\<the level's main scene checksum, 8 hex>.relit beside the exe:
//   - the level's verdict: relit (its bake shows a sun and the light model was found), relit without a sun (rt_sun.h's
//     fit found none - a night or indoor bake: lit by its sky, bounce and lamps, the clock's sun brings the daylight),
//     no model (the bake couldn't be taken apart: too few lit vertices);
//   - the baked sun (rt_scene.h BakedSun: its direction, confidence and the ground height) - every effect's level sun
//     comes from here;
//   - the light model (rt_relit.h RelitModel: the bake's sun a, sky b, bounce g and lamps c - what was taken out of the
//     tints -, whether the bake has no sun, the reference day's sky) and the margins its verdict was decided with;
//   - the level's lamps (rt_relit.h RelitLamp: position, colour x intensity, falloff core and reach) - the lights the
//     L term was traced from;
//   - each world mesh by the hash of its content (relit_mesh_content: its positions, its triangles and building
//     relief's made vertices' sources - not its colours: the level's scene lights and colour animations rewrite those
//     while the game runs, the time of day's night lamps with them), its vertices: the tint - the surface's colour, none
//     of the bake's light in it -, the sun's share toward the bake's sun, the sky's share K, the bounce B (of the relit
//     light at the bake's sun) and the lamps' light L. The normals and which vertices are used are made again from the
//     mesh (relit_normals, with the world's winding kept here).
// The level's content key is the meshes' hashes, sorted: a level whose meshes changed (another overlay) no longer
// matches its data - the game then runs the level's original lighting and says the data is out of date. (v18) Building
// relief or smoothed curves turned on or off since the bake don't: each mesh is also keyed by the mesh as the file
// gives it (relit_mesh_original) - its file's vertices take their data, the vertices those passes made their sources'.
//
// Little-endian; the vertices' parts as half floats. API-neutral (no file access here: the callers read and write).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"

namespace render::rt {

inline constexpr uint32_t kRelitDataMagic = 0x44523254U; // "T2RD"
// (2: the meshes keyed without their colours; 3 - lighting round A: tints with none of the bake's light, no residual /
// missed sun / scale / agreement grid, sunless bakes relit, the lamps' list; 4 - round A2: one light unit - the tints
// albedos, the model's light the level's own in the global unit, the bake's exposure -, the baked sun picked by the
// relit fit, the lamps' intensities solved with their shapes and photometry, the emissive triangles; 5 - the bake's
// colours decoded before the fit (round F's display-referred colours: the albedos and the light linear, the colour
// word encoded); 6 - round S: the bake's sun the real sun's disc (0.27 degrees its radius, was 1.5 - the fit's sun rays
// and the penumbrae in the tints), and the file records the sun's radius it was made with, after the version: a file
// made with another sun is out of date like one of another version - a change of the sun's size can't go unnoticed.
// 7 - round A3: each lamp's switch (rt_lights.h lamp_on: interiors on, street lights by a photocell - no "dark
// level"), each vertex's lamp links (its L made again as lamps switch one by one), the lamps' emitting vertices (their
// bulbs shown by their light), the rays started inside their own surface (no sky at a concave seam), the textured
// ceiling lights, the lamps solved by an instrument. A file of another version is out of date: the game says to run
// relit_bake.)
// (round L) v8: each lamp's light source (solved / its fixture's model / ...), fixture, luminous area and luminance.
// v9: the art's lamp exposure (RelitModel::lamp_exposure) - the solved lamps' flux put from the art's exposure into
// physical units, so solved lamps are kept; each lamp's switch where its light is zero too.
// v10 (round relit-misses): a lamp's emitting vertices its own triangles' in its own mesh (not every vertex at their
// places - a neighbour's faces glowed); the sun's candidates told apart on a sample of a big level.
// v11 (round indoor lamps): lamps told by the bake's light level as well as its edges, lamps found from their pools
// (EmitterKind::kPool), lamp texels read on blended surfaces, the albedos taken over the told lamps' light alone.
// v12 (round light cards): the light of the original's light cards laid on a surface in that surface's bake evidence (a
// floor's light pool drawn as a card), the cards themselves left out of the fit, no cap on a level's lamp candidates.
// v13 (round bounce unit): the bounce's strength the bake's own measure of it (its enclosed vertices' colour over B), the
// daylight scaled against it - the global unit's anchor (rt_relit.h kRelitLeastBounceEvidence).
// v14 (round bounce unit 2): the measure a fit of the bounce and the bake's floor (its ambient: taken out of the tints,
// not lit), the sky's leak taken out, unexplained vertices left out; colours at the bake's clamp a bound, not a measure
// (no edge, evidence or facing median told by them - Proving Ground's sun).
// v15 (round PT-B): one class of each surface for every ray (rt_surface_class.h) - a blended surface drawn opaque (its
// drawn alpha 1 everywhere: Boston's brick facades, road markings) blocks the bake's sun, sky, lamp and bounce rays and
// is a solid lamp candidate; the seen-through surfaces let each ray's light through by their share (seen_through).
// v16 (round P10 - objects relit): the level's objects drawn with their own baked colours that stand still, each mesh
// where it stands (keyed by its content there - its positions through its instance's transform), taken apart by the
// level's model as its own meshes are (tints, emission, lamp links) - their lamps (a globe's) found with the level's;
// the model fitted over the level's own meshes alone.
// v17 (round SL - the game's own lights): every light node's light listed, on or off at the bake's time (a night state's
// street lamps), each lamp that is one of them carrying its node's name (RelitLamp::game_light: the game switches it);
// a light whose head is a found emitter one lamp with it (the head's surface emitting); the surfaces of another lighting
// state looked at for lamp heads.
// v18 (round LV - the data whatever the geometry settings): each mesh also keyed by the mesh as the level's file gives
// it, before the load's geometry passes (smoothing, building relief) - RelitDataMesh::original, file_vertices. Data made
// with smoothing on was "made from other meshes" for a game with it off (and the other way round): Boston's 546
// smoothed meshes not found, the level back to the original's lighting. A v17 file is still read (no original keys:
// its meshes found by their content alone - kRelitDataOldestRead).
// v19 (round BAKEALB - one albedo definition): the bounce B each hit's light x the albedo of the texel it lands on (the
// renderers' texel x tint, not the tint alone) - the fit's g, so every tint, in another unit: a v18 file's tints are
// the texture's mean too dark under every renderer. Older files are not read.
// v20 (round MATALB - a surface's reflectance from its texture and material alone): the bake's bounce B each hit's light
// x the material's albedo - its texture calibrated to its kind's measured range (the probe's hit albedo), the tint no
// albedo - so the fit's g, the lamps' solved light and the data's bounce in another unit. Older files are not read.
// v21 (round EMIT - an emitter's faces): a lamp's surface sends its flux from the faces the game draws (a one-sided
// surface its front, a two-sided one both - RelitMesh::two_sided): its luminance the flux over pi x its drawn area x
// its faces (a two-sided card's half v20's); a solve whose flux is more than its kind's brightest luminance over its
// luminous area sends is the model's; a glow's row of fittings its fittings' typical flux. Older files are not read.
// v22 (round EMIT2 - an emitter's flux conserved): a lamp's surface emits its luminance x the colour as drawn on the
// texels that emit, so its luminance is its flux over pi x that colour's mean over its drawn area (relit_bake.cpp
// emitting_vertices' `emitted`) - 1.5 - 5 x v21's on textured lamps -, its pattern kept; a lamp whose surface draws no
// emitting texel keeps its analytic light alone. Older files are not read.
// v23 (round SUBPIX - one emission map): a surface emits its luminance x its emission map (rt_emission_map.h: the texel
// alone - its colour and its own alpha - under its MESH's lamp-texel code), no longer x the material's colour nor with
// the vertex alpha in the solid test; the luminance is the flux over pi x that map's mean over the drawn area. Older
// files are not read.
// v24 (round FIXTURE): a light volume (rt_lights.h light_volume - a cone or pool of light drawn by its alpha) is no
// mesh of the data (no surface for rays, hidden while relit); a glow lamp with no surface of its own takes its head on
// the full-bright fitting over its cards (relit_bake.cpp emitting_vertices, lamp_fixtures.h kLitFittingLeast). Older
// files are not read.
// v25 (round HAZE): a lamp made from a light card lying on a floor (the pool its fitting throws) is that fitting - at
// the mount over the card, a ceiling panel's luminous size, the card's flux (rt_relit.h mount_pool_cards); and a glow
// lamp's fitting takes no triangle larger than the fitting's whole luminous area (lit_fitting_triangles). Training's
// hall lamps were discs of their pools' 170 m^2 hanging 7 m over them, with their heads on the glass boxes' 9 m^2
// undersides: a skater under one cast no shadow. Older files are not read.
inline constexpr uint32_t kRelitDataVersion = 25;
inline constexpr uint32_t kRelitDataOldestRead = 25;
inline constexpr const char* kRelitDataFolder = "thug2-decomp-relit";
// A level's data matches it when its meshes holding at least this share of the world's vertices are found in it (the
// rest - a script's mesh made later - keep their bake); under it the data is out of date.
inline constexpr float kRelitDataLeastShare = 0.95F;

enum class RelitVerdict : uint8_t {
    kRelit = 0,        // the level is relit from this data
    kRelitSunless = 1, // ... its bake shows no sun (night, indoors): lit by its sky, bounce and lamps
    kNoModel = 2,      // the bake couldn't be taken apart: the level keeps the original game's lighting
};
// Whether a verdict relights the level.
[[nodiscard]] constexpr bool relit_verdict_relights(RelitVerdict verdict)
{
    return verdict == RelitVerdict::kRelit || verdict == RelitVerdict::kRelitSunless;
}
[[nodiscard]] const char* relit_verdict_name(RelitVerdict verdict);

// (lighting round A2 - a path tracer's emitters) An emissive triangle of the level (rt_lights.h: a glow's or a self-lit
// one) with the radiance its lamp's solved intensity gives it (a Lambertian emitter: the intensity over pi, in the global
// unit) and its lamp in the list (kNoEmitter: none - its light dropped as dark).
struct RelitEmissive {
    std::array<Vec3, 3> corners{};
    Rgbf radiance{};
    uint32_t lamp = kNoEmitter;
};

// (lighting round A3) A vertex of a lamp's own surface (a confirmed self-lit or textured emitter's triangle): it shows
// the lamp's radiance (its solved intensity / pi, x how much the lamp is on) - the bulb, the lit panel.
struct RelitEmitting {
    uint32_t vertex = 0;
    uint32_t lamp = 0;
};

struct RelitDataMesh {
    uint64_t content = 0;
    // (v18 - round LV) the mesh as the level's file gives it (relit_mesh_original) and how many of `vertices` are the
    // file's own (the first; the rest the bake's geometry passes made): the key a mesh made with other geometry settings
    // finds it by (0: not known - a v17 file)
    uint64_t original = 0;
    uint32_t file_vertices = 0;
    RelitVertices vertices; // (normal and used not stored: made again from the mesh)
    RelitMeshLinks links;   // (round A3) each vertex's lamps (empty: none) - L made again with the lamps on now
    std::vector<RelitEmitting> emitting; // (round A3)
};

struct RelitLevelData {
    uint32_t scene = 0;   // the level's main scene's checksum (the file's name)
    uint64_t content = 0; // relit_level_content of its meshes
    RelitVerdict verdict = RelitVerdict::kNoModel;
    BakedSun sun;         // (its scene number left 0: the game numbers the fits it publishes)
    RelitModel model;
    float facing = 1.0F;  // the world's winding (relit_facing)
    float sun_radius = kRelitSunRadiusDegrees; // (round S) the sun's angular radius (degrees) the bake traced
    // how the data was made (reported)
    uint32_t sky_rays = 0;
    uint32_t sun_rays = 0;
    uint32_t lamps = 0;   // the lamps the L term was traced from (lamp_list's size)
    std::vector<RelitLamp> lamp_list;
    std::vector<RelitEmissive> emissive; // (round A2)
    // the verdict's margins: the baked sun's confidence over rt_sun.h kMinConfidence, the model's sun (luma) over the
    // least that counts as sunlight - both at least kRelitVerdictMargin to be relit
    float sun_margin = 0.0F;
    float model_margin = 0.0F;
    std::vector<RelitDataMesh> meshes; // by content, ascending
};
// A verdict's margin: the measure at least this times its threshold (a level near the edge is decided once, here, and
// kept - not again at each load).
inline constexpr float kRelitVerdictMargin = 1.0F;

// The level's content key from its meshes' hashes (in any order).
[[nodiscard]] uint64_t relit_level_content(std::span<const uint64_t> meshes);
// The file's name for a level's main scene (no folder).
[[nodiscard]] std::string relit_data_name(uint32_t scene);
// The file's bytes.
[[nodiscard]] std::string write_relit_data(const RelitLevelData& data);
// The file read; false (with `why`) when it isn't one of this version.
bool read_relit_data(std::string_view bytes, RelitLevelData* out, std::string* why);
// (added by the time of day's clock - engine/gfx/day_clock.h) The level's own facts from the start of its file alone -
// its scene, verdict and baked sun's direction: what the simulation's clock needs when the level arrives (the level's
// own day, and whether the data is there for the time of day), read without the rest (the vertices' parts are the
// renderer's). kRelitHeadBytes is enough of the file for it. `stale`: a relit data file of another version, or made with
// another sun's size than kRelitSunRadiusDegrees (made by an older relit bake - out of date).
struct RelitLevelHead {
    uint32_t scene = 0;
    bool stale = false;
    RelitVerdict verdict = RelitVerdict::kNoModel;
    bool sun_found = false;
    std::array<float, 3> toward{}; // toward the baked sun, normalised
};
inline constexpr std::size_t kRelitHeadBytes = 128;
bool read_relit_head(std::string_view bytes, RelitLevelHead* out);
// Whether the bytes are a relit data file of another version than this game's or made with another sun's size (out of
// date: run relit_bake again).
[[nodiscard]] bool relit_data_stale(std::string_view bytes);
// A mesh's content hash (what the data keys it by): its positions, its triangles (RelitMesh's, as the RayTracing numbers
// them) and its made vertices' sources. Not its colours: the game writes them (scene lights at a change of the time of
// day, colour animations) - a mesh found at night is the mesh the bake took apart by day.
[[nodiscard]] uint64_t relit_mesh_content(const RelitMesh& mesh);
// (round LV) A mesh's original key: the mesh as the level's file gives it, whatever the load's geometry passes made of
// it - its file's vertices' positions (RelitMesh::file_vertices) and the file's triangles (RelitMesh::baked_triangles
// where the passes changed them, else its own).
[[nodiscard]] uint64_t relit_mesh_original(const RelitMesh& mesh);
// (round LV) The data of a mesh found by its original key (made with other geometry settings) for the mesh as the
// game made it: its file's vertices take the data's own (the same vertices, first in both), each vertex made after
// them its sources' blended - the stored parts, the lamp links by weight, a lamp's surface where all its sources are
// that lamp's. False (nothing written) when the data doesn't hold the file's vertices or a made vertex has no sources.
bool relit_data_for_geometry(const RelitMesh& mesh, const RelitDataMesh& data, RelitDataMesh* out);

// The half floats the vertices are kept in (round to nearest even; out of range to the largest).
[[nodiscard]] uint16_t to_half(float value);
[[nodiscard]] float from_half(uint16_t half);

} // namespace render::rt
