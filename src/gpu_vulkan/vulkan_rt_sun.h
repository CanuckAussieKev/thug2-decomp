#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the sun, its glare and the bloom (docs/design/vulkan.md "The sun, its glare and the sky"; [Graphics]
// rt_sun, rt_bloom - gpu_vulkan.h's RtSunSettings). After a viewport's world is drawn and fogged (rt::world_view_drawn,
// rt::ViewOrder::kSun: after the volumetric fog - whose shafts are the light in the air: the screen-space rays this pass
// drew, rt_shafts, were retired by the volumetric light -, before tone mapping and the 2D):
//   0. the viewport's colour copied out (any sample count);
//   1. source (full resolution, two 16-bit float images - so the sun's light keeps its range with rt_hdr=0 too): the
//      frame's colour (a multisampled one's mean), and the light added to it - over the sky's pixels (a multisampled
//      one's share of sky samples) the sun's disc (rt_sun: kSunDiscRadius x rt_sun_size wide, kSunDiscIntensity x its
//      colour, limb-darkened, its edge a pixel soft) and the glow round it (the aureole: its colour x
//      (kSunAureoleNear e^(-angle / kSunAureoleNearWidth) + kSunAureoleWide e^(-angle / kSunAureoleWideWidth)));
//   2. bloom (rt_bloom, or rt_sun for the sun's own light): what is spread, X, is kSunGlareBloom x the added light (+
//      rt_bloom_strength x the frame's colour with rt_bloom, unless the game's own bloom is on -
//      rt::ViewDrawn::game_bloom); a chain of half-size images down (Jimenez's 13-tap filter, the weights summing to
//      1: each level holds the same light), then back up (a 3 x 3 tent,
//      each level's weight added: kSunBloomLevels levels at most, equal weights summing to 1) - B, a blur of X keeping
//      its light;
//   3. composite (a full-screen pass over the colour target, alpha kept): colour = frame + light + B - X - the light
//      moved, never made: the picture's sum is unchanged by the bloom. Every sample of a multisampled target gets the pixel's value. With an
//      8-bit target (rt_hdr=0) the target clips the result at 1 as the original's draws do; with rt_hdr=1 the tone
//      mapping that follows maps it.
// rt_sun=1 also tells render_pc to leave out the original's sun sprite, corona and lens flare (rt::set_sun_glare_replaced)
// while this pass draws a sun; a level without one (indoors) keeps the original's. The sun: vulkan_rt_sky.h's
// frame_sun (the `rt sun` override, else the level's baked sun), its colour sky_colours (rt_sun_colour or the model).
namespace render::vulkan {

struct Backend;

inline constexpr double kSunDiscRadius = 0.0093;      // radians (twice the real sun's: it reads as the sun on a screen)
inline constexpr float kSunDiscIntensity = 24.0F;     // x the sun's colour (the game's white is 1)
inline constexpr float kSunAureoleNear = 0.5F;
inline constexpr float kSunAureoleNearWidth = 0.04F; // radians
inline constexpr float kSunAureoleWide = 0.15F;
inline constexpr float kSunAureoleWideWidth = 0.25F;
inline constexpr uint32_t kSunBloomLevels = 7;
inline constexpr uint32_t kSunBloomSmallest = 8; // a level's shorter side at least this
inline constexpr float kSunGlareBloom = 0.25F;   // the share of the sun's own light spread (its glare)
inline constexpr uint32_t kSunViewSlots = 4;

// The usage a depth surface of `format` gets besides being an attachment: VK_IMAGE_USAGE_SAMPLED_BIT while rt_sun or
// rt_bloom is on and the format can be sampled, else none (make_depth_surface).
[[nodiscard]] VkImageUsageFlags rt_sun_depth_usage(const Backend& state, VkFormat format);

// The passes over the device's bound targets for a viewport (the listener's work, also the check's). The caller holds
// the lock. False: skipped (off, nothing to draw, the targets can't be read - logged once).
bool record_rt_sun_pass(Backend& state, const rt::ViewDrawn& view);
[[nodiscard]] uint64_t rt_sun_passes();

// The passes' time on the device (timestamps between them; logged every 240 as "rt_sun: ..."): how many frames were
// timed and the milliseconds of each part - the colour's copy, source, bloom, composite.
struct RtSunTiming {
    uint64_t samples = 0;
    std::array<double, 4> milliseconds{};
};
[[nodiscard]] RtSunTiming rt_sun_timing();

// For the check: one of the pass's images for the viewport last drawn with `rect` read back (the frame's work finished
// first): width x height texels of 4 floats.
enum class RtSunImage : uint8_t {
    kBase,   // step 1: the frame's colour
    kLight,  // step 1: the light added
    kBloom,  // step 2: the chain's top (half resolution) - B before its last upsampling
    kBloomInput, // step 2: the first level down (half resolution), X filtered
};
struct RtSunTexels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 4>> texels;
};
bool read_rt_sun_image(Backend& state, const std::array<uint32_t, 4>& rect, RtSunImage which, RtSunTexels* out);

// The parameters the last pass was given (the check's reference): the sun's place on the screen (the target's pixels,
// centres at whole numbers), whether it is in front, a pixel's angle, the colour.
struct RtSunFrame {
    bool sun = false;
    std::array<float, 3> toward{};
    std::array<float, 3> colour{};
    std::array<float, 2> screen{};
    bool in_front = false;
    float pixel_angle = 0.0F;
    float disc_radius = 0.0F;
};
[[nodiscard]] RtSunFrame rt_sun_frame();

// The passes' objects and images released (the device's shutdown; the caller holds the lock).
void release_rt_sun(Backend& state);

} // namespace render::vulkan
