#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <SDL3/SDL.h>

#include "base/core/raw_view.h"

// The keyboard's state this frame (SDL_GetKeyboardState): one byte a scancode, non-zero while the key is down.
namespace platform {

using KeyState = std::span<const uint8_t>;

inline KeyState keyboard_keys()
{
    int count = 0;
    // (SDL 3's state is a bool a key, SDL 2's a byte: the same bytes, 1 while down - read as the bools' bytes; SDL
    // keeps the array, updated as events are pumped)
    static_assert(sizeof(bool) == sizeof(uint8_t), "a bool is a byte");
    const uint8_t* keys = raw::bytes(SDL_GetKeyboardState(&count));
    return {keys, static_cast<std::size_t>(count)};
}

} // namespace platform
