#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005B4260-0x005B45F0: the front end theme's colours. Each one names a global
// (DE_HIGHLIGHTED_TEXT_COLOR, ...) - the theme's colour for that use - as the script's `return_value` parameter:
// Theme_GetHighlightedTextColor return_value=<name> adds <name>=DE_HIGHLIGHTED_TEXT_COLOR. All are cdecl (params,
// script) and true.
namespace ScriptCommands {

bool __cdecl CFunc_Theme_GetHighlightedTextColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetUnhighlightedTextColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetHighlightedTextColorNew(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetUnhighlightedTextColorNew(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetHighlightBarColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetSpeechBoxColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetPauseBarColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetIconOnColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetFontColors(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetSupportPieceColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetAltColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetMenuTitleColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetHUDColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetHUDSpecialColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetBalanceMeterColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetBalanceArrowColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetBGPartsColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetKeyColor(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_Theme_GetTextTwitchColor(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace ScriptCommands
