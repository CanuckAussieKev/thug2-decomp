#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) 0x005B3A90-0x005B4260: create-a-graphic (the custom tag texture composed from layers
// in a workspace texture, and the workspace synced back from a skater's tag), a 2D position from a vector's length
// and angle, and the custom skater's head bone groups. All are cdecl (params, script) and return whether the script's
// `if` should be taken.
namespace commands {

bool __cdecl cfunc_generate_cag_texture(script::Struct* params, script::Script* script);
bool __cdecl cfunc_sync_cag_workspace(script::Struct* params, script::Script* script);
bool __cdecl cfunc_get_2d_pos_from_2d_vec(script::Struct* params, script::Script* script);
bool __cdecl cfunc_resolve_head_bone_scale(script::Struct* params, script::Script* script);

extern float g_degree_to_radian_ratio; // pi / 180

} // namespace commands
