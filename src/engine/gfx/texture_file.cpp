// Loading an image (the engine's own format: a 32-byte header, a palette for 8-bit images, the texels swizzled)
// into a texture: see bitmap_texture.h.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/hd_ui.h"
#include "engine/gfx/bitmap_texture.h"
#include "platform/platform_d3d.h"
#include "platform/platform_system.h"
#include "engine/gfx/device.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

// The image's header.
struct ImageHeader {
    uint32_t id; // 0x9B: never swizzled (the memory loader only)
    uint32_t unknown4;
    uint32_t width, height;
    uint32_t format;    // 0: 32-bit, 2: 16-bit, 0x13: 8-bit with a palette; others 0-15: their bits with a palette
    uint32_t unknown14; // 0 (not original: hd_ui::kImageMarker | scale in an upscaled image - hd_ui.h)
    uint16_t image_width_, image_height_;
    uint32_t palette_size_; // (the file loader's)
};
static_assert(sizeof(ImageHeader) == 0x20, "the file's header");

// Bits per pixel and whether a palette follows the header.
void decode(uint32_t format, uint32_t* bits, bool* palette)
{
    switch (format) {
    case 0:
        *bits = 32;
        *palette = false;
        break;
    case 2:
        *bits = 16;
        *palette = false;
        break;
    case 0x13:
        *bits = 8;
        *palette = true;
        break;
    default:
        *bits = format;
        *palette = format < 0x10;
        break;
    }
}

bool power_of_two(uint32_t n) { return (n & (n - 1)) == 0; }

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

TextureRef* alloc_pc_texture()
{
    void* memory = memory::alloc(sizeof(TextureRef));
    return memory ? static_cast<TextureRef*>(memory)->make_new() : nullptr;
}

// The texture's sizes from the header: its own (the image's if the size isn't a power of two) and the image's.
void set_sizes(TextureRef* texture, const ImageHeader& header, bool as_image)
{
    if (as_image) {
        texture->width = header.image_width_;
        texture->height = header.image_height_;
    } else {
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
    }
    texture->image_width_ = header.image_width_;
    texture->image_height_ = header.image_height_;
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
        return std::malloc(size);
    return memory::alloc(size);
}
void free_texel_buffer(void* buffer, [[maybe_unused]] const ImageHeader& header)
{
    if (hd_ui::image_scale(header.unknown14) != 1)
        return std::free(buffer);
    memory::Hub::sole()->dealloc(buffer);
}

// Not original - HD menu UI (engine/gfx/hd_ui.h, [features] hd_ui): an upscaled image keeps its full-size
// Direct3D texture but reports the original's sizes, so what sizes itself by them is drawn as large as before.
void apply_hd_scale(TextureRef* texture, const ImageHeader& header)
{
    uint32_t scale = hd_ui::image_scale(header.unknown14);
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

} // namespace

// 0x004E0790
TextureRef* __cdecl read_texture_file(const char* name)
{
    file::Hdl file = file::unseal(name, "rb");
    if (!file)
        return nullptr;
    ImageHeader header;
    file::get_take_in(&header, 0x20, 1, file);
    if (is_hd_dxt(header)) { // (not original - hd_ui.h)
        TextureRef* hd = alloc_pc_texture();
        hd->palette = nullptr;
        input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
                                    reinterpret_cast<IDirect3DTexture9**>(&hd->texture), nullptr);
        set_sizes(hd, header, false);
        D3DLOCKED_RECT locked;
        static_cast<IDirect3DTexture9*>(hd->texture)->LockRect(0, &locked, nullptr, 0);
        file::get_take_in(locked.pBits, hd_dxt_size(header), 1, file);
        file::shut(file);
        get_finish(hd);
        apply_hd_scale(hd, header);
        return hd;
    }
    uint32_t bits;
    bool palettized;
    decode(header.format, &bits, &palettized);
    TextureRef* texture = alloc_pc_texture();
    texture->palette = nullptr;
    uint8_t palette[0x400];
    if (palettized)
        file::get_take_in(palette, header.palette_size_, 1, file);
    // Not a power of two each way: the image is loaded as it is (no swizzling).
    bool plain = !(header.width && power_of_two(header.width) && header.height && power_of_two(header.height));
    input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                reinterpret_cast<IDirect3DTexture9**>(&texture->texture), nullptr);
    set_sizes(texture, header, plain);
    uint32_t size = ((bits >> 3) * texture->height * texture->width + 3) & ~3u;
    D3DLOCKED_RECT locked;
    static_cast<IDirect3DTexture9*>(texture->texture)->LockRect(0, &locked, nullptr, 0);
    if (plain && !palettized) {
        file::get_take_in(locked.pBits, size, 1, file);
    } else {
        void* buffer = new_texel_buffer(size, header);
        file::get_take_in(buffer, size, 1, file);
        void* indices = nullptr;
        void* texels;
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
TextureRef* __cdecl load_texture_memory(const uint8_t* data)
{
    ImageHeader header;
    std::memcpy(&header, data, sizeof(header));
    const uint8_t* at = data + 0x20;
    if (is_hd_dxt(header)) { // (not original - hd_ui.h)
        TextureRef* hd = alloc_pc_texture();
        hd->palette = nullptr;
        input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
                                    reinterpret_cast<IDirect3DTexture9**>(&hd->texture), nullptr);
        if (!hd->texture)
            return nullptr;
        set_sizes(hd, header, false);
        D3DLOCKED_RECT locked;
        static_cast<IDirect3DTexture9*>(hd->texture)->LockRect(0, &locked, nullptr, 0);
        std::memcpy(locked.pBits, at, hd_dxt_size(header));
        get_finish(hd);
        apply_hd_scale(hd, header);
        return hd;
    }
    uint32_t bits;
    bool palettized;
    decode(header.format, &bits, &palettized);
    TextureRef* texture = alloc_pc_texture();
    texture->palette = nullptr;
    uint8_t palette[0x400];
    if (palettized) {
        std::memcpy(palette, at, 0x400);
        at += 0x400;
    }
    bool plain = !(header.width && power_of_two(header.width) && header.height && power_of_two(header.height));
    if (header.id == 0x9B)
        plain = true;
    input_port()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                reinterpret_cast<IDirect3DTexture9**>(&texture->texture), nullptr);
    if (!texture->texture)
        return nullptr; // (the texture object stays made and listed)
    set_sizes(texture, header, plain);
    uint32_t bytes_per_pixel = bits >> 3;
    uint32_t size = (texture->height * texture->width * bytes_per_pixel + 3) & ~3u;
    D3DLOCKED_RECT locked;
    static_cast<IDirect3DTexture9*>(texture->texture)->LockRect(0, &locked, nullptr, 0);
    if (plain && !palettized) {
        std::memcpy(locked.pBits, at, size);
    } else {
        void* buffer = new_texel_buffer(size, header);
        std::memcpy(buffer, at, size);
        void* indices = nullptr;
        void* texels;
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
