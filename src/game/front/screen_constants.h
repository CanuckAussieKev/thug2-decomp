#pragma once

// The front end's (and the animation controller's) float constants from the original's read-only data, by name.
// The first group is defined in screen_element.cpp; the second are defined where their other users are.

namespace Front {
extern float g_aspectToLayout; // 0x00649FEC
extern float g_wideScreen; // 0x00649FE4
extern float g_minusInv448; // 0x00649FE0
extern float g_inv448; // 0x00649FDC
extern float g_inv896; // 0x00649FD0
extern float g_inv1280; // 0x00649FD8
extern float g_invTwoPi; // 0x0064A044
extern float g_twoPi; // 0x0064545C
extern float g_pi; // 0x00649FC8
extern float g_layoutTop; // 0x0064A258
extern float g_layoutBottom; // 0x0064A254
extern float g_shadowBehind; // 0x0064541C
extern float g_sixteenth; // 0x00645A84
extern float g_framesPerSecond; // 0x00645EA0
} // namespace Front

namespace CFuncs {
extern float g_degreesToRadians; // 0x00645464, defined with its other users
extern float g_scriptRadiansToDegrees; // 0x00645468, defined with its other users
} // namespace CFuncs
namespace Game {
extern float g_goalLifeSecondsPerMillisecond; // 0x006461D8, defined with its other users
} // namespace Game
namespace Obj {
extern float g_floatZero; // 0x0066D520, defined with its other users
extern float g_animMoviesSecondsPerFrame; // 0x006463B8, defined with its other users
extern float g_animMoviesOne; // 0x006453A8, defined with its other users
} // namespace Obj
namespace Nx {
extern float g_onePercent; // 0x0064544C, defined with its other users
} // namespace Nx
namespace Mdl {
extern float g_competitionTiny; // 0x006453D4, defined with its other users
} // namespace Mdl
namespace Gfx {
extern float g_half; // 0x006453BC, defined with its other users
extern float g_thousand; // 0x006453C0, defined with its other users
extern float g_twoTo32; // 0x006453B4, defined with its other users
} // namespace Gfx
