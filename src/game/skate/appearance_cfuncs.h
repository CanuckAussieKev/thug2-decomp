#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) of the players' appearances (0x005AAEF0-0x005B0A40, the appearance ones): a player's
// appearance and its face (the face points, the face texture and its overlay), the skater's model rebuilt
// from its profile (colours, scale, visibility, UV, the whole model), the appearance's parts and their colour, scale and
// UV settings. All are cdecl (params, script) and
// return whether the script's `if` should be taken (true where nothing is asked).
namespace commands {

// A face's points (0x38 bytes; the face texture's 0x00487550 gives a copy, 0x00487570 sets them).
struct FacePoints {
    uint32_t words_[14];
};

bool __cdecl cfunc_set_player_appearance(script::Struct* params, script::Script* script);         // 0x005AE550
bool __cdecl cfunc_get_player_face_points(script::Struct* params, script::Script* script);         // 0x005AE5B0
bool __cdecl cfunc_set_player_face_points(script::Struct* params, script::Script* script);         // 0x005AE710
bool __cdecl cfunc_set_player_face_texture(script::Struct* params, script::Script* script);        // 0x005AE7A0
bool __cdecl cfunc_set_player_face_overlay_texture(script::Struct* params, script::Script* script); // 0x005AE820
bool __cdecl cfunc_clear_player_face_texture(script::Struct* params, script::Script* script);      // 0x005AE880
bool __cdecl cfunc_player_face_is_valid(script::Struct* params, script::Script* script);           // 0x005AE8C0
bool __cdecl cfunc_refresh_skater_colors(script::Struct* params, script::Script* script);         // 0x005AF750
bool __cdecl cfunc_refresh_skater_scale(script::Struct* params, script::Script* script);          // 0x005AF7D0
bool __cdecl cfunc_refresh_skater_visibility(script::Struct* params, script::Script* script);     // 0x005AF850
bool __cdecl cfunc_refresh_skater_uv(script::Struct* params, script::Script* script);             // 0x005AF8D0
bool __cdecl cfunc_refresh_skater_model(script::Struct* params, script::Script* script);          // 0x005AFD80
bool __cdecl cfunc_edit_player_appearance(script::Struct* params, script::Script* script);        // 0x005AFF30
bool __cdecl cfunc_get_actual_cas_option_struct(script::Struct* params, script::Script* script);    // 0x005B0420
bool __cdecl cfunc_get_actual_player_appearance_part(script::Struct* params, script::Script* script); // 0x005B0480
bool __cdecl cfunc_get_player_appearance_part(script::Struct* params, script::Script* script);     // 0x005B0500
bool __cdecl cfunc_get_player_appearance(script::Struct* params, script::Script* script);         // 0x005B0580
bool __cdecl cfunc_set_player_appearance_color(script::Struct* params, script::Script* script);    // 0x005B0670
bool __cdecl cfunc_set_player_appearance_scale(script::Struct* params, script::Script* script);    // 0x005B0780
bool __cdecl cfunc_set_player_appearance_uv(script::Struct* params, script::Script* script);       // 0x005B0920

} // namespace commands
