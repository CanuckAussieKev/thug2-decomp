// Loading an image (the engine's own format: a 32-byte header, a palette for 8-bit images, the texels swizzled -
// formats/image_format.h) into a texture: see bitmap_texture.h.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/hd_ui.h"
#include "engine/gfx/bitmap_texture.h"
#include "formats/image_format.h"
#include "formats/byte_stream.h"
#include "platform/platform_d3d.h"
#include "platform/platform_system.h"
#include "engine/gfx/device.h"
#include "engine/core/owner.h"
#include "engine/core/raw_view.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

// The image's header: image_format.h (read through its reader).
using image_format::ImageHeader;

// The swizzled texels into rows and 8-bit indices through a palette (the platform layer's).
void unswizzle(void* out, const void* in, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    os_layer::unswizzle(out, in, static_cast<int>(width), static_cast<int>(height), bytes_per_pixel);
}
void expand_palette(void* out, const void* indices, uint32_t size, const void* palette)
{
    os_layer::expand_palette(static_cast<uint32_t*>(out), static_cast<const uint8_t*>(indices), static_cast<int>(size),
                             static_cast<const uint32_t*>(palette));
}

memory::owner<TextureRef*> alloc_pc_texture()
{
    void* memory = memory::alloc(sizeof(TextureRef));
    return memory ? memory::construct_in<TextureRef>(memory) : nullptr;
}

// The texture's sizes from the header: its own (the image's if the size isn't a power of two) and the image's.
void set_sizes(TextureRef* texture, const ImageHeader& header, bool as_image)
{
    if (as_image) {
        texture->width = header.image_width;
        texture->height = header.image_height;
    } else {
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
    }
    texture->image_width_ = header.image_width;
    texture->image_height_ = header.image_height;
}

void get_finish(TextureRef* texture)
{
    texture->unknowne_ = 0;
    texture->depth = 0x20;
    texture->unknownf_ = 0;
    texture->levels = 1;
    static_cast<IDirect3DTexture9*>(texture->texture)->UnlockRect(0);
}

// The buffer the stored texels are read into. Not original - HD menu UI (engine/gfx/hd_ui.h): an upscaled image's
// (up to 16 MB) comes from the C runtime's heap, not the game's - the game's heap in use while the front end loads its
// sprites hasn't room for them (the first one made memory::alloc fail and the read end the game).
void* new_texel_buffer(uint32_t size, [[maybe_unused]] const ImageHeader& header)
{
    if (hd_ui::image_scale(header.unknown14) != 1)
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): the C runtime's allocator, as the library / original wants
        return std::malloc(size);
    return memory::alloc(size);
}
void free_texel_buffer(void* buffer, [[maybe_unused]] const ImageHeader& header)
{
    if (hd_ui::image_scale(header.unknown14) != 1)
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): the C runtime's allocator, as the library / original wants
        return std::free(buffer);
    memory::Hub::sole()->dealloc(buffer);
}

// Not original - HD menu UI (engine/gfx/hd_ui.h, [features] hd_ui): an upscaled image keeps its full-size
// Direct3D texture but reports the original's sizes, so what sizes itself by them is drawn as large as before.
void apply_hd_scale(TextureRef* texture, const ImageHeader& header)
{
    const uint32_t scale = hd_ui::image_scale(header.unknown14);
    if (scale == 1)
        return;
    hd_ui::note_image(scale, texture->width, texture->height);
    texture->width = static_cast<uint16_t>(texture->width / scale);
    texture->height = static_cast<uint16_t>(texture->height / scale);
    texture->image_width_ = static_cast<uint16_t>(texture->image_width_ / scale);
    texture->image_height_ = static_cast<uint16_t>(texture->image_height_ / scale);
}

// Not original - HD menu UI (hd_ui.h): an upscaled opaque image (a loading screen) stored as DXT1 blocks, header format
// hd_ui::kFormatDxt1. The Direct3D texture is made D3DFMT_DXT1 (one level: drawn full screen, about 1:1) and the blocks
// are read straight into it; the TextureRef is finished like the 32-bit ones (its sizes divided by apply_hd_scale).
bool is_hd_dxt(const ImageHeader& header)
{
    return header.format == hd_ui::kFormatDxt1 && hd_ui::image_scale(header.unknown14) != 1 && header.width % 4 == 0 &&
           header.height % 4 == 0;
}
uint32_t hd_dxt_size(const ImageHeader& header) { return header.width * header.height / 2; }

// Not original - an image whose texels wouldn't fit the texture made for it: refused (the original read them past
// the Direct3D surface / its palette buffer). Valid images: the plain texels within the texture's sizes, the
// palette within 0x400 bytes.
bool fits_its_texture(const ImageHeader& header, bool palettized, uint32_t palette_size, bool plain)
{
    if (palettized && palette_size > image_format::kPaletteCapacity)
        return false;
    return !plain || static_cast<uint32_t>(header.image_width) * header.image_height <= header.width * header.height;
}

} // namespace

