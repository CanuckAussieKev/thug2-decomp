#pragma once

// NOT ORIGINAL - a check's window (the Vulkan checks') shown behind the user's windows: not activated
// (tests::no_error_dialogs turns SDL's activate hints off) and sent to the bottom of the stack, where it keeps drawing -
// a window shown without the focus is otherwise still put on top of the others.

#include <SDL3/SDL.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tests {

inline void show_behind(SDL_Window* window)
{
    SDL_ShowWindow(window);
#ifdef _WIN32
    auto* handle = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (handle != nullptr) {
        SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
}

} // namespace tests
