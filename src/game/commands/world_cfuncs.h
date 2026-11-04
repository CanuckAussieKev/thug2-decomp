#pragma once

#include <gsl/pointers>
#include <memory>
#include "engine/memory/mem.h"

namespace script {
class Struct;
class Script;
} // namespace script

// Script commands (CFunctions) 0x005A5360-0x005A595F: light groups and IsAlive (through the node command
// dispatcher), pausing and unpausing the skaters, SkatersAreReady, the viewer, AddSkaterEarly, the weather (rain,
// splashes, snow, its grid) and the screen effects, IsPal. All are cdecl (params, script) and return whether the
// script's `if` should be taken.
namespace modules {
struct Hub;
}

namespace commands {

// 0x005A5360 / 0x005A5380: the node command dispatcher (0x005A4EF0) with command 7 / 5.
bool cfunc_set_light_group(script::Struct* params, script::Script* script);
bool cfunc_is_alive(script::Struct* params, script::Script* script);
// 0x005A53A0: PauseSkaters [hide]: every skater paused (its vtable +0x2C) - and hidden with `hide` - unless its
// +0xBC is clear.
bool cfunc_pause_skaters(script::Struct* params, script::Script* script);
// 0x005A5420: UnPauseSkaters: every skater unpaused (vtable +0x30) and shown.
bool cfunc_un_pause_skaters(script::Struct* params, script::Script* script);
// 0x005A5480: whether the network game is ready to play and there is a skater (or we only watch).
bool cfunc_skaters_are_ready(script::Struct* params, script::Script* script);
// 0x005A54E0: LaunchViewer: the first time, the viewer module made, registered and started, and the viewer camera
// object made from viewercam_composite_structure (name=viewer_cam, flag 2).
bool cfunc_launch_viewer(script::Struct* params, script::Script* script);
// 0x005A5610: AddSkaterEarly: without a skater, the first profile's skater added (local).
bool cfunc_add_skater_early(script::Struct* params, script::Script* script);

// The weather (render::WeatherSystem, 0x006FA7C8) - each setter is the weather's own method on (params, script).
bool cfunc_weather_update_grid(script::Struct* params, script::Script* script);           // 0x005A5680
bool cfunc_weather_set_rain_height(script::Struct* params, script::Script* script);       // 0x005A5690
bool cfunc_weather_set_rain_frames(script::Struct* params, script::Script* script);       // 0x005A56B0
bool cfunc_weather_set_rain_length(script::Struct* params, script::Script* script);       // 0x005A56D0
bool cfunc_weather_set_rain_blend_mode(script::Struct* params, script::Script* script);   // 0x005A56F0
bool cfunc_weather_set_rain_rate(script::Struct* params, script::Script* script);         // 0x005A5710
bool cfunc_weather_set_rain_color(script::Struct* params, script::Script* script);        // 0x005A5730
bool cfunc_weather_set_splash_rate(script::Struct* params, script::Script* script);       // 0x005A5750
bool cfunc_weather_set_splash_life(script::Struct* params, script::Script* script);       // 0x005A5770
bool cfunc_weather_set_splash_size(script::Struct* params, script::Script* script);       // 0x005A5790
bool cfunc_weather_set_splash_color(script::Struct* params, script::Script* script);      // 0x005A57B0
bool cfunc_weather_set_splash_blend_mode(script::Struct* params, script::Script* script); // 0x005A57D0
bool cfunc_weather_set_snow_height(script::Struct* params, script::Script* script);       // 0x005A57F0
bool cfunc_weather_set_snow_frames(script::Struct* params, script::Script* script);       // 0x005A5810
bool cfunc_weather_set_snow_size(script::Struct* params, script::Script* script);         // 0x005A5830
bool cfunc_weather_set_snow_blend_mode(script::Struct* params, script::Script* script);   // 0x005A5850
bool cfunc_weather_set_snow_rate(script::Struct* params, script::Script* script);         // 0x005A5870
bool cfunc_weather_set_snow_color(script::Struct* params, script::Script* script);        // 0x005A5890
bool cfunc_weather_set_snow_active(script::Struct* params, script::Script* script);       // 0x005A58B0
bool cfunc_weather_set_rain_active(script::Struct* params, script::Script* script);       // 0x005A58D0

// The screen effects (0x006FA7CC).
bool cfunc_set_screen_effects_on(script::Struct* params, script::Script* script);         // 0x005A58F0
bool cfunc_set_screen_effects_off(script::Struct* params, script::Script* script);        // 0x005A5910
bool cfunc_set_screen_effects_dof_params(script::Struct* params, script::Script* script); // 0x005A5930

// 0x005A5950: IsPal: whether the game's region / language setting (0x007CEFA0) isn't 2.
bool cfunc_is_pal(script::Struct* params, script::Script* script);

} // namespace commands
