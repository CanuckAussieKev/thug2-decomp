#pragma once

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) preloading what the level's nodes and the pedestrians use (0x005AAFA0-0x005ABBEF):
// the vehicles' and game objects' models (with their collision and the vehicles' scripts), the pedestrians' models
// and CAS parts, the random pedestrians' part choices, the cars' textures replaced. All are cdecl (params, script)
// and return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_preload_models(script::Struct* params, script::Script* script);               // 0x005AAFA0
bool __cdecl cfunc_preload_pedestrians(script::Struct* params, script::Script* script);          // 0x005AB300
bool __cdecl cfunc_preselect_random_pedestrians(script::Struct* params, script::Script* script); // 0x005AB690
bool __cdecl cfunc_replace_car_textures(script::Struct* params, script::Script* script);         // 0x005ABB20

} // namespace commands
