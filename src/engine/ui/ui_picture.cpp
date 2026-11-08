// NOT ORIGINAL - a game picture as a texture for our UI layers (ui_backend.h: load_picture) - the new front end's
// loading screens and the level editor's level list.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/file/file_load.h"
#include "engine/gfx/hd_ui.h"
#include "engine/ui/ui_backend.h"
#include "platform/platform_system.h"

namespace frontend::ui_backend {

namespace {

constexpr uint32_t kMaxPictureSide = 8192;     // the pictures' limit, in texels
constexpr std::size_t kPictureHeaderBytes = 0x20; // the picture file's header before its texels
constexpr uint32_t kTrueColourBits = 32;       // the bit depths the pictures may have
constexpr uint32_t kPaletteBits = 8;
constexpr uint32_t kBitsPerByte = 8;
constexpr uint8_t kOpaqueAlpha = 0xFF;         // a full-bleed picture's alpha
constexpr uint32_t kFormatPalettized = 0x13;   // the header's formats: 8-bit palettized, 16-bit
constexpr uint32_t kFormat16 = 2;
constexpr uint32_t kFormatCountBelow = 0x10;   // (a format under this, not 0 or 2: the bit count itself)
constexpr std::size_t kDefaultPaletteBytes = 0x400;
constexpr std::size_t kTexelBytes = 4;

// BGRA texels and their size.
struct Texels {
    std::vector<uint8_t> bgra;
    uint32_t width{};
    uint32_t height{};
};

// `texels` made `shrink` times smaller each way: each texel the average of its block.
Texels shrunk(const Texels& texels, uint32_t shrink)
{
    Texels out{.bgra = {}, .width = texels.width / shrink, .height = texels.height / shrink};
    out.bgra.resize(static_cast<std::size_t>(out.width) * out.height * kTexelBytes);
    for (uint32_t row = 0; row < out.height; ++row) {
        for (uint32_t column = 0; column < out.width; ++column) {
            for (std::size_t channel = 0; channel < kTexelBytes; ++channel) {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < shrink; ++dy) {
                    for (uint32_t dx = 0; dx < shrink; ++dx) {
                        const std::size_t from_row = (static_cast<std::size_t>(row) * shrink) + dy;
                        const std::size_t from_column = (static_cast<std::size_t>(column) * shrink) + dx;
                        sum += core::at(texels.bgra, (((from_row * texels.width) + from_column) * kTexelBytes) + channel);
                    }
                }
                const std::size_t to = (static_cast<std::size_t>(row) * out.width) + column;
                core::at(out.bgra, (to * kTexelBytes) + channel) = static_cast<uint8_t>(sum / (shrink * shrink));
            }
        }
    }
    return out;
}

} // namespace

