#pragma once

#include <cstdint>

#include "engine/core/arena_alloc.h"

// The engine's own entry points (render::CEngine's statics, cdecl) at 0x0048BF20-0x0048C74F: starting it, a frame's
// update and render, model and scene files, and the list of loaded scenes. (The ones that only jump into the
// platform are in engine_glue.h.) From the disassembly of 0x0048BF20-0x0048C741.
namespace maths {
class Vec4;
}

namespace render {

struct WorldScene;

// (A loaded scene's node in the engine's list - lists::Elem<WorldScene>, data = the scene; the list's head is at
// 0x006FA7E0, its data -1 - is a memory::ListNode: its destructors 0x0048C4B0 / 0x0048C4D0 are uncouple / destroy.)

namespace engine {

// 0x0048C1D0: the engine started: the text pool (`text_pool_size` entries), the 2D window pool, the screen (4:3, the
// script float camera_fov's angle, no extra field of view), the viewports, screen mode 0, the flexible particle
// manager (0x006FA7C0), the imposter manager (0x006FA7BC), the effects, then the platform (start_engine).
void __cdecl prepare_all(uint32_t text_pool_size);
// 0x0048BF20: the frame finished (FinishRendering), then the cameras marked for deletion deleted.
void __cdecl end_render();
// 0x0048C330: a frame: the flashes, spawned polygons, texture splats, shatters and particles updated, the weather
// (by the slow-motion frame time) and the imposters; 0x004904E0; then the world rendered unless rendering is skipped
// (0x007D1EC0).
void __cdecl render_frame();

// 0x0048C0A0: the object manager the engine's code uses (0x006FA720).
void __cdecl change_entity_hub(void* manager);
// 0x0048C0B0: the scenes the last collision query's grids belong to (0x006FA738).
uint32_t* __cdecl get_collision_scenes();
// 0x0048C0D0: the next render mode (0x006FA734: 0-3 in turn).
void __cdecl toggle_render_mode();
// 0x0048C0F0: the metrics display (0x006FA7D6) switched on / off.
void __cdecl toggle_metrics();
// 0x0048C110: the render's debug ignore values (ignore_1 0x006FA728, ignore_0 0x006FA724).
void __cdecl set_render_ignore(int ignore1, int ignore0);
// 0x0048C1C0: the function the timer runs while waiting for a vblank (0x006FA7D8; null: none).
using IdleFunction = void(__cdecl*)();
IdleFunction __cdecl get_idle_function();

// 0x0048BFE0: `name` copied to `base` without a ".skin" (then a ".mdl") in it. Whether it had ".skin".
bool __cdecl split_skin_name(const char* name, char* base);
// 0x0048C030: a model from geometry in memory (load_mesh_from_memory, under the "model_data_buffer" profile). The last
// three arguments aren't used. `skin_size`: the skin's bytes (not the original's).
void* __cdecl load_model_from_memory(uint32_t checksum, const uint8_t* data, int size, const void* skin,
                                     size_t skin_size, void* textures, int unused5, uint32_t unused6, uint32_t unused7);
// 0x0048C390: a level's scene file name, "levels\<name>\<name>.scn.<platform>". The third argument isn't used.
void __cdecl build_scene_file_path(char* out, const char* name, uint32_t unused);
// 0x0048C3D0: a model's mesh loaded from "<name>.skin.<platform>" (a name with ".skin") or "<name>.mdl.<platform>"
// (lower case). The last three arguments are passed on (the mesh load reads only the name and textures).
void* __cdecl load_model_file(const char* file_name, void* textures, int tex_dict_offset, bool shadow_volume);
// 0x0048C490: sphere_visible - always 0 with more than one viewport (0x00701378).
int __cdecl sphere_visible_in_one_viewport(const maths::Vec4* centre, float radius);

// --- the loaded scenes (the list 0x006FA7E0) ---
// 0x0048C510: the sector `checksum` from the first scene that has it, or null.
void* __cdecl look_up_sector(uint32_t checksum);
// 0x0048C560: `scene` (from the file of the level `name`) named and given `textures`; unless it's `loaded`, its
// load finished (with `arena`); added at the end of the list. (No scene: a crash, as the original.)
void __cdecl add_scene(WorldScene* scene, const char* name, void* textures, bool loaded, void* arena);
// 0x0048C650: every scene's added scenes toggled.
void __cdecl toggle_add_scenes();
// 0x0048C690: `scene` unloaded (its added scene first) and deleted, its node too. False when the platform couldn't
// unload it or it isn't in the list.
bool __cdecl unload_scene(WorldScene* scene, int flags);
// 0x0048C700: the scene named `checksum`, or null.
WorldScene* __cdecl find_scene(uint32_t checksum);

} // namespace engine

extern bool g_suppress_draw; // rendering skipped (frames not drawn or shown)

extern void* g_flexible_particle_manager;
extern void* g_imposter_manager;
extern int g_render_mode;
extern memory::ListNode g_scene_list;
} // namespace render
