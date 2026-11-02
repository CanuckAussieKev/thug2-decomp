#pragma once

#include "engine/objects/anim_movies.h"
#include "engine/script/math_cfuncs.h"
#include "game/skate/contest.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/gfx/movie.h"
#include "engine/gfx/tod_manager.h"
#include "game/goals/objective.h"

// The front end's (and the animation controller's) float constants from the original's read-only data, by name.
// The first group is defined in screen_element.cpp; the second are defined where their other users are.

namespace frontend {
inline constexpr float kAspectToLayout{0.75000006f}; // 0x00649FEC
inline constexpr float kWideScreen{0.9f}; // 0x00649FE4
inline constexpr float kMinusInv448{-0.002232143f}; // 0x00649FE0
inline constexpr float kInv448{0.002232143f}; // 0x00649FDC
inline constexpr float kInv896{0.0011160715f}; // 0x00649FD0
inline constexpr float kInv1280{0.00078125f}; // 0x00649FD8
inline constexpr float kInvTwoPi{0.15915494f}; // 0x0064A044
inline constexpr float kTwoPi{6.2831855f}; // 0x0064545C
extern float g_pi;                // 0x00649FC8
inline constexpr float kLayoutTop{-2e+02f}; // 0x0064A258
inline constexpr float kLayoutBottom{648.0f}; // 0x0064A254
inline constexpr float kShadowBehind{0.005f}; // 0x0064541C
inline constexpr float kSixteenth{0.0625f}; // 0x00645A84
inline constexpr float kFramesPerSecond{6e+01f}; // 0x00645EA0
} // namespace frontend

namespace commands {
} // namespace commands
namespace goals {
} // namespace goals
namespace objects {
} // namespace objects
namespace render {
} // namespace render
namespace modules {
} // namespace modules
namespace gfx {
} // namespace gfx
