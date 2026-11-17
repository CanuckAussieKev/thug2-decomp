#pragma once

#include <array>
#include <string>
#include <volk.h>

#include "engine/gfx/rt_brdf.h"
#include "engine/gfx/rt_light_field.h"

// NOT ORIGINAL - round W2: the light store round the camera on the device (engine/gfx/light_window.h; docs/design/
// vulkan.md "Round W2: grass and clutter lit by the one light") - one first-order probe a cell of the published light
// field (the sky's, the bounce's and the lamps' light a surface facing n receives), one RGBA16F 2D image (the three
// channels' planes and the layers stacked), filled on a worker when the camera has moved an eighth of the window from its
// centre or the field changed, copied up when ready. The passes that draw things the level's vertices don't carry - the
// grass, the clutter - read a point's light through light_window_glsl. Until a window is up (or without a field) the
// probe at the camera (the field's, or the clock's open sky) stands for every place: SheetLighting's `fallback`.
namespace render::vulkan {

struct Backend;

// The light the sheets are lit by this frame (a block's members: the shaders read them through light_window_glsl).
struct SheetLighting {
    bool window = false;            // a window on the device
    VkDescriptorImageInfo image{};  // the window (or the blank texture: bind it anyway)
    std::array<float, 4> origin{};  // xyz the window's first corner (world units), w 1 with a window
    std::array<std::array<float, 4>, 3> fallback{}; // per channel (c0, c1): the probe used without a window
    std::array<float, 4> toward{};  // toward the sun or the moon (w 1 when it lights)
    std::array<float, 4> sun{};     // its light per unit of N.L (linear, the frame's unit)
};

// The window brought round `camera` and this frame's lighting. The caller holds the lock; records outside a pass.
[[nodiscard]] SheetLighting update_light_window(Backend& state, const std::array<float, 3>& camera);
// The probe at a point without a window: the field's at the camera, else the clock's open sky (a white diffuser facing
// n under the sky's light S receives S (1 + n.y) / 2), else the reference day's (1 on a level surface).
[[nodiscard]] rt::ProbeL1 fallback_probe(const std::array<float, 3>& camera);

// GLSL: `uniform sampler2D <sampler>`; `block` the block whose members `<block>.light_origin`, `.light_fallback[3]`
// hold SheetLighting's origin and fallback. Declares
//   void window_probe(vec3 world, out vec4 r, out vec4 g, out vec4 b)   - the probe (c0, c1) per channel
//   vec3 probe_light(vec4 r, vec4 g, vec4 b, vec3 n)                    - what a surface facing n receives (at least 0)
[[nodiscard]] std::string light_window_glsl(const char* sampler, const char* block);

// Whether a window is on the device now (the checks wait for the worker's first).
[[nodiscard]] bool light_window_on();

void release_light_window(Backend& state);

} // namespace render::vulkan
