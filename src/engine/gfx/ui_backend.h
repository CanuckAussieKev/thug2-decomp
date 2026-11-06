#pragma once

// NOT ORIGINAL - render::UiBackend: what our Dear ImGui layers (the level editor's UI, the new front end) ask of the
// graphics backend, a frame at a time: ImGui's platform and renderer backends started for a context, a frame begun and
// its draw data drawn, the device objects dropped before a reset (made again by the next frame), and the pictures ImGui
// shows - images uploaded from texels in memory, and two targets the renderer draws into for the UI (the Add palette's
// preview and its thumbnails' atlas). app/ makes it and installs it at start-up (install_ui_backend), as it does
// render::Device: Direct3D 9's (gpu_d3d9/d3d9_ui_backend.h: ImGui's SDL3 and Direct3D 9 backends) or Vulkan's
// (gpu_vulkan/vulkan_ui_backend.cpp: ImGui's SDL3 backend, the lists drawn by the Vulkan Gpu). Texture ids are ImGui's
// (ImTextureID, 64 bits - the backend's own texture names), the targets are begun and ended around the renderer's
// draws, and a lost device is the backend's business between invalidate_device_objects and the next frame.
//
// The grain is a frame (new_frame, render_draw_data), an event (process_event), a device reset or a resource (an image,
// a target); never per ImGui draw command or vertex - ImGui's draw lists are the UI's own.
//
// Every method acts on the CURRENT ImGui context: each context (the editor's, the new front end's) has its own backend
// data, which ImGui's backends keep in its IO.
//
// Not installed, a call dereferences null: the UI backend is installed before any UI runs.

#include <cstdint>

#include "base/core/packed_colour.h"

struct ImDrawData;
struct SDL_Window;
union SDL_Event;

namespace render {

// An image as ImGui names it (ImTextureID): the backend's texture as a 64-bit id; 0 none.
using UiTextureId = uint64_t;

// What starting the backends on a context came to.
enum class UiStart : uint8_t {
    kStarted,
    kNoDevice,       // the graphics device isn't made yet (tried again later)
    kWindowRefused,  // ImGui's platform backend refused the window
    kDeviceRefused,  // ImGui's renderer backend refused the device
};

// An image's texels: 32-bit BGRA (A8R8G8B8), or rows of 4x4 DXT1 blocks.
enum class UiPixelLayout : uint8_t { kBgra32, kDxt1 };
// An image's bytes per row and its row count.
struct UiRowLayout {
    int row_bytes_{};
    int rows{};
};

class UiBackend {
public:
    UiBackend() = default;
    UiBackend(const UiBackend&) = delete;
    UiBackend& operator=(const UiBackend&) = delete;
    UiBackend(UiBackend&&) = delete;
    UiBackend& operator=(UiBackend&&) = delete;

    // --- a context's backends ---
    // ImGui's platform backend started on the game's window and its renderer backend on the device, for the current
    // context (once; the platform one shut down again if the renderer one refuses).
    virtual UiStart start(SDL_Window* window) = 0;
    // What the backends are, for the log ("SDL3 and Direct3D 9").
    [[nodiscard]] virtual const char* describe() const = 0;
    // An SDL event for ImGui (true: ImGui used it).
    virtual bool process_event(const SDL_Event* event) = 0;
    // The frame's start (before ImGui::NewFrame): the renderer backend (its device objects made again if a reset
    // dropped them), then the platform backend (window size, time, cursor).
    virtual void new_frame() = 0;
    // ImGui's draw data (after ImGui::Render) drawn on the device as it is (the caller scales it to the back buffer).
    // The backend leaves the device's state as it found it.
    virtual void render_draw_data(ImDrawData* data) = 0;
    // The current context's device objects (its font texture) dropped: before a device reset, or after the fonts
    // change; the next new_frame makes them again.
    virtual void invalidate_device_objects() = 0;

    // --- images: made from texels in memory, kept over a device reset; 0 when one can't be made ---
    virtual UiTextureId create_texture(int width, int height, UiPixelLayout format, const void* data,
                                       UiRowLayout row_layout) = 0;
    virtual void release_texture(UiTextureId id) = 0;

    // --- the pictures the renderer draws for the UI ---
    // The preview: a square colour target of `size` pixels with its own depth surface, made on first use (again when
    // the size changes) and freed by free_offscreen - before a device reset too. begin_offscreen makes it the device's
    // target (the target, depth surface, viewport and the world / view / projection transforms saved first), cleared to
    // `argb`; the renderer then draws into it; end_offscreen puts back what was saved. False (nothing switched): no
    // device, or the target couldn't be made (logged once).
    virtual bool begin_offscreen(int size, colour::Packed argb_colour) = 0;
    virtual void end_offscreen() = 0;
    [[nodiscard]] virtual UiTextureId offscreen_texture() = 0;
    virtual void free_offscreen() = 0;
    // The thumbnails' atlas: one square target of `atlas_size` pixels with its own depth surface, made on first use and
    // kept (freed by free_atlas). begin_atlas_cell makes it the target with the viewport on one cell (x, y, size in
    // pixels) - saving as begin_offscreen does - and clears that cell to `argb` (colour and depth); end_atlas_cell puts
    // back what was saved. atlas_generation changes whenever the atlas is freed (its cells are then empty).
    virtual bool begin_atlas_cell(int atlas_size, int x, int y, int size, colour::Packed argb_colour) = 0;
    virtual void end_atlas_cell() = 0;
    [[nodiscard]] virtual UiTextureId atlas_texture() = 0;
    [[nodiscard]] virtual uint32_t atlas_generation() = 0;
    virtual void free_atlas() = 0;

protected:
    ~UiBackend() = default;
};

// The installed UI backend (app/ installs it at start-up, once).
UiBackend& ui_backend();
void install_ui_backend(UiBackend& backend);

} // namespace render
