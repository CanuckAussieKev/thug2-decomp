// The PC texture and texture dictionary: see pc_texture.h. The colour conversions follow the original's x87 code:
// double precision where it keeps values in registers, floats where it stores them.

#include "render_pc/pc_texture.h"
#include "engine/gfx/hsv_colour.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/texture_dict.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>
#include "engine/memory/manager.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/file/file_load.h"
#include "render_pc/bitmap_texture.h"
#include "engine/lists/key_map.h"
#include "base/maths/maths.h"
#include "formats/texture_format.h"
#include "formats/byte_stream.h"
#include "platform/crt.h"
#include <gsl/pointers>
#include <array>
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/cursor.h"
#include "base/core/span_copy.h"
#include "base/core/packed_colour.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "base/core/checksums.h"
#include "base/maths/vec2.h"
#include "base/maths/rect.h"
#include <span>
#include <algorithm>
#include <gsl/assert>

namespace render {

constexpr std::size_t kPathBytes = 0x100;
constexpr int kDepth8 = 8;
constexpr int kDepth16 = 16;
constexpr int kDepth32 = 32;
constexpr int kPalettedFlag = 0x20;
constexpr int kBitsPerByte = 8;
// Sampling positions are fixed point with 10 fraction bits.
constexpr int kSubpixelBits = 10;
constexpr int kSubpixelMask = 0x3FF;
constexpr int kSubpixelOne = 0x400;
constexpr float kSubpixelScale = 1024.0F;
constexpr int kIntMax = 0x7FFFFFFF;
constexpr int kByteMax = 0xFF;
constexpr int kHalfAlphaShift = 7;
constexpr float kHalf = 0.5F;
constexpr float kByteMaxFloat = 255.0F;
constexpr float kHueFull = 360.0F;
constexpr int kMaxSizeBits = 12;
constexpr int kCompressionDxt5 = 5;
constexpr float kHueSectorGreenBlue = 2.0F;
constexpr float kHueSectorBlueRed = 4.0F;

namespace {

TextureEdit* get_edit(const PcTexture* texture) { return static_cast<TextureEdit*>(texture->image()); }

// NOT ORIGINAL - an edit's pixel buffers as runs of the texture's width x height (the size begin_edit allocates them
// at); empty when the texture is not being edited or the buffer is not there.
std::span<uint32_t> run_of(const PcTexture* texture, uint32_t* pixels)
{
    if (((texture->flag_bits() & 1) == 0U) || !pixels) {
        return {};
    }
    const TextureRef* image = get_edit(texture)->texture;
    return {pixels, static_cast<size_t>(image->width) * image->height};
}
std::span<uint32_t> edit_pixels(const PcTexture* texture)
{
    return ((texture->flag_bits() & 1) != 0U) ? run_of(texture, get_edit(texture)->pixels) : std::span<uint32_t>();
}
std::span<uint32_t> edit_original(const PcTexture* texture)
{
    return ((texture->flag_bits() & 1) != 0U) ? run_of(texture, get_edit(texture)->original) : std::span<uint32_t>();
}

gsl::owner<PcTexture*> alloc_pc_texture() { return new PcTexture; }

} // namespace

TextureRef* image_of(const BitmapTexture* texture) { return core::down_cast<const PcTexture>(texture)->get_texture(); }

TextureRef* PcTexture::get_texture() const
{
    return ((flag_bits() & 1) != 0U) ? get_edit(this)->texture : static_cast<TextureRef*>(image_);
}

// 0x004C7EF0
void PcTexture::set_texture(TextureRef* texture)
{
    image_ = texture;
    set_checksum(texture->checksum);
}

// 0x004C9D00
PcTexture::~PcTexture()
{
    if ((flag_bits() & 1) != 0U) {
        end_edit();
    }
    if (auto* texture = static_cast<TextureRef*>(image_)) {
        texture->~TextureRef();
        memory::Hub::dealloc(texture);
    }
}

// 0x004C7F00
bool PcTexture::ingest_file(const char* name, int /*unused1*/, int /*unused2*/)
{
    std::array<char, kPathBytes> path{};
    const size_t length = std::strlen(name); // (unchecked, as the original)
    core::copy_into(std::span<const char>(name, length), std::span<char>(path)); // (its terminator with the extension)
    core::copy_into(std::span<const char>(".img.xbx"), std::span<char>(path).subspan(length));
    gsl::owner<TextureRef*> image = read_texture_file(path.data()); // (the texture's)
    image_ = image;
    return image_ != nullptr;
}

// 0x004C7F80
bool PcTexture::load_memory(const uint8_t* data, uint32_t size, uint32_t /*unused*/, uint32_t /*unused*/,
                            uint32_t /*unused*/)
{
    // (not original: the size passed on as the reader's bound)
    gsl::owner<TextureRef*> image = load_texture_memory(data, size); // (the texture's)
    image_ = image;
    return image_ != nullptr;
}

// 0x004C9EC0
bool PcTexture::write_back(int /*unused1*/, const void* /*unused2*/)
{
    const TextureRef* image = get_texture();
    const std::span<uint32_t> texels(static_cast<uint32_t*>(image->get_lock()),
                                     static_cast<size_t>(image->width) * image->height);
    core::Cursor<const uint32_t> pixels{edit_pixels(this)}; // (empty without an edit: the original read null)
    const uint32_t width = fetch_breadth();
    const uint32_t height = fetch_tallness();
    core::Cursor<uint32_t> row(texels, static_cast<std::ptrdiff_t>(height - 1) * static_cast<std::ptrdiff_t>(width));
    for (uint32_t row_y = height; row_y != 0U; --row_y) {
        for (uint32_t column_x = 0; column_x < width; ++column_x) {
            row[column_x] = *pixels++;
        }
        row -= width;
    }
    get_texture()->get_unlock();
    return true;
}

// 0x004C98B0
bool PcTexture::end_edit()
{
    // (the edit's fields read whether or not it is one, as the original)
    auto* edit = get_edit(this);
    void* texture = ((flag_bits() & 1) != 0U) ? edit->texture : image_;
    if (edit->pixels) {
        memory::Hub::dealloc(edit->pixels);
        get_edit(this)->pixels = nullptr;
    }
    if (get_edit(this)->original) {
        memory::Hub::dealloc(get_edit(this)->original);
        get_edit(this)->original = nullptr;
    }
    if (TextureRef* kept = get_edit(this)->kept) {
        kept->~TextureRef();
        memory::Hub::dealloc(kept);
        get_edit(this)->kept = nullptr;
    }
    memory::Hub::dealloc(image_);
    image_ = texture;
    flag_bits() &= ~1U;
    return true;
}

// 0x004C7FB0
uint16_t PcTexture::fetch_breadth() const
{
    const TextureRef* texture = get_texture();
    return texture ? texture->image_width_ : 0;
}

// 0x004C7FE0
uint16_t PcTexture::fetch_tallness() const
{
    const TextureRef* texture = get_texture();
    return texture ? texture->image_height_ : 0;
}

// 0x004C8010
uint8_t PcTexture::get_depth()
{
    const TextureRef* texture = get_texture();
    return texture ? texture->depth : 0;
}

// 0x004C8030
uint8_t PcTexture::get_unknown_e()
{
    const TextureRef* texture = get_texture();
    return texture ? texture->unknowne_ : 0;
}

// 0x004C8050
uint8_t PcTexture::get_unknown_c() { return unknownc_; }

// 0x004C8060
uint8_t PcTexture::get_unknown_d() { return unknownd_; }

// 0x004C8450
PcTexture::PcTexture(const PcTexture* source) : unknownc_(source->unknownc_)
{
    flag_bits() = source->flag_bits();
    TextureRef* copy = nullptr;
    if (source->get_texture()) {
        void* memory = memory::alloc(sizeof(TextureRef));
        copy = memory ? static_cast<TextureRef*>(memory)->construct_copy(source->get_texture()) : nullptr;
    }
    // (the source's size through its virtual functions)
    const uint32_t width = source->fetch_breadth();
    const uint32_t height = source->fetch_tallness();
    if ((source->flag_bits() & 1) == 0U) {
        image_ = copy;
        return;
    }
    auto* edit = static_cast<TextureEdit*>(memory::alloc(sizeof(TextureEdit)));
    if (edit) { // (no check after: written through null, as the original)
        *edit = {};
    }
    image_ = edit;
    Expects(edit != nullptr);
    edit->texture = copy;
    const TextureEdit* from = get_edit(source);
    if (from->original) {
        const uint32_t bytes = height * width * 4;
        edit->original = static_cast<uint32_t*>(memory::alloc(bytes));
        std::memcpy(edit->original, from->original, bytes);
    } else {
        edit->original = nullptr;
    }
    // (its own pixels tested, not the source's: always null here, so never copied - as the original)
    if (edit->pixels) {
        edit->pixels = static_cast<uint32_t*>(memory::alloc(height * height * 4));
        std::memcpy(edit->pixels, from->pixels, width * height * 4);
    } else {
        edit->pixels = nullptr;
    }
    if (from->kept) {
        void* memory = memory::alloc(sizeof(TextureRef));
        edit->kept = memory ? static_cast<TextureRef*>(memory)->construct_copy(from->kept) : nullptr;
    } else {
        edit->kept = nullptr;
    }
}

// 0x004C8830
bool PcTexture::create_image(checksums::Name texture_name, ImageFormat image_format, int /*unused*/, int /*unused*/)
{
    const auto width = image_format.width;
    const auto height = image_format.height;
    auto bits_per_pixel = image_format.bits_per_pixel_;
    const uint32_t new_checksum = texture_name.value();
    const TextureRef* old = get_texture();
    if (old && old->texture) { // (released, the old texture itself kept)
        gpu().release_texture(old->texture);
    }
    void* memory = memory::alloc(sizeof(TextureRef));
    TextureRef* texture = memory ? memory::construct_in<TextureRef>(memory) : nullptr;
    set_checksum(new_checksum);
    image_ = texture;                 // (an edit's place too)
    Expects(texture != nullptr);
    texture->checksum = new_checksum; // (no check, as the original)
    texture->unknownf_ = 0;
    texture->image_width_ = static_cast<uint16_t>(width);
    texture->image_height_ = static_cast<uint16_t>(height);
    if (bits_per_pixel < kDepth8) {
        bits_per_pixel = kDepth32;
    }
    uint32_t format = new_checksum; // (any other depth: the checksum taken for the format, as the original)
    switch (bits_per_pixel) {
    case 1:
    case 2:
    case 4:
    case kDepth8:
        format = gpu_value::kFormatP8; // (paletted: a Vulkan Gpu expands it to RGBA at upload)
        texture->depth = kDepth8;
        break;
    case kDepth16:
        format = gpu_value::kFormatX1R5G5B5;
        texture->depth = kDepth16;
        break;
    case kDepth32:
        format = gpu_value::kFormatX8R8G8B8;
        texture->depth = kDepth32;
        break;
    default:
        break;
    }
    GpuTexture** d3d = &texture->texture;
    gpu().create_texture({.width = static_cast<uint32_t>(width),
                          .height = static_cast<uint32_t>(height),
                          .levels = 0,
                          .usage = 0,
                          .format = format,
                          .pool = gpu_value::kPoolManaged},
                         d3d);
    GpuSize size;
    gpu().texture_level_size(*d3d, 0, &size);
    texture->height = static_cast<uint16_t>(size.height);
    texture->width = static_cast<uint16_t>(size.width);
    texture->unknowne_ = format == gpu_value::kFormatP8 ? kPalettedFlag : 0;
    return true;
}

// 0x004C86A0
bool PcTexture::copy_from(const BitmapTexture* source_texture)
{
    const auto* source = core::down_cast<const PcTexture>(source_texture);
    const TextureRef* from = source->get_texture();
    GpuTexture** into = &get_texture()->texture;
    if (*into) {
        gpu().release_texture(*into);
    }
    GpuTexture* from_texture = from->texture;
    const uint32_t levels = gpu().texture_level_count(from_texture);
    GpuLevelDesc desc;
    gpu().texture_level_desc(from_texture, 0, &desc);
    if (gpu().create_texture({.width = desc.width,
                              .height = desc.height,
                              .levels = levels,
                              .usage = 0,
                              .format = desc.format,
                              .pool = gpu_value::kPoolManaged},
                             into) != gpu_value::kResultOk) {
        crt::exit_fatal(0); // (the game's exit(), without the static owners' teardown)
    }
    for (uint32_t level = 0; level < levels; ++level) {
        gpu().texture_level_desc(from->texture, level, &desc);
        uint32_t row_bytes = 0;
        uint32_t rows = desc.height;
        if (desc.format == gpu_value::kFormatA8R8G8B8 || desc.format == gpu_value::kFormatX8R8G8B8) {
            row_bytes = desc.width << 2;
        } else { // (blocks of 4 rows; the row's bytes as the original works them out)
            rows = (rows + 3) >> 2;
            row_bytes = desc.format == gpu_value::kFormatDxt1 ? desc.width << 3 : desc.width << 4;
        }
        GpuLockedRect from_rect;
        GpuLockedRect into_rect;
        gpu().lock_texture(from->texture, level, &from_rect, nullptr, 0);
        gpu().lock_texture(*into, level, &into_rect, nullptr, 0);
        const std::span<uint8_t> write(static_cast<uint8_t*>(into_rect.bits), static_cast<size_t>(into_rect.pitch) * rows);
        const std::span<const uint8_t> read(static_cast<const uint8_t*>(from_rect.bits),
                                            static_cast<size_t>(from_rect.pitch) * rows);
        size_t write_at = 0;
        size_t read_at = 0;
        for (uint32_t row = rows; row != 0U; --row) {
            core::copy_into(read.subspan(read_at, row_bytes), write.subspan(write_at));
            write_at += into_rect.pitch;
            read_at += from_rect.pitch;
        }
        gpu().unlock_texture(from->texture, level);
        gpu().unlock_texture(*into, level);
    }
    return true;
}

// 0x004C8A60
bool PcTexture::begin_edit(bool keep_texture, bool keep_copy)
{
    TextureRef* texture = get_texture();
    const uint32_t width = texture->width;
    const uint32_t height = texture->height;
    auto* edit = static_cast<TextureEdit*>(memory::alloc(sizeof(TextureEdit)));
    if (edit) { // (no check after: written through null, as the original)
        *edit = {};
    }
    image_ = edit;
    Expects(edit != nullptr);
    edit->texture = texture;
    const uint32_t bytes = width * height * 4;
    edit->pixels = static_cast<uint32_t*>(memory::alloc(bytes));
    flag_bits() |= 1;
    const std::span<uint32_t> pixels(get_edit(this)->pixels, static_cast<size_t>(width) * height);
    GpuTexture* d3d = texture->texture;
    GpuSize size;
    gpu().texture_level_size(d3d, 0, &size);
    GpuLockedRect rect;
    gpu().lock_texture(d3d, 0, &rect, nullptr, gpu_value::kLockReadOnly);
    if (texture->depth == kDepth32) { // (rows bottom up, the pitch taken as the width; the original also tests != 8)
        const std::span<const uint32_t> surface(static_cast<const uint32_t*>(rect.bits),
                                                static_cast<size_t>(width) * height);
        size_t row = static_cast<size_t>(height - 1) * width;
        size_t into_at = 0;
        for (uint32_t row_y = height; row_y != 0U; --row_y) {
            core::copy_into(surface.subspan(row, width), pixels.subspan(into_at));
            into_at += width;
            row -= width;
        }
    }
    gpu().unlock_texture(texture->texture, 0);
    if (keep_copy) {
        get_edit(this)->original = static_cast<uint32_t*>(memory::alloc(bytes));
        std::memcpy(get_edit(this)->original, get_edit(this)->pixels, bytes);
    }
    if (keep_texture) {
        void* memory = memory::alloc(sizeof(TextureRef));
        get_edit(this)->kept = memory ? memory::construct_in<TextureRef>(memory) : nullptr;
        TextureRef* kept = get_edit(this)->kept; // (no check, as the original)
        Expects(kept != nullptr);
        kept->image_width_ = static_cast<uint16_t>(width);
        kept->image_height_ = static_cast<uint16_t>(height);
        kept->depth = kDepth32;
        GpuTexture** kept_d3d = &kept->texture;
        gpu().create_texture({.width = width,
                              .height = height,
                              .levels = 0,
                              .usage = 0,
                              .format = gpu_value::kFormatA8R8G8B8,
                              .pool = gpu_value::kPoolManaged},
                             kept_d3d);
        gpu().texture_level_size(*kept_d3d, 0, &size);
        kept->height = static_cast<uint16_t>(size.height);
        kept->width = static_cast<uint16_t>(size.width);
        GpuLockedRect kept_rect;
        gpu().lock_texture(*kept_d3d, 0, &kept_rect, nullptr, 0);
        std::memcpy(kept_rect.bits, get_edit(this)->pixels, bytes);
        gpu().unlock_texture(*kept_d3d, 0);
    }
    return true;
}

namespace {

// The four texels around a sample: top left, top right, bottom left, bottom right.
struct Corners {
    uint32_t c00{};
    uint32_t c01{};
    uint32_t c10{};
    uint32_t c11{};
};

// A pixel of four bytes sampled between four (10-bit fractions; each channel rounded).
uint32_t bilinear(Corners corners, int fx, int fy)
{
    const auto c00 = corners.c00;
    const auto c01 = corners.c01;
    const auto c10 = corners.c10;
    const auto c11 = corners.c11;
    const int w00 = ((0x400 - fx) * (0x400 - fy)) >> 10;
    const int w01 = ((0x400 - fy) * fx) >> 10;
    const int w10 = ((0x400 - fx) * fy) >> 10;
    const int w11 = (fy * fx) >> 10;
    uint32_t result = 0;
    for (int k = 0; k < kDepth32; k += kBitsPerByte) {
        const int sum = (static_cast<int>(c11 >> k & 0xFF) * w11) + (static_cast<int>(c10 >> k & 0xFF) * w10) +
                        (static_cast<int>(c01 >> k & 0xFF) * w01) + (static_cast<int>(c00 >> k & 0xFF) * w00) + 0x200;
        result |= static_cast<uint32_t>(static_cast<uint8_t>(sum >> kSubpixelBits)) << k;
    }
    return result;
}

// A sample's texel (sx, sy) and its fractions towards the next one (fx, fy, in 256ths).
struct SamplePoint {
    int sx{};
    int sy{};
    int fx{};
    int fy{};
};

// The four pixels about (sx, sy) of a width x height image (the right / lower ones the same at the edges).
uint32_t sample_at(std::span<const uint32_t> image, maths::PixelSize size, SamplePoint point)
{
    const auto sx = point.sx;
    const auto sy = point.sy;
    const auto fx = point.fx;
    const auto fy = point.fy;
    const auto width = size.width;
    const auto height = size.height;
    const int step = sx < width - 1 ? 1 : 0;
    int index = (sy * width) + sx;
    const uint32_t c00 = core::at(image, index);
    const uint32_t c01 = core::at(image, index + step);
    if (sy < height - 1) {
        index += width;
    }
    return bilinear({.c00 = c00, .c01 = c01, .c10 = core::at(image, index), .c11 = core::at(image, index + step)}, fx,
                    fy);
}

int ftol(double value) { return static_cast<int>(value); } // (the game's _ftol: towards zero)

} // namespace

// 0x004C9200
void resize_image(std::span<const uint32_t> from, ImageSize16 from_size, std::span<uint32_t> into, ImageSize16 into_size)
{
    const auto into_width = into_size.width;
    const auto into_height = into_size.height;
    const auto from_width = from_size.width;
    const auto from_height = from_size.height;
    const int x_step = ftol(static_cast<double>(static_cast<int>(from_width)) / static_cast<int>(into_width) * 1024.0F);
    const int y_step =
        ftol(static_cast<double>(static_cast<int>(from_height)) / static_cast<int>(into_height) * 1024.0F);
    size_t written = 0;
    int y = 0;
    for (int row = into_height; row > 0; --row, y += y_step) {
        const int sy = static_cast<int16_t>(y >> 10);
        const int fy = y & 0x3FF;
        int x = 0;
        for (int column = into_width; column > 0; --column, x += x_step) {
            core::at(into, written++) = sample_at(
                from, {.width = from_width, .height = from_height},
                {.sx = static_cast<int16_t>(x >> kSubpixelBits), .sy = sy, .fx = x & kSubpixelMask, .fy = fy});
        }
    }
}

// 0x004C9520
void zoom_image(std::span<const uint32_t> from, uint16_t width, uint16_t height, std::span<uint32_t> into, float x_zoom,
                float y_zoom)
{
    const int x_step = ftol(1.0F / static_cast<double>(x_zoom) * 1024.0F);
    const int y_step = ftol(1.0F / static_cast<double>(y_zoom) * 1024.0F);
    const int x_start = ((0x400 - x_step) * static_cast<int>(width)) >> 1; // (about the centre)
    int y = ((kSubpixelOne - y_step) * static_cast<int>(height)) >> 1;
    const int x_max = (width * 1024) - 1024;
    const int y_max = (height * 1024) - 1024;
    size_t written = 0;
    for (int row = height; row > 0; --row, y += y_step) {
        const int cy = y < 0 ? 0 : std::min(y, y_max);
        const int sy = static_cast<int16_t>(cy >> 10);
        int x = x_start;
        for (int column = width; column > 0; --column, x += x_step) {
            const int cx = x < 0 ? 0 : std::min(x, x_max);
            core::at(into, written++) = sample_at(from, {.width = width, .height = height},
                                                  {.sx = static_cast<int16_t>(cx >> kSubpixelBits),
                                                   .sy = sy,
                                                   .fx = cx & kSubpixelMask,
                                                   .fy = cy & kSubpixelMask});
        }
    }
}

// 0x004C97D0
uint8_t PcTexture::nearest_palette_index(uint32_t colour)
{
    const uint8_t depth = get_depth();
    const int count = 1 << depth;
    const std::span<const uint32_t> entries = render::TextureRef::palette_entries();
    uint8_t nearest = 0;
    int best = kIntMax;
    const int c0 = static_cast<int>(colour & 0xFF);
    const int c1 = static_cast<int>(colour >> 8 & 0xFF);
    const int c2 = static_cast<int>(colour >> 16 & 0xFF);
    const int c3 = static_cast<int>(colour >> 24);
    for (int i = 0; i < count; ++i) {
        const colour::Bytes entry = colour::bytes_of(core::at(entries, i));
        const int d0 = core::at(entry, 0) - c0;
        const int d1 = core::at(entry, 1) - c1;
        const int d2 = core::at(entry, 2) - c2;
        const int d3 = core::at(entry, 3) - c3;
        const int distance = (d2 * d2) + (d3 * d3 * 2) + (d1 * d1) + (d0 * d0);
        if (distance < best) {
            best = distance;
            nearest = static_cast<uint8_t>(i);
        }
    }
    return nearest;
}

// 0x004C9950
bool PcTexture::modulate(uint32_t colour, bool cumulative)
{
    // (the edit's fields read without checking it is one, as the original)
    const std::span<uint32_t> into = edit_pixels(this);
    const std::span<const uint32_t> from = get_edit(this)->original && !cumulative ? edit_original(this) : into;
    const uint32_t red = (colour & 0xFF) * 0xFF >> 7;
    const uint32_t green = (colour >> 8 & 0xFF) * 0xFF >> 7;
    const uint32_t blue = (colour >> 16 & 0xFF) * 0xFF >> 7;
    const uint32_t alpha = colour >> 24;
    const int width = fetch_breadth();
    const int count = fetch_tallness() * width;
    // a pixel's bytes (B, G, R, A) scaled by the colour's
    auto scaled = [=](uint32_t pixel) {
        const colour::Bytes from_bytes = colour::bytes_of(pixel);
        return colour::packed_of({static_cast<uint8_t>(core::at(from_bytes, 0) * blue / kByteMax),
                                  static_cast<uint8_t>(core::at(from_bytes, 1) * green / kByteMax),
                                  static_cast<uint8_t>(core::at(from_bytes, 2) * red / kByteMax),
                                  static_cast<uint8_t>(core::at(from_bytes, 3) * alpha >> kHalfAlphaShift)});
    };
    for (int i = 0; i < count; ++i) {
        core::at(into, i) = scaled(core::at(from, i));
    }
    if (get_depth() <= kDepth8 && !cumulative) { // (the palette too, in place)
        const std::span<uint32_t> palette = render::TextureRef::palette_entries();
        const int entries = 1 << get_depth();
        for (int i = 0; i < entries; ++i) {
            core::at(palette, i) = scaled(core::at(palette, i));
        }
    }
    return true;
}

// 0x004C9D80
bool PcTexture::resize_from(const BitmapTexture* source_texture, int /*unused*/)
{
    const auto* source = core::down_cast<const PcTexture>(source_texture);
    if (((flag_bits() & 1) == 0U) || ((source->flag_bits() & 1) == 0U)) {
        return false;
    }
    const TextureRef* into = get_edit(this)->texture;
    const TextureRef* from = get_edit(source)->texture;
    resize_image(edit_pixels(source), {.width = from->image_width_, .height = from->image_height_}, edit_pixels(this), {.width = into->image_width_, .height = into->image_height_});
    return true;
}

// 0x004C9DE0
bool PcTexture::get_zoom(float x_zoom, float y_zoom)
{
    const std::span<uint32_t> pixels = edit_pixels(this); // (read before the flag's test, as the original)
    const TextureRef* texture = get_texture();
    const uint32_t width = texture->image_width_;
    const uint32_t height = texture->image_height_;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    const uint32_t bytes = height * width * 4;
    auto* zoomed_block = static_cast<uint32_t*>(memory::alloc(bytes));
    const std::span<uint32_t> zoomed(zoomed_block, static_cast<size_t>(height) * width);
    std::ranges::fill(zoomed, 0U);
    memory::Hub::get_pull()->leave_heap_scope();
    zoom_image(pixels, static_cast<uint16_t>(width), static_cast<uint16_t>(height), zoomed, x_zoom, y_zoom);
    core::copy_into(std::span<const uint32_t>(zoomed), pixels);
    memory::Hub::dealloc(zoomed_block);
    return true;
}

// 0x004C9F90
void PcTexture::read_rect(std::span<uint32_t> out, TexelArea area)
{
    const auto x = area.x;
    const auto y = area.y;
    const auto width = area.width;
    const auto height = area.height;
    const uint32_t image_width = get_texture()->image_width_;
    if (((flag_bits() & 1) != 0U) && get_edit(this)->pixels) {
        const std::span<const uint32_t> pixels = edit_pixels(this);
        size_t from_at = (static_cast<size_t>(y) * image_width) + x;
        size_t out_at = 0;
        for (uint32_t row = height; row != 0U; --row) {
            core::copy_into(pixels.subspan(from_at, width), out.subspan(out_at));
            out_at += width;
            from_at += image_width;
        }
        return;
    }
    // Palettized: the whole image (not the rectangle's size, as the original), bottom row first.
    const uint32_t rows = get_texture()->image_height_;
    const uint32_t columns = get_texture()->image_width_;
    const std::span<const uint8_t> texels(static_cast<const uint8_t*>(get_texture()->get_lock()),
                                          static_cast<size_t>(rows) * columns);
    const std::span<const uint32_t> palette = render::TextureRef::palette_entries();
    core::Cursor<const uint8_t> row(texels, x + ((static_cast<int>(rows) - y - 1) * static_cast<int>(columns)));
    core::Cursor<uint32_t> out_cursor(out);
    for (uint32_t row_index = rows; row_index != 0U; --row_index) {
        for (uint32_t column = 0; column < columns; ++column) {
            *out_cursor++ = core::at(palette, row[column]);
        }
        row -= columns;
    }
    get_texture()->get_unlock();
}

// 0x004CA0D0
void PcTexture::write_rect(std::span<const uint32_t> in, TexelArea area)
{
    const auto x = area.x;
    const auto y = area.y;
    const auto width = area.width;
    const auto height = area.height;
    const uint32_t image_width = get_texture()->image_width_;
    const uint32_t image_height = get_texture()->image_height_;
    if (((flag_bits() & 1) != 0U) && get_edit(this)->pixels) {
        const std::span<uint32_t> pixels = edit_pixels(this);
        size_t into_at = (static_cast<size_t>(y) * image_width) + x;
        size_t in_at = 0;
        for (uint32_t row = height; row != 0U; --row) {
            core::copy_into(in.subspan(in_at, width), pixels.subspan(into_at));
            in_at += width;
            into_at += image_width;
        }
        return;
    }
    const std::span<uint8_t> texels(static_cast<uint8_t*>(get_texture()->get_lock()),
                                    static_cast<size_t>(image_height) * image_width);
    core::Cursor<uint8_t> row(texels, ((static_cast<int>(image_height) - y - 1) * static_cast<int>(image_width)) + x);
    core::Cursor<const uint32_t> in_cursor(in);
    for (uint32_t row_index = height; row_index != 0U; --row_index) {
        for (uint32_t column = 0; column < width; ++column) {
            row[column] = nearest_palette_index(*in_cursor++);
        }
        row -= image_width;
    }
    get_texture()->get_unlock();
}

// 0x004C8D30
bool PcTexture::spin_by(float angle, maths::Vec2 centre)
{
    auto centre_x = centre.x;
    auto centre_y = centre.y;
    const std::span<uint32_t> pixels = edit_pixels(this); // (read before the flag's test, as the original)
    const uint32_t width = get_texture()->width;
    const uint32_t height = get_texture()->height;
    if (centre_x < 0.0F) { // (below 0: the middle)
        centre_x = static_cast<float>((static_cast<double>(width) - 1.0F) * kHalf);
    }
    if (centre_y < 0.0F) {
        centre_y = static_cast<float>((static_cast<double>(height) - 1.0F) * kHalf);
    }
    std::array<float, 4> rotation{};
    maths::create_rotate_2d(rotation, -angle);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    const uint32_t bytes = height * width * 4;
    auto* rotated_block = static_cast<uint32_t*>(memory::alloc(bytes));
    const std::span<uint32_t> rotated(rotated_block, static_cast<size_t>(height) * width);
    memory::Hub::get_pull()->leave_heap_scope();
    const int y_max = static_cast<int>((height * 1024) - 1024);
    const int x_max = static_cast<int>((width * 1024) - 1024);
    // the image's axes turned (each a float, as the original stores them), in 1024ths
    auto xx = static_cast<float>((static_cast<double>(core::at(rotation, 2)) * 0.0F) + core::at(rotation, 0));
    auto xy = static_cast<float>((static_cast<double>(core::at(rotation, 3)) * 0.0F) + core::at(rotation, 1));
    auto yx = static_cast<float>((static_cast<double>(core::at(rotation, 0)) * 0.0F) + core::at(rotation, 2));
    auto yy = static_cast<float>((static_cast<double>(core::at(rotation, 1)) * 0.0F) + core::at(rotation, 3));
    const int xx_step = ftol(static_cast<double>(xx) * 1024.0F);
    const int xy_step = ftol(static_cast<double>(xy) * 1024.0F);
    const int yx_step = ftol(static_cast<double>(yx) * 1024.0F);
    const int yy_step = ftol(static_cast<double>(yy) * 1024.0F);
    // where the top left comes from: the centre moved back through the turn
    const double nx = -static_cast<double>(centre_x);
    const double ny = -static_cast<double>(centre_y);
    auto ox = static_cast<float>((core::at(rotation, 2) * ny) + (nx * core::at(rotation, 0)));
    auto oy = static_cast<float>((ny * core::at(rotation, 3)) + (core::at(rotation, 1) * nx));
    const double origin_coord_x = static_cast<double>(ox) + centre_x; // (kept in the register, unrounded)
    auto origin_coord_y = static_cast<float>(static_cast<double>(oy) + centre_y);
    int row_x = ftol(origin_coord_x * kSubpixelScale);
    int row_y = ftol(static_cast<double>(origin_coord_y) * kSubpixelScale);
    core::Cursor<uint32_t> out(rotated);
    for (uint32_t row = height; row != 0U; --row, row_x += yx_step, row_y += yy_step) {
        int x = row_x;
        int y = row_y;
        for (uint32_t column = width; column != 0U; --column, x += xx_step, y += xy_step) {
            const int cx = x < 0 ? 0 : std::min(x, x_max);
            const int cy = y < 0 ? 0 : std::min(y, y_max);
            *out = sample_at(pixels, {.width = static_cast<int>(width), .height = static_cast<int>(height)}, {.sx = static_cast<int16_t>(cx >> kSubpixelBits), .sy = static_cast<int16_t>(cy >> kSubpixelBits), .fx = cx & kSubpixelMask, .fy = cy & kSubpixelMask});
            ++out;
        }
    }
    core::copy_into(std::span<const uint32_t>(rotated), pixels);
    memory::Hub::dealloc(rotated_block);
    return true;
}

// 0x004CA220
bool PcTexture::get_stretch(Stretch stretch)
{
    const auto position = stretch.position;
    const auto vertical = stretch.vertical;
    auto amount = stretch.amount;
    const uint32_t width = get_texture()->image_width_;
    const uint32_t height = get_texture()->image_height_;
    if (amount == 0) {
        return true;
    }
    const int at = position;
    if (at + amount <= 0) {
        amount = 1 - at;
    }
    auto end = static_cast<uint32_t>(at + amount);
    uint16_t read_x = 0;
    uint16_t read_y = 0;
    uint16_t read_width = 0;
    uint16_t read_height = 0;
    uint16_t new_width = 0;
    uint16_t new_height = 0;
    int offset = 0;
    if (vertical == 0) { // (the part left of the column stretched by -amount, or the part from it by amount)
        if (end >= width) {
            amount = static_cast<int>(width) - at - 1;
        }
        read_height = new_height = static_cast<uint16_t>(height);
        if (amount < 0) {
            read_width = static_cast<uint16_t>(at + 1);
            new_width = static_cast<uint16_t>(at - amount + 1);
            offset = -amount;
        } else {
            read_width = static_cast<uint16_t>(width - at);
            new_width = static_cast<uint16_t>(amount - at + static_cast<int>(width));
            read_x = position;
            offset = 0;
        }
    } else {
        if (end >= height) {
            amount = static_cast<int>(height) - at - 1;
        }
        read_width = new_width = static_cast<uint16_t>(width);
        if (amount < 0) {
            read_height = static_cast<uint16_t>(at + 1);
            new_height = static_cast<uint16_t>(at - amount + 1);
            offset = -amount * static_cast<int>(width);
        } else {
            read_height = static_cast<uint16_t>(height - at);
            new_height = static_cast<uint16_t>(amount - at + static_cast<int>(height));
            read_y = position;
            offset = 0;
        }
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->cinematic_high_heap() ? manager->cinematic_high_heap() : manager->top_down_heap());
    auto* part_block = static_cast<uint32_t*>(memory::alloc(static_cast<uint32_t>(read_height) * read_width * 4));
    auto* stretched_block = static_cast<uint32_t*>(memory::alloc(static_cast<uint32_t>(new_height) * new_width * 4));
    const std::span<uint32_t> part(part_block, static_cast<size_t>(read_height) * read_width);
    const std::span<uint32_t> stretched(stretched_block, static_cast<size_t>(new_height) * new_width);
    memory::Hub::get_pull()->leave_heap_scope();
    read_rect(part, {.x = read_x, .y = read_y, .width = read_width, .height = read_height});
    resize_image(part, {.width = read_width, .height = read_height}, stretched, {.width = new_width, .height = new_height});
    copy_sub_image(part, 0, 0, {.width = read_width, .height = read_height}, stretched.subspan(static_cast<uint16_t>(offset)), {.width = new_width, .height = new_height});
    write_rect(part, {.x = read_x, .y = read_y, .width = read_width, .height = read_height});
    if (part_block) {
        memory::Hub::dealloc(part_block);
    }
    if (stretched_block) {
        memory::Hub::dealloc(stretched_block);
    }
    return true;
}

// 0x004CA7B0
bool PcTexture::get_mirror()
{
    const uint32_t width = get_texture()->image_width_;
    const int height = get_texture()->image_height_;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    auto* column_block = static_cast<uint32_t*>(memory::alloc(height * 4));
    const std::span<uint32_t> column(column_block, static_cast<size_t>(height));
    memory::Hub::get_pull()->leave_heap_scope();
    const std::span<uint32_t> pixels = edit_pixels(this);
    int64_t left = 0;
    int64_t right = static_cast<int64_t>(width) - 1;
    for (; left < right; ++left, --right) { // (the columns swapped)
        for (int i = 0; i < height; ++i) {
            core::at(column, i) = core::at(pixels, left + (static_cast<int64_t>(i) * width));
        }
        for (int i = 0; i < height; ++i) {
            core::at(pixels, left + (static_cast<int64_t>(i) * width)) =
                core::at(pixels, right + (static_cast<int64_t>(i) * width));
        }
        for (int i = 0; i < height; ++i) {
            core::at(pixels, right + (static_cast<int64_t>(i) * width)) = core::at(column, i);
        }
    }
    memory::Hub::dealloc(column_block);
    return true;
}

// 0x004CA8E0
bool PcTexture::get_overlay(BitmapTexture* over_texture, int /*unused*/)
{
    auto* over = core::down_cast<PcTexture>(over_texture);
    const uint32_t width = over->fetch_breadth();
    const uint32_t height = over->fetch_tallness();
    const std::span<const uint8_t> texels(static_cast<const uint8_t*>(over->get_texture()->get_lock()),
                                          static_cast<size_t>(height) * width * 4);
    core::Cursor<uint32_t> into{edit_pixels(this)};
    core::Cursor<const uint8_t> row(texels, static_cast<std::ptrdiff_t>(height - 1) * static_cast<std::ptrdiff_t>(width) * 4); // (bottom row first, by its alpha)
    for (uint32_t row_y = height; row_y != 0U; --row_y, row -= width * 4) {
        core::Cursor<const uint8_t> from = row;
        for (uint32_t column_x = 0; column_x < width; ++column_x, from += 4, ++into) {
            const uint8_t alpha = from[3];
            const int rest = static_cast<uint8_t>(0xFF - alpha);
            colour::Bytes pixel = colour::bytes_of(*into);
            core::at(pixel, 0) =
                static_cast<uint8_t>(((from[0] * alpha) + (core::at(pixel, 0) * rest)) >> kBitsPerByte);
            core::at(pixel, 1) =
                static_cast<uint8_t>(((from[1] * alpha) + (core::at(pixel, 1) * rest)) >> kBitsPerByte);
            core::at(pixel, 2) =
                static_cast<uint8_t>(((from[2] * alpha) + (core::at(pixel, 2) * rest)) >> kBitsPerByte);
            core::at(pixel, 3) = std::max(core::at(pixel, 3), alpha);
            *into = colour::packed_of(pixel);
        }
    }
    over->get_texture()->get_unlock();
    return false;
}

// 0x004CAA20
bool PcTexture::shift_hue_sat_value(HsvShift shift, bool cumulative)
{
    const auto hue = shift.hue;
    const auto saturation = shift.saturation;
    const auto value = shift.value;
    const std::span<uint32_t> into = edit_pixels(this);
    std::span<const uint32_t> from;
    if ((flag_bits() & 1) != 0U) {
        from = get_edit(this)->original && !cumulative ? edit_original(this) : into;
    }
    const int width = fetch_breadth();
    const int count = fetch_tallness() * width;
    for (int i = 0; i < count; ++i) {
        const colour::Bytes source = colour::bytes_of(core::at(from, i));
        std::array<float, 3> hsv{};
        rgb_to_hsv({{static_cast<float>(core::at(source, 0)) * (1.0F / kByteMaxFloat),
                     static_cast<float>(core::at(source, 1)) * (1.0F / kByteMaxFloat),
                     static_cast<float>(core::at(source, 2)) * (1.0F / kByteMaxFloat)}},
                   &hsv);
        float pixel_hue = core::at(hsv, 0);
        float pixel_saturation = core::at(hsv, 1);
        float v = core::at(hsv, 2);
        const double turned = static_cast<double>(pixel_hue) + hue;
        pixel_hue = static_cast<float>(turned);
        if (!(turned <= kHueFull)) {
            pixel_hue = static_cast<float>(static_cast<double>(pixel_hue) - kHueFull);
        }
        double scaled = static_cast<double>(pixel_saturation) * saturation;
        pixel_saturation = scaled < 1.0F ? static_cast<float>(scaled) : 1.0F;
        scaled = static_cast<double>(v) * value;
        v = scaled < 1.0F ? static_cast<float>(scaled) : 1.0F;
        std::array<float, 3> rgb{};
        hsv_to_rgb({{pixel_hue, pixel_saturation, v}}, &rgb);
        colour::Bytes pixel = colour::bytes_of(core::at(into, i));
        for (int k = 0; k < 3; ++k) {
            core::at(pixel, k) =
                static_cast<uint8_t>(ftol((static_cast<double>(core::at(rgb, k)) * kByteMaxFloat) + kHalf));
        }
        core::at(into, i) = colour::packed_of(pixel);
    }
    return true;
}

// 0x004CABE0
bool PcTexture::get_offset(int dx, int dy, bool keep_edges, int /*unused*/)
{
    const int width = fetch_breadth();
    const int height = fetch_tallness();
    int from_x = 0;
    int into_x = 0;
    int part_width = 0;
    int from_y = 0;
    int into_y = 0;
    int part_height = 0;
    if (dx >= 0) {
        from_x = 0;
        into_x = dx;
        part_width = width - dx;
    } else {
        from_x = -dx;
        into_x = 0;
        part_width = width + dx;
    }
    if (dy >= 0) {
        into_y = dy;
        part_height = height - dy;
    } else {
        from_y = -dy;
        into_y = 0;
        part_height = height + dy;
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    // (the C runtime's heap, as the original's malloc)
    std::vector<uint32_t> part_pixels(static_cast<size_t>(part_height) * static_cast<size_t>(part_width));
    auto& part = part_pixels;
    memory::Hub::get_pull()->leave_heap_scope();
    read_rect(part, {.x = static_cast<uint16_t>(from_x), .y = static_cast<uint16_t>(from_y), .width = static_cast<uint16_t>(part_width), .height = static_cast<uint16_t>(part_height)});
    write_rect(part, {.x = static_cast<uint16_t>(into_x), .y = static_cast<uint16_t>(into_y), .width = static_cast<uint16_t>(part_width), .height = static_cast<uint16_t>(part_height)});
    const std::span<uint32_t> edit = edit_pixels(this);
    if (!keep_edges) { // (what was uncovered filled from the moved part's nearest edge)
        for (int texel_y = 0, sy = -into_y; texel_y < height; ++texel_y, ++sy) {
            int row = sy <= 0 ? 0 : sy;
            row = std::min(row, part_height - 1);
            const int row_start = row * part_width;
            for (int texel_x = 0, sx = -into_x; texel_x < width; ++texel_x, ++sx) {
                if (sx >= 0 && sx < part_width && sy >= 0 && sy < part_height) {
                    continue;
                }
                int column = sx <= 0 ? 0 : sx;
                column = std::min(column, part_width - 1);
                const uint32_t pixel = core::at(part, row_start + column);
                const uint32_t texels_wide = get_texture()->width;
                if (((flag_bits() & 1) != 0U) && get_edit(this)->pixels) {
                    core::at(edit, (static_cast<uint16_t>(texel_y) * texels_wide) + static_cast<uint16_t>(texel_x)) =
                        pixel;
                }
            }
        }
    }
    return true;
}

// 0x004CADC0
bool PcTexture::get_border(uint32_t colour)
{
    const std::span<uint32_t> pixels = edit_pixels(this);
    const uint32_t width = get_texture()->width;
    const int height = get_texture()->height;
    const int64_t bottom = static_cast<int64_t>(height - 1) * width;
    const int64_t right = static_cast<int64_t>(width) - 1;
    for (int i = 0; i < height; ++i) {
        core::at(pixels, static_cast<int64_t>(i) * width) = colour;
    }
    for (int i = 0; i < height; ++i) {
        core::at(pixels, right + (static_cast<int64_t>(i) * width)) = colour;
    }
    for (uint32_t i = 0; i < width; ++i) {
        core::at(pixels, i) = colour;
        core::at(pixels, bottom + i) = colour;
    }
    return true;
}

// 0x004CA410
bool PcTexture::squash(BitmapTexture::Stretch stretch, bool fill, uint32_t colour)
{
    const auto position = stretch.position;
    const auto vertical = stretch.vertical;
    auto amount = stretch.amount;
    const uint32_t height = get_texture()->image_height_;
    const uint32_t width = get_texture()->image_width_;
    if (amount == 0) {
        return true;
    }
    const int at = position;
    if (at + amount <= 0) {
        amount = 1 - at;
    }
    const int end = at + amount;
    uint16_t read_x = 0;
    uint16_t read_y = 0;
    uint16_t into_x = 0;
    uint16_t into_y = 0;
    uint16_t read_width = 0;
    uint16_t read_height = 0;
    uint16_t new_width = 0;
    uint16_t new_height = 0;
    if (vertical == 0) {
        if (end >= ints::as_signed(width)) {
            amount = static_cast<int>(width) - at - 1;
        }
        new_height = read_height = static_cast<uint16_t>(height);
        if (amount < 0) {
            new_width = static_cast<uint16_t>(amount - at + static_cast<int>(width));
            read_width = static_cast<uint16_t>(width - at);
            read_x = position;
        } else {
            read_width = static_cast<uint16_t>(at + 1);
            new_width = static_cast<uint16_t>(at - amount + 1);
            into_x = static_cast<uint16_t>(amount);
        }
    } else {
        if (end >= ints::as_signed(height)) {
            amount = static_cast<int>(height) - at - 1;
        }
        read_width = new_width = static_cast<uint16_t>(width);
        if (amount < 0) {
            read_height = static_cast<uint16_t>(height - at);
            new_height = static_cast<uint16_t>(amount - at + static_cast<int>(height));
            read_y = position;
        } else {
            read_height = static_cast<uint16_t>(at + 1);
            new_height = static_cast<uint16_t>(at - amount + 1);
            into_y = static_cast<uint16_t>(amount);
        }
    }
    auto crt_malloc = crt::alloc_block; // (the C runtime's)
    auto crt_free = crt::dealloc;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    const int count = read_height * read_width;
    auto* part_block = static_cast<uint32_t*>(crt_malloc(count * 4));
    auto* squashed_block = static_cast<uint32_t*>(crt_malloc(new_height * new_width * 4));
    const std::span<uint32_t> part(part_block, static_cast<size_t>(count));
    const std::span<uint32_t> squashed(squashed_block, static_cast<size_t>(new_height) * new_width);
    memory::Hub::get_pull()->leave_heap_scope();
    read_rect(part, {.x = read_x, .y = read_y, .width = read_width, .height = read_height});
    resize_image(part, {.width = read_width, .height = read_height}, squashed, {.width = new_width, .height = new_height});
    if (fill) {
        for (int i = 0; i < count; ++i) {
            core::at(part, i) = colour;
        }
    }
    copy_sub_image(part, into_x, into_y, {.width = read_width, .height = read_height}, squashed, {.width = new_width, .height = new_height});
    write_rect(part, {.x = read_x, .y = read_y, .width = read_width, .height = read_height});
    if (!fill) { // (what was uncovered filled with the squashed part's edge column / row)
        if (vertical == 0) {
            const bool right = amount > 0;
            const int source = right ? 0 : new_width - 1;
            manager = memory::Hub::get_pull();
            manager->enter_heap_scope(manager->top_down_heap());
            const int rows = static_cast<uint16_t>(height);
            auto* column_block = static_cast<uint32_t*>(crt_malloc(rows * 4));
            const std::span<uint32_t> column(column_block, static_cast<size_t>(rows));
            memory::Hub::get_pull()->leave_heap_scope();
            for (int i = 0; i < rows; ++i) {
                core::at(column, i) = core::at(squashed, source + (i * new_width));
            }
            const int first = right ? 0 : static_cast<int>(width) + amount;
            const int last = right ? amount - 1 : static_cast<int>(width) - 1;
            for (int column_x = first; column_x <= last; ++column_x) {
                write_rect(column, {.x = static_cast<uint16_t>(column_x),
                                    .y = 0,
                                    .width = 1,
                                    .height = static_cast<uint16_t>(height)});
            }
            crt_free(column_block);
        } else {
            const bool down = amount > 0;
            const int source = down ? 0 : new_height - 1;
            manager = memory::Hub::get_pull();
            manager->enter_heap_scope(manager->top_down_heap());
            const uint32_t bytes = static_cast<uint16_t>(width) * 4U;
            auto* row_block = static_cast<uint32_t*>(crt_malloc(bytes));
            const std::span<uint32_t> row(row_block, bytes / 4);
            memory::Hub::get_pull()->leave_heap_scope();
            core::copy_into(std::span<const uint32_t>(squashed).subspan(static_cast<size_t>(new_width) * source, bytes / 4), row);
            const int first = down ? 0 : static_cast<int>(height) + amount;
            const int last = down ? amount - 1 : static_cast<int>(height) - 1;
            for (int row_y = first; row_y <= last; ++row_y) {
                write_rect(
                    row,
                    {.x = 0, .y = static_cast<uint16_t>(row_y), .width = static_cast<uint16_t>(width), .height = 1});
            }
            crt_free(row_block);
        }
    }
    if (part_block) {
        crt_free(part_block);
    }
    if (squashed_block) {
        crt_free(squashed_block);
    }
    return true;
}

// 0x004C9B00
BitmapTexture* PcTexDict::load_texture_file(const char* name, uint32_t a1, uint32_t /*a2*/)
{
    BitmapTexture* texture = alloc_pc_texture();
    // (no check: the engine's load called on null without memory, as the original)
    texture->BitmapTexture::load_by_name(name, static_cast<int>(a1), 1);
    return texture;
}

// 0x004C89B0
BitmapTexture* PcTexDict::load_texture_data(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3,
                                            uint32_t a4, uint32_t /*a6*/)
{
    BitmapTexture* texture = alloc_pc_texture();
    texture->BitmapTexture::read_from_ram_block(data, size, checksums::Name{checksum}, a3, a4, 1);
    return texture;
}

// 0x004C9BA0
BitmapTexture* PcTexDict::make_texture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel,
                                       uint32_t a4, uint32_t a5)
{
    auto* texture = static_cast<BitmapTexture*>(textures()->fetch_entry(checksum));
    if (!texture) {
        gsl::owner<PcTexture*> made = alloc_pc_texture(); // (the caller adds it to the dictionary)
        texture = made;
    }
    texture->BitmapTexture::fabricate(checksum, width, height, bits_per_pixel, a4, a5);
    return texture;
}

// 0x004C8090
bool PcTexDict::destroy_texture(BitmapTexture* texture)
{
    gsl::owner<BitmapTexture*> owned = memory::owned_element(texture); // (the dictionary's)
    delete owned; // (through its vtable)
    return true;
}

// 0x004C9C80
gsl::owner<BitmapTexture*> PcTexDict::duplicate_texture(uint32_t checksum, BitmapTexture* source)
{
    auto* texture = memory::make_owned<PcTexture>(core::down_cast<const PcTexture>(source));
    texture->get_texture()->checksum = checksum; // (no check, as the original)
    return texture;
}

namespace {

// The dictionary's table remade for `count` textures: 2^bits buckets, bits the first with 2^bits at least twice the
// count (1-12), in the heap the old table was in (the old one freed).
lists::KeyMap* remake_table(lists::KeyMap* table, uint32_t count)
{
    uint32_t size = 2;
    uint32_t bits = 1;
    const uint32_t wanted = count * 2;
    if (wanted > size) {
        for (bool again = true; again; again = wanted > size) {
            if (bits >= kMaxSizeBits) {
                break;
            }
            size <<= 1;
            ++bits;
        }
    }
    memory::MemSource* heap =
        memory::fetch_chunk_prefix(table)->allocator(); // (read before the null test, as the original)
    if (table) {
        if (table->buckets()) {
            table->empty_table();
            memory::Hub::dealloc(table->buckets());
            table->set_buckets(nullptr);
        }
        memory::Hub::dealloc(table);
    }
    memory::Hub::get_pull()->enter_heap_scope(heap);
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    auto* remade = memory ? memory::construct_in<lists::KeyMap>(memory, static_cast<int>(bits)) : nullptr;
    memory::Hub::get_pull()->leave_heap_scope();
    return remade;
}

// A texture's compression and its depth in bits.
struct PixelFormat {
    uint8_t compression{};
    uint8_t depth{};
};

// A texture's format from its header: by depth (8 P8, 16 A1R5G5B5, 32 A8R8G8B8) or its compression (1, 2: DXT1, 5:
// DXT5); anything else keeps the last one's (the original's stack).
uint32_t pixel_layout(PixelFormat pixel_format, uint32_t last)
{
    const auto compression = pixel_format.compression;
    const auto depth = pixel_format.depth;
    if (compression != 0U) {
        if (compression == 1 || compression == 2) {
            return gpu_value::kFormatDxt1;
        }
        if (compression == kCompressionDxt5) {
            return gpu_value::kFormatDxt5;
        }
        return last;
    }
    if (depth == kDepth8) {
        return gpu_value::kFormatP8;
    }
    if (depth == kDepth16) {
        return gpu_value::kFormatA1R5G5B5;
    }
    if (depth == kDepth32) {
        return gpu_value::kFormatA8R8G8B8;
    }
    return last;
}

// The texture under its checksum in the table.
void add_to_table(lists::KeyMap* table, TextureRef* texture)
{
    gsl::owner<render::PcTexture*> holder{memory::make_owned<PcTexture>()};
    holder->set_image(texture); // (no check, as the original)
    holder->set_checksum(texture->checksum);
    Expects(table != nullptr);
    table->store_entry(texture->checksum, holder);
}

gsl::owner<TextureRef*> new_engine_texture()
{
    void* memory = memory::alloc(sizeof(TextureRef));
    return memory ? memory::construct_in<TextureRef>(memory) : nullptr;
}

} // namespace

// 0x004CAE90
lists::KeyMap* load_textures_from_file(const char* name, lists::KeyMap* table, bool remake)
{
    file::Hdl file = file::unseal(name, "rb");
    if (!file) {
        return table;
    }
    // The file read in order; its headers through texture_format's reader (read into a buffer first). Not original
    // - a level larger than its texture's (texture_format::level_size), or a file cut short, stops the dictionary
    // there: the original read any size into the surface.
    auto read = [file](void* into, int size, int count) {
        return file::get_take_in(into, size, count, file) == size * count; // (bytes read)
    };
    std::array<uint8_t, texture_format::kTextureHeaderSize> bytes{};
    texture_format::DictionaryHeader dictionary;
    util::ByteReader dictionary_reader(bytes.data(), texture_format::kDictionaryHeaderSize);
    if (!read(bytes.data(), texture_format::kDictionaryHeaderSize, 1) ||
        !texture_format::read_dictionary_header(dictionary_reader, &dictionary)) {
        file::shut(file);
        return table;
    }
    const int count = dictionary.count;
    if (remake) {
        table = remake_table(table, static_cast<uint32_t>(count));
    }
    uint32_t format = 0; // (the original's is whatever its stack held until a texture sets it)
    for (int i = 0; i < count; ++i) {
        texture_format::TextureHeader header;
        util::ByteReader header_reader(bytes.data(), sizeof(bytes));
        if (!read(bytes.data(), sizeof(bytes), 1) || !texture_format::read_texture_header(header_reader, &header)) {
            debug::printf("%s: cut short at texture %d of %d\n", name, i, count);
            break;
        }
        gsl::owner<TextureRef*> texture = new_engine_texture();
        texture->checksum = header.checksum; // (no check, as the original)
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
        texture->levels = static_cast<uint8_t>(header.levels);
        texture->depth = static_cast<uint8_t>(header.depth);
        texture->unknowne_ = static_cast<uint8_t>(header.palette_depth);
        texture->unknownf_ = static_cast<uint8_t>(header.compression);
        format = pixel_layout({.compression = texture->unknownf_, .depth = texture->depth}, format);
        texture->height = std::max<uint16_t>(texture->height, 4);
        GpuTexture** d3d = &texture->texture;
        bool damaged = false;
        if (header.palette_size != 0U) {
            // Palettized: an A8R8G8B8 texture made, the palette and the levels read - and dropped (the texture's
            // texels left as they are, as the original).
            texture->depth = kDepth32;
            format = gpu_value::kFormatA8R8G8B8;
            gpu().create_texture({.width = texture->width,
                                  .height = texture->height,
                                  .levels = texture->levels,
                                  .usage = 0,
                                  .format = gpu_value::kFormatA8R8G8B8,
                                  .pool = gpu_value::kPoolManaged},
                                 d3d);
            void* palette = memory::alloc(header.palette_size);
            read(palette, static_cast<int>(header.palette_size), 1);
            for (uint32_t level = 0; level < texture->levels && !damaged; ++level) {
                uint32_t size = 0;
                read(&size, 4, 1);
                if (size > texture_format::level_size(header, level)) {
                    damaged = true;
                    break;
                }
                GpuLockedRect rect;
                gpu().lock_texture(*d3d, level, &rect, nullptr, 0);
                GpuSize level_size;
                gpu().texture_level_size(*d3d, level, &level_size);
                void* texels = memory::alloc(size);
                read(texels, static_cast<int>(size), 1);
                memory::Hub::dealloc(texels);
                gpu().unlock_texture(*d3d, level);
            }
            memory::Hub::dealloc(palette);
        } else {
            gpu().create_texture({.width = texture->width,
                                  .height = texture->height,
                                  .levels = texture->levels,
                                  .usage = 0,
                                  .format = format,
                                  .pool = gpu_value::kPoolManaged},
                                 d3d);
            texture->palette = nullptr;
            for (uint32_t level = 0; level < texture->levels; ++level) {
                uint32_t size = 0;
                read(&size, 4, 1);
                if (size > texture_format::level_size(header, level)) {
                    damaged = true;
                    break;
                }
                GpuLockedRect rect;
                gpu().lock_texture(*d3d, level, &rect, nullptr, 0);
                read(rect.bits, static_cast<int>(size), 1);
                gpu().unlock_texture(*d3d, level);
            }
        }
        add_to_table(table, texture);
        if (damaged) {
            debug::printf("%s: texture %08x has a level larger than its size\n", name, header.checksum);
            break;
        }
    }
    file::shut(file);
    return table;
}

// 0x004CB420
lists::KeyMap* load_textures_from_memory(const uint8_t** cursor, size_t size, lists::KeyMap* table, bool remake)
{
    // Read through texture_format's reader, bounded by `size` (the cursor not moved on, as the original). Not
    // original - a texture or level past the end, or a level larger than its texture's, stops the dictionary there.
    util::ByteReader reader(*cursor, size);
    texture_format::DictionaryHeader dictionary;
    if (!texture_format::read_dictionary_header(reader, &dictionary)) {
        return table;
    }
    const int count = dictionary.count;
    if (remake) {
        table = remake_table(table, static_cast<uint32_t>(count));
    }
    uint32_t format = 0; // (as the file loader's)
    for (int i = count; i > 0; --i) {
        texture_format::TextureHeader header;
        if (!texture_format::read_texture_header(reader, &header)) {
            debug::printf("texture dictionary: cut short\n");
            break;
        }
        gsl::owner<TextureRef*> texture = new_engine_texture();
        texture->checksum = header.checksum;
        texture->levels = static_cast<uint8_t>(header.levels);
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
        texture->depth = static_cast<uint8_t>(header.depth);
        texture->unknowne_ = static_cast<uint8_t>(header.palette_depth);
        texture->unknownf_ = static_cast<uint8_t>(header.compression);
        format = pixel_layout({.compression = static_cast<uint8_t>(header.compression), .depth = static_cast<uint8_t>(header.depth)}, format);
        GpuTexture** d3d = &texture->texture;
        gpu().create_texture({.width = static_cast<uint16_t>(header.width),
                              .height = static_cast<uint16_t>(header.height),
                              .levels = static_cast<uint8_t>(header.levels),
                              .usage = 0,
                              .format = format,
                              .pool = gpu_value::kPoolManaged},
                             d3d);
        // (no palette read: the memory loader expects none, as the original)
        if (header.palette_size == 0U) {
            texture->palette = nullptr;
        }
        bool damaged = false;
        for (uint32_t level = 0; level < texture->levels; ++level) {
            const uint8_t* texels = nullptr;
            uint32_t level_bytes = 0;
            if (!texture_format::read_level(reader, &texels, &level_bytes) ||
                level_bytes > texture_format::level_size(header, level)) {
                damaged = true;
                break;
            }
            GpuLockedRect rect;
            gpu().lock_texture(*d3d, level, &rect, nullptr, 0);
            std::memcpy(rect.bits, texels, level_bytes);
            gpu().unlock_texture(*d3d, level);
        }
        add_to_table(table, texture);
        if (damaged) {
            debug::printf("texture dictionary: texture %08x damaged\n", header.checksum);
            break;
        }
    }
    return table;
}

namespace {

// Whether the table has any textures (iterated from the start).
bool has_textures(lists::KeyMap* table)
{
    table->set_iter_bucket(-1);
    table->set_iter_item(nullptr);
    int count = 0;
    while (table->step_iterator() && table->iter_item() && table->iter_item()->value) {
        ++count;
    }
    return count != 0;
}

} // namespace

// 0x004CB380
bool PcTexDict::ingest_file(const char* name)
{
    auto* table = textures();
    if (!table) {
        load_textures_from_file(name, nullptr, false);
    } else if (!has_textures(table)) {
        set_textures(load_textures_from_file(name, table, true));
    } else {
        load_textures_from_file(name, table, false);
    }
    return true;
}

// 0x004CB7C0
bool PcTexDict::load_memory(const uint8_t* data, size_t size)
{
    auto* table = textures();
    if (!table) {
        load_textures_from_memory(&data, size, nullptr, false);
    } else if (!has_textures(table)) {
        set_textures(load_textures_from_memory(&data, size, table, true));
    } else {
        load_textures_from_memory(&data, size, table, false);
    }
    return true;
}

// 0x004CB760
PcTexDict::PcTexDict(const char* name) : TextureSet(name, true) { ingest_file(name); }

// 0x004C7270
gsl::owner<PcTexDict*> load_tex_dict_file(const char* name) { return new PcTexDict(name); }

// 0x004C72E0
gsl::owner<PcTexDict*> load_tex_dict_memory(uint32_t checksum, const uint8_t* data, size_t size)
{
    gsl::owner<render::PcTexDict*> dict{memory::make_owned<PcTexDict>(checksum)};
    dict->load_memory(data, size); // (called on null without memory, as the original)
    return dict;
}

// 0x004C7360
gsl::owner<PcTexDict*> create_tex_dict(uint32_t checksum) { return new PcTexDict(checksum); }

// 0x004C80B0
void copy_sub_image(std::span<uint32_t> into, uint16_t x, uint16_t y, ImageSize16 into_size,
                    std::span<const uint32_t> from, ImageSize16 from_size)
{
    const auto from_width = from_size.width;
    const auto from_height = from_size.height;
    const auto into_width = into_size.width;
    const auto into_height = into_size.height;
    int columns = from_width;
    columns = std::min(columns, into_width - x);
    int rows = from_height < into_height - y ? from_height : into_height - y;
    size_t into_at = (static_cast<size_t>(y) * into_width) + x;
    size_t from_at = 0;
    for (; rows > 0; --rows) {
        if (columns > 0) {
            core::copy_into(from.subspan(from_at, static_cast<size_t>(columns)), into.subspan(into_at));
        }
        from_at += from_width;
        into_at += into_width;
    }
}

// 0x004C8180
// (`hsv`: hue in degrees, saturation, value; the hue left as it was for a grey)
void rgb_to_hsv(const std::array<float, 3>& rgb, std::array<float, 3>* hsv)
{
    const float red = core::at(rgb, 0);
    const float green = core::at(rgb, 1);
    const float blue = core::at(rgb, 2);
    float* hue_out = &core::at(*hsv, 0);
    float* saturation_out = &core::at(*hsv, 1);
    float* v = &core::at(*hsv, 2);
    float low = green < blue ? green : blue;
    low = std::min(low, red);
    float high = green > blue ? green : blue;
    high = std::max(high, red);
    *v = high;
    auto delta = static_cast<float>(static_cast<double>(high) - low);
    if (high == 0.0F) {
        *saturation_out = 0.0F;
        *hue_out = -1.0F;
        return;
    }
    *saturation_out = static_cast<float>(static_cast<double>(delta) / high);
    if (delta == 0.0F) {
        return;
    }
    double hue = 0.0;
    if (red == high) {
        hue = (static_cast<double>(green) - blue) / delta;
    } else if (green == high) {
        hue = ((static_cast<double>(blue) - red) / delta) + kHueSectorGreenBlue;
    } else {
        hue = ((static_cast<double>(red) - green) / delta) + kHueSectorBlueRed;
    }
    *hue_out = static_cast<float>(hue);
    const double degrees = static_cast<double>(*hue_out) * 60.0F;
    *hue_out = static_cast<float>(degrees);
    if (degrees < 0.0) {
        *hue_out = static_cast<float>(degrees + kHueFull);
    }
}

} // namespace render
