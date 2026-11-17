#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <volk.h>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/gpu_vulkan.h"

// NOT ORIGINAL - rain (docs/design/vulkan.md "Rain"; [Graphics] rain - gpu_vulkan.h's RainSettings): the Vulkan Gpu's
// side of engine/gfx/rain.h. One weather state (the rate, the drops it makes, the wind, the water on the ground) drawn in
// four places, all reading the precipitation occlusion map (vulkan_precip.h) for where the rain reaches:
//   1. The wet ground (the world's draws): a draw render_pc marks as the ground or a scene object gets kTrailWet - its
//      material wet before its light (ff_shaders.cpp kWetBlock, rain::surface_glsl: the albedo darkened by Lekner and
//      Dorf's law where water fills the pores, lies as a film or stands in a puddle; the roughness toward still water's
//      there; rings from the drops on the puddles). WetUniforms at kWetUniformsBinding, the map at kWetMapBinding.
//   2. The wet mask (opaque_drawn, rt::ViewOrder::kRain: after the decals, before the reflections): the share of each
//      pixel's surface under a film or a puddle - the same function - over the decals' puddles (the most of either):
//      the ray-traced reflections' trace reads it (rain_wet_mask): there the ground reflects as water does.
//   3. The drops (world_drawn, the same order: before the fog and the tone mapping): kRainNearDrops in a box round the
//      camera and kRainFarDrops in a larger one round it, each a drop of the rate's size distribution (Marshall-Palmer
//      from kRainLeastDrawn mm) falling at its own terminal speed with the wind, wrapped into its box in the world's frame
//      (the camera moves through the rain); none where the rain doesn't reach. A compute pass places and lights each
//      (light now in the frame's linear unit: the scene behind it - a drop refracts what lies behind it over a wide
//      angle, Garg and Nayar - from a blurred copy of the frame; the sun's or moon's light scattered by a water sphere -
//      Henyey-Greenstein with its asymmetry kRainDropPhaseG - where a ray reaches it; the view's lamps the same, by their
//      falloff), then each is drawn as a streak: its move on the screen over the camera's shutter (the snow's camera),
//      widened by the defocus, its coverage spread over the streak and times the drops it stands for (the true number
//      in its box over those drawn). With the upscaler's motion: the reactive mask, and its own motion where it covers
//      enough of the pixel.
//   4. The splashes (the same pass): kRainSplashes impacts on whatever the rain meets within kRainSplashReach - the
//      occlusion map's first hit -, each a crown growing over kRainSplashLife, the impacts of drops of kRainSplashLeast mm
//      and more at their true rate (each drawn standing for its share), lit as the drops.
namespace render::vulkan {

struct Backend;
struct DrawCall;
struct Image;

inline constexpr uint32_t kRainNearDrops = 131072;
inline constexpr uint32_t kRainFarDrops = 65536;
inline constexpr float kRainNearBox = 480.0F;    // world units a side (12 m) ...
inline constexpr float kRainNearHeight = 320.0F; // ... and high (8 m: 0.6 below the eye, 0.4 above)
inline constexpr float kRainFarBox = 1600.0F;    // (40 m; the near box's drops left out of it)
inline constexpr float kRainFarHeight = 800.0F;
inline constexpr double kRainLeastDrawn = 0.5; // mm: smaller drops are the air's extinction alone
inline constexpr float kRainNearClear = 12.0F; // no drop drawn nearer the lens than this (units: 30 cm)
inline constexpr uint32_t kRainSplashes = 16384;
inline constexpr float kRainSplashReach = 400.0F; // units round the camera's line of fall (10 m)
inline constexpr double kRainSplashLeast = 1.0;   // mm: the drops whose splash is seen
inline constexpr double kRainSplashLife = 0.05;   // s: a crown's rise and fall
inline constexpr float kRainSplashSpread = 8.0F;  // a crown's radius at its widest over the drop's
inline constexpr uint32_t kRainMostLamps = 16;
inline constexpr float kRainDropPhaseG = 0.87F; // a water sphere's asymmetry (geometric optics; Hansen and Travis 1974)
inline constexpr float kRainEnvironmentPixels =
    32.0F;                                      // the blurred frame's width a drop reads (texels: its wide angle)
inline constexpr float kRainMotionShare = 0.3F; // a drop covering this share of a pixel: its own motion there

// A draw's wet descriptors (the world's draws with kTrailWet).
struct WetDescriptors {
    bool on = false;
    VkDescriptorBufferInfo block{};
    VkDescriptorImageInfo map{};
};
// The draw's key with the wet ground where it takes it (rain on, a draw render_pc marked as the ground, not water). The
// caller holds the lock.
void rain_key(Backend& state, const GpuStateBlock& block, const DrawCall& call, ShaderKey* shaders,
              WetDescriptors* out);

// The listener's work (also the check's). The caller holds the lock.
void begin_rain_view(Backend& state, const rt::ViewDrawn& view);
bool record_rain_wet_mask(Backend& state, const rt::ViewDrawn& view);
bool record_rain_air(Backend& state, const rt::ViewDrawn& view);
// The wet mask drawn this frame for `rect` (R8, SHADER_READ_ONLY_OPTIMAL; the decals' puddles in it too), or null.
[[nodiscard]] const Image* rain_wet_mask(const Backend& state, const std::array<uint32_t, 4>& rect);

struct RainStats {
    uint64_t air_passes = 0;
    uint64_t wet_masks = 0;
    uint64_t wet_draws = 0;
    uint32_t last_drops = 0;
    uint32_t last_splashes = 0;
    uint32_t last_lamps = 0;
    bool last_traced = false;
    float near_weight = 0.0F; // the drops a near drop drawn stands for
    float far_weight = 0.0F;
    float splash_weight = 0.0F;
};
[[nodiscard]] RainStats rain_stats();

// A drop's place (the drops' compute shader's twin - the check's): the rain's clock (wrapped), its drift downwind.
struct RainClock {
    double clock = 0.0;
    double drift = 0.0;
};
struct DropPlace {
    std::array<float, 3> world{};
    float diameter = 0.0F; // mm
    float speed = 0.0F;    // units a second
    bool in_near_box = false;
    bool drawn = true; // (a far drop inside the near box: not drawn)
};
[[nodiscard]] DropPlace drop_place(uint32_t index, const RainClock& clock, const std::array<float, 3>& camera,
                                   double rate, const std::array<float, 2>& downwind);
// (the check's) Every pass's shaders and pipelines made for a colour target's format (and the depth's; with the motion
// attachment or not): whether they were - why not in `error` (the shader compiler's messages). The caller holds the lock.
bool make_rain_passes(Backend& state, VkFormat colour, VkFormat depth, bool motion, std::string* error);
// The drops' compute output read back (the check's): per drop the world place (xyz) and visible (w 1 / 0).
bool read_rain_drops(Backend& state, std::vector<std::array<float, 4>>* drops);

void release_rain(Backend& state);

} // namespace render::vulkan
