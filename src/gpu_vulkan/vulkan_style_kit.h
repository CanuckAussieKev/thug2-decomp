#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>
#include <volk.h>

#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_style_looks.h"

// NOT ORIGINAL - what the vhs, retro and watercolour styles' passes share (vulkan_style_looks.h): one descriptor layout
// (pushed descriptors), one pipeline layout with LookConstants as push constants, a linear sampler, the full-screen
// triangle, the viewport's colour copied out, the passes' timing; and their processor twins' helpers. Internal to the
// three styles' files.
namespace render::vulkan {

struct Backend;
struct Image;

namespace look_kit {

// The bindings: three sampled images (the linear sampler; texelFetch reads them unfiltered) and a compute pass's output.
inline constexpr uint32_t kBindingSource = 0; // the viewport's colour copied out (SHADER_READ_ONLY_OPTIMAL)
inline constexpr uint32_t kBindingA = 1;      // a work image (GENERAL)
inline constexpr uint32_t kBindingB = 2;      // another (GENERAL)
inline constexpr uint32_t kBindingOut = 3;    // a compute pass's output (storage, GENERAL)
inline constexpr uint32_t kBindingCount = 4;
inline constexpr uint32_t kGroupSide = 8;      // a compute pass's work group: 8 x 8
inline constexpr VkFormat kWorkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// The GLSL every pass starts with: the version, the push constants (LookConstants: rect, info, a - f) and the helpers -
// finite / finite3 (0 - 1, not a number: 0), hash / hash01 / hash2 (integer hashes: the twins' below), value_noise.
[[nodiscard]] const char* glsl_common();

// The shared objects (once); false names what failed.
bool ensure_common(Backend& state, std::string* error);
[[nodiscard]] VkPipeline make_compute(Backend& state, const std::string& source, std::string* error);
// A full-screen pass's pipeline for a colour target (colour written, alpha kept); the caller keeps it per format and
// samples (FragmentPipelines).
struct FragmentPipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
[[nodiscard]] VkPipeline fragment_pipeline(Backend& state, std::vector<FragmentPipeline>* made, const std::string& source,
                                           VkFormat format, VkSampleCountFlagBits samples, std::string* error);
void destroy_pipelines(Backend& state, std::vector<FragmentPipeline>* made);
void destroy_pipeline(Backend& state, VkPipeline* pipeline);

// A work image of kWorkFormat (storage, sampled, readable), made again when its size changed; it rests in GENERAL.
bool ensure_work_image(Backend& state, Image* image, Extent extent);
void destroy_image(Backend& state, Image* image);

// The images a pass reads and writes (null: not bound).
struct Bound {
    const Image* source = nullptr;
    const Image* a = nullptr;
    const Image* b = nullptr;
    const Image* out = nullptr;
};

// The pass begun: the viewport's colour (resolved when multisampled) copied out into the shared source image (made
// when needed), the work images' last reads finished, the timing begun, the constants pushed. Null when the source
// can't be made. `work` the style's work images.
[[nodiscard]] VkCommandBuffer begin(Backend& state, const LookTarget& target, const LookConstants& constants,
                                    std::initializer_list<Image*> work);
// The shared source image (the colour copied out; SHADER_READ_ONLY_OPTIMAL after begin).
[[nodiscard]] const Image* source();
void dispatch(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, uint32_t width, uint32_t height);
// A full-screen pass over the target's viewport.
void draw(VkCommandBuffer commands, VkPipeline pipeline, const Bound& bound, const LookTarget& target);
// The pass ended: the timing's second stamp (its result logged as `name`'s cost every 300), the target back to its
// resting layout, the images marked used, the backend's bound state forgotten.
void end(Backend& state, VkCommandBuffer commands, const LookTarget& target, const char* name,
         std::initializer_list<Image*> work);

void release_common(Backend& state);

// --- the processor twins' helpers ------------------------------------------------------------------------------------

[[nodiscard]] float finite(float value);
[[nodiscard]] LookRgb finite3(const LookRgb& colour);
[[nodiscard]] float as_half(float value); // a value as a 16-bit float stores it (rounded to the nearest)
[[nodiscard]] LookRgb as_half3(const LookRgb& colour);
[[nodiscard]] float smoothstep(float low, float high, float value);
[[nodiscard]] float mix(float from, float to, float part);
[[nodiscard]] LookRgb mix3(const LookRgb& from, const LookRgb& to, float part);
[[nodiscard]] uint32_t hash(uint32_t value);
[[nodiscard]] float hash01(uint32_t value);
[[nodiscard]] uint32_t hash2(int32_t column, int32_t row, uint32_t seed);
// A place on an image (pixels or texels: x across, y down).
struct LookPoint {
    float x = 0.0F;
    float y = 0.0F;
};
[[nodiscard]] float value_noise(LookPoint at, uint32_t seed);
// An image's texel, its coordinates clamped into it.
[[nodiscard]] const LookRgb& texel(const std::vector<LookRgb>& image, Extent size, int32_t column, int32_t row);
// The image read with the linear filter at `at` - texel units, texel centres at + 0.5, clamped to the edge.
[[nodiscard]] LookRgb bilinear(const std::vector<LookRgb>& image, Extent size, LookPoint at);
[[nodiscard]] Extent size_of(const LookConstants& constants);

// --- the styles (each style's file) ----------------------------------------------------------------------------------

// The style's passes for a view (the caller holds the lock; the settings are vulkan_style_looks.cpp's); false: nothing
// recorded (logged once when the passes can't be made).
bool record_vhs(Backend& state, const rt::ViewDrawn& view, const VhsSettings& settings, float strength,
                const LookTarget& target);
bool record_retro(Backend& state, const RetroSettings& settings, float strength, const LookTarget& target);
bool record_watercolour(Backend& state, const WatercolourSettings& settings, float strength, const LookTarget& target);
bool read_watercolour_paint(Backend& state, std::vector<uint16_t>* halves, Extent* size);
void release_vhs(Backend& state);
void release_retro(Backend& state);
void release_watercolour(Backend& state);
// A once-only report (Backend::reported) with its own key.
bool first_report(Backend& state, uint64_t what);

} // namespace look_kit

} // namespace render::vulkan
