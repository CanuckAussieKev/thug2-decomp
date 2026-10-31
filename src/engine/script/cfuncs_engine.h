#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) of the engine, 0x00445B50-0x004489B0: the lens flare and the sun, gamma, fog,
// dynamic lighting, UV wibble and texture animation of world sectors, render modes, the animation system, the light
// manager, the screen, texture splats, clipping distances, scene lights, spawned polygons, vertex colours, metrics
// and the memory manager's commands (cfuncs_lighting.cpp, cfuncs_render.cpp, cfuncs_polys.cpp,
// cfuncs_memory.cpp). All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

// --- cfuncs_lighting.cpp -------------------------------------------------------------------------------------

// 0x00445DD0: the gamma (red, green, blue) written to the registry (Settings\Gamma R, G, B - floats stored with
// type REG_DWORD; nothing if the key can't be created).
void __cdecl write_gamma(float red, float green, float blue);

bool __cdecl cfunc_hide_lens_flare(script::Struct* params, script::Script* script);                     // 0x00445B50
bool __cdecl cfunc_unhide_lens_flare(script::Struct* params, script::Script* script);                   // 0x00445B70
bool __cdecl cfunc_get_terrain_type_param(script::Struct* params, script::Script* script);              // 0x00445B90
bool __cdecl cfunc_set_sun_props(script::Struct* params, script::Script* script);                       // 0x00445C50
bool __cdecl cfunc_set_progressive(script::Struct* params, script::Script* script);                     // 0x00445D50
bool __cdecl cfunc_set_color_buffer_clear(script::Struct* params, script::Script* script);              // 0x00445D90
bool __cdecl cfunc_apply_change_gamma(script::Struct* params, script::Script* script);                  // 0x00445E60
bool __cdecl cfunc_set_dynamic_light_modulation_factor(script::Struct* params, script::Script* script); // 0x00446000
bool __cdecl cfunc_enable_fog(script::Struct* params, script::Script* script);                          // 0x004460B0
bool __cdecl cfunc_disable_fog(script::Struct* params, script::Script* script);                         // 0x004460D0
bool __cdecl cfunc_set_fog_exponent(script::Struct* params, script::Script* script);                    // 0x004460F0
bool __cdecl cfunc_vc_wibble_play_from_keyframe(script::Struct* params, script::Script* script);        // 0x00446130
bool __cdecl cfunc_set_uv_wibble_params(script::Struct* params, script::Script* script);                // 0x004461D0
bool __cdecl cfunc_enable_explicit_uv_wibble(script::Struct* params, script::Script* script);           // 0x004462D0
bool __cdecl cfunc_disable_explicit_uv_wibble(script::Struct* params, script::Script* script);          // 0x00446300
bool __cdecl cfunc_set_uv_wibble_offsets(script::Struct* params, script::Script* script);               // 0x00446330
bool __cdecl cfunc_reset_texture_anim(script::Struct* params, script::Script* script);                  // 0x004463A0
bool __cdecl cfunc_start_texture_anim(script::Struct* params, script::Script* script);                  // 0x004463D0
bool __cdecl cfunc_stop_texture_anim(script::Struct* params, script::Script* script);                   // 0x00446420
bool __cdecl cfunc_toggle_render_mode(script::Struct* params, script::Script* script);                  // 0x00446450

// --- cfuncs_render.cpp ---------------------------------------------------------------------------------------

