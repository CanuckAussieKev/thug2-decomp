#pragma once

#include <span>

#include "engine/gfx/render_device.h"

// NOT ORIGINAL - what our level editor (src/editor) asks of the Direct3D 9 backend, through render::Device
// (render_device.h, "what our level editor asks"): its old HUD's lines (until stage 11 round 16 the editor's own
// Direct3D code, editor/level_editor.cpp).
namespace render::editor_support {

// The lines drawn on the device with the renderer's cached state (no texture, the screen vertex format, no pixel
// shader) and texture stage 0 taking the vertex colour, put back after (the renderer doesn't know of it).
void draw_screen_lines(std::span<const ScreenLineVertex> vertices);

} // namespace render::editor_support