// 0x004E0790
TextureRef* read_texture_file(const char* name)
{
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return nullptr;
    uint8_t header_bytes[image_format::kHeaderSize] = {};
    file::get_take_in(header_bytes, image_format::kHeaderSize, 1, file);
    util::ByteReader header_reader(header_bytes, sizeof(header_bytes));
    ImageHeader header;
    image_format::read_image_header(header_reader, &header);
    if (is_hd_dxt(header)) { // (not original - hd_ui.h)
        TextureRef* hd = alloc_pc_texture();
        hd->palette = nullptr;
        input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
                                    raw::as<IDirect3DTexture9*>(&hd->texture), nullptr);
        set_sizes(hd, header, false);
        D3DLOCKED_RECT locked;
        static_cast<IDirect3DTexture9*>(hd->texture)->LockRect(0, &locked, nullptr, 0);
        file::get_take_in(locked.pBits, static_cast<int>(hd_dxt_size(header)), 1, file);
        file::shut(file);
        get_finish(hd);
        apply_hd_scale(hd, header);
        return hd;
    }
    uint32_t bits = 0;
    bool palettized = false;
    image_format::decode_format(header.format, &bits, &palettized);
    // Not as they are stored: the image is loaded as it is (no swizzling).
    const bool plain = image_format::stored_plain(header, false);
    if (!fits_its_texture(header, palettized, header.palette_size, plain)) {
        debug::printf("%s: the image doesn't fit its texture\n", name);
        file::shut(file);
        return nullptr;
    }
    TextureRef* texture = alloc_pc_texture();
    texture->palette = nullptr;
    uint8_t palette[image_format::kPaletteCapacity];
    if (palettized)
        file::get_take_in(palette, static_cast<int>(header.palette_size), 1, file);
    input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                raw::as<IDirect3DTexture9*>(&texture->texture), nullptr);
    set_sizes(texture, header, plain);
    const uint32_t size = image_format::texel_size(header, plain);
    D3DLOCKED_RECT locked;
    static_cast<IDirect3DTexture9*>(texture->texture)->LockRect(0, &locked, nullptr, 0);
    if (plain && !palettized) {
        file::get_take_in(locked.pBits, static_cast<int>(size), 1, file);
    } else {
        void* buffer = new_texel_buffer(size, header);
        file::get_take_in(buffer, static_cast<int>(size), 1, file);
        void* indices = nullptr;
        void* texels = nullptr;
        if (plain) {
            texels = buffer;
        } else {
            texels = palettized ? (indices = memory::alloc(size)) : locked.pBits;
            unswizzle(texels, buffer, header.width, header.height, bits >> 3);
        }
        if (palettized)
            expand_palette(locked.pBits, texels, size, palette);
        free_texel_buffer(buffer, header);
        memory::Hub::sole()->dealloc(indices);
    }
    file::shut(file);
    get_finish(texture);
    apply_hd_scale(texture, header); // (not original - hd_ui.h)
    return texture;
}

// 0x004E0A90
TextureRef* load_texture_memory(const uint8_t* data, size_t data_size)
{
    // Read through image_format's reader, bounded by the image's size (not original: the original read on past it;
    // texels cut short are filled with zeros, an image that doesn't fit its texture is refused).
    util::ByteReader reader(data, data_size ? data_size : util::ByteReader::kUnknownSize);
    ImageHeader header;
    if (!image_format::read_image_header(reader, &header))
        return nullptr;
    if (is_hd_dxt(header)) { // (not original - hd_ui.h)
        if (reader.remaining() < hd_dxt_size(header))
            return nullptr;
        TextureRef* hd = alloc_pc_texture();
        hd->palette = nullptr;
        input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
                                    raw::as<IDirect3DTexture9*>(&hd->texture), nullptr);
        if (!hd->texture)
            return nullptr;
        set_sizes(hd, header, false);
        D3DLOCKED_RECT locked;
        static_cast<IDirect3DTexture9*>(hd->texture)->LockRect(0, &locked, nullptr, 0);
        reader.bytes(locked.pBits, hd_dxt_size(header));
        get_finish(hd);
        apply_hd_scale(hd, header);
        return hd;
    }
    uint32_t bits = 0;
    bool palettized = false;
    image_format::decode_format(header.format, &bits, &palettized);
    const bool plain = image_format::stored_plain(header, true);
    // (the memory loader's palette is always 0x400 bytes)
    if (!fits_its_texture(header, palettized, image_format::kPaletteCapacity, plain) ||
        (palettized && reader.remaining() < image_format::kPaletteCapacity))
        return nullptr;
    TextureRef* texture = alloc_pc_texture();
    texture->palette = nullptr;
    uint8_t palette[image_format::kPaletteCapacity];
    if (palettized)
        reader.bytes(palette, image_format::kPaletteCapacity);
    input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                raw::as<IDirect3DTexture9*>(&texture->texture), nullptr);
    if (!texture->texture)
        return nullptr; // (the texture object stays made and listed)
    set_sizes(texture, header, plain);
    const uint32_t bytes_per_pixel = bits >> 3;
    const uint32_t size = image_format::texel_size(header, plain);
    // The texels: as many as there are, the rest zeros.
    auto copy_texels = [&reader, size](void* into) {
        const size_t available = reader.remaining() < size ? reader.remaining() : size;
        std::memcpy(into, reader.cursor(), available);
        std::memset(static_cast<uint8_t*>(into) + available, 0, size - available);
    };
    D3DLOCKED_RECT locked;
    static_cast<IDirect3DTexture9*>(texture->texture)->LockRect(0, &locked, nullptr, 0);
    if (plain && !palettized) {
        copy_texels(locked.pBits);
    } else {
        void* buffer = new_texel_buffer(size, header);
        copy_texels(buffer);
        void* indices = nullptr;
        void* texels = nullptr;
        if (plain) {
            texels = buffer;
        } else {
            texels = palettized ? (indices = memory::alloc(size)) : locked.pBits;
            unswizzle(texels, buffer, header.width, header.height, bytes_per_pixel);
        }
        if (palettized)
            expand_palette(locked.pBits, texels, size, palette);
        free_texel_buffer(buffer, header);
        memory::Hub::sole()->dealloc(indices);
    }
    get_finish(texture);
    apply_hd_scale(texture, header); // (not original - hd_ui.h)
    return texture;
}

} // namespace render
