#pragma once

// pi / 180 as the original's float constant holds it (the script commands' degree-to-radian conversions, the screen
// elements' and the time of day's copies). Was in cag_cfuncs.h; its own header so the engine's users don't include
// a game command file.
namespace commands {
inline constexpr float kDegreeToRadianRatio{0.017453292F}; // pi / 180
} // namespace commands
