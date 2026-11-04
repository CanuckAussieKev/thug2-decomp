#pragma once

#include "engine/gfx/ui_backend.h"

// NOT ORIGINAL - the Direct3D 9 backend's render::UiBackend (d3d9_ui_backend.cpp): Dear ImGui's official SDL2
// platform backend and Direct3D 9 renderer backend on the game's window and device, the UI's images and its two
// render targets. app/ installs it at start-up.
namespace render::d3d9 {

// The Direct3D 9 UI backend (made on first use, never destroyed).
UiBackend& ui_backend();

} // namespace render::d3d9
