#pragma once

#include <cstdint>
#include <string>
#include "base/core/api.h"
#include "base/core/packed_colour.h"
#include "engine/gfx/ui_backend.h"

union SDL_Event;

// NOT ORIGINAL - our Dear ImGui UI layers' backend (ours; the level editor's UI: editor/level_editor_ui.h, the new front
// end: frontend/new_frontend.h): their way to Dear ImGui's platform and renderer backends - the installed render::UiBackend (engine/gfx/ui_backend.h;
// app/ installs Direct3D 9's, gpu_d3d9/d3d9_ui_backend.h: ImGui's official SDL3 platform backend - the game's
// window and input are SDL3's: platform/sdl_window.h, engine/pad_input/sdl_input.cpp - and its Direct3D 9 renderer
// backend on the game's device). Renderer-independence rules (the user's requirement - Vulkan later only swaps the
// UI backend):
//   - no editor or front-end file includes Direct3D or an ImGui backend header (gpu_d3d9 alone does);
//   - every gizmo and line is drawn through ImGui draw lists (level_editor_ui.cpp), projected to the screen;
//   - a picture ImGui shows is the UI backend's (an image it made, or its preview / atlas target).
// ImGui's coordinates are the window's pixels (what SDL's mouse events are in); Render scales its draw data to the
// back buffer when the two differ.
namespace frontend::ui_backend {

// The backends started on the game's window and device (once; false - logged - while either is missing or a
// backend refuses). An ImGui context must be current.
THUG2_API bool prepare_all();
THUG2_API bool get_ready();
// An SDL event for ImGui (true: ImGui used it).
THUG2_API bool process_event(const SDL_Event* event);
// The frame's start (before ImGui::NewFrame): the platform backend (window size, time, cursor) and the renderer
// backend (its device objects made again if a reset dropped them).
THUG2_API void new_frame();
// ImGui's draw data (after ImGui::Render) drawn on the device, scaled from the window's pixels to the back buffer's.
THUG2_API void render();
// Before the device is reset (frame_loop::reset_device_if_lost: alt-tab, a lost device): ImGui's default-pool objects dropped;
// NewFrame makes them again. Also after the fonts change (the font texture is made again).
THUG2_API void invalidate_device_objects();
// The window's size in pixels (ImGui's display size) and the back buffer's.
THUG2_API void window_size(int* width, int* height);
// The window's title, and a new one (the level editor names itself and its level while it is open); "" / nothing
// before the window is made.
THUG2_API std::string window_title();
THUG2_API void set_window_title(const std::string& title);
// Where a width and a height go.
struct SizeOut {
    int* width{};
    int* height{};
};
THUG2_API void back_buffer_size(SizeOut size);
// Several ImGui contexts may use these backends (the editor's and the new front end's - frontend/new_frontend.h):
// Init / Ready / process_event / NewFrame / Render act on the CURRENT context (each keeps its own backend data).
// invalidate_context_objects drops only the current context's device objects (its font texture), not the editor's
// pictures below.
void invalidate_context_objects();

// A picture for ImGui made from texels in memory (the new front end's loading screens): `rows` rows of `row_bytes`
// bytes - 32-bit BGRA texels (A8R8G8B8), or rows of 4x4 DXT1 blocks. A managed texture (it survives a device reset);
// 0 when it can't be made. release_texture frees it.
using PixelLayout = render::UiPixelLayout;
// An image's bytes per row and its row count.
using RowLayout = render::UiRowLayout;

uint64_t create_texture(int width, int height, PixelLayout format, const void* data, RowLayout row_layout);
THUG2_API void release_texture(uint64_t id);
// A game picture (images\<name>.img.xbx, through the data overlay first: a loading screen, a level select picture) as
// such a texture, `shrink` times smaller each way (1: as it is; a DXT1 picture - an upscaled one - stays as it is):
// its texture (0: no such picture, or one of a kind no loading screen is) and its size. Its rows are bottom first
// (`bottom_up`: drawn with v from 1 at the top).
struct Picture {
    uint64_t texture{};
    float width{};
    float height{};
    bool bottom_up{};
};
THUG2_API Picture load_picture(const std::string& name, int shrink);

// An off-screen picture (the Add palette's preview, level_editor_preview.h): a square colour target of `size` pixels
// with its own depth surface, made on first use (again when the size changes) and freed by free_offscreen and with the
// device objects (invalidate_device_objects: before a reset). begin_offscreen makes it the device's target (the target,
// depth surface, viewport and the world / view / projection transforms saved first), cleared to `argb`; the game's
// renderer then draws into it; end_offscreen puts back what was saved. False (nothing switched): no device or the
// target couldn't be made (logged once).
THUG2_API bool begin_offscreen(int size, colour::Packed argb_colour);
THUG2_API void end_offscreen();
// The picture's ImGui texture id (0: none made).
THUG2_API uint64_t offscreen_texture();
THUG2_API void free_offscreen();

// The Add palette's thumbnails (level_editor_thumbs.h): one square atlas of `atlas_size` pixels with its own depth
// surface, made on first use and kept (freed by free_atlas and with the device objects, as the picture above).
// begin_atlas_cell makes it the device's target with the viewport on one cell (x, y, size in pixels) - the target, depth
// surface, viewport and transforms saved first as begin_offscreen does - and clears that cell to `argb` (colour and
// depth); end_atlas_cell puts back what was saved. atlas_generation changes whenever the atlas is freed (its cells are
// then empty: what they held is drawn again).
THUG2_API bool begin_atlas_cell(int atlas_size, int x, int y, int size, colour::Packed argb_colour);
THUG2_API void end_atlas_cell();
THUG2_API uint64_t atlas_texture();
THUG2_API uint32_t atlas_generation();
THUG2_API void free_atlas();

} // namespace frontend::ui_backend
