#pragma once

#include <cstdint>

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) 0x005B2420-0x005B2FC0: the custom skater's face texture (from the profile, from a
// video capture), changing a texture (offset, rotate, push to a point, tint) and converting colours between HSV and
// RGB (the game's ranges: hue 0-360, saturation 0-90, value 0-60, RGB 0-128). All are cdecl (params, script) and
// return whether the script's `if` should be taken.
namespace commands {

bool cfunc_load_face_texture_from_profile(script::Struct* params, script::Context context);
bool cfunc_set_face_texture_from_video_capture(script::Struct* params, script::Context context);
bool cfunc_offset_texture(script::Struct* params, script::Context context);
bool cfunc_rotate_texture(script::Struct* params, script::Context context);
bool cfunc_push_texture_to_point(script::Struct* params, script::Context context);
bool cfunc_apply_rgba_to_texture(script::Struct* params, script::Context context);
bool cfunc_hs_vto_rgb(script::Struct* params, script::Context context);
bool cfunc_rg_bto_hsv(script::Struct* params, script::Context context);

} // namespace commands
