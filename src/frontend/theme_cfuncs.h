#pragma once

#include <cstdint>

namespace script {
class Struct;
class Script;
} // namespace Script

// Script commands (CFunctions) 0x005B4260-0x005B45F0: the front end theme's colours. Each one names a global
// (DE_HIGHLIGHTED_TEXT_COLOR, ...) - the theme's colour for that use - as the script's `return_value` parameter:
// Theme_GetHighlightedTextColor return_value=<name> adds <name>=DE_HIGHLIGHTED_TEXT_COLOR. All are cdecl (params,
// script) and true.
namespace commands {

bool __cdecl cfunc_theme_get_highlighted_text_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_unhighlighted_text_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_highlighted_text_color_new(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_unhighlighted_text_color_new(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_highlight_bar_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_speech_box_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_pause_bar_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_icon_on_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_font_colors(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_support_piece_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_alt_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_menu_title_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_hud_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_hud_special_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_balance_meter_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_balance_arrow_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_bg_parts_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_key_color(script::Struct* params, script::Script* script);
bool __cdecl cfunc_theme_get_text_twitch_color(script::Struct* params, script::Script* script);

} // namespace commands
