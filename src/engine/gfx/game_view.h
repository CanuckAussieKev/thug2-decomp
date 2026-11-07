#pragma once

#include <cstdint>

#include "base/core/api.h"

// NOT ORIGINAL - the game view: the game's frame drawn into a picture instead of the window, for a UI that shows it in
// a panel of its own (the level editor's Scene panel). Off unless asked for, frame by frame: a UI asks once a frame
// (request) for the next frame to be drawn into a picture of a size, and says where in the window it shows it; a frame
// drawn without a request goes to the window again, as the original draws it.
//
// While a frame is drawn into the picture the renderer's back buffer is the picture (render_pc/game_view_frame.cpp):
// its size is the back buffer's (the viewports, the 640x480 layout's factors and the screen copies follow it), the
// screen's shape is the picture's (render::desired_aspect_ratio: the cameras' aspect and field of view), and the mouse
// on the game's menus is mapped through where the picture is shown. After the game's 2D pass the window is the target
// again, so the UI drawn over the frame (ext::draw_over_frame) is drawn on the window, the picture among its panels.
namespace render::game_view {

// Where the UI shows the picture: the window's pixels (what SDL's mouse events are in).
struct Placement {
    float x{};
    float y{};
    float width{};
    float height{};
};
// The picture asked for: its size in pixels (the back buffer's pixels: the window's scaled as the window shows the
// back buffer), and where it is shown.
struct Request {
    int width{};
    int height{};
    Placement shown;
};
// A picture's size.
struct Size {
    int width{};
    int height{};
};

// --- the UI's side (the plugin API) ---

// The next frame drawn into a picture of `request`'s size (a size under 16 pixels either way asks for none).
THUG2_API void request(const Request& request);
// The last frame drawn into the picture: its ImGui texture id (render::UiTextureId; 0: the last frame went to the
// window) and its size.
THUG2_API uint64_t texture();
THUG2_API Size drawn_size();

// --- the engine's side ---

// The 640x480 layout's conversion to the picture's pixels, as render::set_letterbox made it for the picture's size
// (picture pixel = layout * scale + offset).
struct LayoutMap {
    float scale_x{};
    float scale_y{};
    int offset_x{};
    int offset_y{};
};
// What the renderer drew this frame (render_pc's frame: game_view_frame.cpp).
struct Drawn {
    Size size;
    Placement shown;
    uint64_t texture{};
    LayoutMap layout;
};

// The request for this frame, taken (false: none - this frame goes to the window).
bool take_request(Request* request);
// The frame was drawn into the picture / to the window.
void note_drawn(const Drawn& drawn);
void note_window();
// Whether the frames are drawn into the picture (the last frame drawn was), and its size: the screen's shape is the
// picture's meanwhile. (on is the UI's too: what the frame being drawn notes of its view is the picture's.)
THUG2_API bool on();
Size size();
// A point of the window (its pixels) as the picture's pixels (false: the game view is off or the picture has no size).
struct PicturePoint {
    double x{};
    double y{};
};
bool window_to_picture(float window_x, float window_y, PicturePoint* point);
// The layout's conversion for the picture (while on).
LayoutMap layout_map();

} // namespace render::game_view
