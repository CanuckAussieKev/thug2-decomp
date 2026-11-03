// Script commands (CFunctions) 0x005B4260-0x005B45F0: the front end theme's colours (see theme_cfuncs.h).

#include "frontend/theme_cfuncs.h"

#include <cstdint>

#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"

namespace commands {

namespace {

constexpr std::uint32_t kDEALTCOLOR = 0x4F3558A5u; // DE_ALT_COLOR
constexpr std::uint32_t kDEBALANCEARROWCOLOR = 0xAAA1CC0Eu; // DE_BALANCE_ARROW_COLOR
constexpr std::uint32_t kDEBALANCEMETERCOLOR = 0xD52B8EBDu; // DE_BALANCE_METER_COLOR
constexpr std::uint32_t kDEBGPARTSCOLOR = 0x494CDDA6u; // DE_BG_PARTS_COLOR
constexpr std::uint32_t kDEFONTCOLORS = 0x64D11782u; // DE_FONT_COLORS
constexpr std::uint32_t kDEHIGHLIGHTBARCOLOR = 0x5C38FD75u; // DE_HIGHLIGHT_BAR_COLOR
constexpr std::uint32_t kDEHIGHLIGHTEDTEXTCOLOR = 0x79EA5E44u; // DE_HIGHLIGHTED_TEXT_COLOR
constexpr std::uint32_t kDEHIGHLIGHTEDTEXTCOLORNEW = 0xF8D09C8Du; // DE_HIGHLIGHTED_TEXT_COLOR_NEW
constexpr std::uint32_t kDEHUDCOLOR = 0xE7BC5830u; // DE_HUD_COLOR
constexpr std::uint32_t kDEHUDSPECIALCOLOR = 0xEA9F69FEu; // DE_HUD_SPECIAL_COLOR
constexpr std::uint32_t kDEICONONVALUE = 0x7D9ED7D9u; // DE_ICON_ON_VALUE
constexpr std::uint32_t kDEKEYCOLOR = 0xA483A8D0u; // DE_KEY_COLOR
constexpr std::uint32_t kDEMENUTITLECOLOR = 0x0A576C22u; // DE_MENU_TITLE_COLOR
constexpr std::uint32_t kDEPAUSEBARCOLOR = 0x329679D2u; // DE_PAUSEBAR_COLOR
constexpr std::uint32_t kDESPEECHBOXCOLOR = 0xC7560604u; // DE_SPEECH_BOX_COLOR
constexpr std::uint32_t kDESUPPORTPIECECOLOR = 0x8471059Du; // DE_SUPPORTPIECE_COLOR
constexpr std::uint32_t kDETEXTTWITCHVALUE = 0x568461F5u; // DE_TEXT_TWITCH_VALUE
constexpr std::uint32_t kDEUNHIGHLIGHTEDTEXTCOLOR = 0x65822032u; // DE_UNHIGHLIGHTED_TEXT_COLOR
constexpr std::uint32_t kDEUNHIGHLIGHTEDTEXTCOLORNEW = 0x51CE9B77u; // DE_UNHIGHLIGHTED_TEXT_COLOR_NEW

constexpr uint32_t kReturnValue = 0x3B3F0BA7; // "return_value"

// The script's return_value parameter names the parameter to add; its value is the theme global `colour`.
bool return_theme_colour(script::Struct* params, script::Script* script, uint32_t colour)
{
    uint32_t name = 0;
    params->read_hash_value(kReturnValue, &name, 1);
    script->args()->put_name_key(checksums::Name{name}, colour);
    return true;
}

} // namespace

// 0x005B4260
bool cfunc_theme_get_highlighted_text_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEHIGHLIGHTEDTEXTCOLOR); // DE_HIGHLIGHTED_TEXT_COLOR
}
// 0x005B4290
bool cfunc_theme_get_unhighlighted_text_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEUNHIGHLIGHTEDTEXTCOLOR); // DE_UNHIGHLIGHTED_TEXT_COLOR
}
// 0x005B42C0
bool cfunc_theme_get_highlighted_text_color_new(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEHIGHLIGHTEDTEXTCOLORNEW); // DE_HIGHLIGHTED_TEXT_COLOR_NEW
}
// 0x005B42F0
bool cfunc_theme_get_unhighlighted_text_color_new(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEUNHIGHLIGHTEDTEXTCOLORNEW); // DE_UNHIGHLIGHTED_TEXT_COLOR_NEW
}
// 0x005B4320
bool cfunc_theme_get_highlight_bar_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEHIGHLIGHTBARCOLOR); // DE_HIGHLIGHT_BAR_COLOR
}
// 0x005B4350
bool cfunc_theme_get_speech_box_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDESPEECHBOXCOLOR); // DE_SPEECH_BOX_COLOR
}
// 0x005B4380
bool cfunc_theme_get_pause_bar_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEPAUSEBARCOLOR); // DE_PAUSEBAR_COLOR
}
// 0x005B43B0
bool cfunc_theme_get_icon_on_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEICONONVALUE); // DE_ICON_ON_VALUE
}
// 0x005B43E0
bool cfunc_theme_get_font_colors(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEFONTCOLORS); // DE_FONT_COLORS
}
// 0x005B4410
bool cfunc_theme_get_support_piece_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDESUPPORTPIECECOLOR); // DE_SUPPORTPIECE_COLOR
}
// 0x005B4440
bool cfunc_theme_get_alt_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEALTCOLOR); // DE_ALT_COLOR
}
// 0x005B4470
bool cfunc_theme_get_menu_title_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEMENUTITLECOLOR); // DE_MENU_TITLE_COLOR
}
// 0x005B44A0
bool cfunc_theme_get_hud_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEHUDCOLOR); // DE_HUD_COLOR
}
// 0x005B44D0
bool cfunc_theme_get_hud_special_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEHUDSPECIALCOLOR); // DE_HUD_SPECIAL_COLOR
}
// 0x005B4500
bool cfunc_theme_get_balance_meter_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEBALANCEMETERCOLOR); // DE_BALANCE_METER_COLOR
}
// 0x005B4530
bool cfunc_theme_get_balance_arrow_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEBALANCEARROWCOLOR); // DE_BALANCE_ARROW_COLOR
}
// 0x005B4560
bool cfunc_theme_get_bg_parts_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEBGPARTSCOLOR); // DE_BG_PARTS_COLOR
}
// 0x005B4590
bool cfunc_theme_get_key_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDEKEYCOLOR); // DE_KEY_COLOR
}
// 0x005B45C0
bool cfunc_theme_get_text_twitch_color(script::Struct* params, script::Script* script)
{
    return return_theme_colour(params, script, kDETEXTTWITCHVALUE); // DE_TEXT_TWITCH_VALUE
}

} // namespace commands
