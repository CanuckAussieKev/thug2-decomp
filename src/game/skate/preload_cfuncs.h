#pragma once

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) preloading what the level's nodes and the pedestrians use (0x005AAFA0-0x005ABBEF):
// the vehicles' and game objects' models (with their collision and the vehicles' scripts), the pedestrians' models
// and CAS parts, the random pedestrians' part choices, the cars' textures replaced. All are cdecl (params, script)
// and return whether the script's `if` should be taken.
namespace commands {

bool cfunc_preload_models(script::Struct* params, script::Context context);               // 0x005AAFA0
bool cfunc_preload_pedestrians(script::Struct* params, script::Context context);          // 0x005AB300
bool cfunc_preselect_random_pedestrians(script::Struct* params, script::Context context); // 0x005AB690
bool cfunc_replace_car_textures(script::Struct* params, script::Context context);         // 0x005ABB20

} // namespace commands