bool __cdecl cfunc_debug_render_ignore(script::Struct* params, script::Script* script);                 // 0x00446460
bool __cdecl cfunc_init_anim_compress_table(script::Struct* params, script::Script* script);            // 0x004464C0
bool __cdecl cfunc_init_anim_system(script::Struct* params, script::Script* script);                    // 0x00446570
bool __cdecl cfunc_update_anim_cache(script::Struct* params, script::Script* script);                   // 0x00446650
bool __cdecl cfunc_finish_rendering(script::Struct* params, script::Script* script);                    // 0x004466F0
bool __cdecl cfunc_init_light_manager(script::Struct* params, script::Script* script);                  // 0x00446700
bool __cdecl cfunc_uninit_light_manager(script::Struct* params, script::Script* script);                // 0x00446760
bool __cdecl cfunc_push_world_lights(script::Struct* params, script::Script* script);                   // 0x00446770
bool __cdecl cfunc_pop_world_lights(script::Struct* params, script::Script* script);                    // 0x00446780
bool __cdecl cfunc_set_screen(script::Struct* params, script::Script* script);                          // 0x00446790
bool __cdecl cfunc_allow_bone_lod(script::Struct* params, script::Script* script);                      // 0x00446810
bool __cdecl cfunc_debug_line(script::Struct* params, script::Script* script);                          // 0x00446850
bool __cdecl cfunc_get_pause_pos_by_resolution(script::Struct* params, script::Script* script);         // 0x004468E0
bool __cdecl cfunc_kill_all_texture_splats(script::Struct* params, script::Script* script);             // 0x00446930
bool __cdecl cfunc_get_gamma_values(script::Struct* params, script::Script* script);                    // 0x00446C20
bool __cdecl cfunc_get_dynamic_light_modulation_factor(script::Struct* params, script::Script* script); // 0x00446CD0
bool __cdecl cfunc_set_clipping_distances(script::Struct* params, script::Script* script);              // 0x00446D70
bool __cdecl cfunc_set_fog_distance(script::Struct* params, script::Script* script);                    // 0x00446DF0
bool __cdecl cfunc_set_fog_color(script::Struct* params, script::Script* script);                       // 0x00446E50
bool __cdecl cfunc_set_render_mode(script::Struct* params, script::Script* script);                     // 0x00446F40
bool __cdecl cfunc_set_wireframe_mode(script::Struct* params, script::Script* script);                  // 0x00446F70
bool __cdecl cfunc_set_light_ambient_color(script::Struct* params, script::Script* script);             // 0x00446FA0
bool __cdecl cfunc_get_light_direction(script::Struct* params, script::Script* script);                 // 0x00447010
bool __cdecl cfunc_set_light_diffuse_color(script::Struct* params, script::Script* script);             // 0x004470A0
bool __cdecl cfunc_get_light_current_color(script::Struct* params, script::Script* script);             // 0x00447130
bool __cdecl cfunc_get_bone_position(script::Struct* params, script::Script* script);                   // 0x00447220

// --- cfuncs_polys.cpp ----------------------------------------------------------------------------------------

// 0x004472D0: a spawned polygon's details given back to their pool pair (0x00700500).
void __cdecl free_spawned_poly_details(void* details);
// 0x00447710: a scene's sector vertex colours (of `lightGroup`, if not 0) moved towards `target` by `factor`.
void __cdecl compress_scene_vertex_colours(uint8_t* scene, float target, float factor, uint32_t light_group);
// 0x00447A60: a scene's sector vertex colours scaled by a pseudo-random amount (by position) of up to `amount`.
void __cdecl nudge_scene_vertex_colours(uint8_t* scene, float amount);

bool __cdecl cfunc_spawn_poly(script::Struct* params, script::Script* script);          // 0x00447330
bool __cdecl cfunc_compress_vc(script::Struct* params, script::Script* script);         // 0x00447980
bool __cdecl cfunc_nudge_vc(script::Struct* params, script::Script* script);            // 0x00447D90
bool __cdecl cfunc_set_light_direction(script::Struct* params, script::Script* script); // 0x00447E00
bool __cdecl cfunc_get_metrics(script::Struct* params, script::Script* script);         // 0x00447F30

// --- cfuncs_memory.cpp ---------------------------------------------------------------------------------------

// 0x00448330 (the first flag in BL: dump_pool_usage_stub): the pools' usage written to the dump file (memory::dump_text)
// and / or printed.
void __cdecl dump_pool_usage(bool to_dump_file, bool print);
void dump_pool_usage_stub();

bool __cdecl cfunc_push_mem_profile(script::Struct* params, script::Script* script);    // 0x00447FF0
bool __cdecl cfunc_pop_mem_profile(script::Struct* params, script::Script* script);     // 0x00448020
bool __cdecl cfunc_set_mem_thread_safe(script::Struct* params, script::Script* script); // 0x00448030
bool __cdecl cfunc_dump_fragments(script::Struct* params, script::Script* script);      // 0x00448060
bool __cdecl cfunc_mem_view_toggle(script::Struct* params, script::Script* script);     // 0x00448090
bool __cdecl cfunc_got_extra_memory(script::Struct* params, script::Script* script);    // 0x004480A0
bool __cdecl cfunc_mem_push_context(script::Struct* params, script::Script* script);    // 0x004480B0
bool __cdecl cfunc_mem_pop_context(script::Struct* params, script::Script* script);     // 0x00448130
bool __cdecl cfunc_mem_init_heap(script::Struct* params, script::Script* script);       // 0x00448140
bool __cdecl cfunc_mem_delete_heap(script::Struct* params, script::Script* script);     // 0x004481A0
bool __cdecl cfunc_mem_heap_exists(script::Struct* params, script::Script* script);     // 0x00448200
bool __cdecl cfunc_analyze_heap(script::Struct* params, script::Script* script);        // 0x00448270
bool __cdecl cfunc_print_mem_info(script::Struct* params, script::Script* script);      // 0x004482C0
bool __cdecl cfunc_dump_heaps(script::Struct* params, script::Script* script);          // 0x00448840

} // namespace commands
