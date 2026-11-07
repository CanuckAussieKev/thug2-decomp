#pragma once

// NOT ORIGINAL - the game view's frame (engine/gfx/game_view.h): the frame drawn into the picture a UI asked for
// instead of the window. render::draw_world calls begin first and end after the 2D pass, before what the extensions
// draw over the frame.
namespace render::game_view_frame {

// The frame's start: the request taken; with one, the picture made at its size (device.h: make_view_targets), the
// frame's targets switched to it and cleared, and the renderer's back buffer size and its 640x480 layout's factors made
// the picture's. False: no request (the picture is let go) or it couldn't be made - the frame goes to the window as
// the original draws it.
bool begin();
// The window's targets, size and factors back, cleared for the UI; the frame noted as drawn into the picture.
void end();

} // namespace render::game_view_frame
