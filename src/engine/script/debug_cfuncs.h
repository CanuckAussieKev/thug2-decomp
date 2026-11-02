#pragma once

#include <cstdint>
#include <array>

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) for debugging and engine odds and ends, 0x004489B0-0x00449960: the heaps' free
// memory display, a dump of the script value layouts, .qb loading, screenshots, metrics, the language tests,
// proximity cleanup, resetting the engine. All are cdecl (params, script) and return whether the script's `if`
// should be taken.
namespace commands {

bool cfunc_display_free_mem(script::Struct* params, script::Script* script); // 0x004489B0
bool cfunc_test(script::Struct* params, script::Script* script);             // 0x00449110
bool cfunc_load_qb(script::Struct* params, script::Script* script);          // 0x004493B0
bool cfunc_unload_qb(script::Struct* params, script::Script* script);        // 0x00449450
bool cfunc_screen_shot(script::Struct* params, script::Script* script);      // 0x004494A0
bool cfunc_toggle_metrics(script::Struct* params, script::Script* script);   // 0x004494E0
bool cfunc_german(script::Struct* params, script::Script* script);           // 0x004494F0
bool cfunc_french(script::Struct* params, script::Script* script);           // 0x00449500
bool cfunc_spanish(script::Struct* params, script::Script* script);          // 0x00449510
bool cfunc_italian(script::Struct* params, script::Script* script);          // 0x00449520
bool cfunc_english(script::Struct* params, script::Script* script);          // 0x00449530
bool cfunc_proxim_cleanup(script::Struct* params, script::Script* script);   // 0x004498F0
bool cfunc_inside(script::Struct* params, script::Script* script);           // 0x00449950
bool cfunc_reset_engine(script::Struct* params, script::Script* script);     // 0x00449960

// The .qb files loaded with LoadQB's LevelSpecific flag (0x00696900, 15 name checksums; 0 = a free slot).
std::array<uint32_t, 15>& level_specific_qb_files();
constexpr int kLevelSpecificQbFiles = 15;

extern std::array<uint32_t, 15> g_level_qb_files;

} // namespace commands
