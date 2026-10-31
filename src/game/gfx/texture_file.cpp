// Loading an image (the engine's own format: a 32-byte header, a palette for 8-bit images, the texels swizzled)
// into a texture: see texture.h.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/gfx/hd_ui.h"
#include "game/gfx/texture.h"
#include "game/sys/platform_d3d.h"
#include "game/sys/platform_system.h"
#include "game/gfx/device.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }

// The image's header.
struct ImageHeader {
    uint32_t id;           // 0x9B: never swizzled (the memory loader only)
    uint32_t unknown4;
    uint32_t width, height;
    uint32_t format;       // 0: 32-bit, 2: 16-bit, 0x13: 8-bit with a palette; others 0-15: their bits with a palette
    uint32_t unknown14;    // 0 (not original: HdUi::kImageMarker | scale in an upscaled image - hd_ui.h)
    uint16_t imageWidth, imageHeight;
    uint32_t paletteSize;  // (the file loader's)
};
static_assert(sizeof(ImageHeader) == 0x20, "the file's header");

// Bits per pixel and whether a palette follows the header.
void Decode(uint32_t format, uint32_t* bits, bool* palette)
{
    switch (format) {
    case 0: *bits = 32; *palette = false; break;
    case 2: *bits = 16; *palette = false; break;
    case 0x13: *bits = 8; *palette = true; break;
    default: *bits = format; *palette = format < 0x10; break;
    }
}

bool PowerOfTwo(uint32_t n) { return (n & (n - 1)) == 0; }

// The swizzled texels into rows and 8-bit indices through a palette (the platform layer's).
void Unswizzle(void* out, const void* in, uint32_t width, uint32_t height, uint32_t bytesPerPixel)
{
    Platform::Unswizzle(out, in, static_cast<int>(width), static_cast<int>(height), bytesPerPixel);
}
void ExpandPalette(void* out, const void* indices, uint32_t size, const void* palette)
{
    Platform::ExpandPalette(static_cast<uint32_t*>(out), static_cast<const uint8_t*>(indices), static_cast<int>(size),
                            static_cast<const uint32_t*>(palette));
}

sTexture* NewTexture()
{
    void* memory = Mem::New(sizeof(sTexture));
    return memory ? static_cast<sTexture*>(memory)->Construct() : nullptr;
}

// The texture's sizes from the header: its own (the image's if the size isn't a power of two) and the image's.
void SetSizes(sTexture* texture, const ImageHeader& header, bool asImage)
{
    if (asImage) {
        texture->width = header.imageWidth;
        texture->height = header.imageHeight;
    } else {
        texture->width = static_cast<uint16_t>(header.width);
        texture->height = static_cast<uint16_t>(header.height);
    }
    texture->imageWidth = header.imageWidth;
    texture->imageHeight = header.imageHeight;
}

void Finish(sTexture* texture)
{
    texture->unknownE = 0;
    texture->depth = 0x20;
    texture->unknownF = 0;
    texture->levels = 1;
    static_cast<IDirect3DTexture9*>(texture->texture)->UnlockRect(0);
}

// The buffer the stored texels are read into. Not original - HD menu UI (game/gfx/hd_ui.h): an upscaled image's
// (up to 16 MB) comes from the C runtime's heap, not the game's - the game's heap in use while the front end loads its
// sprites hasn't room for them (the first one made Mem::New fail and the read end the game).
void* NewTexelBuffer(uint32_t size, [[maybe_unused]] const ImageHeader& header)
{
    if (HdUi::ImageScale(header.unknown14) != 1)
        return std::malloc(size);
    return Mem::New(size);
}
void FreeTexelBuffer(void* buffer, [[maybe_unused]] const ImageHeader& header)
{
    if (HdUi::ImageScale(header.unknown14) != 1)
        return std::free(buffer);
    Mem::Manager::Instance()->Free(buffer);
}

// Not original - HD menu UI (game/gfx/hd_ui.h, [features] hd_ui): an upscaled image keeps its full-size
// Direct3D texture but reports the original's sizes, so what sizes itself by them is drawn as large as before.
void ApplyHdScale(sTexture* texture, const ImageHeader& header)
{
    uint32_t scale = HdUi::ImageScale(header.unknown14);
    if (scale == 1)
        return;
    HdUi::NoteImage(scale, texture->width, texture->height);
    texture->width = static_cast<uint16_t>(texture->width / scale);
    texture->height = static_cast<uint16_t>(texture->height / scale);
    texture->imageWidth = static_cast<uint16_t>(texture->imageWidth / scale);
    texture->imageHeight = static_cast<uint16_t>(texture->imageHeight / scale);
}

// Not original - HD menu UI (hd_ui.h): an upscaled opaque image (a loading screen) stored as DXT1 blocks, header format
// HdUi::kFormatDxt1. The Direct3D texture is made D3DFMT_DXT1 (one level: drawn full screen, about 1:1) and the blocks
// are read straight into it; the sTexture is finished like the 32-bit ones (its sizes divided by ApplyHdScale).
bool IsHdDxt(const ImageHeader& header)
{
    return header.format == HdUi::kFormatDxt1 && HdUi::ImageScale(header.unknown14) != 1 && header.width % 4 == 0 &&
           header.height % 4 == 0;
}
uint32_t HdDxtSize(const ImageHeader& header) { return header.width * header.height / 2; }

} // namespace

