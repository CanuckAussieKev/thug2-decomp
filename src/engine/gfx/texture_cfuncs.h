#pragma once

#include <cstdint>
#include <array>

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) and helpers 0x005A9D80-0x005AA92F: SceAltPt, two searches through a structure for a
// value's name and an angle wrap (helpers of the cutscene / CAT code next to them), CompositeObjectExists, the
// textures (the texture dictionary 0x00700F1C: load, create, unload, the 32-bit image, flip, resize, scale, regions,
// pull to an edge, brightness, HSV, copy, combine; the particle dictionary 0x00700F20: particle and SFP textures) and
// SetParkName. The CFunctions are cdecl (params, script) and return whether the script's `if` should be taken.
namespace commands {

constexpr std::size_t kParkNameSize = 128;

// 0x005A9D80: SceAltPt: the flag 0x007CE441 set. True.
bool cfunc_sce_alt_pt(script::Struct* params, script::Context context);

// 0x005A9D90: the name of the first name component (in `structure`, its structures, and the global structures its
// components' values name - 10 levels at most) whose value is `value`; 0 if none.
uint32_t find_name_of_value(script::Struct* structure, uint32_t value, int depth);
// 0x005A9E30: the name of the last integer component of `structure` that
// is `value` (each found one printed: "s_find_cat_in_mapping found %i"); 0 if none.
uint32_t find_name_of_integer(script::Struct* structure, uint32_t value);
// 0x005A9E80: an angle in degrees brought into 0..360 (as the x87 computes it: in double precision).
float wrap_angle(float degrees);

// 0x005A9ED0: CompositeObjectExists name=<id>: whether there's an object with that id.
bool cfunc_composite_object_exists(script::Struct* params, script::Context context);

// The texture dictionary's commands (each finds its texture by the unnamed checksum; true unless noted).
bool cfunc_load_texture(script::Struct* params, script::Context context); // 0x005A9F00 (loaded?)
// (not original) Whether the texture dictionary has the texture of a file name as ingest_skin / UnloadTexture take it
// ("folder/name": the dictionary's own name lookup, as UnloadTexture's).
bool texture_loaded(const char* file);
bool cfunc_create_texture(script::Struct* params, script::Context context);               // 0x005A9F60 (made?)
bool cfunc_unload_texture(script::Struct* params, script::Context context);               // 0x005AA040
bool cfunc_generate32_bit_image(script::Struct* params, script::Context context);         // 0x005AA0C0
bool cfunc_put32_bit_image_into_texture(script::Struct* params, script::Context context); // 0x005AA160
bool cfunc_flip_texture(script::Struct* params, script::Context context);                 // 0x005AA1E0
bool cfunc_resize_texture_into(script::Struct* params, script::Context context);          // 0x005AA270
bool cfunc_scale_texture(script::Struct* params, script::Context context);                // 0x005AA2E0
bool cfunc_adjust_texture_region(script::Struct* params, script::Context context);        // 0x005AA340
bool cfunc_pull_texture_to_edge(script::Struct* params, script::Context context);         // 0x005AA460
bool cfunc_adjust_texture_brightness(script::Struct* params, script::Context context);    // 0x005AA500
bool cfunc_adjust_texture_hsv(script::Struct* params, script::Context context);           // 0x005AA590
bool cfunc_copy_texture(script::Struct* params, script::Context context);                 // 0x005AA670
bool cfunc_combine_textures(script::Struct* params, script::Context context);             // 0x005AA6E0
// The particle texture dictionary's (by the unnamed file name string).
bool cfunc_load_particle_texture(script::Struct* params, script::Context context);   // 0x005AA7B0 (loaded?)
bool cfunc_unload_particle_texture(script::Struct* params, script::Context context); // 0x005AA830
bool cfunc_load_sfp_texture(script::Struct* params, script::Context context);        // 0x005AA880 (loaded?)
bool cfunc_unload_sfp_texture(script::Struct* params, script::Context context);      // 0x005AA8B0

// 0x005AA8F0: SetParkName <string>: copied to the park name (0x007CED58, no length limit). False.
bool cfunc_set_park_name(script::Struct* params, script::Context context);

bool texture_flag_ce441();
std::array<char, kParkNameSize>& park_name();
} // namespace commands
