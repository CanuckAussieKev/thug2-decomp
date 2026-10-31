#pragma once

namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

// Script commands (CFunctions) 0x005A5360-0x005A595F: light groups and IsAlive (through the node command
// dispatcher), pausing and unpausing the skaters, SkatersAreReady, the viewer, AddSkaterEarly, the weather (rain,
// splashes, snow, its grid) and the screen effects, IsPal. All are cdecl (params, script) and return whether the
// script's `if` should be taken.
namespace GameModules {
struct Manager;
}

namespace ScriptCommands {

// 0x005A5360 / 0x005A5380: the node command dispatcher (0x005A4EF0) with command 7 / 5.
bool __cdecl CFunc_SetLightGroup(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_IsAlive(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A53A0: PauseSkaters [hide]: every skater paused (its vtable +0x2C) - and hidden with `hide` - unless its
// +0xBC is clear.
bool __cdecl CFunc_PauseSkaters(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A5420: UnPauseSkaters: every skater unpaused (vtable +0x30) and shown.
bool __cdecl CFunc_UnPauseSkaters(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A5480: whether the network game is ready to play and there is a skater (or we only watch).
bool __cdecl CFunc_SkatersAreReady(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A54E0: LaunchViewer: the first time, the viewer module made, registered and started, and the viewer camera
// object made from viewercam_composite_structure (name=viewer_cam, flag 2).
bool __cdecl CFunc_LaunchViewer(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005A5610: AddSkaterEarly: without a skater, the first profile's skater added (local).
bool __cdecl CFunc_AddSkaterEarly(Scripts::CStruct* params, Scripts::CScript* script);

// The weather (Engine3D::CWeatherSystem, 0x006FA7C8) - each setter is the weather's own method on (params, script).
bool __cdecl CFunc_WeatherUpdateGrid(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005A5680
bool __cdecl CFunc_WeatherSetRainHeight(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A5690
bool __cdecl CFunc_WeatherSetRainFrames(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A56B0
bool __cdecl CFunc_WeatherSetRainLength(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A56D0
bool __cdecl CFunc_WeatherSetRainBlendMode(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A56F0
bool __cdecl CFunc_WeatherSetRainRate(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A5710
bool __cdecl CFunc_WeatherSetRainColor(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A5730
bool __cdecl CFunc_WeatherSetSplashRate(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A5750
bool __cdecl CFunc_WeatherSetSplashLife(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A5770
bool __cdecl CFunc_WeatherSetSplashSize(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A5790
bool __cdecl CFunc_WeatherSetSplashColor(Scripts::CStruct* params, Scripts::CScript* script);  // 0x005A57B0
bool __cdecl CFunc_WeatherSetSplashBlendMode(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A57D0
bool __cdecl CFunc_WeatherSetSnowHeight(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A57F0
bool __cdecl CFunc_WeatherSetSnowFrames(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A5810
bool __cdecl CFunc_WeatherSetSnowSize(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A5830
bool __cdecl CFunc_WeatherSetSnowBlendMode(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A5850
bool __cdecl CFunc_WeatherSetSnowRate(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005A5870
bool __cdecl CFunc_WeatherSetSnowColor(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005A5890
bool __cdecl CFunc_WeatherSetSnowActive(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A58B0
bool __cdecl CFunc_WeatherSetRainActive(Scripts::CStruct* params, Scripts::CScript* script);   // 0x005A58D0

// The screen effects (0x006FA7CC).
bool __cdecl CFunc_SetScreenEffectsOn(Scripts::CStruct* params, Scripts::CScript* script);        // 0x005A58F0
bool __cdecl CFunc_SetScreenEffectsOff(Scripts::CStruct* params, Scripts::CScript* script);       // 0x005A5910
bool __cdecl CFunc_SetScreenEffectsDOFParams(Scripts::CStruct* params, Scripts::CScript* script); // 0x005A5930

// 0x005A5950: IsPal: whether the game's region / language setting (0x007CEFA0) isn't 2.
bool __cdecl CFunc_IsPal(Scripts::CStruct* params, Scripts::CScript* script);

extern GameModules::Manager* g_moduleManager;
} // namespace ScriptCommands
