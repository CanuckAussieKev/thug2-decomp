#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B4260-0x005B45F0: the front end theme's colours. Each one names a global
// (DE_HIGHLIGHTED_TEXT_COLOR, ...) - the theme's colour for that use - as the script's `return_value` parameter:
// Theme_GetHighlightedTextColor return_value=<name> adds <name>=DE_HIGHLIGHTED_TEXT_COLOR. All are cdecl (params,
// script) and true.
namespace CFuncs {

bool __cdecl CFunc_Theme_GetHighlightedTextColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetUnhighlightedTextColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetHighlightedTextColorNew(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetUnhighlightedTextColorNew(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetHighlightBarColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetSpeechBoxColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetPauseBarColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetIconOnColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetFontColors(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetSupportPieceColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetAltColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetMenuTitleColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetHUDColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetHUDSpecialColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetBalanceMeterColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetBalanceArrowColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetBGPartsColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetKeyColor(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_Theme_GetTextTwitchColor(Script::CStruct* params, Script::CScript* script);

} // namespace CFuncs
