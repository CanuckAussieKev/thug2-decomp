#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) 0x005B2420-0x005B2FC0: the custom skater's face texture (from the profile, from a
// video capture), changing a texture (offset, rotate, push to a point, tint) and converting colours between HSV and
// RGB (the game's ranges: hue 0-360, saturation 0-90, value 0-60, RGB 0-128). All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace commands {

bool __cdecl cfunc_load_face_texture_from_profile(script::Struct* params, script::Script* script);
bool __cdecl cfunc_set_face_texture_from_video_capture(script::Struct* params, script::Script* script);
bool __cdecl cfunc_offset_texture(script::Struct* params, script::Script* script);
bool __cdecl cfunc_rotate_texture(script::Struct* params, script::Script* script);
bool __cdecl cfunc_push_texture_to_point(script::Struct* params, script::Script* script);
bool __cdecl cfunc_apply_rgba_to_texture(script::Struct* params, script::Script* script);
bool __cdecl cfunc_hs_vto_rgb(script::Struct* params, script::Script* script);
bool __cdecl cfunc_rg_bto_hsv(script::Struct* params, script::Script* script);

} // namespace commands