// The image file (the game's .img.xbx: a 32-byte header, a palette for 8-bit images, the texels - swizzled when both
// sides are powers of two; an upscaled loading screen may be DXT1 blocks - engine/gfx/hd_ui.h) as a texture.
Picture load_picture(const std::string& name, int shrink)
{
    const std::string path = "images\\" + name + ".img.xbx";
    file::Hdl file = file::unseal(path.c_str(), "rb");
    if (!file) {
        return {};
    }
    const uint32_t size = file::get_file_size(file);
    std::vector<uint8_t> data(size);
    const bool read = size > kPictureHeaderBytes && file::get_take_in(data.data(), static_cast<int>(size), 1, file) > 0;
    file::shut(file);
    if (!read) {
        return {};
    }
    auto u32 = [&](std::size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, std::span<const uint8_t>(data).subspan(at, 4).data(), 4);
        return v;
    };
    const uint32_t width = u32(8);
    const uint32_t height = u32(12);
    const uint32_t format = u32(16);
    const uint32_t marker = u32(20);
    const uint32_t palette_size = u32(28);
    if (width == 0 || height == 0 || width > kMaxPictureSide || height > kMaxPictureSide) {
        return {};
    }
    std::span<const uint8_t> texels = std::span<const uint8_t>(data).subspan(kPictureHeaderBytes);
    std::size_t left = data.size() - kPictureHeaderBytes;
    Picture picture;
    picture.width = static_cast<float>(width);
    picture.height = static_cast<float>(height);
    // (a loading screen's rows are stored bottom first: render::draw_loading_screen_picture puts v 1 at the screen's top)
    picture.bottom_up = true;
    if (format == render::hd_ui::kFormatDxt1 && render::hd_ui::image_scale(marker) != 1 && width % 4 == 0 &&
        height % 4 == 0) {
        const int row_bytes = static_cast<int>(width / 4 * 8);
        const int rows = static_cast<int>(height / 4);
        if (left < static_cast<std::size_t>(row_bytes) * rows) {
            return {};
        }
        picture.texture = create_texture(static_cast<int>(width), static_cast<int>(height), PixelLayout::kDxt1,
                                         texels.data(), {.row_bytes_ = row_bytes, .rows = rows});
        return picture;
    }
    // (the bits per texel: format 0 true colour, 2 16-bit, 0x13 8-bit palettized; else the format is the count)
    const uint32_t other_bits = format == kFormatPalettized ? kPaletteBits : format;
    const uint32_t not_true_colour_bits = format == kFormat16 ? 16 : other_bits;
    const uint32_t bits = format == 0 ? kTrueColourBits : not_true_colour_bits;
    const bool palettized = format == kFormatPalettized || (format != 0 && format != kFormat16 && format < kFormatCountBelow);
    if (bits != kTrueColourBits && !(palettized && bits == kPaletteBits)) {
        return {}; // (16-bit and 4-bit images: no loading screen is one)
    }
    // (the palette: the colours index the rest of the file, as the original's read did - an index past a short
    // palette reads the texels that follow)
    std::span<const uint8_t> palette;
    if (palettized) {
        const std::size_t palette_bytes = (palette_size != 0U) ? palette_size : kDefaultPaletteBytes;
        if (left < palette_bytes) {
            return {};
        }
        palette = texels;
        texels = texels.subspan(palette_bytes);
        left -= palette_bytes;
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (left < count * (bits / kBitsPerByte)) {
        return {};
    }
    const bool powers = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    std::vector<uint8_t> plain;
    std::span<const uint8_t> rows_data = texels;
    if (powers) {
        plain.resize(count * (bits / kBitsPerByte));
        os_layer::unswizzle(plain, texels.first(plain.size()),
                            {.width = static_cast<int>(width),
                             .height = static_cast<int>(height),
                             .bytes_per_texel_ = bits / kBitsPerByte});
        rows_data = plain;
    }
    std::vector<uint8_t> bgra(count * kTexelBytes);
    for (std::size_t i = 0; i < count; ++i) {
        const std::span<const uint8_t> px =
            palettized ? palette.subspan(std::size_t{core::at(rows_data, i)} * kTexelBytes) : rows_data.subspan(i * kTexelBytes);
        core::at(bgra, (i * kTexelBytes) + 0) = core::at(px, 0);
        core::at(bgra, (i * kTexelBytes) + 1) = core::at(px, 1);
        core::at(bgra, (i * kTexelBytes) + 2) = core::at(px, 2);
        // (a full-bleed picture: opaque - the files' alpha is the PS2's half scale)
        core::at(bgra, (i * kTexelBytes) + 3) = kOpaqueAlpha;
    }
    Texels made{.bgra = std::move(bgra), .width = width, .height = height};
    if (shrink > 1 && width / static_cast<uint32_t>(shrink) > 0 && height / static_cast<uint32_t>(shrink) > 0) {
        made = shrunk(made, static_cast<uint32_t>(shrink));
        picture.width = static_cast<float>(made.width);
        picture.height = static_cast<float>(made.height);
    }
    picture.texture = create_texture(static_cast<int>(made.width), static_cast<int>(made.height), PixelLayout::kBgra32,
                                     made.bgra.data(),
                                     {.row_bytes_ = static_cast<int>(made.width * kTexelBytes), .rows = static_cast<int>(made.height)});
    return picture;
}

} // namespace frontend::ui_backend
