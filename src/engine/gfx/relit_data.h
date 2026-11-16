#pragma once

// NOT ORIGINAL - a level's relit data (docs/design/vulkan.md "Relit: the bake taken apart, offline"): the decomposition
// of the level's baked vertex lighting that the relit mode lights the world from, made offline by the relit bake
// (render_pc/relit_bake.h - `thug2-decomp.exe -relit-bake <level>`, driven for every level by tools/game/relit_bake.ts)
// and only read by the game: nothing of it is traced or fitted while the game runs.
//
// One file a level, thug2-decomp-relit\<the level's main scene checksum, 8 hex>.relit beside the exe:
//   - the level's verdict: relit (its bake shows a sun and the light model was found), no sun (rt_sun.h's fit found
//     none: indoors - the level keeps the original game's lighting), no model (the fit found no sunlight in the bake);
//   - the baked sun (rt_scene.h BakedSun: its direction, confidence, the agreement grid and the ground height) - every
//     effect's level sun comes from here;
//   - the light model (rt_relit.h RelitModel: the sun's a, the sky's b, the bounce's g, the lamps' c, the level's
//     scale) and the margins its verdict was decided with;
//   - each world mesh by the hash of its content (relit_mesh_content: its positions, its triangles and building
//     relief's made vertices' sources - not its colours: the level's scene lights and colour animations rewrite those
//     while the game runs, the time of day's night lamps with them), its vertices' parts: the sun's share toward the bake's sun, the
//     sky's share K, the bounce B and its local part, the tint, the residual, the lamps' light L, the bake's sun the
//     rays missed. The normals and which vertices are used are made again from the mesh (relit_normals, with the
//     world's winding kept here).
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
inline constexpr uint32_t kRelitDataVersion = 2; // (2: the meshes keyed without their colours)
inline constexpr const char* kRelitDataFolder = "thug2-decomp-relit";
// A level's data matches it when its meshes holding at least this share of the world's vertices are found in it (the
// rest - a script's mesh made later - keep their bake); under it the data is out of date.
inline constexpr float kRelitDataLeastShare = 0.95F;

enum class RelitVerdict : uint8_t {
    kRelit = 0,   // the level is relit from this data
    kNoSun = 1,   // the bake shows no sun: the level keeps the original game's lighting
    kNoModel = 2, // the fit found no sunlight in the bake: likewise
};
[[nodiscard]] const char* relit_verdict_name(RelitVerdict verdict);

struct RelitDataMesh {
    uint64_t content = 0;
    RelitVertices vertices; // (normal and used not stored: made again from the mesh)
};

struct RelitLevelData {
    uint32_t scene = 0;   // the level's main scene's checksum (the file's name)
    uint64_t content = 0; // relit_level_content of its meshes
    RelitVerdict verdict = RelitVerdict::kNoSun;
    BakedSun sun;         // (its scene number left 0: the game numbers the fits it publishes)
    RelitModel model;
    float facing = 1.0F;  // the world's winding (relit_facing)
    // how the data was made (reported)
    uint32_t sky_rays = 0;
    uint32_t sun_rays = 0;
    uint32_t lamps = 0;   // the lamps the L term was traced from
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
// renderer's). kRelitHeadBytes is enough of the file for it.
struct RelitLevelHead {
    uint32_t scene = 0;
    RelitVerdict verdict = RelitVerdict::kNoSun;
    bool sun_found = false;
    std::array<float, 3> toward{}; // toward the baked sun, normalised
};
inline constexpr std::size_t kRelitHeadBytes = 128;
bool read_relit_head(std::string_view bytes, RelitLevelHead* out);
// A mesh's content hash (what the data keys it by): its positions, its triangles (RelitMesh's, as the RayTracing numbers
// them) and its made vertices' sources. Not its colours: the game writes them (scene lights at a change of the time of
// day, colour animations) - a mesh found at night is the mesh the bake took apart by day.
[[nodiscard]] uint64_t relit_mesh_content(const RelitMesh& mesh);

// The half floats the vertices are kept in (round to nearest even; out of range to the largest).
[[nodiscard]] uint16_t to_half(float value);
[[nodiscard]] float from_half(uint16_t half);

} // namespace render::rt
