// NOT ORIGINAL - texture texels decoded on the processor: see texel_decode.h.
#include "engine/gfx/texel_decode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "base/core/checked_index.h"

namespace render::texel {

namespace {

constexpr uint32_t kA8R8G8B8 = 21;
constexpr uint32_t kX8R8G8B8 = 22;
constexpr uint32_t kR5G6B5 = 23;
constexpr uint32_t kX1R5G5B5 = 24;
constexpr uint32_t kA1R5G5B5 = 25;
constexpr uint32_t kA4R4G4B4 = 26;
constexpr uint32_t kDxt1 = 0x31545844;
constexpr uint32_t kDxt3 = 0x33545844;
constexpr uint32_t kDxt5 = 0x35545844;
constexpr uint32_t kBlock = 4;
constexpr uint32_t kOpaque = 255;

uint16_t read16(std::span<const uint8_t> bytes, std::size_t at)
{
    uint16_t value = 0;
    std::memcpy(&value, bytes.subspan(at, sizeof(value)).data(), sizeof(value));
    return value;
}
uint32_t read32(std::span<const uint8_t> bytes, std::size_t at)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.subspan(at, sizeof(value)).data(), sizeof(value));
    return value;
}
uint8_t expand(uint32_t value, uint32_t bits) // an n-bit channel to 8 (its top bits repeated)
{
    const uint32_t shifted = value << (8U - bits);
    return static_cast<uint8_t>(shifted | (shifted >> bits));
}
std::array<uint8_t, 4> from565(uint16_t value)
{
    constexpr uint32_t kFive = 0x1F;
    constexpr uint32_t kSix = 0x3F;
    constexpr uint32_t kRedShift = 11;
    constexpr uint32_t kGreenShift = 5;
    return {expand((value >> kRedShift) & kFive, 5), expand((value >> kGreenShift) & kSix, 6), expand(value & kFive, 5),
            static_cast<uint8_t>(kOpaque)};
}

// A DXT colour block's 16 texels (`one_bit`: DXT1's - colour 3 transparent black where c0 <= c1).
void colour_block(std::span<const uint8_t> block, bool one_bit, std::array<std::array<uint8_t, 4>, 16>* out)
{
    const uint16_t c0 = read16(block, 0);
    const uint16_t c1 = read16(block, 2);
    std::array<std::array<uint8_t, 4>, 4> palette{from565(c0), from565(c1), {}, {}};
    const auto mix = [](uint8_t a, uint8_t b, int wa, int wb, int sum) {
        return static_cast<uint8_t>(((wa * a) + (wb * b)) / sum);
    };
    for (std::size_t k = 0; k < 3; ++k) {
        const uint8_t a = core::at(core::at(palette, 0), k);
        const uint8_t b = core::at(core::at(palette, 1), k);
        if (!one_bit || c0 > c1) {
            core::at(core::at(palette, 2), k) = mix(a, b, 2, 1, 3);
            core::at(core::at(palette, 3), k) = mix(a, b, 1, 2, 3);
        } else {
            core::at(core::at(palette, 2), k) = mix(a, b, 1, 1, 2);
            core::at(core::at(palette, 3), k) = 0;
        }
    }
    std::get<3>(core::at(palette, 2)) = static_cast<uint8_t>(kOpaque);
    std::get<3>(core::at(palette, 3)) = (!one_bit || c0 > c1) ? static_cast<uint8_t>(kOpaque) : uint8_t{0};
    const uint32_t bits = read32(block, 4);
    for (uint32_t i = 0; i < 16; ++i) {
        core::at(*out, i) = core::at(palette, (bits >> (2U * i)) & 3U);
    }
}

} // namespace

std::array<uint8_t, 4> Image::at(float u, float v) const
{
    if (texels.empty() || width == 0 || height == 0) {
        return {};
    }
    const float fu = u - std::floor(u);
    const float fv = v - std::floor(v);
    const auto x = std::min(width - 1, static_cast<uint32_t>(fu * static_cast<float>(width)));
    const auto y = std::min(height - 1, static_cast<uint32_t>(fv * static_cast<float>(height)));
    return core::at(texels, (std::size_t{y} * width) + x);
}

bool decodable(uint32_t format)
{
    switch (format) {
    case kA8R8G8B8:
    case kX8R8G8B8:
    case kR5G6B5:
    case kX1R5G5B5:
    case kA1R5G5B5:
    case kA4R4G4B4:
    case kDxt1:
    case kDxt3:
    case kDxt5:
        return true;
    default:
        return false;
    }
}

