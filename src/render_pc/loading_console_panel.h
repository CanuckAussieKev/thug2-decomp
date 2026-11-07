#pragma once

#include <cstdint>
#include <span>

#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/loading_console.h"

struct ImFontAtlas;

// NOT ORIGINAL - the loading console's panel (loading_console.h): drawn by the graphics backend over the loading screen.
namespace gfx::loading_console {

// The panel's ends for install_panel (app/main.cpp installs them).
PanelHooks panel_hooks();

// Drawn by the loading screen's frame (render_pc/loading_bar.cpp, 0x004B6FF0), inside its scene, with the loading
// screen's lock held: the panel over the picture, the device's state put back after.
void draw_panel();

// What the panel draws with (also the editor mode's loading dialog: loading_dialog_panel.h) - a vertex in the screen's
// pixels (pretransformed) with its colour (0xAARRGGBB) and texture coordinates.
struct ScreenVertex {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
};
// A triangle list drawn over what the scene holds, alpha-blended, textured with `texture` (filtered linearly when
// `smooth`, else by point), the device's state saved and put back after. Inside a scene, with the loading screen's lock.
void draw_screen_triangles(render::GpuTexture* texture, std::span<const ScreenVertex> vertices, bool smooth);
// A built font atlas's coverage as a texture: white, its alpha the coverage (null when it can't be made - logged with
// `who`).
render::GpuTexture* make_coverage_texture(ImFontAtlas& atlas, const char* who);

} // namespace gfx::loading_console
