#pragma once

#include <cstdint>

#include <SDL3/SDL_timer.h>

// NOT ORIGINAL - SDL's millisecond clock as the code reads it: SDL 2's SDL_GetTicks was 32 bits (wrapping after 49
// days), SDL 3's is 64 - its low 32 bits, so the differences the callers take stay what they were.
namespace platform {

inline uint32_t ticks_ms() { return static_cast<uint32_t>(SDL_GetTicks()); }

} // namespace platform