bool decode(uint32_t format, uint32_t width, uint32_t height, std::span<const uint8_t> bytes, uint32_t pitch, Image* out)
{
    *out = Image{};
    if (!decodable(format) || width == 0 || height == 0) {
        return false;
    }
    const bool block = format == kDxt1 || format == kDxt3 || format == kDxt5;
    const uint32_t rows = block ? (height + kBlock - 1) / kBlock : height;
    if (pitch == 0 || bytes.size() < static_cast<std::size_t>(rows) * pitch) {
        return false;
    }
    Image image;
    image.width = width;
    image.height = height;
    image.texels.assign(static_cast<std::size_t>(width) * height, {});
    if (block) {
        const uint32_t block_bytes = format == kDxt1 ? 8U : 16U;
        const uint32_t across = (width + kBlock - 1) / kBlock;
        if (static_cast<std::size_t>(across) * block_bytes > pitch) {
            return false;
        }
        for (uint32_t by = 0; by < rows; ++by) {
            for (uint32_t bx = 0; bx < across; ++bx) {
                const std::span<const uint8_t> whole =
                    bytes.subspan((static_cast<std::size_t>(by) * pitch) + (static_cast<std::size_t>(bx) * block_bytes), block_bytes);
                std::array<std::array<uint8_t, 4>, 16> texels{};
                colour_block(whole.subspan(block_bytes - 8U, 8U), format == kDxt1, &texels);
                if (format == kDxt3) {
                    for (uint32_t i = 0; i < 16; ++i) {
                        const uint32_t nibble = (core::at(whole, i / 2U) >> ((i % 2U) * 4U)) & 0xFU;
                        std::get<3>(core::at(texels, i)) = expand(nibble, 4);
                    }
                } else if (format == kDxt5) {
                    const uint8_t a0 = core::at(whole, 0);
                    const uint8_t a1 = core::at(whole, 1);
                    std::array<uint8_t, 8> alphas{a0, a1, 0, 0, 0, 0, 0, 0};
                    for (uint32_t k = 2; k < 8; ++k) {
                        if (a0 > a1) {
                            core::at(alphas, k) = static_cast<uint8_t>((((8U - k) * a0) + ((k - 1U) * a1)) / 7U);
                        } else if (k < 6) {
                            core::at(alphas, k) = static_cast<uint8_t>((((6U - k) * a0) + ((k - 1U) * a1)) / 5U);
                        } else {
                            core::at(alphas, k) = k == 6 ? uint8_t{0} : static_cast<uint8_t>(kOpaque);
                        }
                    }
                    uint64_t bits = 0;
                    for (uint32_t k = 0; k < 6; ++k) {
                        bits |= static_cast<uint64_t>(core::at(whole, 2U + k)) << (8U * k);
                    }
                    for (uint32_t i = 0; i < 16; ++i) {
                        std::get<3>(core::at(texels, i)) = core::at(alphas, static_cast<std::size_t>((bits >> (3U * i)) & 7U));
                    }
                }
                for (uint32_t i = 0; i < 16; ++i) {
                    const uint32_t x = (bx * kBlock) + (i % kBlock);
                    const uint32_t y = (by * kBlock) + (i / kBlock);
                    if (x < width && y < height) {
                        core::at(image.texels, (std::size_t{y} * width) + x) = core::at(texels, i);
                    }
                }
            }
        }
        *out = std::move(image);
        return true;
    }
    const uint32_t size = format == kA8R8G8B8 || format == kX8R8G8B8 ? 4U : 2U;
    if (static_cast<std::size_t>(width) * size > pitch) {
        return false;
    }
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * pitch) + (static_cast<std::size_t>(x) * size);
            std::array<uint8_t, 4> texel{};
            if (size == 4U) {
                const uint32_t value = read32(bytes, at); // (B, G, R, A in memory)
                texel = {static_cast<uint8_t>((value >> 16U) & 0xFFU), static_cast<uint8_t>((value >> 8U) & 0xFFU),
                         static_cast<uint8_t>(value & 0xFFU),
                         format == kA8R8G8B8 ? static_cast<uint8_t>(value >> 24U) : static_cast<uint8_t>(kOpaque)};
            } else {
                const uint16_t value = read16(bytes, at);
                if (format == kR5G6B5) {
                    texel = from565(value);
                } else if (format == kA4R4G4B4) {
                    texel = {expand((value >> 8U) & 0xFU, 4), expand((value >> 4U) & 0xFU, 4), expand(value & 0xFU, 4),
                             expand((value >> 12U) & 0xFU, 4)};
                } else {
                    texel = {expand((value >> 10U) & 0x1FU, 5), expand((value >> 5U) & 0x1FU, 5), expand(value & 0x1FU, 5),
                             format == kA1R5G5B5 ? ((value & 0x8000U) != 0 ? static_cast<uint8_t>(kOpaque) : uint8_t{0})
                                                 : static_cast<uint8_t>(kOpaque)};
                }
            }
            core::at(image.texels, (std::size_t{y} * width) + x) = texel;
        }
    }
    *out = std::move(image);
    return true;
}

} // namespace render::texel
