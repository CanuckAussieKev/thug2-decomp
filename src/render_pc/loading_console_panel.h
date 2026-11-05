#pragma once

#include "engine/gfx/loading_console.h"

// NOT ORIGINAL - the loading console's panel (loading_console.h): drawn by the graphics backend over the loading screen.
namespace gfx::loading_console {

// The panel's ends for install_panel (app/main.cpp installs them).
PanelHooks panel_hooks();

// Drawn by the loading screen's frame (render_pc/loading_bar.cpp, 0x004B6FF0), inside its scene, with the loading
// screen's lock held: the panel over the picture, the device's state put back after.
void draw_panel();

} // namespace gfx::loading_console
