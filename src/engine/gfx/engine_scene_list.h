#pragma once

#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace maths {
struct Seg;
}
namespace script {
class Script;
class Struct;
} // namespace script

// The engine's loaded scenes (render::CEngine's statics, cdecl): the list of them (lists::Foremost<WorldScene> at 0x006FA7E0 - its
// first node at its next; nodes memory::ListNode {the scene, priority, next, prev}, the head's data -1), the main
// scene and the sky told apart by the scene's is_sky_, scenes loaded from a file or from memory and registered,
// reloaded, unloaded, given a scene added over them; and what's asked of all of them - the collision grids a line
// reaches, a sector name no scene uses, their statistics.
namespace render {

class CollGrid;
struct WorldScene;
class TextureSet;

// The list's head (lists::Foremost<WorldScene>; the node's destructor body is 0x0048C4B0).
struct SceneListHead : memory::ListNode {
    // 0x0048CC30: construct - data -1, priority 0, linked to itself. Returns this.
    SceneListHead* make_new();
    // (0x0048CC50, its scalar deleting destructor, isn't called: the head is a global.)
};

// 0x0048C7F0: the first node of the list (null when it's empty).
memory::ListNode* __cdecl first_scene_node();
// 0x0048C750: the scene named `name` (its checksum looked up, 0x0048C700), or null.
WorldScene* __cdecl get_scene_by_name(const char* name);
// 0x0048C770 / 0x0048C7B0: the first scene that isn't a sky / that is one (null if none).
WorldScene* __cdecl get_main_scene();
WorldScene* __cdecl get_sky_scene();
// 0x0048C810: every scene unloaded with its collision (0x0048C690), then its texture dictionary; the debug lines
// cleared. True.
bool __cdecl unload_all_scenes();
// 0x0048C870: a texture's shadow caster added for `model` (0x004B2C30) - in `scene`, or (null) the main scene.
void __cdecl add_shadow_caster(const uint8_t* texture, void* model, WorldScene* scene);
// 0x0048C8F0: the static collision objects of every scene but the skies listed (timed: the time is read before and
// after, and not used).
void __cdecl print_coll_statics();
// 0x0048C940: the first scene's collision grid that there is (null if none has one).
CollGrid* __cdecl get_first_coll_grid();
// 0x0048C980: the collision grids whose boxes the line's box touches (NaN doesn't rule one out), at most 15, their
// scenes alongside (0x006FA738): a null-terminated list (0x006FA778), or null if none.
CollGrid** __cdecl get_coll_grids_on_line(const maths::Seg* line);
// 0x0048CA80: a sector name (0x0067DC84, counted on) that no scene has a sector of.
uint32_t __cdecl new_sector_name();
// 0x0048CAF0: the ToggleAddScenes script command - every scene's added scene shown or hidden (0x0048C650). True.
bool __cdecl cfunc_toggle_add_scenes(script::Struct* params, script::Script* script);
// 0x0048CB00: the main scene's and the sky's statistics (WorldScene::fetch_diagnostics) each into a new structure, added to
// `info` (0x9B10525C, 0x02C5EA34).
void __cdecl get_scene_debug_info(script::Struct* info);
// 0x0048CC80: a new, empty scene (the platform's) registered under `name` with `textures` (0x0048C560, already
// loaded).
WorldScene* __cdecl build_world_scene(const char* name, TextureSet* textures, bool with_super_sectors);
// 0x0048CCB0: a scene loaded from "levels\<name>\<name>.scn.<ext>" (with a vertex arena of its own when 0x00786AB6
// is set) and registered (0x0048C560 - `unused` passed on, and not used there).
WorldScene* __cdecl load_scene(const char* name, TextureSet* textures, bool with_super_sectors, bool is_sky,
                               bool mesh_flag, int unused);
// 0x0048CD80: a scene loaded from geometry in memory and registered under `name` (the same way).
WorldScene* __cdecl load_named_scene_from_memory(const char* name, const uint8_t* data, TextureSet* textures,
                                                 bool with_super_sectors, bool is_sky, bool mesh_flag, int unused);
// 0x0048CDD0: the scene "levels\<add>\<add>.scn.<ext>" added over the scene `scene_name` (that scene's added scene
// let go of first, with its texture dictionary, if it's this one), with its texture dictionary ("levels\<add>\
// <add>.tex"). The scene.
WorldScene* __cdecl add_scene(const char* scene_name, const char* add_name);
// 0x0048CEF0: the scene `name` unloaded and loaded again (its textures, sky or not kept). Whether there was one.
bool __cdecl reload_scene(const char* name);

extern WorldScene* g_collision_scenes[16];

} // namespace render
