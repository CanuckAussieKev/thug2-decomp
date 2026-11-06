#pragma once

// NOT ORIGINAL: which graphics backend the exe runs on - [Graphics] renderer=vulkan|d3d9 in thug2-decomp.ini (default
// vulkan; renderer=d3d9 chooses Direct3D 9). Both exes (the game's and the level editor's) share this choice: main.cpp installs the backend's
// render::Device and render::UiBackend through install_renderer, and WinMain makes the window for the same API
// (renderer_window_api). The key is read once, at the first call.
//
// Vulkan is used only when this build has the Vulkan library (CMake THUG2_VULKAN) and the machine has a device it can
// draw with (a Vulkan 1.3 loader and device with dynamic rendering and push descriptors: render::vulkan::usable);
// otherwise the choice logs why and falls back to Direct3D 9, with the ordinary window - so a machine or a build
// without Vulkan always runs.

#include <cstdint>

#include "platform/sdl_window.h"

namespace app {

enum class Renderer : uint8_t { kDirect3D9, kVulkan };

// The renderer in use (the key, after the fallback).
Renderer chosen_renderer();
// The window flag it needs.
sys::WindowApi renderer_window_api();
// Installs the chosen renderer's render::Device and render::UiBackend.
void install_renderer();

} // namespace app
