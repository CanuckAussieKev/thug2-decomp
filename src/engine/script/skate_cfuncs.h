#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) of the skate game, 0x0059EDF0-0x0059F610: controller debounce, fake lights,
// nodes, the viewer, spawning and killing scripts, the loading screen, Omnigon, the level name, DumpScripts.
// All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

// The controller's input state (the object at the controller's +0x18): one byte per button, non-zero = held.
struct PadInputSample {
    uint8_t unknown0[4];
    uint8_t right;    // +0x04
    uint8_t left;     // +0x05
    uint8_t up;       // +0x06
    uint8_t down;     // +0x07
    uint8_t triangle; // +0x08
    uint8_t circle;   // +0x09
    uint8_t x;        // +0x0A
    uint8_t square;   // +0x0B
    uint8_t l1;       // +0x0C
    uint8_t r1;       // +0x0D
    uint8_t l2;       // +0x0E
    uint8_t r2;       // +0x0F
    uint8_t unknown10[6];
    uint8_t black; // +0x16
    uint8_t white; // +0x17
    uint8_t z;     // +0x18
};

// The per-button debounce times (0x007CE758, 22 entries, indexed like the button table 0x0067D6D8): a button
// doesn't count as pressed until the game clock (ms) reaches its time.
uint32_t* debounce_times();

// 0x0059EE80: whether `button` (a checksum: up, down, left, right, the diagonals, x, triangle, square,
// circle, l1, l2, r1, r2, black, white, z) is held on `controller` (its input state at +0x18) and isn't being
// debounced. A diagonal is held when both its directions are. Unknown buttons: false.
bool controller_pressed(const void* controller, uint32_t button);

bool cfunc_controller_debounce(script::Struct* params, script::Script* script);          // 0x0059EDF0
bool cfunc_fake_lights(script::Struct* params, script::Script* script);                  // 0x0059F0F0
bool cfunc_update_fx_lighting(script::Struct* params, script::Script* script);           // 0x0059F1B0
bool cfunc_node_exists(script::Struct* params, script::Script* script);                  // 0x0059F1C0
bool cfunc_load_node_array(script::Struct* params, script::Script* script);              // 0x0059F1F0
bool cfunc_reset_camera(script::Struct* params, script::Script* script);                 // 0x0059F260
bool cfunc_set_movement_velocity(script::Struct* params, script::Script* script);        // 0x0059F290
bool cfunc_set_rotate_velocity(script::Struct* params, script::Script* script);          // 0x0059F2C0
bool cfunc_clear_viewer_object(script::Struct* params, script::Script* script);          // 0x0059F2F0
bool cfunc_cas_artist(script::Struct* params, script::Script* script);                   // 0x0059F310
bool cfunc_spawn_script(script::Struct* params, script::Script* script);                 // 0x0059F330
bool cfunc_un_block_interrupted_script(script::Struct* params, script::Script* script);  // 0x0059F440
bool cfunc_kill_spawned_script(script::Struct* params, script::Script* script);          // 0x0059F450
bool cfunc_pause_spawned_scripts(script::Struct* params, script::Script* script);        // 0x0059F4C0
bool cfunc_un_pause_spawned_scripts(script::Struct* params, script::Script* script);     // 0x0059F4D0
bool cfunc_display_loading_screen(script::Struct* params, script::Script* script);       // 0x0059F4E0
bool cfunc_hide_loading_screen(script::Struct* params, script::Script* script);          // 0x0059F550
bool cfunc_last_broadcasted_omnigon_was(script::Struct* params, script::Script* script); // 0x0059F560
bool cfunc_omnigon_allowed(script::Struct* params, script::Script* script);              // 0x0059F590
bool cfunc_set_level_name(script::Struct* params, script::Script* script);               // 0x0059F5B0
bool cfunc_dump_scripts(script::Struct* params, script::Script* script);                 // 0x0059F5E0

extern char g_node_list_name[128];

} // namespace commands