// 0x004E0790
sTexture* __cdecl LoadTextureFile(const char* name)
{
    File::Handle file = File::Open(name, "rb");
    if (!file)
        return nullptr;
    ImageHeader header;
    File::Read(&header, 0x20, 1, file);
    if (IsHdDxt(header)) { // (not original - hd_ui.h)
        sTexture* hd = NewTexture();
        hd->palette = nullptr;
        Device()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
                                reinterpret_cast<IDirect3DTexture9**>(&hd->texture), nullptr);
        SetSizes(hd, header, false);
        D3DLOCKED_RECT locked;
        static_cast<IDirect3DTexture9*>(hd->texture)->LockRect(0, &locked, nullptr, 0);
        File::Read(locked.pBits, HdDxtSize(header), 1, file);
        File::Close(file);
        Finish(hd);
        ApplyHdScale(hd, header);
        return hd;
    }
    uint32_t bits;
    bool palettized;
    Decode(header.format, &bits, &palettized);
    sTexture* texture = NewTexture();
    texture->palette = nullptr;
    uint8_t palette[0x400];
    if (palettized)
        File::Read(palette, header.paletteSize, 1, file);
    // Not a power of two each way: the image is loaded as it is (no swizzling).
    bool plain = !(header.width && PowerOfTwo(header.width) && header.height && PowerOfTwo(header.height));
    Device()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                            reinterpret_cast<IDirect3DTexture9**>(&texture->texture), nullptr);
    SetSizes(texture, header, plain);
    uint32_t size = ((bits >> 3) * texture->height * texture->width + 3) & ~3u;
    D3DLOCKED_RECT locked;
    static_cast<IDirect3DTexture9*>(texture->texture)->LockRect(0, &locked, nullptr, 0);
    if (plain && !palettized) {
        File::Read(locked.pBits, size, 1, file);
    } else {
        void* buffer = NewTexelBuffer(size, header);
        File::Read(buffer, size, 1, file);
        void* indices = nullptr;
        void* texels;
        if (plain) {
            texels = buffer;
        } else {
            texels = palettized ? (indices = Mem::New(size)) : locked.pBits;
            Unswizzle(texels, buffer, header.width, header.height, bits >> 3);
        }
        if (palettized)
            ExpandPalette(locked.pBits, texels, size, palette);
        FreeTexelBuffer(buffer, header);
        Mem::Manager::Instance()->Free(indices);
    }
    File::Close(file);
    Finish(texture);
    ApplyHdScale(texture, header); // (not original - hd_ui.h)
    return texture;
}

// 0x004E0A90
sTexture* __cdecl LoadTextureMemory(const uint8_t* data)
{
    ImageHeader header;
    std::memcpy(&header, data, sizeof(header));
    const uint8_t* at = data + 0x20;
    if (IsHdDxt(header)) { // (not original - hd_ui.h)
        sTexture* hd = NewTexture();
        hd->palette = nullptr;
        Device()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED,
                                reinterpret_cast<IDirect3DTexture9**>(&hd->texture), nullptr);
        if (!hd->texture)
            return nullptr;
        SetSizes(hd, header, false);
        D3DLOCKED_RECT locked;
        static_cast<IDirect3DTexture9*>(hd->texture)->LockRect(0, &locked, nullptr, 0);
        std::memcpy(locked.pBits, at, HdDxtSize(header));
        Finish(hd);
        ApplyHdScale(hd, header);
        return hd;
    }
    uint32_t bits;
    bool palettized;
    Decode(header.format, &bits, &palettized);
    sTexture* texture = NewTexture();
    texture->palette = nullptr;
    uint8_t palette[0x400];
    if (palettized) {
        std::memcpy(palette, at, 0x400);
        at += 0x400;
    }
    bool plain = !(header.width && PowerOfTwo(header.width) && header.height && PowerOfTwo(header.height));
    if (header.id == 0x9B)
        plain = true;
    Device()->CreateTexture(header.width, header.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                            reinterpret_cast<IDirect3DTexture9**>(&texture->texture), nullptr);
    if (!texture->texture)
        return nullptr; // (the texture object stays made and listed)
    SetSizes(texture, header, plain);
    uint32_t bytesPerPixel = bits >> 3;
    uint32_t size = (texture->height * texture->width * bytesPerPixel + 3) & ~3u;
    D3DLOCKED_RECT locked;
    static_cast<IDirect3DTexture9*>(texture->texture)->LockRect(0, &locked, nullptr, 0);
    if (plain && !palettized) {
        std::memcpy(locked.pBits, at, size);
    } else {
        void* buffer = NewTexelBuffer(size, header);
        std::memcpy(buffer, at, size);
        void* indices = nullptr;
        void* texels;
        if (plain) {
            texels = buffer;
        } else {
            texels = palettized ? (indices = Mem::New(size)) : locked.pBits;
            Unswizzle(texels, buffer, header.width, header.height, bytesPerPixel);
        }
        if (palettized)
            ExpandPalette(locked.pBits, texels, size, palette);
        FreeTexelBuffer(buffer, header);
        Mem::Manager::Instance()->Free(indices);
    }
    Finish(texture);
    ApplyHdScale(texture, header); // (not original - hd_ui.h)
    return texture;
}

} // namespace Nx
