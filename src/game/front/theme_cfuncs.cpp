// Script commands (CFunctions) 0x005B4260-0x005B45F0: the front end theme's colours (see theme_cfuncs.h).

#include "game/front/theme_cfuncs.h"

#include <cstdint>

#include "game/script/cscript.h"
#include "game/script/struct.h"

namespace CFuncs {

namespace {

constexpr uint32_t kReturnValue = 0x3B3F0BA7; // "return_value"

// The script's return_value parameter names the parameter to add; its value is the theme global `colour`.
bool ReturnThemeColour(Script::CStruct* params, Script::CScript* script, uint32_t colour)
{
    uint32_t name;
    params->GetChecksum(kReturnValue, &name, 1);
    script->m_params->AddChecksum(name, colour);
    return true;
}

} // namespace

// 0x005B4260
bool __cdecl CFunc_Theme_GetHighlightedTextColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x79EA5E44); // DE_HIGHLIGHTED_TEXT_COLOR
}
// 0x005B4290
bool __cdecl CFunc_Theme_GetUnhighlightedTextColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x65822032); // DE_UNHIGHLIGHTED_TEXT_COLOR
}
// 0x005B42C0
bool __cdecl CFunc_Theme_GetHighlightedTextColorNew(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xF8D09C8D); // DE_HIGHLIGHTED_TEXT_COLOR_NEW
}
// 0x005B42F0
bool __cdecl CFunc_Theme_GetUnhighlightedTextColorNew(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x51CE9B77); // DE_UNHIGHLIGHTED_TEXT_COLOR_NEW
}
// 0x005B4320
bool __cdecl CFunc_Theme_GetHighlightBarColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x5C38FD75); // DE_HIGHLIGHT_BAR_COLOR
}
// 0x005B4350
bool __cdecl CFunc_Theme_GetSpeechBoxColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xC7560604); // DE_SPEECH_BOX_COLOR
}
// 0x005B4380
bool __cdecl CFunc_Theme_GetPauseBarColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x329679D2); // DE_PAUSEBAR_COLOR
}
// 0x005B43B0
bool __cdecl CFunc_Theme_GetIconOnColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x7D9ED7D9); // DE_ICON_ON_VALUE
}
// 0x005B43E0
bool __cdecl CFunc_Theme_GetFontColors(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x64D11782); // DE_FONT_COLORS
}
// 0x005B4410
bool __cdecl CFunc_Theme_GetSupportPieceColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x8471059D); // DE_SUPPORTPIECE_COLOR
}
// 0x005B4440
bool __cdecl CFunc_Theme_GetAltColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x4F3558A5); // DE_ALT_COLOR
}
// 0x005B4470
bool __cdecl CFunc_Theme_GetMenuTitleColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x0A576C22); // DE_MENU_TITLE_COLOR
}
// 0x005B44A0
bool __cdecl CFunc_Theme_GetHUDColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xE7BC5830); // DE_HUD_COLOR
}
// 0x005B44D0
bool __cdecl CFunc_Theme_GetHUDSpecialColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xEA9F69FE); // DE_HUD_SPECIAL_COLOR
}
// 0x005B4500
bool __cdecl CFunc_Theme_GetBalanceMeterColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xD52B8EBD); // DE_BALANCE_METER_COLOR
}
// 0x005B4530
bool __cdecl CFunc_Theme_GetBalanceArrowColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xAAA1CC0E); // DE_BALANCE_ARROW_COLOR
}
// 0x005B4560
bool __cdecl CFunc_Theme_GetBGPartsColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x494CDDA6); // DE_BG_PARTS_COLOR
}
// 0x005B4590
bool __cdecl CFunc_Theme_GetKeyColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0xA483A8D0); // DE_KEY_COLOR
}
// 0x005B45C0
bool __cdecl CFunc_Theme_GetTextTwitchColor(Script::CStruct* params, Script::CScript* script)
{
    return ReturnThemeColour(params, script, 0x568461F5); // DE_TEXT_TWITCH_VALUE
}

} // namespace CFuncs
