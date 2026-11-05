#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <volk.h>

// NOT ORIGINAL - the Vulkan Gpu's formats (vulkan_formats.cpp; docs/design/vulkan.md, V6): Direct3D 9's texture, surface
// and depth formats (gpu_value::kFormat*, the numbers render::Gpu passes) as Vulkan formats, the texels converted where
// Vulkan has no format with the same bytes, Direct3D 9's level layouts (what a lock hands the caller), and the sampler
// states' values as Vulkan's. No Vulkan object is made here: these are the rules the resources follow.
namespace render::vulkan {

// How the bytes the caller writes into a locked level become the image's texels.
enum class TexelConversion : uint8_t {
    kCopy,     // the same bytes (A8R8G8B8 / X8R8G8B8 as B8G8R8A8, the 16-bit formats where the device has them, BC)
    kPalette,  // P8: 8-bit indices through the palette (p8_palette) to B8G8R8A8
    kR5g6b5,   // R5G6B5 widened to B8G8R8A8, each channel to its nearest 8-bit value (a device without the format)
    kA1r5g5b5, // A1R5G5B5 widened to B8G8R8A8, its alpha bit 0 or 255
    kX1r5g5b5, // X1R5G5B5 widened to B8G8R8A8, opaque
};

// What the device offers of the formats with a choice (the physical device's format properties).
struct FormatSupport {
    bool r5g6b5 = false;                          // VK_FORMAT_R5G6B5_UNORM_PACK16 sampled, filtered
    bool a1r5g5b5 = false;                        // VK_FORMAT_A1R5G5B5_UNORM_PACK16 sampled, filtered
    bool block_compressed = false;                // BC1 / BC2 / BC3 sampled (textureCompressionBC)
    VkFormat depth_stencil = VK_FORMAT_UNDEFINED; // D24_UNORM_S8_UINT, else D32_SFLOAT_S8_UINT (the AMD iGPU here)
};

// A Direct3D 9 format as the image that holds it.
struct FormatPlan {
    VkFormat format = VK_FORMAT_UNDEFINED;
    TexelConversion conversion = TexelConversion::kCopy;
    bool alpha_one = false; // sampled with alpha 1 (the X formats: their fourth channel is not alpha)
    bool block_compressed = false;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

// The image for a Direct3D 9 format on this device; none for a format the Gpu doesn't make (or BC without BC support).
std::optional<FormatPlan> plan_format(uint32_t d3d_format, const FormatSupport& support);

// A level's size in pixels.
struct Extent {
    uint32_t width = 0;
    uint32_t height = 0;
};

// A level's bytes as Direct3D 9 lays them out for a lock: rows of `pitch` bytes (a block-compressed format's rows are
// rows of 4x4 blocks), `rows` of them.
struct LevelLayout {
    uint32_t pitch = 0;
    uint32_t rows = 0;
    [[nodiscard]] uint32_t bytes() const { return pitch * rows; }
};
LevelLayout d3d_level_layout(uint32_t d3d_format, Extent extent);
// The same level as the image's texels (after the conversion), tightly packed for a buffer-to-image copy.
LevelLayout image_level_layout(const FormatPlan& plan, uint32_t d3d_format, Extent extent);
// The bytes a pixel takes in Direct3D 9's layout (0 for the block-compressed formats: 4x4 blocks), and a block's.
uint32_t d3d_pixel_bytes(uint32_t d3d_format);
uint32_t d3d_block_bytes(uint32_t d3d_format);

// A level's size: the top level halved `level` times, never below 1.
Extent level_extent(Extent top, uint32_t level);
// Direct3D 9's whole chain for a texture made with 0 levels: down to 1x1.
uint32_t full_chain_levels(Extent top);

// The palette a P8 texture's indices go through (B8G8R8A8 words: 0xAARRGGBB): grey ramp, index n = (n, n, n), opaque.
// The game never sets a Direct3D palette, so the original showed the driver's own (Direct3D 9 drivers of today don't make
// P8 textures at all): a palette here is a choice, kept in one place.
inline constexpr std::size_t kPaletteEntries = 256;
const std::array<uint32_t, kPaletteEntries>& p8_palette();

// One level converted from Direct3D 9's bytes (`source`, rows of `layout.pitch`) into the image's (`texels`, tightly
// packed, image_level_layout's size). kCopy copies row by row.
void convert_level(TexelConversion conversion, Extent extent, LevelLayout layout, std::span<const uint8_t> source,
                   std::span<uint8_t> texels);

// Direct3D 9's sampler values as Vulkan's.
VkFilter filter_of(uint32_t d3d_filter);               // POINT / NONE: nearest; LINEAR, ANISOTROPIC: linear
VkSamplerMipmapMode mip_mode_of(uint32_t d3d_filter);  // POINT / NONE: nearest; LINEAR: linear
VkSamplerAddressMode address_of(uint32_t d3d_address); // WRAP, MIRROR, CLAMP, BORDER, MIRRORONCE
// The border colour (D3DCOLOR) as the nearest of Vulkan's three built-in ones (the game only uses 0, transparent black).
VkBorderColor border_of(uint32_t d3d_colour);
// D3DMULTISAMPLE_TYPE (0 none, else the sample count) as Vulkan's sample count.
VkSampleCountFlagBits samples_of(uint32_t d3d_samples);

} // namespace render::vulkan
