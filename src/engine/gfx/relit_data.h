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
// The level's content key is the meshes' hashes, sorted: a level whose meshes changed (another overlay, building relief
// or smoothed curves turned on or off) no longer matches its data - the game then runs the level's original lighting
// and says the data is out of date.
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
// relit fit, the lamps' intensities solved with their shapes and photometry, the emissive triangles. A file of another
// version is out of date: the game says to run relit_bake.)
inline constexpr uint32_t kRelitDataVersion = 4;
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

struct RelitDataMesh {
    uint64_t content = 0;
    RelitVertices vertices; // (normal and used not stored: made again from the mesh)
};

struct RelitLevelData {
    uint32_t scene = 0;   // the level's main scene's checksum (the file's name)
    uint64_t content = 0; // relit_level_content of its meshes
    RelitVerdict verdict = RelitVerdict::kNoModel;
    BakedSun sun;         // (its scene number left 0: the game numbers the fits it publishes)
    RelitModel model;
    float facing = 1.0F;  // the world's winding (relit_facing)
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
// renderer's). kRelitHeadBytes is enough of the file for it. `stale`: a relit data file of another version (made by an
// older relit bake - out of date).
struct RelitLevelHead {
    uint32_t scene = 0;
    bool stale = false;
    RelitVerdict verdict = RelitVerdict::kNoModel;
    bool sun_found = false;
    std::array<float, 3> toward{}; // toward the baked sun, normalised
};
inline constexpr std::size_t kRelitHeadBytes = 128;
bool read_relit_head(std::string_view bytes, RelitLevelHead* out);
// Whether the bytes are a relit data file of another version than this game's (out of date: run relit_bake again).
[[nodiscard]] bool relit_data_stale(std::string_view bytes);
// A mesh's content hash (what the data keys it by): its positions, its triangles (RelitMesh's, as the RayTracing numbers
// them) and its made vertices' sources. Not its colours: the game writes them (scene lights at a change of the time of
// day, colour animations) - a mesh found at night is the mesh the bake took apart by day.
[[nodiscard]] uint64_t relit_mesh_content(const RelitMesh& mesh);

// The half floats the vertices are kept in (round to nearest even; out of range to the largest).
[[nodiscard]] uint16_t to_half(float value);
[[nodiscard]] float from_half(uint16_t half);

} // namespace render::rt
