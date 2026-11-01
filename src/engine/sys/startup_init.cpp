// The original's start-up initialisers, in their order (see startup_init.h). The table of the C++ initialisers
// (0x0067C000 - 0x0067C13C, run by _cinit 0x006249D1 before WinMain) entry by entry.
//
// Not reproduced, and why:
// - The C initialisers (0x0067C144 - 0x0067C160) and entry 1 (0x00628777, the security cookie): the C runtime's own,
//   which the C runtime we link does for itself.
// - Initialisers whose work is zeros over zero storage (.bss): the smart pointers 0x006408D0 / 0x00640920, the script
//   components 0x00640990 / 0x00640A60 / 0x00640A90, the stream frame slot 0x006409A0, the font slots' +0x18 words
//   0x00640B20, the file tables 0x00644A20. And 0x00640CF0 assigns "" to the loading-screen picture's std::string,
//   whose image bytes already are an empty string (capacity 15).
// - 0x00640AD0 and 0x00640D60 only register exit functions; every initialiser's exit function (atexit: free the
//   tables, close the mutexes...) is left out - the game ends with ExitProcess (sys::fast_exit), which never runs them.
// - 0x00644A30: the original's stdext::hash_map of ended occlusion queries (0x007D69FC, the C++ library's): our
//   platform_d3d.cpp keeps them in a function-local std::unordered_map, constructed on first use.
// - 0x004E1950 (memory::ensure_manager) sits in the slot after the table's end marker (0x0067C140): _cinit never runs it.

#include "engine/sys/startup_init.h"

#include <cstdint>
#include <array>

#include "engine/script/names.h"

namespace sys {

namespace {

// 16 bytes nothing reads, set to a null (virtual-inheritance) member function pointer by 0x006408A0.
std::array<uint32_t, 4> g_null_member_function_67d_6c_0;

// 0x006408A0
void set_null_member_function()
{
    g_null_member_function_67d_6c_0[0] = 0;
    g_null_member_function_67d_6c_0[1] = 0;
    g_null_member_function_67d_6c_0[2] = 0;
    g_null_member_function_67d_6c_0[3] = 0xFFFFFFFFu;
}

} // namespace

void run_startup_initialisers()
{
    render::construct_shared_orientation(); // 0x00640860
    render::construct_movables();           // 0x00640880
    set_null_member_function();             // 0x006408A0
    // 0x006408D0: the proximity skater's smart pointer (zeros)
    objects::construct_rigid_body_cache(); // 0x00640900
    // 0x00640920: the rigid bodies' pusher smart pointers (zeros)
    surface_kind::construct_terrain_types();   // 0x00640950 (the first allocation: sets the memory manager up)
    surface_kind::construct_terrain_actions(); // 0x00640970
    // 0x00640990: FormatText's script component (zeros); 0x006409A0: the stream frame slot (zeros)
    audio::create_stream_lock();             // 0x006409D0
    objects::construct_spare_object_list();  // 0x006409F0
    objects::construct_instance_list();      // 0x00640A10
    objects::construct_cutscene_rotations(); // 0x00640A30
    // 0x00640A60: the evaluator's result component (zeros)
    script::construct_evaluator(); // 0x00640A70
    // 0x00640A90: the parser's scratch component (zeros)
    gfx::construct_spare_rotations(); // 0x00640AA0
    // 0x00640AD0: an exit function only (the hidden models' list)
    gfx::construct_bone_volume_table(); // 0x00640AE0
    render::construct_scene_list();     // 0x00640B00
    // 0x00640B20: the font slots' +0x18 words (zeros)
    render::construct_font_table();           // 0x00640B40
    render::set_world_lights_ambient();       // 0x00640B60
    render::construct_world_light_stack();    // 0x00640B90
    render::construct_scene_lights();         // 0x00640BC0
    render::construct_loaded_tex_dicts();     // 0x00640BF0
    render::set_up_game_texture_dict();       // 0x00640C10
    render::set_up_particle_texture_dict();   // 0x00640C60
    render::construct_spare_tex_dict_table(); // 0x00640CB0
    render::construct_shadow_edge_table();    // 0x00640CD0
    // 0x00640CF0: the loading-screen picture's name = "" (as the image has it)
    render::create_screen_mutex();          // 0x00640D10
    render::construct_projection_batches(); // 0x00640D30
    // 0x00640D60: an exit function only (it does nothing)
    // (0x00640D70, the spare material's constructor: C++ static initialisation - pc_material.cpp)
    render::construct_grass_textures();          // 0x00640D90
    render::construct_grass_texture_animation(); // 0x00640DC0
    render::construct_shader_cache();            // 0x00640DF0
    render::construct_glow_table();              // 0x00640E10
    script::fill_member_function_tables();       // 0x00640E30 - 0x006448C0 (33 tables)
    objects::fill_proximity_member_functions();  // 0x00644910
    commands::construct_cutscene_destroy_list(); // 0x00644A00
    // 0x00644A20: the file tables (zeros); 0x00644A30: the occlusion queries' hash_map (see above)
}

} // namespace sys
