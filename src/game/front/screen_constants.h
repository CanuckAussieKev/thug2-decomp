#pragma once

#include "hook/game_global.h"

// The front end's (and the animation controller's) float constants from the original's read-only data, by name.
// The first group is defined in screen_element.cpp; the second are defined where their other users are.

namespace Front {
GAME_GLOBAL_EXTERN(float, g_aspectToLayout); // 0x00649FEC
GAME_GLOBAL_EXTERN(float, g_wideScreen); // 0x00649FE4
GAME_GLOBAL_EXTERN(float, g_minusInv448); // 0x00649FE0
GAME_GLOBAL_EXTERN(float, g_inv448); // 0x00649FDC
GAME_GLOBAL_EXTERN(float, g_inv896); // 0x00649FD0
GAME_GLOBAL_EXTERN(float, g_inv1280); // 0x00649FD8
GAME_GLOBAL_EXTERN(float, g_invTwoPi); // 0x0064A044
GAME_GLOBAL_EXTERN(float, g_twoPi); // 0x0064545C
GAME_GLOBAL_EXTERN(float, g_pi); // 0x00649FC8
GAME_GLOBAL_EXTERN(float, g_layoutTop); // 0x0064A258
GAME_GLOBAL_EXTERN(float, g_layoutBottom); // 0x0064A254
GAME_GLOBAL_EXTERN(float, g_shadowBehind); // 0x0064541C
GAME_GLOBAL_EXTERN(float, g_sixteenth); // 0x00645A84
GAME_GLOBAL_EXTERN(float, g_framesPerSecond); // 0x00645EA0
} // namespace Front

namespace CFuncs {
GAME_GLOBAL_EXTERN(float, g_degreesToRadians); // 0x00645464, defined with its other users
GAME_GLOBAL_EXTERN(float, g_scriptRadiansToDegrees); // 0x00645468, defined with its other users
} // namespace CFuncs
namespace Game {
GAME_GLOBAL_EXTERN(float, g_goalLifeSecondsPerMillisecond); // 0x006461D8, defined with its other users
} // namespace Game
namespace Obj {
GAME_GLOBAL_EXTERN(float, g_floatZero); // 0x0066D520, defined with its other users
GAME_GLOBAL_EXTERN(float, g_animMoviesSecondsPerFrame); // 0x006463B8, defined with its other users
GAME_GLOBAL_EXTERN(float, g_animMoviesOne); // 0x006453A8, defined with its other users
} // namespace Obj
namespace Nx {
GAME_GLOBAL_EXTERN(float, g_onePercent); // 0x0064544C, defined with its other users
} // namespace Nx
namespace Mdl {
GAME_GLOBAL_EXTERN(float, g_competitionTiny); // 0x006453D4, defined with its other users
} // namespace Mdl
namespace Gfx {
GAME_GLOBAL_EXTERN(float, g_half); // 0x006453BC, defined with its other users
GAME_GLOBAL_EXTERN(float, g_thousand); // 0x006453C0, defined with its other users
GAME_GLOBAL_EXTERN(float, g_twoTo32); // 0x006453B4, defined with its other users
} // namespace Gfx
