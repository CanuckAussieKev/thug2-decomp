// NOT ORIGINAL - the Vulkan Gpu's formats (vulkan_formats.h).
#include "gpu_vulkan/vulkan_formats.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"

namespace render::vulkan {

namespace {

constexpr uint32_t kBlockSide = 4;       // a BC / DXT block: 4x4 pixels
constexpr uint32_t kBc1BlockBytes = 8;   // DXT1
constexpr uint32_t kBc23BlockBytes = 16; // DXT3, DXT5
constexpr uint32_t kTexelBytes64 = 8; // (four 16-bit floats)
constexpr uint32_t kTexelBytes32 = 4;
constexpr uint32_t kTexelBytes16 = 2;
constexpr uint32_t kTexelBytes8 = 1;
constexpr uint32_t kOpaque = 0xFF000000U;
constexpr uint32_t kGreyStep = 0x00010101U; // one more in red, green and blue
// Direct3D 9's sampler values that gpu.h doesn't name (D3DTEXTUREADDRESS)
constexpr uint32_t kAddressMirror = 2;
constexpr uint32_t kAddressMirrorOnce = 5;
// a D3DCOLOR's alpha / colour bits, and the two thresholds of "nearest built-in border colour"
constexpr uint32_t kAlphaShift = 24;
constexpr uint32_t kHalf = 0x80;
constexpr uint32_t kChannelMask = 0xFF;
constexpr uint32_t kGreenShift = 8;
constexpr uint32_t kRedShift = 16;
constexpr uint32_t kSampleCounts = 64; // Vulkan's largest sample count

// 5- and 6-bit channels widened to the nearest 8-bit value (what sampling the 16-bit format reads, value / 31 or
// value / 63, as closely as 8 bits hold it; 0 and the largest value stay 0 and 255).
constexpr uint32_t kMask5 = 0x1F;
constexpr uint32_t kMask6 = 0x3F;
constexpr uint32_t kFull = 0xFF;
// where the channels of a 16-bit texel start
constexpr uint32_t kGreenAt = 5;
constexpr uint32_t kRedAt565 = 11;
constexpr uint32_t kRedAt1555 = 10;
constexpr uint32_t kAlphaAt1555 = 15;
constexpr uint8_t widen5(uint32_t value) { return static_cast<uint8_t>(((value * kFull) + (kMask5 / 2)) / kMask5); }
constexpr uint8_t widen6(uint32_t value) { return static_cast<uint8_t>(((value * kFull) + (kMask6 / 2)) / kMask6); }
constexpr uint32_t kByteBits = 8;

// One B8G8R8A8 texel written from its four channels.
void put_texel(std::span<uint8_t> texel, std::array<uint8_t, 4> bgra) { std::ranges::copy(bgra, texel.begin()); }

// A 16-bit word (little-endian) as B8G8R8A8.
std::array<uint8_t, 4> texel_565(uint32_t word)
{
    return {widen5(word & kMask5), widen6((word >> kGreenAt) & kMask6), widen5((word >> kRedAt565) & kMask5),
            static_cast<uint8_t>(kFull)};
}
std::array<uint8_t, 4> texel_1555(uint32_t word, bool alpha_bit)
{
    const bool opaque = !alpha_bit || (word >> kAlphaAt1555) != 0U;
    return {widen5(word & kMask5), widen5((word >> kGreenAt) & kMask5), widen5((word >> kRedAt1555) & kMask5),
            static_cast<uint8_t>(opaque ? kFull : 0)};
}

// DXT1 / DXT3 / DXT5 as BC1 (its RGBA variant: DXT1's one-bit alpha) / BC2 / BC3.
VkFormat block_format(uint32_t d3d_format)
{
    switch (d3d_format) {
    case gpu_value::kFormatDxt1:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case gpu_value::kFormatDxt3:
        return VK_FORMAT_BC2_UNORM_BLOCK;
    default:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    }
}

bool is_block_compressed(uint32_t d3d_format)
{
    return d3d_format == gpu_value::kFormatDxt1 || d3d_format == gpu_value::kFormatDxt3 ||
           d3d_format == gpu_value::kFormatDxt5;
}

} // namespace

std::optional<FormatPlan> plan_format(uint32_t d3d_format, const FormatSupport& support)
{
    switch (d3d_format) {
    case gpu_value::kFormatA8R8G8B8:
        return FormatPlan{.format = VK_FORMAT_B8G8R8A8_UNORM};
    case gpu_value::kFormatX8R8G8B8:
        return FormatPlan{.format = VK_FORMAT_B8G8R8A8_UNORM, .alpha_one = true};
    case gpu_value::kFormatR5G6B5:
        // (Direct3D's and Vulkan's R5G6B5 are the same word: red in the top five bits)
        if (support.r5g6b5) {
            return FormatPlan{.format = VK_FORMAT_R5G6B5_UNORM_PACK16};
        }
        return FormatPlan{.format = VK_FORMAT_B8G8R8A8_UNORM, .conversion = TexelConversion::kR5g6b5};
    case gpu_value::kFormatA1R5G5B5:
        if (support.a1r5g5b5) {
            return FormatPlan{.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16};
        }
        return FormatPlan{.format = VK_FORMAT_B8G8R8A8_UNORM, .conversion = TexelConversion::kA1r5g5b5};
    case gpu_value::kFormatX1R5G5B5:
        if (support.a1r5g5b5) {
            return FormatPlan{.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16, .alpha_one = true};
        }
        return FormatPlan{.format = VK_FORMAT_B8G8R8A8_UNORM, .conversion = TexelConversion::kX1r5g5b5};
    case gpu_value::kFormatP8:
        return FormatPlan{.format = VK_FORMAT_B8G8R8A8_UNORM, .conversion = TexelConversion::kPalette};
    case gpu_value::kFormatDxt1:
    case gpu_value::kFormatDxt3:
    case gpu_value::kFormatDxt5:
        if (!support.block_compressed) {
            return std::nullopt;
        }
        return FormatPlan{.format = block_format(d3d_format), .block_compressed = true};
    case gpu_value::kFormatD24S8:
    case gpu_value::kFormatD24X8:
        if (support.depth_stencil == VK_FORMAT_UNDEFINED) {
            return std::nullopt;
        }
        return FormatPlan{.format = support.depth_stencil,
                          .aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT};
    case gpu_value::kFormatD16:
        return FormatPlan{.format = VK_FORMAT_D16_UNORM, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT};
    // NOT ORIGINAL - HDR's floating-point targets (vulkan_hdr.h): the same 8 bytes a texel in both
    case gpu_value::kFormatA16B16G16R16F:
        return FormatPlan{.format = VK_FORMAT_R16G16B16A16_SFLOAT};
    case gpu_value::kFormatX16B16G16R16F:
        return FormatPlan{.format = VK_FORMAT_R16G16B16A16_SFLOAT, .alpha_one = true};
    default:
        return std::nullopt;
    }
}

uint32_t d3d_pixel_bytes(uint32_t d3d_format)
{
    switch (d3d_format) {
    case gpu_value::kFormatA8R8G8B8:
    case gpu_value::kFormatX8R8G8B8:
    case gpu_value::kFormatD24S8:
    case gpu_value::kFormatD24X8:
        return kTexelBytes32;
    case gpu_value::kFormatR5G6B5:
    case gpu_value::kFormatA1R5G5B5:
    case gpu_value::kFormatX1R5G5B5:
    case gpu_value::kFormatD16:
        return kTexelBytes16;
    case gpu_value::kFormatP8:
        return kTexelBytes8;
    case gpu_value::kFormatA16B16G16R16F: // (NOT ORIGINAL - HDR)
    case gpu_value::kFormatX16B16G16R16F:
        return kTexelBytes64;
    default:
        return 0;
    }
}

uint32_t d3d_block_bytes(uint32_t d3d_format)
{
    if (d3d_format == gpu_value::kFormatDxt1) {
        return kBc1BlockBytes;
    }
    return is_block_compressed(d3d_format) ? kBc23BlockBytes : 0;
}

LevelLayout d3d_level_layout(uint32_t d3d_format, Extent extent)
{
    if (is_block_compressed(d3d_format)) {
        const uint32_t across = (extent.width + kBlockSide - 1) / kBlockSide;
        const uint32_t down = (extent.height + kBlockSide - 1) / kBlockSide;
        return {.pitch = across * d3d_block_bytes(d3d_format), .rows = down};
    }
    return {.pitch = extent.width * d3d_pixel_bytes(d3d_format), .rows = extent.height};
}

LevelLayout image_level_layout(const FormatPlan& plan, uint32_t d3d_format, Extent extent)
{
    if (plan.conversion == TexelConversion::kCopy) {
        return d3d_level_layout(d3d_format, extent);
    }
    return {.pitch = extent.width * kTexelBytes32, .rows = extent.height};
}

Extent level_extent(Extent top, uint32_t level)
{
    return {.width = std::max(top.width >> level, 1U), .height = std::max(top.height >> level, 1U)};
}

uint32_t full_chain_levels(Extent top)
{
    return static_cast<uint32_t>(std::bit_width(std::max({top.width, top.height, 1U})));
}

const std::array<uint32_t, kPaletteEntries>& p8_palette()
{
    static const std::array<uint32_t, kPaletteEntries> kPalette = [] {
        std::array<uint32_t, kPaletteEntries> grey{};
        uint32_t colour = kOpaque;
        for (uint32_t& entry : grey) {
            entry = colour;
            colour += kGreyStep;
        }
        return grey;
    }();
    return kPalette;
}

void convert_level(TexelConversion conversion, Extent extent, LevelLayout layout, std::span<const uint8_t> source,
                   std::span<uint8_t> texels)
{
    if (conversion == TexelConversion::kCopy) {
        // (the same bytes: Direct3D's rows are already tightly packed in the formats the Gpu makes)
        const auto bytes = std::min<std::size_t>({source.size(), texels.size(), layout.bytes()});
        std::ranges::copy(source.first(bytes), texels.begin());
        return;
    }
    const uint32_t source_bytes = conversion == TexelConversion::kPalette ? kTexelBytes8 : kTexelBytes16;
    const std::array<uint32_t, kPaletteEntries>& palette = p8_palette();
    for (uint32_t line = 0; line < extent.height; ++line) {
        const std::span<const uint8_t> row = source.subspan(static_cast<std::size_t>(line) * layout.pitch,
                                                            static_cast<std::size_t>(extent.width) * source_bytes);
        const std::span<uint8_t> out = texels.subspan(static_cast<std::size_t>(line) * extent.width * kTexelBytes32,
                                                      static_cast<std::size_t>(extent.width) * kTexelBytes32);
        for (uint32_t column = 0; column < extent.width; ++column) {
            const std::span<uint8_t> texel =
                out.subspan(static_cast<std::size_t>(column) * kTexelBytes32, kTexelBytes32);
            if (conversion == TexelConversion::kPalette) {
                const uint32_t colour = core::at(palette, core::at(row, column));
                put_texel(texel, {static_cast<uint8_t>(colour & kChannelMask),
                                  static_cast<uint8_t>((colour >> kGreenShift) & kChannelMask),
                                  static_cast<uint8_t>((colour >> kRedShift) & kChannelMask),
                                  static_cast<uint8_t>(colour >> kAlphaShift)});
                continue;
            }
            const uint32_t word = static_cast<uint32_t>(core::at(row, column * kTexelBytes16)) |
                                  (static_cast<uint32_t>(core::at(row, (column * kTexelBytes16) + 1)) << kByteBits);
            switch (conversion) {
            case TexelConversion::kR5g6b5:
                put_texel(texel, texel_565(word));
                break;
            case TexelConversion::kA1r5g5b5:
                put_texel(texel, texel_1555(word, true));
                break;
            case TexelConversion::kX1r5g5b5:
                put_texel(texel, texel_1555(word, false));
                break;
            case TexelConversion::kCopy:
            case TexelConversion::kPalette:
                break;
            }
        }
    }
}

VkFilter filter_of(uint32_t d3d_filter)
{
    return d3d_filter >= gpu_value::kFilterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

VkSamplerMipmapMode mip_mode_of(uint32_t d3d_filter)
{
    return d3d_filter >= gpu_value::kFilterLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkSamplerAddressMode address_of(uint32_t d3d_address)
{
    switch (d3d_address) {
    case kAddressMirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case gpu_value::kAddressClamp:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case gpu_value::kAddressBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case kAddressMirrorOnce:
        // (core in Vulkan 1.2; the context asks for 1.3 where the loader has it - the game never uses it)
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    case gpu_value::kAddressWrap:
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkBorderColor border_of(uint32_t d3d_colour)
{
    if ((d3d_colour >> kAlphaShift) < kHalf) {
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
    const uint32_t sum = (d3d_colour & kChannelMask) + ((d3d_colour >> kGreenShift) & kChannelMask) +
                         ((d3d_colour >> kRedShift) & kChannelMask);
    return sum >= 3 * kHalf ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}

VkSampleCountFlagBits samples_of(uint32_t d3d_samples)
{
    if (d3d_samples <= 1 || d3d_samples > kSampleCounts) {
        return VK_SAMPLE_COUNT_1_BIT;
    }
    return static_cast<VkSampleCountFlagBits>(std::bit_floor(d3d_samples));
}

} // namespace render::vulkan
