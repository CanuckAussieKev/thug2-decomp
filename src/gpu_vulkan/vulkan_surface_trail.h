#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <volk.h>

#include "engine/gfx/surface_trail.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_memory.h"

// NOT ORIGINAL - the surface trails on the device (engine/gfx/surface_trail.h; docs/design/vulkan.md "Surface trails"):
// the window both the snow (vulkan_snow.h) and the paint (vulkan_paint.h) draw their fields through. A field is kept
// whole on the processor for the level session; the device holds a window of `tiles` x `tiles` of its tiles round the
// camera, each tile in the slot its place modulo `tiles` (sampled repeating: the window is toroidal). As the camera moves
// the tiles that come in are copied into the slots of those that went - the others stay -, and a tile is copied again
// only when its version changed: nothing is lost near the camera. Each of the field's planes goes into an image of its
// own (4 bytes a texel), at `first_column` of it (a window beside others in one image: the paint's three fields).
namespace render::vulkan {

struct Backend;

// A slot of a window: which tile it holds (and its version), or none.
struct TrailSlot {
    bool filled = false; // a tile's texels (else zeros)
    bool known = false;  // the slot's content is known (false: never written)
    int x = 0;
    int z = 0;
    uint32_t version = 0;
};

struct TrailWindow {
    int tiles = 0;              // a side
    float texel = 0.0F;         // world units
    uint64_t field = 0;         // the field it shows (Field::serial: another - a new session - and every slot is copied again)
    uint32_t first_column = 0;  // where it is in its images (texels)
    std::array<int, 2> first{}; // its first tile (the plane's u, v)
    std::vector<TrailSlot> slots;
    uint64_t tiles_copied = 0;  // tiles copied in (a new tile in a slot, a changed one)
    uint64_t slots_cleared = 0; // slots emptied (no tile there)
};

// The window placed round `centre` (the field's plane: u, v) and every slot whose tile came in or changed copied into
// `images` (one a plane, R8G8B8A8 / R32 formats - 4 bytes a texel); `moved` (if given) gets each slot given another
// place (x, z of the slot). The caller holds the lock and has made the images (tiles x kTileTexels a side at least).
bool update_trail_window(Backend& state, TrailWindow* window, const surface_trail::Field& field, const surface_trail::Point& centre,
                         int tiles, std::span<Image* const> images, std::vector<std::array<int, 2>>* moved);

// The view's inverse, as the cover's blocks hand it on (camera space -> the world; a row-vector matrix, D3D's layout).
Float4x4 trail_eye_to_world(const std::array<std::array<float, 4>, 4>& view);
// An image the trails keep (made again when its format or size changed; storage images rest in the general layout).
bool ensure_trail_image(Backend& state, Image* image, VkFormat format, Extent extent, VkImageUsageFlags usage);
// Bytes into the staging ring; the regions copied into `image` (moved to be written, then back to its resting layout).
bool copy_into_image(Backend& state, Image* image, const std::vector<uint8_t>& bytes, std::span<VkBufferImageCopy> regions);
VkBufferImageCopy image_region(VkDeviceSize offset, int32_t x, int32_t y, uint32_t width, uint32_t height);

} // namespace render::vulkan
