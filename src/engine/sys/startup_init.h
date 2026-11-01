#pragma once

// The original's start-up initialisers as source. Before WinMain the original's C runtime start-up
// (_cinit) runs two tables of initialisers: the C ones (only the C runtime's own: its atexit table, clock(), SSE2
// detection, stdio, the multibyte tables, the C++ unhandled-exception filter) and the C++ ones - after the security
// cookie, 77 game initialisers that construct globals (hash tables, list heads, lights, textures, the expression
// evaluator...), fill the script member-function tables' code pointers and create two mutexes. Here every global starts
// with its start value, before any initialiser, so run_startup_initialisers does each one's work in the original's order.
//
// The pieces are defined next to the globals they make (the owners' files), each named after what it does, with the
// original initialiser's address.
namespace sys {

// Every effect of the original's start-up initialisers, in their order (the table at 0x0067C000):
// called first thing in WinMain (by then the compiler's own dynamic initialisers have all run, as the original's CRT had
// run its own). Once: it constructs, it doesn't reset.
void run_startup_initialisers();

} // namespace sys

namespace render {
void construct_shared_orientation();      // 0x00640860
void construct_movables();                // 0x00640880
void construct_scene_list();              // 0x00640B00
void construct_font_table();              // 0x00640B40
void set_world_lights_ambient();          // 0x00640B60
void construct_world_light_stack();       // 0x00640B90
void construct_scene_lights();            // 0x00640BC0
void construct_loaded_tex_dicts();        // 0x00640BF0
void set_up_game_texture_dict();          // 0x00640C10
void set_up_particle_texture_dict();      // 0x00640C60
void construct_spare_tex_dict_table();    // 0x00640CB0
void construct_shadow_edge_table();       // 0x00640CD0
void create_screen_mutex();               // 0x00640D10
void construct_projection_batches();      // 0x00640D30
void construct_grass_textures();          // 0x00640D90
void construct_grass_texture_animation(); // 0x00640DC0
void construct_shader_cache();            // 0x00640DF0
void construct_glow_table();              // 0x00640E10
} // namespace render

namespace objects {
void construct_rigid_body_cache();      // 0x00640900
void construct_spare_object_list();     // 0x006409F0
void construct_instance_list();         // 0x00640A10
void construct_cutscene_rotations();    // 0x00640A30
void fill_proximity_member_functions(); // 0x00644910
} // namespace objects

namespace surface_kind {
void construct_terrain_types();   // 0x00640950
void construct_terrain_actions(); // 0x00640970
} // namespace surface_kind

namespace audio {
void create_stream_lock(); // 0x006409D0
} // namespace audio

namespace script {
void construct_evaluator(); // 0x00640A70
} // namespace script

namespace gfx {
void construct_spare_rotations();   // 0x00640AA0
void construct_bone_volume_table(); // 0x00640AE0
} // namespace gfx

namespace commands {
void construct_cutscene_destroy_list(); // 0x00644A00
} // namespace commands
