#pragma once

// NOT ORIGINAL - the relit bake (docs/design/vulkan.md "Relit: the bake taken apart, offline"): a level's relit data
// (engine/gfx/relit_data.h) made by the game itself, in a batch run - `thug2-decomp.exe -relit-bake <level>` (the
// level's name as -level takes it; tools/game/relit_bake.ts runs it for every level of a game folder). The game starts
// straight in the level, muted, windowed and behind the user's windows (platform/test_workspace.h batch_run), with its
// own settings over the user's ini (overrides: the Vulkan renderer with ray tracing, the level's own time of day, the
// relit mode and the lighting passes off - the vertex buffers hold the bake); the geometry settings (building relief,
// smoothed curves) are the user's, so the data matches the meshes the user's game makes.
//
// Every mesh of the level's world scenes is recorded and traced, shown or hidden (rt_record.cpp: a bake's world is the
// level's static meshes, whatever its scripts show in the first frames - the same data every time). Once the world's
// structures are built, its baked sun fitted (rt_sun.h, over the same meshes) and its emitters found (rt_lights.h),
// and that has held for kSteadyFrames frames:
//   - the meshes read (rt_relit_world::read_world_mesh), one each by content, in the order of their content's hash;
//   - their light traced (kRelitBakeSkyRays sky rays and kRelitBakeSunRays sun rays a vertex - no time limit), the
//     lamps' light (every emitter and point light the lamps pass would shine, with shadow rays);
//   - the model fitted over the whole level (rt_relit.h fit_relit_model, then fit_relit_lamps), the tints and the rest
//     taken apart (decompose_relit);
//   - the verdict - relit, no sun (the baked sun's fit found none), no model (no sunlight found in the bake) - and its
//     margins; the file written beside the exe (thug2-decomp-relit\<scene>.relit), a summary logged ("relit bake:
//     ..."), and the game closed.
// Nothing here runs in a normal game.

#include <cstdint>
#include <span>
#include <string>

#include "engine/config/settings_model.h"
#include "engine/gfx/rt_scene.h"
#include "render_pc/rt_relit_world.h"

namespace render::relit_bake {

inline constexpr uint32_t kSteadyFrames = 90; // the world ready this many frames in a row first (its scripts settle)
// Only the level asked for is baked: its load script running (the skate module's level - the front end's main menu is a
// level of its own, load_mainmenu, its world up long before the level's) and its world the scene its level structure
// names (level=). Not so within this long of the first frame, the bake is given up - nothing written, the process's
// exit code platform::kBatchNoLevel or kBatchOtherScene (platform/test_workspace.h; tools/game/relit_bake.ts reports it).
inline constexpr double kLevelWaitSeconds = 300.0;

// Whether this run is a relit bake (the command line's -relit-bake), and the level it names.
[[nodiscard]] bool requested();
[[nodiscard]] std::string level_name();
// The settings the bake runs with over the user's ini (config::set_overrides, at start-up).
[[nodiscard]] config::IniValues overrides();
// Each frame of a bake (rt_record.cpp, after a new world's work and the emitters'): the world's meshes recorded this
// frame, whether its structures are settled and its sun fitted for them, the loaded scenes' key.
void step(std::span<const rt_relit_world::WorldMesh> world, bool ready, uint64_t scene_key, rt::RayTracing& tracer);

} // namespace render::relit_bake
