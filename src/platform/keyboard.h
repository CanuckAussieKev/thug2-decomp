#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <SDL2/SDL.h>

// The keyboard's state this frame (SDL_GetKeyboardState): one byte a scancode, non-zero while the key is down.
namespace platform {

using KeyState = std::span<const uint8_t>;

inline KeyState keyboard_keys()
{
    int count = 0;
    const uint8_t* keys = SDL_GetKeyboardState(&count);
    return {keys, static_cast<std::size_t>(count)};
}

} // namespace platform
