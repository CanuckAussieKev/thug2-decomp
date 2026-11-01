#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands of the engine, 0x00445000-0x00445B30 (LoadScene, 0x00445130, is in gfx/scene.cpp): terrain sounds,
// models, scenes and their collision, animations and other assets, the system configuration and the sun's lens
// flare (cfuncs_scene.cpp). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

// 0x00445000: LoadTerrainSounds terrain=<checksum>: the terrain's sounds loaded (0x00444EB0). False without a terrain.
bool cfunc_load_terrain_sounds(script::Struct* params, script::Script* script);
// 0x00445030: PreloadModel name="<file>" [AllowReplaceTex]: the model <file>.mdl loaded into a model made for the
// purpose, which is destroyed again (its assets stay loaded). Always true.
bool cfunc_preload_model(script::Struct* params, script::Script* script);
// 0x00445120: UnloadAllLevelGeometry: every scene unloaded (0x0048C810). Always true.
bool cfunc_unload_all_level_geometry(script::Struct* params, script::Script* script);
// 0x00445320: LoadCollision scene="<name>" [is_net]: the scene's collision loaded. Always true.
bool cfunc_load_collision(script::Struct* params, script::Script* script);
// 0x00445380: AddCollision scene="<name>" add="<name>": collision added to the scene. Always true.
bool cfunc_add_collision(script::Struct* params, script::Script* script);
// 0x004453F0: QuickReload scene="<name>": the scene reloaded (0x0048CEF0). Always true.
bool cfunc_quick_reload(script::Struct* params, script::Script* script);
// 0x00445430: UnloadAnim name="<file>" [descChecksum] [refChecksum]: the animation asset unloaded - by its checksum
// (descChecksum plus refChecksum, or the asset manager's reference checksum), or the name's. Always true.
bool cfunc_unload_anim(script::Struct* params, script::Script* script);
// 0x004454E0: LoadAsset name="<file>" [parent] [async] [desc]: an asset loaded (or found loaded). Always true.
bool cfunc_load_asset(script::Struct* params, script::Script* script);
// 0x004455A0: LoadAnim name="<file>" descChecksum [async] [use_pip]: an animation loaded, its checksum offset by the
// asset manager's reference checksum. Returns the asset manager's result (false: already there or failed).
bool cfunc_load_anim(script::Struct* params, script::Script* script);
// 0x00445640: LoadSkeleton name="<file>" <checksum>: a skeleton asset loaded under the checksum. Always true.
bool cfunc_load_skeleton(script::Struct* params, script::Script* script);
// 0x004456A0: SetReferenceChecksum <checksum | integer>: the asset manager's reference checksum (what LoadAnim and
// UnloadAnim offset animations by). Always true.
bool cfunc_set_reference_checksum(script::Struct* params, script::Script* script);
// 0x004456F0: SetDefaultPermanent <integer>: whether assets load permanent by default. Always true.
bool cfunc_set_default_permanent(script::Struct* params, script::Script* script);
// 0x00445720: AddScene scene="<name>" add="<name>": geometry added to a scene (0x0048CDD0), on the debug heap.
// Always true.
bool cfunc_add_scene(script::Struct* params, script::Script* script);
// 0x004457B0: UnloadScene scene="<name>": the scene unloaded, then its texture dictionary. A name that isn't loaded
// unloads the first scene in the engine's table instead (the original's) and returns false; true if it was found.
bool cfunc_unload_scene(script::Struct* params, script::Script* script);
// 0x00445820: SetConfig [language=<name>] [CD | NotCD] [GotExtraMemory | NoExtraMemory] [Pal | NTSC]
// [framerate=<n>]: the system configuration's settings. Always true.
bool cfunc_set_config(script::Struct* params, script::Script* script);
// 0x00445980: PrintConfig: the system configuration to the debug output. Always true.
bool cfunc_print_config(script::Struct* params, script::Script* script);
// 0x00445A30: Bootstrap: the configuration's bootstrap flag (0x007CEFAD - it also picks the front end region's size and
// the data folder).
bool cfunc_bootstrap(script::Struct* params, script::Script* script);
// 0x00445A40: InitSun theta phi radius size texture corona_texture lens_flare_data: the sun and its lens flare set
// up (0x004429D0). Always true.
bool cfunc_init_sun(script::Struct* params, script::Script* script);
// 0x00445B30: UpdateLensFlare: the lens flare's elements placed again (0x00442D70 with the same arguments). Always
// true.
bool cfunc_update_lens_flare(script::Struct* params, script::Script* script);

extern bool g_got_extra_memory;

extern int g_config_ntsc;
} // namespace commands
