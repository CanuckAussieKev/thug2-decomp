#pragma once

#include <cstdint>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/pad_input/input_hub.h"

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) of the skate game, 0x0059EDF0-0x0059F610: controller debounce, fake lights,
// nodes, the viewer, spawning and killing scripts, the loading screen, Omnigon, the level name, DumpScripts.
// All are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

// The controller's input state (its data this frame): one byte per button, non-zero = held.
struct PadInputSample {
    const pad_input::Payload* data;
    [[nodiscard]] uint8_t right() const { return core::at(data->incident_, 0x04); }
    [[nodiscard]] uint8_t left() const { return core::at(data->incident_, 0x05); }
    [[nodiscard]] uint8_t up() const { return core::at(data->incident_, 0x06); }
    [[nodiscard]] uint8_t down() const { return core::at(data->incident_, 0x07); }
    [[nodiscard]] uint8_t triangle() const { return core::at(data->incident_, 0x08); }
    [[nodiscard]] uint8_t circle() const { return core::at(data->incident_, 0x09); }
    [[nodiscard]] uint8_t x() const { return core::at(data->incident_, 0x0A); }
    [[nodiscard]] uint8_t square() const { return core::at(data->incident_, 0x0B); }
    [[nodiscard]] uint8_t l1() const { return core::at(data->incident_, 0x0C); }
    [[nodiscard]] uint8_t r1() const { return core::at(data->incident_, 0x0D); }
    [[nodiscard]] uint8_t l2() const { return core::at(data->incident_, 0x0E); }
    [[nodiscard]] uint8_t r2() const { return core::at(data->incident_, 0x0F); }
    [[nodiscard]] uint8_t black() const { return core::at(data->incident_, 0x16); }
    [[nodiscard]] uint8_t white() const { return core::at(data->incident_, 0x17); }
    [[nodiscard]] uint8_t z() const { return core::at(data->incident_, 0x18); }
};

// The per-button debounce times (0x007CE758, 22 entries, indexed like the button table 0x0067D6D8): a button
// doesn't count as pressed until the game clock (ms) reaches its time.
std::array<uint32_t, 22>& debounce_times();

// 0x0059EE80: whether `button` (a checksum: up, down, left, right, the diagonals, x, triangle, square,
// circle, l1, l2, r1, r2, black, white, z) is held on `controller` (its input state at +0x18) and isn't being
// debounced. A diagonal is held when both its directions are. Unknown buttons: false.
bool controller_pressed(const pad_input::InputHandler* controller, uint32_t button);

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

extern std::array<char, 128> g_node_list_name;

} // namespace commands
