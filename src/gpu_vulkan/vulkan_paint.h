#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - paint trails (docs/design/vulkan.md "Paint trails"; [Graphics] paint_trails - gpu_vulkan.h's
// PaintSettings): the Vulkan Gpu's side of engine/gfx/paint_trails.h. The paint's three fields (the level seen along x, y
// and z) are put on the device through the surface trails' windows (vulkan_surface_trail.h) - each a window of
// paint::kWindowTiles tiles a side round the camera, side by side in two images: the coats (RGBA8, premultiplied) and the
// painted places along each axis (R32F) -, brought up to date by the frame's first view (only tiles that came in or
// changed copied). A draw render_pc marks as the ground or a scene object (surface_trail::current_ground) on one of the
// per-pixel lighting's programs or the fixed stages with a texture gets FragmentKey::trails' kTrailPaint: its albedo
// mixed toward the coat where its surface is painted (ff_shaders.cpp kPaintBlock; PaintUniforms per view at
// kPaintUniformsBinding, the coats and places at kPaintCoatBinding / kPaintPlaceBinding) - before its light, so the one
// lighting path lights it (the bake, the time of day's relit light, the per-pixel lighting, the traced shadows). Never on
// characters (their draws aren't marked) or water (its own key).
namespace render::vulkan {

struct Backend;

// A draw's paint descriptors (the world's draws with kTrailPaint).
struct PaintDescriptors {
    bool on = false;
    VkDescriptorBufferInfo block{};
    VkDescriptorImageInfo coats{};
    VkDescriptorImageInfo places{};
};
// The draw's key with the paint's coat where it takes it (paint on, the windows on the device, a draw render_pc marked as
// the ground, a program that takes it, not water), and its descriptors. The caller holds the lock.
void paint_key(Backend& state, const GpuStateBlock& block, ShaderKey* shaders, PaintDescriptors* out);
// What the view begins with: the view noted, the windows brought up to date (the frame's first view).
void begin_paint_view(Backend& state, const rt::ViewDrawn& view);

struct PaintStats {
    uint64_t covered_draws = 0; // world draws given the coat
    uint64_t tiles_copied = 0;  // tiles copied into the windows
    uint64_t slots_cleared = 0;
    std::array<std::array<int, 2>, 3> windows{}; // each field's window's first tile (u, v)
    int window_size = 0;                         // tiles a side
};
[[nodiscard]] PaintStats paint_stats();

// The windows on the device read back (the check's): the coats or the places (`places`), 4 bytes a texel, `width` x
// `height` (the three windows side by side - x's, y's, z's). The caller holds the lock; the frame's work is finished.
bool read_paint_windows(Backend& state, bool places, std::vector<uint32_t>* texels, uint32_t* width, uint32_t* height);

// The objects released (the device's shutdown; the caller holds the lock).
void release_paint(Backend& state);

} // namespace render::vulkan
