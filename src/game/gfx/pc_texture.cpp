// The PC texture and texture dictionary: see pc_texture.h. The colour conversions follow the original's x87 code:
// double precision where it keeps values in registers, floats where it stores them.

#include "game/gfx/pc_texture.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/texture_dict.h"

#include <windows.h>

#include <d3d9.h>

#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/alloc.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/lst/hashtable.h"
#include "game/mth/math.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/core/stubs.h"
#include "game/gfx/device.h"
#include "game/sys/crt.h"

namespace Nx {

namespace {

extern void** const kTextureVtable;
extern void** const kTexDictVtable;

sTextureEdit* Edit(const CPcTexture* texture) { return static_cast<sTextureEdit*>(texture->engine); }
IDirect3DDevice9* Device() { return g_d3dDevice; }

CPcTexture* NewTexture()
{
    void* memory = Mem::New(sizeof(CPcTexture));
    return memory ? static_cast<CPcTexture*>(memory)->Construct() : nullptr;
}

} // namespace

sTexture* CPcTexture::Texture() const
{
    return flags & 1 ? Edit(this)->texture : static_cast<sTexture*>(engine);
}

// 0x004C7ED0
CPcTexture* CPcTexture::Construct()
{
    reinterpret_cast<CTexture*>(this)->CTexture::Construct();
    unknownD = 0;
    engine = nullptr;
    unknownC = 0;
    flags = 0;
    vtable = reinterpret_cast<void**>(kTextureVtable);
    return this;
}

// 0x004C7EF0
void CPcTexture::SetTexture(sTexture* texture)
{
    engine = texture;
    checksum = texture->checksum;
}

// 0x004C9D00
void CPcTexture::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kTextureVtable);
    if (flags & 1)
        EndEdit();
    if (auto* texture = static_cast<sTexture*>(engine)) {
        texture->DestroyBody();
        Mem::Manager::Get()->Free(texture);
    }
    reinterpret_cast<CTexture*>(this)->CTexture::DestroyBody();
}

// 0x004CAE60
CPcTexture* CPcTexture::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C7F00
bool CPcTexture::LoadFile(const char* name, int /*unused1*/, int /*unused2*/)
{
    char path[0x100];
    size_t length = std::strlen(name); // (unchecked, as the original)
    std::memcpy(path, name, length);
    std::memcpy(path + length, ".img.xbx", 9);
    engine = LoadTextureFile(path);
    return engine != nullptr;
}

// 0x004C7F80
bool CPcTexture::LoadMemory(const uint8_t* data, int /*size*/, int, int, int)
{
    engine = LoadTextureMemory(data);
    return engine != nullptr;
}

// 0x004C9EC0
bool CPcTexture::WriteBack(int /*unused1*/, int /*unused2*/)
{
    auto* texels = static_cast<uint32_t*>(Texture()->Lock());
    const uint32_t* pixels = flags & 1 ? Edit(this)->pixels : nullptr; // (null read from, as the original)
    using Get = uint16_t(__thiscall*)(CPcTexture*);
    uint32_t width = reinterpret_cast<Get>(vtable[33])(this);
    uint32_t height = reinterpret_cast<Get>(vtable[34])(this);
    uint32_t* row = texels + (height - 1) * width;
    for (uint32_t y = height; y; --y) {
        for (uint32_t x = 0; x < width; ++x)
            row[x] = *pixels++;
        row -= width;
    }
    Texture()->Unlock();
    return true;
}

// 0x004C98B0
bool CPcTexture::EndEdit()
{
    // (the edit's fields read whether or not it is one, as the original)
    auto* edit = Edit(this);
    sTexture* texture = flags & 1 ? edit->texture : static_cast<sTexture*>(engine);
    if (edit->pixels) {
        Mem::Manager::Get()->Free(edit->pixels);
        Edit(this)->pixels = nullptr;
    }
    if (Edit(this)->original) {
        Mem::Manager::Get()->Free(Edit(this)->original);
        Edit(this)->original = nullptr;
    }
    if (sTexture* kept = Edit(this)->kept) {
        kept->DestroyBody();
        Mem::Manager::Get()->Free(kept);
        Edit(this)->kept = nullptr;
    }
    Mem::Manager::Get()->Free(engine);
    engine = texture;
    flags &= ~1u;
    return true;
}

// 0x004C7FB0
uint16_t CPcTexture::GetWidth()
{
    sTexture* texture = Texture();
    return texture ? texture->imageWidth : 0;
}

// 0x004C7FE0
uint16_t CPcTexture::GetHeight()
{
    sTexture* texture = Texture();
    return texture ? texture->imageHeight : 0;
}

// 0x004C8010
uint8_t CPcTexture::GetDepth()
{
    sTexture* texture = Texture();
    return texture ? texture->depth : 0;
}

// 0x004C8030
uint8_t CPcTexture::GetUnknownE()
{
    sTexture* texture = Texture();
    return texture ? texture->unknownE : 0;
}

// 0x004C8050
uint8_t CPcTexture::GetUnknownC() { return unknownC; }

// 0x004C8060
uint8_t CPcTexture::GetUnknownD() { return unknownD; }

// 0x004C8450
CPcTexture* CPcTexture::ConstructCopy(const CPcTexture* source)
{
    reinterpret_cast<CTexture*>(this)->CTexture::Construct();
    vtable = reinterpret_cast<void**>(kTextureVtable);
    unknownC = source->unknownC;
    flags = source->flags;
    sTexture* copy = nullptr;
    if (source->Texture()) {
        void* memory = Mem::New(sizeof(sTexture));
        copy = memory ? static_cast<sTexture*>(memory)->ConstructCopy(source->Texture()) : nullptr;
    }
    using Get = uint16_t(__thiscall*)(const CPcTexture*);
    uint32_t width = reinterpret_cast<Get>(source->vtable[33])(source);
    uint32_t height = reinterpret_cast<Get>(source->vtable[34])(source);
    if (!(source->flags & 1)) {
        engine = copy;
        return this;
    }
    auto* edit = static_cast<sTextureEdit*>(Mem::New(sizeof(sTextureEdit)));
    if (edit) // (no check after: written through null, as the original)
        *edit = {};
    engine = edit;
    edit->texture = copy;
    const sTextureEdit* from = Edit(source);
    if (from->original) {
        uint32_t bytes = height * width * 4;
        edit->original = static_cast<uint32_t*>(Mem::New(bytes));
        std::memcpy(edit->original, from->original, bytes);
    } else {
        edit->original = nullptr;
    }
    // (its own pixels tested, not the source's: always null here, so never copied - as the original)
    if (edit->pixels) {
        edit->pixels = static_cast<uint32_t*>(Mem::New(height * height * 4));
        std::memcpy(edit->pixels, from->pixels, width * height * 4);
    } else {
        edit->pixels = nullptr;
    }
    if (from->kept) {
        void* memory = Mem::New(sizeof(sTexture));
        edit->kept = memory ? static_cast<sTexture*>(memory)->ConstructCopy(from->kept) : nullptr;
    } else {
        edit->kept = nullptr;
    }
    return this;
}

// 0x004C8830
bool CPcTexture::Create(uint32_t newChecksum, uint32_t width, uint32_t height, int bitsPerPixel, int, int)
{
    sTexture* old = Texture();
    if (old && old->texture) // (released, the old texture itself kept)
        static_cast<IUnknown*>(old->texture)->Release();
    void* memory = Mem::New(sizeof(sTexture));
    sTexture* texture = memory ? static_cast<sTexture*>(memory)->Construct() : nullptr;
    checksum = newChecksum;
    engine = texture; // (an edit's place too)
    texture->checksum = newChecksum; // (no check, as the original)
    texture->unknownF = 0;
    texture->imageWidth = static_cast<uint16_t>(width);
    texture->imageHeight = static_cast<uint16_t>(height);
    if (bitsPerPixel < 8)
        bitsPerPixel = 32;
    uint32_t format = newChecksum; // (any other depth: the checksum taken for the format, as the original)
    switch (bitsPerPixel) {
    case 1:
    case 2:
    case 4:
    case 8:
        format = D3DFMT_P8;
        texture->depth = 8;
        break;
    case 16:
        format = D3DFMT_X1R5G5B5;
        texture->depth = 16;
        break;
    case 32:
        format = D3DFMT_X8R8G8B8;
        texture->depth = 32;
        break;
    default:
        break;
    }
    auto** d3d = reinterpret_cast<IDirect3DTexture9**>(&texture->texture);
    Device()->CreateTexture(width, height, 0, 0, static_cast<D3DFORMAT>(format), D3DPOOL_MANAGED, d3d, nullptr);
    D3DSURFACE_DESC desc;
    (*d3d)->GetLevelDesc(0, &desc);
    texture->height = static_cast<uint16_t>(desc.Height);
    texture->width = static_cast<uint16_t>(desc.Width);
    texture->unknownE = format == D3DFMT_P8 ? 0x20 : 0;
    return true;
}

// 0x004C86A0
bool CPcTexture::CopyFrom(const CPcTexture* source)
{
    sTexture* from = source->Texture();
    auto** into = reinterpret_cast<IDirect3DTexture9**>(&Texture()->texture);
    if (*into)
        (*into)->Release();
    auto* fromTexture = static_cast<IDirect3DTexture9*>(from->texture);
    DWORD levels = fromTexture->GetLevelCount();
    D3DSURFACE_DESC desc;
    fromTexture->GetLevelDesc(0, &desc);
    if (Device()->CreateTexture(desc.Width, desc.Height, levels, 0, desc.Format, D3DPOOL_MANAGED, into, nullptr) !=
        D3D_OK)
        Crt::Exit(0); // (the game's exit())
    for (DWORD level = 0; level < levels; ++level) {
        static_cast<IDirect3DTexture9*>(from->texture)->GetLevelDesc(level, &desc);
        uint32_t rowBytes, rows = desc.Height;
        if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) {
            rowBytes = desc.Width << 2;
        } else { // (blocks of 4 rows; the row's bytes as the original works them out)
            rows = (rows + 3) >> 2;
            rowBytes = desc.Format == D3DFMT_DXT1 ? desc.Width << 3 : desc.Width << 4;
        }
        D3DLOCKED_RECT fromRect, intoRect;
        static_cast<IDirect3DTexture9*>(from->texture)->LockRect(level, &fromRect, nullptr, 0);
        (*into)->LockRect(level, &intoRect, nullptr, 0);
        auto* write = static_cast<uint8_t*>(intoRect.pBits);
        auto* read = static_cast<const uint8_t*>(fromRect.pBits);
        for (uint32_t row = rows; row; --row) {
            std::memcpy(write, read, rowBytes);
            write += intoRect.Pitch;
            read += fromRect.Pitch;
        }
        static_cast<IDirect3DTexture9*>(from->texture)->UnlockRect(level);
        (*into)->UnlockRect(level);
    }
    return true;
}

// 0x004C8A60
bool CPcTexture::BeginEdit(bool keepTexture, bool keepCopy)
{
    sTexture* texture = Texture();
    uint32_t width = texture->width, height = texture->height;
    auto* edit = static_cast<sTextureEdit*>(Mem::New(sizeof(sTextureEdit)));
    if (edit) // (no check after: written through null, as the original)
        *edit = {};
    engine = edit;
    edit->texture = texture;
    uint32_t bytes = width * height * 4;
    edit->pixels = static_cast<uint32_t*>(Mem::New(bytes));
    flags |= 1;
    uint32_t* pixels = Edit(this)->pixels;
    auto* d3d = static_cast<IDirect3DTexture9*>(texture->texture);
    D3DSURFACE_DESC desc;
    d3d->GetLevelDesc(0, &desc);
    D3DLOCKED_RECT rect;
    d3d->LockRect(0, &rect, nullptr, D3DLOCK_READONLY);
    if (texture->depth != 8 && texture->depth == 32) { // (rows bottom up, the pitch taken as the width)
        const uint32_t* row = static_cast<const uint32_t*>(rect.pBits) + (height - 1) * width;
        for (uint32_t y = height; y; --y) {
            std::memcpy(pixels, row, width * 4);
            pixels += width;
            row -= width;
        }
    }
    static_cast<IDirect3DTexture9*>(texture->texture)->UnlockRect(0);
    if (keepCopy) {
        Edit(this)->original = static_cast<uint32_t*>(Mem::New(bytes));
        std::memcpy(Edit(this)->original, Edit(this)->pixels, bytes);
    }
    if (keepTexture) {
        void* memory = Mem::New(sizeof(sTexture));
        Edit(this)->kept = memory ? static_cast<sTexture*>(memory)->Construct() : nullptr;
        sTexture* kept = Edit(this)->kept; // (no check, as the original)
        kept->imageWidth = static_cast<uint16_t>(width);
        kept->imageHeight = static_cast<uint16_t>(height);
        kept->depth = 32;
        auto** keptD3d = reinterpret_cast<IDirect3DTexture9**>(&kept->texture);
        Device()->CreateTexture(width, height, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, keptD3d, nullptr);
        (*keptD3d)->GetLevelDesc(0, &desc);
        kept->height = static_cast<uint16_t>(desc.Height);
        kept->width = static_cast<uint16_t>(desc.Width);
        D3DLOCKED_RECT keptRect;
        (*keptD3d)->LockRect(0, &keptRect, nullptr, 0);
        std::memcpy(keptRect.pBits, Edit(this)->pixels, bytes);
        (*keptD3d)->UnlockRect(0);
    }
    return true;
}

namespace {

// A pixel of four bytes sampled between four (10-bit fractions; each channel rounded).
uint32_t Bilinear(uint32_t c00, uint32_t c01, uint32_t c10, uint32_t c11, int fx, int fy)
{
    int w00 = ((0x400 - fx) * (0x400 - fy)) >> 10;
    int w01 = ((0x400 - fy) * fx) >> 10;
    int w10 = ((0x400 - fx) * fy) >> 10;
    int w11 = (fy * fx) >> 10;
    uint32_t result = 0;
    for (int k = 0; k < 32; k += 8) {
        int sum = static_cast<int>(c11 >> k & 0xFF) * w11 + static_cast<int>(c10 >> k & 0xFF) * w10 +
                  static_cast<int>(c01 >> k & 0xFF) * w01 + static_cast<int>(c00 >> k & 0xFF) * w00 + 0x200;
        result |= static_cast<uint32_t>(static_cast<uint8_t>(sum >> 10)) << k;
    }
    return result;
}

// The four pixels about (sx, sy) of a width x height image (the right / lower ones the same at the edges).
uint32_t SampleAt(const uint32_t* image, int width, int height, int sx, int sy, int fx, int fy)
{
    int step = sx < width - 1 ? 1 : 0;
    const uint32_t* p = image + (sy * width + sx);
    uint32_t c00 = p[0], c01 = p[step];
    if (sy < height - 1)
        p += width;
    return Bilinear(c00, c01, p[0], p[step], fx, fy);
}

int Ftol(double value) { return static_cast<int>(value); } // (the game's _ftol: towards zero)

} // namespace

// 0x004C9200
void __cdecl ResizeImage(const uint8_t* from, uint16_t fromWidth, uint16_t fromHeight, uint8_t* into, uint16_t intoWidth,
                         uint16_t intoHeight, uint16_t bitsPerPixel)
{
    if (bitsPerPixel == 8) { // (nearest, the bytes cleared first - as many as the source has)
        std::memset(into, 0, static_cast<uint32_t>(fromHeight) * fromWidth);
        float width = static_cast<float>(static_cast<int>(fromWidth));
        float xScale = static_cast<float>(static_cast<double>(width) / static_cast<int>(intoWidth));
        float yScale = static_cast<float>(static_cast<double>(static_cast<int>(fromHeight)) / static_cast<int>(intoHeight));
        for (int y = 0; y < intoHeight; ++y) {
            double row = static_cast<double>(y) * width * yScale;
            uint8_t* out = into + intoWidth * y;
            for (int x = 0; x < intoWidth; ++x)
                out[x] = from[Ftol(static_cast<double>(x) * xScale + row)];
        }
        return;
    }
    int xStep = Ftol(static_cast<double>(static_cast<int>(fromWidth)) / static_cast<int>(intoWidth) * 1024.0f);
    int yStep = Ftol(static_cast<double>(static_cast<int>(fromHeight)) / static_cast<int>(intoHeight) * 1024.0f);
    auto* out = reinterpret_cast<uint32_t*>(into);
    const auto* image = reinterpret_cast<const uint32_t*>(from);
    int y = 0;
    for (int row = intoHeight; row > 0; --row, y += yStep) {
        int sy = static_cast<int16_t>(y >> 10);
        int fy = y & 0x3FF;
        int x = 0;
        for (int column = intoWidth; column > 0; --column, x += xStep)
            *out++ = SampleAt(image, fromWidth, fromHeight, static_cast<int16_t>(x >> 10), sy, x & 0x3FF, fy);
    }
}

// 0x004C9520
void __cdecl ZoomImage(const uint8_t* from, uint16_t width, uint16_t height, uint8_t* into, float xZoom, float yZoom)
{
    int xStep = Ftol(1.0f / static_cast<double>(xZoom) * 1024.0f);
    int yStep = Ftol(1.0f / static_cast<double>(yZoom) * 1024.0f);
    int xStart = ((0x400 - xStep) * static_cast<int>(width)) >> 1; // (about the centre)
    int y = ((0x400 - yStep) * static_cast<int>(height)) >> 1;
    int xMax = width * 1024 - 1024, yMax = height * 1024 - 1024;
    auto* out = reinterpret_cast<uint32_t*>(into);
    const auto* image = reinterpret_cast<const uint32_t*>(from);
    for (int row = height; row > 0; --row, y += yStep) {
        int cy = y < 0 ? 0 : y > yMax ? yMax : y;
        int sy = static_cast<int16_t>(cy >> 10);
        int x = xStart;
        for (int column = width; column > 0; --column, x += xStep) {
            int cx = x < 0 ? 0 : x > xMax ? xMax : x;
            *out++ = SampleAt(image, width, height, static_cast<int16_t>(cx >> 10), sy, cx & 0x3FF, cy & 0x3FF);
        }
    }
}

// 0x004C97D0
uint8_t CPcTexture::NearestPaletteIndex(uint32_t colour)
{
    uint8_t depth = reinterpret_cast<uint8_t(__thiscall*)(CPcTexture*)>(vtable[35])(this);
    int count = 1 << depth;
    const auto* entry = reinterpret_cast<const uint8_t*>(Texture()->PaletteOwner()); // (its entries, as the original)
    uint8_t nearest = 0;
    int best = 0x7FFFFFFF;
    int c0 = colour & 0xFF, c1 = colour >> 8 & 0xFF, c2 = colour >> 16 & 0xFF, c3 = colour >> 24;
    for (int i = 0; i < count; ++i, entry += 4) {
        int d0 = entry[0] - c0, d1 = entry[1] - c1, d2 = entry[2] - c2, d3 = entry[3] - c3;
        int distance = d2 * d2 + d3 * d3 * 2 + d1 * d1 + d0 * d0;
        if (distance < best) {
            best = distance;
            nearest = static_cast<uint8_t>(i);
        }
    }
    return nearest;
}

// 0x004C9950
bool CPcTexture::Modulate(uint32_t colour, bool cumulative)
{
    // (the edit's fields read without checking it is one, as the original)
    uint8_t* into = reinterpret_cast<uint8_t*>(Edit(this)->pixels);
    const uint8_t* from = Edit(this)->original && !cumulative ? reinterpret_cast<const uint8_t*>(Edit(this)->original)
                                                             : reinterpret_cast<const uint8_t*>(Edit(this)->pixels);
    uint32_t red = (colour & 0xFF) * 0xFF >> 7, green = (colour >> 8 & 0xFF) * 0xFF >> 7,
             blue = (colour >> 16 & 0xFF) * 0xFF >> 7, alpha = colour >> 24;
    using Get = uint16_t(__thiscall*)(CPcTexture*);
    using GetDepth = uint8_t(__thiscall*)(CPcTexture*);
    int width = reinterpret_cast<Get>(vtable[33])(this);
    int count = reinterpret_cast<Get>(vtable[34])(this) * width;
    for (int i = 0; i < count; ++i, from += 4, into += 4) {
        into[0] = static_cast<uint8_t>(from[0] * blue / 0xFF);
        into[1] = static_cast<uint8_t>(from[1] * green / 0xFF);
        into[2] = static_cast<uint8_t>(from[2] * red / 0xFF);
        into[3] = static_cast<uint8_t>(from[3] * alpha >> 7);
    }
    if (reinterpret_cast<GetDepth>(vtable[35])(this) <= 8 && !cumulative) { // (the palette too, in place)
        auto* entry = reinterpret_cast<uint8_t*>(Texture()->PaletteOwner());
        int entries = 1 << reinterpret_cast<GetDepth>(vtable[35])(this);
        for (int i = 0; i < entries; ++i, entry += 4) {
            entry[0] = static_cast<uint8_t>(entry[0] * blue / 0xFF);
            entry[1] = static_cast<uint8_t>(entry[1] * green / 0xFF);
            entry[2] = static_cast<uint8_t>(entry[2] * red / 0xFF);
            entry[3] = static_cast<uint8_t>(entry[3] * alpha >> 7);
        }
    }
    return true;
}

// 0x004C9D80
bool CPcTexture::ResizeFrom(const CPcTexture* source, int /*unused*/)
{
    if (!(flags & 1) || !(source->flags & 1))
        return false;
    const sTexture* into = Edit(this)->texture;
    const sTexture* from = Edit(source)->texture;
    ResizeImage(reinterpret_cast<const uint8_t*>(Edit(source)->pixels), from->imageWidth, from->imageHeight,
                reinterpret_cast<uint8_t*>(Edit(this)->pixels), into->imageWidth, into->imageHeight, 32);
    return true;
}

// 0x004C9DE0
bool CPcTexture::Zoom(float xZoom, float yZoom)
{
    uint32_t* pixels = Edit(this)->pixels; // (read before the flag's test, as the original)
    const sTexture* texture = Texture();
    uint32_t width = texture->imageWidth, height = texture->imageHeight;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    uint32_t bytes = height * width * 4;
    auto* zoomed = static_cast<uint8_t*>(Mem::New(bytes));
    std::memset(zoomed, 0, bytes);
    Mem::Manager::Get()->PopContext();
    ZoomImage(reinterpret_cast<const uint8_t*>(pixels), static_cast<uint16_t>(width), static_cast<uint16_t>(height),
              zoomed, xZoom, yZoom);
    std::memcpy(pixels, zoomed, bytes);
    Mem::Manager::Get()->Free(zoomed);
    return true;
}

// 0x004C9F90
void CPcTexture::ReadRect(uint32_t* out, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t imageWidth = Texture()->imageWidth;
    if (flags & 1 && Edit(this)->pixels) {
        const uint32_t* from = Edit(this)->pixels + (y * imageWidth + x);
        for (uint32_t row = height; row; --row) {
            std::memcpy(out, from, width * 4);
            out += width;
            from += imageWidth;
        }
        return;
    }
    // Palettized: the whole image (not the rectangle's size, as the original), bottom row first.
    const auto* texels = static_cast<const uint8_t*>(Texture()->Lock());
    const auto* palette = reinterpret_cast<const uint32_t*>(Texture()->PaletteOwner());
    uint32_t rows = Texture()->imageHeight, columns = Texture()->imageWidth;
    const uint8_t* row = texels + x + (static_cast<int>(rows) - y - 1) * static_cast<int>(columns);
    for (uint32_t r = rows; r; --r) {
        for (uint32_t c = 0; c < columns; ++c)
            *out++ = palette[row[c]];
        row -= columns;
    }
    Texture()->Unlock();
}

// 0x004CA0D0
void CPcTexture::WriteRect(const uint32_t* in, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t imageWidth = Texture()->imageWidth, imageHeight = Texture()->imageHeight;
    if (flags & 1 && Edit(this)->pixels) {
        uint32_t* into = Edit(this)->pixels + (y * imageWidth + x);
        for (uint32_t row = height; row; --row) {
            std::memcpy(into, in, width * 4);
            in += width;
            into += imageWidth;
        }
        return;
    }
    auto* texels = static_cast<uint8_t*>(Texture()->Lock());
    uint8_t* row = texels + (static_cast<int>(imageHeight) - y - 1) * static_cast<int>(imageWidth) + x;
    for (uint32_t r = height; r; --r) {
        for (uint32_t c = 0; c < width; ++c)
            row[c] = NearestPaletteIndex(*in++);
        row -= imageWidth;
    }
    Texture()->Unlock();
}

// 0x004C8D30
bool CPcTexture::Rotate(float angle, float centreX, float centreY)
{
    uint32_t* pixels = Edit(this)->pixels; // (read before the flag's test, as the original)
    uint32_t width = Texture()->width, height = Texture()->height;
    if (centreX < 0.0f) // (below 0: the middle)
        centreX = static_cast<float>((static_cast<double>(width) - 1.0f) * 0.5f);
    if (centreY < 0.0f)
        centreY = static_cast<float>((static_cast<double>(height) - 1.0f) * 0.5f);
    float m[4];
    Mth::CreateRotate2D(m, -angle);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    uint32_t bytes = height * width * 4;
    auto* rotated = static_cast<uint32_t*>(Mem::New(bytes));
    Mem::Manager::Get()->PopContext();
    int yMax = static_cast<int>(height * 1024 - 1024), xMax = static_cast<int>(width * 1024 - 1024);
    // the image's axes turned (each a float, as the original stores them), in 1024ths
    float xx = static_cast<float>(static_cast<double>(m[2]) * 0.0f + m[0]);
    float xy = static_cast<float>(static_cast<double>(m[3]) * 0.0f + m[1]);
    float yx = static_cast<float>(static_cast<double>(m[0]) * 0.0f + m[2]);
    float yy = static_cast<float>(static_cast<double>(m[1]) * 0.0f + m[3]);
    int xxStep = Ftol(static_cast<double>(xx) * 1024.0f), xyStep = Ftol(static_cast<double>(xy) * 1024.0f);
    int yxStep = Ftol(static_cast<double>(yx) * 1024.0f), yyStep = Ftol(static_cast<double>(yy) * 1024.0f);
    // where the top left comes from: the centre moved back through the turn
    double nx = -static_cast<double>(centreX), ny = -static_cast<double>(centreY);
    float ox = static_cast<float>(m[2] * ny + nx * m[0]);
    float oy = static_cast<float>(ny * m[3] + m[1] * nx);
    double startX = static_cast<double>(ox) + centreX; // (kept in the register, unrounded)
    float startY = static_cast<float>(static_cast<double>(oy) + centreY);
    int rowX = Ftol(startX * 1024.0f);
    int rowY = Ftol(static_cast<double>(startY) * 1024.0f);
    uint32_t* out = rotated;
    for (uint32_t row = height; row; --row, rowX += yxStep, rowY += yyStep) {
        int x = rowX, y = rowY;
        for (uint32_t column = width; column; --column, x += xxStep, y += xyStep) {
            int cx = x < 0 ? 0 : x > xMax ? xMax : x;
            int cy = y < 0 ? 0 : y > yMax ? yMax : y;
            *out++ = SampleAt(pixels, static_cast<int>(width), static_cast<int>(height), static_cast<int16_t>(cx >> 10),
                              static_cast<int16_t>(cy >> 10), cx & 0x3FF, cy & 0x3FF);
        }
    }
    std::memcpy(pixels, rotated, bytes);
    Mem::Manager::Get()->Free(rotated);
    return true;
}

// 0x004CA220
bool CPcTexture::Stretch(uint16_t position, int vertical, int amount)
{
    uint32_t width = Texture()->imageWidth, height = Texture()->imageHeight;
    if (!amount)
        return true;
    int at = position;
    if (at + amount <= 0)
        amount = 1 - at;
    uint32_t end = static_cast<uint32_t>(at + amount);
    uint16_t readX = 0, readY = 0, readWidth, readHeight, newWidth, newHeight;
    int offset;
    if (!vertical) { // (the part left of the column stretched by -amount, or the part from it by amount)
        if (end >= width)
            amount = static_cast<int>(width) - at - 1;
        readHeight = newHeight = static_cast<uint16_t>(height);
        if (amount < 0) {
            readWidth = static_cast<uint16_t>(at + 1);
            newWidth = static_cast<uint16_t>(at - amount + 1);
            offset = -amount;
        } else {
            readWidth = static_cast<uint16_t>(width - at);
            newWidth = static_cast<uint16_t>(amount - at + static_cast<int>(width));
            readX = position;
            offset = 0;
        }
    } else {
        if (end >= height)
            amount = static_cast<int>(height) - at - 1;
        readWidth = newWidth = static_cast<uint16_t>(width);
        if (amount < 0) {
            readHeight = static_cast<uint16_t>(at + 1);
            newHeight = static_cast<uint16_t>(at - amount + 1);
            offset = -amount * static_cast<int>(width);
        } else {
            readHeight = static_cast<uint16_t>(height - at);
            newHeight = static_cast<uint16_t>(amount - at + static_cast<int>(height));
            readY = position;
            offset = 0;
        }
    }
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_cutsceneTopDownHeap ? manager->m_cutsceneTopDownHeap : manager->m_topDownHeap);
    auto* part = static_cast<uint32_t*>(Mem::New(static_cast<uint32_t>(readHeight) * readWidth * 4));
    auto* stretched = static_cast<uint32_t*>(Mem::New(static_cast<uint32_t>(newHeight) * newWidth * 4));
    Mem::Manager::Get()->PopContext();
    ReadRect(part, readX, readY, readWidth, readHeight);
    ResizeImage(reinterpret_cast<const uint8_t*>(part), readWidth, readHeight, reinterpret_cast<uint8_t*>(stretched),
                newWidth, newHeight, 32);
    CopySubImage(reinterpret_cast<uint8_t*>(part), 0, 0, readWidth, readHeight,
                 reinterpret_cast<const uint8_t*>(stretched + static_cast<uint16_t>(offset)), newWidth, newHeight, 32);
    WriteRect(part, readX, readY, readWidth, readHeight);
    if (part)
        Mem::Manager::Get()->Free(part);
    if (stretched)
        Mem::Manager::Get()->Free(stretched);
    return true;
}

// 0x004CA7B0
bool CPcTexture::Mirror()
{
    uint32_t width = Texture()->imageWidth;
    int height = Texture()->imageHeight;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    auto* column = static_cast<uint32_t*>(Mem::New(height * 4));
    Mem::Manager::Get()->PopContext();
    uint32_t* left = flags & 1 ? Edit(this)->pixels : nullptr;
    uint32_t* right = (flags & 1 ? Edit(this)->pixels : nullptr) + width - 1;
    for (; left < right; ++left, --right) { // (the columns swapped)
        for (int i = 0; i < height; ++i)
            column[i] = left[i * width];
        for (int i = 0; i < height; ++i)
            left[i * width] = right[i * width];
        for (int i = 0; i < height; ++i)
            right[i * width] = column[i];
    }
    Mem::Manager::Get()->Free(column);
    return true;
}

// 0x004CA8E0
bool CPcTexture::Overlay(CPcTexture* over, int /*unused*/)
{
    const auto* texels = static_cast<const uint8_t*>(over->Texture()->Lock());
    auto* into = reinterpret_cast<uint8_t*>(flags & 1 ? Edit(this)->pixels : nullptr);
    using Get = uint16_t(__thiscall*)(CPcTexture*);
    uint32_t width = reinterpret_cast<Get>(over->vtable[33])(over);
    uint32_t height = reinterpret_cast<Get>(over->vtable[34])(over);
    const uint8_t* row = texels + (height - 1) * width * 4; // (bottom row first, by its alpha)
    for (uint32_t y = height; y; --y, row -= width * 4) {
        const uint8_t* from = row;
        for (uint32_t x = 0; x < width; ++x, from += 4, into += 4) {
            uint8_t alpha = from[3];
            int rest = static_cast<uint8_t>(0xFF - alpha);
            into[0] = static_cast<uint8_t>((from[0] * alpha + into[0] * rest) >> 8);
            into[1] = static_cast<uint8_t>((from[1] * alpha + into[1] * rest) >> 8);
            into[2] = static_cast<uint8_t>((from[2] * alpha + into[2] * rest) >> 8);
            if (alpha > into[3])
                into[3] = alpha;
        }
    }
    over->Texture()->Unlock();
    return false;
}

// 0x004CAA20
bool CPcTexture::AdjustHsv(float hue, float saturation, float value, bool cumulative)
{
    auto* into = reinterpret_cast<uint8_t*>(flags & 1 ? Edit(this)->pixels : nullptr);
    const uint8_t* from = nullptr;
    if (flags & 1)
        from = reinterpret_cast<const uint8_t*>(Edit(this)->original && !cumulative ? Edit(this)->original
                                                                                    : Edit(this)->pixels);
    using Get = uint16_t(__thiscall*)(CPcTexture*);
    int width = reinterpret_cast<Get>(vtable[33])(this);
    int count = reinterpret_cast<Get>(vtable[34])(this) * width;
    for (int i = 0; i < count; ++i, from += 4, into += 4) {
        float h, s, v;
        RgbToHsv(static_cast<float>(from[0] * (1.0f / 255.0f)), static_cast<float>(from[1] * (1.0f / 255.0f)),
                 static_cast<float>(from[2] * (1.0f / 255.0f)), &h, &s, &v);
        double turned = static_cast<double>(h) + hue;
        h = static_cast<float>(turned);
        if (!(turned <= 360.0f))
            h = static_cast<float>(static_cast<double>(h) - 360.0f);
        double scaled = static_cast<double>(s) * saturation;
        s = scaled < 1.0f ? static_cast<float>(scaled) : 1.0f;
        scaled = static_cast<double>(v) * value;
        v = scaled < 1.0f ? static_cast<float>(scaled) : 1.0f;
        float rgb[3];
        HsvToRgb(&rgb[0], &rgb[1], &rgb[2], h, s, v);
        for (int k = 0; k < 3; ++k)
            into[k] = static_cast<uint8_t>(Ftol(static_cast<double>(rgb[k]) * 255.0f + 0.5f));
    }
    return true;
}

// 0x004CABE0
bool CPcTexture::Offset(int dx, int dy, bool keepEdges, int /*unused*/)
{
    using Get = uint16_t(__thiscall*)(CPcTexture*);
    int width = reinterpret_cast<Get>(vtable[33])(this);
    int height = reinterpret_cast<Get>(vtable[34])(this);
    int fromX, intoX, partWidth, fromY = 0, intoY, partHeight;
    if (dx >= 0) {
        fromX = 0;
        intoX = dx;
        partWidth = width - dx;
    } else {
        fromX = -dx;
        intoX = 0;
        partWidth = width + dx;
    }
    if (dy >= 0) {
        intoY = dy;
        partHeight = height - dy;
    } else {
        fromY = -dy;
        intoY = 0;
        partHeight = height + dy;
    }
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    auto* part = static_cast<uint32_t*>(
        Crt::Malloc(partHeight * partWidth * 4)); // (the C runtime's malloc)
    Mem::Manager::Get()->PopContext();
    ReadRect(part, static_cast<uint16_t>(fromX), static_cast<uint16_t>(fromY), static_cast<uint16_t>(partWidth),
             static_cast<uint16_t>(partHeight));
    WriteRect(part, static_cast<uint16_t>(intoX), static_cast<uint16_t>(intoY), static_cast<uint16_t>(partWidth),
              static_cast<uint16_t>(partHeight));
    if (!keepEdges) { // (what was uncovered filled from the moved part's nearest edge)
        for (int y = 0, sy = -intoY; y < height; ++y, ++sy) {
            int row = sy <= 0 ? 0 : sy;
            if (row >= partHeight - 1)
                row = partHeight - 1;
            int rowStart = row * partWidth;
            for (int x = 0, sx = -intoX; x < width; ++x, ++sx) {
                if (sx >= 0 && sx < partWidth && sy >= 0 && sy < partHeight)
                    continue;
                int column = sx <= 0 ? 0 : sx;
                if (column >= partWidth - 1)
                    column = partWidth - 1;
                uint32_t pixel = part[rowStart + column];
                uint32_t textureWidth = Texture()->width;
                if (flags & 1 && Edit(this)->pixels)
                    Edit(this)->pixels[static_cast<uint16_t>(y) * textureWidth + static_cast<uint16_t>(x)] = pixel;
            }
        }
    }
    Crt::Free(part); // (the C runtime's free)
    return true;
}

// 0x004CADC0
bool CPcTexture::Border(uint32_t colour)
{
    uint32_t* pixels = flags & 1 ? Edit(this)->pixels : nullptr;
    uint32_t width = Texture()->width;
    int height = Texture()->height;
    uint32_t* bottom = pixels + (height - 1) * width;
    uint32_t* right = pixels + width - 1;
    uint32_t* top = pixels;
    for (int i = 0; i < height; ++i)
        pixels[i * width] = colour;
    for (int i = 0; i < height; ++i)
        right[i * width] = colour;
    for (uint32_t i = 0; i < width; ++i) {
        top[i] = colour;
        bottom[i] = colour;
    }
    return true;
}

// 0x004CA410
bool CPcTexture::Squash(uint16_t position, int vertical, int amount, bool fill, uint32_t colour)
{
    uint32_t height = Texture()->imageHeight, width = Texture()->imageWidth;
    if (!amount)
        return true;
    int at = position;
    if (at + amount <= 0)
        amount = 1 - at;
    int end = at + amount;
    uint16_t readX = 0, readY = 0, intoX = 0, intoY = 0, readWidth, readHeight, newWidth, newHeight;
    if (!vertical) {
        if (end >= static_cast<int>(width))
            amount = static_cast<int>(width) - at - 1;
        newHeight = readHeight = static_cast<uint16_t>(height);
        if (amount < 0) {
            newWidth = static_cast<uint16_t>(amount - at + static_cast<int>(width));
            readWidth = static_cast<uint16_t>(width - at);
            readX = position;
        } else {
            readWidth = static_cast<uint16_t>(at + 1);
            newWidth = static_cast<uint16_t>(at - amount + 1);
            intoX = static_cast<uint16_t>(amount);
        }
    } else {
        if (end >= static_cast<int>(height))
            amount = static_cast<int>(height) - at - 1;
        readWidth = newWidth = static_cast<uint16_t>(width);
        if (amount < 0) {
            readHeight = static_cast<uint16_t>(height - at);
            newHeight = static_cast<uint16_t>(amount - at + static_cast<int>(height));
            readY = position;
        } else {
            readHeight = static_cast<uint16_t>(at + 1);
            newHeight = static_cast<uint16_t>(at - amount + 1);
            intoY = static_cast<uint16_t>(amount);
        }
    }
    auto crtMalloc = Crt::Malloc; // (the C runtime's)
    auto crtFree = Crt::Free;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    int count = readHeight * readWidth;
    auto* part = static_cast<uint32_t*>(crtMalloc(count * 4));
    auto* squashed = static_cast<uint32_t*>(crtMalloc(newHeight * newWidth * 4));
    Mem::Manager::Get()->PopContext();
    ReadRect(part, readX, readY, readWidth, readHeight);
    ResizeImage(reinterpret_cast<const uint8_t*>(part), readWidth, readHeight, reinterpret_cast<uint8_t*>(squashed),
                newWidth, newHeight, 32);
    if (fill) {
        for (int i = 0; i < count; ++i)
            part[i] = colour;
    }
    CopySubImage(reinterpret_cast<uint8_t*>(part), intoX, intoY, readWidth, readHeight,
                 reinterpret_cast<const uint8_t*>(squashed), newWidth, newHeight, 32);
    WriteRect(part, readX, readY, readWidth, readHeight);
    if (!fill) { // (what was uncovered filled with the squashed part's edge column / row)
        if (!vertical) {
            bool right = amount > 0;
            int source = right ? 0 : newWidth - 1;
            manager = Mem::Manager::Get();
            manager->PushContext(manager->m_topDownHeap);
            int rows = static_cast<uint16_t>(height);
            auto* column = static_cast<uint32_t*>(crtMalloc(rows * 4));
            Mem::Manager::Get()->PopContext();
            for (int i = 0; i < rows; ++i)
                column[i] = squashed[source + i * newWidth];
            int first = right ? 0 : static_cast<int>(width) + amount;
            int last = right ? amount - 1 : static_cast<int>(width) - 1;
            for (int x = first; x <= last; ++x)
                WriteRect(column, static_cast<uint16_t>(x), 0, 1, static_cast<uint16_t>(height));
            crtFree(column);
        } else {
            bool down = amount > 0;
            int source = down ? 0 : newHeight - 1;
            manager = Mem::Manager::Get();
            manager->PushContext(manager->m_topDownHeap);
            uint32_t bytes = static_cast<uint16_t>(width) * 4u;
            auto* row = static_cast<uint32_t*>(crtMalloc(bytes));
            Mem::Manager::Get()->PopContext();
            std::memcpy(row, squashed + newWidth * source, bytes);
            int first = down ? 0 : static_cast<int>(height) + amount;
            int last = down ? amount - 1 : static_cast<int>(height) - 1;
            for (int y = first; y <= last; ++y)
                WriteRect(row, 0, static_cast<uint16_t>(y), static_cast<uint16_t>(width), 1);
            crtFree(row);
        }
    }
    if (part)
        crtFree(part);
    if (squashed)
        crtFree(squashed);
    return true;
}

// 0x004C8070
CPcTexDict* CPcTexDict::Construct(uint32_t checksum)
{
    reinterpret_cast<CTexDict*>(this)->CTexDict::Construct(checksum, true);
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    return this;
}

// 0x004C9AF0
void CPcTexDict::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    reinterpret_cast<CTexDict*>(this)->CTexDict::DestroyBody();
}

// 0x004C9F60
CPcTexDict* CPcTexDict::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C9B00
CPcTexture* CPcTexDict::LoadTexture(const char* name, int a2, int /*unused*/)
{
    CPcTexture* texture = NewTexture();
    // (no check: the engine's load called on null without memory, as the original)
    reinterpret_cast<CTexture*>(texture)->CTexture::LoadByName(name, a2, 1);
    return texture;
}

// 0x004C89B0
CPcTexture* CPcTexDict::CreateTexture(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                      uint32_t /*unused*/)
{
    CPcTexture* texture = NewTexture();
    reinterpret_cast<CTexture*>(texture)->CTexture::LoadFromMemory(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(a1)), a2, a3, a4, a5, 1);
    return texture;
}

// 0x004C9BA0
CPcTexture* CPcTexDict::ReloadTexture(uint32_t checksum, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                      uint32_t a6)
{
    auto* texture = static_cast<CPcTexture*>(static_cast<Lst::HashTable*>(textures)->GetItem(checksum));
    if (!texture)
        texture = NewTexture();
    reinterpret_cast<CTexture*>(texture)->CTexture::Create(checksum, a2, a3, a4, a5, a6);
    return texture;
}

// 0x004C8090
bool CPcTexDict::UnloadTexture(CPcTexture* texture)
{
    if (texture) // (through its vtable)
        reinterpret_cast<CPcTexture*(__thiscall*)(CPcTexture*, unsigned int)>(texture->vtable[0])(texture, 1);
    return true;
}

// 0x004C9C80
CPcTexture* CPcTexDict::CopyTexture(uint32_t checksum, const CPcTexture* source)
{
    void* memory = Mem::New(sizeof(CPcTexture));
    CPcTexture* texture = memory ? static_cast<CPcTexture*>(memory)->ConstructCopy(source) : nullptr;
    texture->Texture()->checksum = checksum; // (no check, as the original)
    return texture;
}

namespace {

// The dictionary's table remade for `count` textures: 2^bits buckets, bits the first with 2^bits at least twice the
// count (1-12), in the heap the old table was in (the old one freed).
Lst::HashTable* RemakeTable(Lst::HashTable* table, uint32_t count)
{
    uint32_t size = 2, bits = 1;
    uint32_t wanted = count * 2;
    if (wanted > size) {
        do {
            if (bits >= 12)
                break;
            size <<= 1;
            ++bits;
        } while (wanted > size);
    }
    Mem::Allocator* heap = Mem::GetBlockHeader(table)->allocator; // (read before the null test, as the original)
    if (table) {
        if (table->m_buckets) {
            table->FlushAllItems();
            Mem::Manager::Get()->Free(table->m_buckets);
            table->m_buckets = nullptr;
        }
        Mem::Manager::Get()->Free(table);
    }
    Mem::Manager::Get()->PushContext(heap);
    void* memory = Mem::New(sizeof(Lst::HashTable));
    auto* remade = memory ? new (memory) Lst::HashTable(static_cast<int>(bits)) : nullptr;
    Mem::Manager::Get()->PopContext();
    return remade;
}

// A texture's format from its header: by depth (8 P8, 16 A1R5G5B5, 32 A8R8G8B8) or its compression (1, 2: DXT1, 5:
// DXT5); anything else keeps the last one's (the original's stack).
uint32_t TextureFormat(uint8_t compression, uint8_t depth, uint32_t last)
{
    if (compression) {
        if (compression == 1 || compression == 2)
            return D3DFMT_DXT1;
        if (compression == 5)
            return D3DFMT_DXT5;
        return last;
    }
    if (depth == 8)
        return D3DFMT_P8;
    if (depth == 16)
        return D3DFMT_A1R5G5B5;
    if (depth == 32)
        return D3DFMT_A8R8G8B8;
    return last;
}

// The texture under its checksum in the table.
void AddToTable(Lst::HashTable* table, sTexture* texture)
{
    void* memory = Mem::New(sizeof(CPcTexture));
    CPcTexture* holder = memory ? static_cast<CPcTexture*>(memory)->Construct() : nullptr;
    holder->engine = texture; // (no check, as the original)
    holder->checksum = texture->checksum;
    table->PutItem(texture->checksum, holder);
}

sTexture* NewEngineTexture()
{
    void* memory = Mem::New(sizeof(sTexture));
    return memory ? static_cast<sTexture*>(memory)->Construct() : nullptr;
}

} // namespace

// 0x004CAE90
Lst::HashTable* __cdecl LoadTexturesFromFile(const char* name, Lst::HashTable* table, bool remake)
{
    File::Handle file = File::Open(name, "rb");
    if (!file)
        return table;
    auto read = [file](void* into, int size, int count) { File::Read(into, size, count, file); };
    uint32_t version;
    read(&version, 4, 1);
    int count;
    read(&count, 4, 1);
    if (remake)
        table = RemakeTable(table, static_cast<uint32_t>(count));
    uint32_t format = 0; // (the original's is whatever its stack held until a texture sets it)
    for (int i = 0; i < count; ++i) {
        sTexture* texture = NewEngineTexture();
        read(&texture->checksum, 4, 1); // (no check, as the original)
        uint32_t width, height, levels, depth, paletteDepth, compression, paletteSize;
        read(&width, 4, 1);
        read(&height, 4, 1);
        read(&levels, 4, 1);
        read(&depth, 4, 1);
        read(&paletteDepth, 4, 1);
        read(&compression, 4, 1);
        read(&paletteSize, 4, 1);
        texture->width = static_cast<uint16_t>(width);
        texture->height = static_cast<uint16_t>(height);
        texture->levels = static_cast<uint8_t>(levels);
        texture->depth = static_cast<uint8_t>(depth);
        texture->unknownE = static_cast<uint8_t>(paletteDepth);
        texture->unknownF = static_cast<uint8_t>(compression);
        format = TextureFormat(texture->unknownF, texture->depth, format);
        if (texture->height < 4)
            texture->height = 4;
        auto** d3d = reinterpret_cast<IDirect3DTexture9**>(&texture->texture);
        if (paletteSize) {
            // Palettized: an A8R8G8B8 texture made, the palette and the levels read - and dropped (the texture's
            // texels left as they are, as the original).
            texture->depth = 32;
            format = D3DFMT_A8R8G8B8;
            Device()->CreateTexture(texture->width, texture->height, texture->levels, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, d3d, nullptr);
            void* palette = Mem::New(paletteSize);
            read(palette, static_cast<int>(paletteSize), 1);
            for (uint32_t level = 0; level < texture->levels; ++level) {
                uint32_t size;
                read(&size, 4, 1);
                D3DLOCKED_RECT rect;
                (*d3d)->LockRect(level, &rect, nullptr, 0);
                D3DSURFACE_DESC desc;
                (*d3d)->GetLevelDesc(level, &desc);
                void* texels = Mem::New(size);
                read(texels, static_cast<int>(size), 1);
                Mem::Manager::Get()->Free(texels);
                (*d3d)->UnlockRect(level);
            }
            Mem::Manager::Get()->Free(palette);
        } else {
            Device()->CreateTexture(texture->width, texture->height, texture->levels, 0, static_cast<D3DFORMAT>(format),
                                    D3DPOOL_MANAGED, d3d, nullptr);
            texture->palette = nullptr;
            for (uint32_t level = 0; level < texture->levels; ++level) {
                uint32_t size;
                read(&size, 4, 1);
                D3DLOCKED_RECT rect;
                (*d3d)->LockRect(level, &rect, nullptr, 0);
                read(rect.pBits, static_cast<int>(size), 1);
                (*d3d)->UnlockRect(level);
            }
        }
        AddToTable(table, texture);
    }
    File::Close(file);
    return table;
}

// 0x004CB420
Lst::HashTable* __cdecl LoadTexturesFromMemory(const uint8_t** cursor, Lst::HashTable* table, bool remake)
{
    const auto* p = reinterpret_cast<const uint32_t*>(*cursor); // (the cursor not moved on, as the original)
    int count = static_cast<int>(p[1]);
    p += 2;
    if (remake)
        table = RemakeTable(table, static_cast<uint32_t>(count));
    uint32_t format = 0; // (as the file loader's)
    for (int i = count; i > 0; --i) {
        sTexture* texture = NewEngineTexture();
        texture->checksum = *p++;
        uint32_t width = *p++, height = *p++, levels = *p++, depth = *p++, paletteDepth = *p++, compression = *p++;
        uint32_t paletteSize = *p++;
        texture->levels = static_cast<uint8_t>(levels);
        texture->width = static_cast<uint16_t>(width);
        texture->height = static_cast<uint16_t>(height);
        texture->depth = static_cast<uint8_t>(depth);
        texture->unknownE = static_cast<uint8_t>(paletteDepth);
        texture->unknownF = static_cast<uint8_t>(compression);
        format = TextureFormat(static_cast<uint8_t>(compression), static_cast<uint8_t>(depth), format);
        auto** d3d = reinterpret_cast<IDirect3DTexture9**>(&texture->texture);
        Device()->CreateTexture(static_cast<uint16_t>(width), static_cast<uint16_t>(height),
                                static_cast<uint8_t>(levels), 0, static_cast<D3DFORMAT>(format), D3DPOOL_MANAGED, d3d,
                                nullptr);
        if (!paletteSize)
            texture->palette = nullptr;
        for (uint32_t level = 0; level < texture->levels; ++level) {
            uint32_t size = *p++;
            D3DLOCKED_RECT rect;
            (*reinterpret_cast<IDirect3DTexture9**>(&texture->texture))->LockRect(level, &rect, nullptr, 0);
            std::memcpy(rect.pBits, p, size);
            p = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p) + size);
            (*reinterpret_cast<IDirect3DTexture9**>(&texture->texture))->UnlockRect(level);
        }
        AddToTable(table, texture);
    }
    return table;
}

namespace {

// Whether the table has any textures (iterated from the start).
bool HasTextures(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    int count = 0;
    while (table->IterateNext() && table->m_iterItem && table->m_iterItem->value)
        ++count;
    return count != 0;
}

} // namespace

// 0x004CB380
bool CPcTexDict::LoadFile(const char* name)
{
    auto* table = static_cast<Lst::HashTable*>(textures);
    if (!table) {
        LoadTexturesFromFile(name, nullptr, false);
    } else if (!HasTextures(table)) {
        textures = LoadTexturesFromFile(name, table, true);
    } else {
        LoadTexturesFromFile(name, table, false);
    }
    return true;
}

// 0x004CB7C0
bool CPcTexDict::LoadMemory(const uint8_t* data)
{
    auto* table = static_cast<Lst::HashTable*>(textures);
    if (!table) {
        LoadTexturesFromMemory(&data, nullptr, false);
    } else if (!HasTextures(table)) {
        textures = LoadTexturesFromMemory(&data, table, true);
    } else {
        LoadTexturesFromMemory(&data, table, false);
    }
    return true;
}

// 0x004CB760
CPcTexDict* CPcTexDict::ConstructFromFile(const char* name)
{
    reinterpret_cast<CTexDict*>(this)->CTexDict::ConstructNamed(name, true);
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    LoadFile(name);
    return this;
}

// 0x004C7270
CPcTexDict* __cdecl LoadTexDictFile(const char* name)
{
    void* memory = Mem::New(sizeof(CPcTexDict));
    return memory ? static_cast<CPcTexDict*>(memory)->ConstructFromFile(name) : nullptr;
}

// 0x004C72E0
CPcTexDict* __cdecl LoadTexDictMemory(uint32_t checksum, const uint8_t* data)
{
    void* memory = Mem::New(sizeof(CPcTexDict));
    CPcTexDict* dict = memory ? static_cast<CPcTexDict*>(memory)->Construct(checksum) : nullptr;
    dict->LoadMemory(data); // (called on null without memory, as the original)
    return dict;
}

// 0x004C7360
CPcTexDict* __cdecl CreateTexDict(uint32_t checksum)
{
    void* memory = Mem::New(sizeof(CPcTexDict));
    return memory ? static_cast<CPcTexDict*>(memory)->Construct(checksum) : nullptr;
}

// 0x004C80B0
void __cdecl CopySubImage(uint8_t* into, uint16_t x, uint16_t y, uint16_t intoWidth, uint16_t intoHeight,
                          const uint8_t* from, uint16_t fromWidth, uint16_t fromHeight, uint16_t bitsPerPixel)
{
    int columns = fromWidth;
    if (columns >= intoWidth - x)
        columns = intoWidth - x;
    int rows = fromHeight < intoHeight - y ? fromHeight : intoHeight - y;
    int offset = y * intoWidth;
    uint32_t bytesPerPixel = bitsPerPixel >> 3;
    int rowBytes;
    uint32_t fromStride, intoStride;
    if (bytesPerPixel) {
        rowBytes = columns * static_cast<int>(bytesPerPixel);
        fromStride = fromWidth * bytesPerPixel;
        into += (offset + x) * static_cast<int>(bytesPerPixel);
        intoStride = intoWidth * bytesPerPixel;
    } else { // (4 bits: two pixels a byte)
        into += (offset + x) / 2;
        rowBytes = columns / 2;
        fromStride = fromWidth >> 1u;
        intoStride = intoWidth >> 1u;
    }
    for (; rows > 0; --rows) {
        std::memcpy(into, from, static_cast<uint32_t>(rowBytes));
        from += fromStride;
        into += intoStride;
    }
}

// 0x004C8180
void __cdecl RgbToHsv(float r, float g, float b, float* h, float* s, float* v)
{
    float low = g < b ? g : b;
    if (r < low)
        low = r;
    float high = g > b ? g : b;
    if (r > high)
        high = r;
    *v = high;
    float delta = static_cast<float>(static_cast<double>(high) - low);
    if (high == 0.0f) {
        *s = 0.0f;
        *h = -1.0f;
        return;
    }
    *s = static_cast<float>(static_cast<double>(delta) / high);
    if (delta == 0.0f)
        return;
    double hue;
    if (r == high)
        hue = (static_cast<double>(g) - b) / delta;
    else if (g == high)
        hue = (static_cast<double>(b) - r) / delta + 2.0f;
    else
        hue = (static_cast<double>(r) - g) / delta + 4.0f;
    *h = static_cast<float>(hue);
    double degrees = static_cast<double>(*h) * 60.0f;
    *h = static_cast<float>(degrees);
    if (degrees < 0.0)
        *h = static_cast<float>(degrees + 360.0f);
}

// 0x004C82D0
void __cdecl HsvToRgb(float* r, float* g, float* b, float h, float s, float v)
{
    if (s == 0.0f) {
        *b = v;
        *g = v;
        *r = v;
        return;
    }
    double sector = static_cast<double>(h) * (1.0f / 60.0f);
    int i = static_cast<int>(sector);
    double f = sector - i;
    float p = static_cast<float>((1.0f - static_cast<double>(s)) * v);
    float q = static_cast<float>((1.0f - s * f) * v);
    float t = static_cast<float>((1.0f - (1.0f - f) * s) * v);
    switch (static_cast<uint32_t>(i)) {
    case 0: *r = v, *g = t, *b = p; break;
    case 1: *r = q, *g = v, *b = p; break;
    case 2: *r = p, *g = v, *b = t; break;
    case 3: *r = p, *g = q, *b = v; break;
    case 4: *r = t, *g = p, *b = v; break;
    default: *r = v, *g = p, *b = q; break;
    }
}

namespace {
GAME_VTABLE(kTextureVtable, 0x0064A770, &Nx::CPcTexture::Destroy, &Nx::CPcTexture::LoadFile,
            &Nx::CPcTexture::LoadMemory, &Nx::CPcTexture::Create, &Nx::CPcTexture::CopyFrom, Stubs::ReturnFalse4,
            Stubs::ReturnFalse1, Stubs::ReturnFalse1, &Nx::CPcTexture::BeginEdit, &Nx::CPcTexture::WriteBack,
            &Nx::CPcTexture::EndEdit, Stubs::ReturnFalse5, &Nx::CPcTexture::Modulate, &Nx::CPcTexture::Mirror,
            Stubs::ReturnFalse, &Nx::CPcTexture::ResizeFrom, &Nx::CPcTexture::Zoom, &Nx::CPcTexture::Rotate,
            &Nx::CPcTexture::Offset, &Nx::CPcTexture::Border, Stubs::ReturnFalse7, &Nx::CPcTexture::Stretch,
            &Nx::CPcTexture::Squash, Stubs::ReturnFalse2, &Nx::CPcTexture::AdjustHsv, Stubs::ReturnZero,
            Stubs::ReturnZero, Stubs::ReturnZero, Stubs::ReturnZero, Stubs::IgnoreArguments2, Stubs::ReturnFalse1,
            Stubs::ReturnFalse, Stubs::ReturnFalse, &Nx::CPcTexture::GetWidth, &Nx::CPcTexture::GetHeight,
            &Nx::CPcTexture::GetDepth, Stubs::ReturnZero, Stubs::IgnoreArgument, &Nx::CPcTexture::GetUnknownE,
            &Nx::CPcTexture::GetUnknownC, &Nx::CPcTexture::GetUnknownD, &Nx::CPcTexture::Overlay);
GAME_VTABLE(kTexDictVtable, 0x0064A824, &Nx::CPcTexDict::Destroy, &Nx::CPcTexDict::LoadTexture,
            &Nx::CPcTexDict::CreateTexture, &Nx::CPcTexDict::ReloadTexture, Stubs::ReturnZero1,
            &Nx::CPcTexDict::UnloadTexture, Stubs::IgnoreArgument, Stubs::ReturnFalse1,
            &Nx::CPcTexDict::CopyTexture);
} // namespace

} // namespace Nx

REPLACE(0x004C7ED0, &Nx::CPcTexture::Construct);
REPLACE(0x004C7EF0, &Nx::CPcTexture::SetTexture);
REPLACE(0x004C9D00, &Nx::CPcTexture::DestroyBody);
REPLACE(0x004C8450, &Nx::CPcTexture::ConstructCopy);
REPLACE(0x004C8830, &Nx::CPcTexture::Create);
REPLACE(0x004C86A0, &Nx::CPcTexture::CopyFrom);
REPLACE(0x004C8A60, &Nx::CPcTexture::BeginEdit);
REPLACE(0x004CAE60, &Nx::CPcTexture::Destroy);
REPLACE(0x004C7F00, &Nx::CPcTexture::LoadFile);
REPLACE(0x004C7F80, &Nx::CPcTexture::LoadMemory);
REPLACE(0x004C9EC0, &Nx::CPcTexture::WriteBack);
REPLACE(0x004C98B0, &Nx::CPcTexture::EndEdit);
REPLACE(0x004C7FB0, &Nx::CPcTexture::GetWidth);
REPLACE(0x004C7FE0, &Nx::CPcTexture::GetHeight);
REPLACE(0x004C8010, &Nx::CPcTexture::GetDepth);
REPLACE(0x004C8030, &Nx::CPcTexture::GetUnknownE);
REPLACE_SOURCE_ONLY(0x004C8050, &Nx::CPcTexture::GetUnknownC); // (4 bytes)
REPLACE_SOURCE_ONLY(0x004C8060, &Nx::CPcTexture::GetUnknownD); // (4 bytes)
REPLACE(0x004C8070, &Nx::CPcTexDict::Construct);
REPLACE(0x004C9AF0, &Nx::CPcTexDict::DestroyBody);
REPLACE(0x004C9F60, &Nx::CPcTexDict::Destroy);
REPLACE(0x004C9B00, &Nx::CPcTexDict::LoadTexture);
REPLACE(0x004C89B0, &Nx::CPcTexDict::CreateTexture);
REPLACE(0x004C9BA0, &Nx::CPcTexDict::ReloadTexture);
REPLACE(0x004C8090, &Nx::CPcTexDict::UnloadTexture);
REPLACE(0x004C9C80, &Nx::CPcTexDict::CopyTexture);
REPLACE(0x004C7270, Nx::LoadTexDictFile);
REPLACE(0x004C72E0, Nx::LoadTexDictMemory);
REPLACE(0x004C7360, Nx::CreateTexDict);
REPLACE(0x004C80B0, Nx::CopySubImage);
REPLACE(0x004C8180, Nx::RgbToHsv);
REPLACE(0x004C82D0, Nx::HsvToRgb);
REPLACE(0x004C9200, Nx::ResizeImage);
REPLACE(0x004C9520, Nx::ZoomImage);
REPLACE(0x004C97D0, &Nx::CPcTexture::NearestPaletteIndex);
REPLACE(0x004C9950, &Nx::CPcTexture::Modulate);
REPLACE(0x004C9D80, &Nx::CPcTexture::ResizeFrom);
REPLACE(0x004C9DE0, &Nx::CPcTexture::Zoom);
REPLACE(0x004C9F90, &Nx::CPcTexture::ReadRect);
REPLACE(0x004CA0D0, &Nx::CPcTexture::WriteRect);
REPLACE(0x004CA220, &Nx::CPcTexture::Stretch);
REPLACE(0x004CA410, &Nx::CPcTexture::Squash);
REPLACE(0x004CA7B0, &Nx::CPcTexture::Mirror);
REPLACE(0x004CA8E0, &Nx::CPcTexture::Overlay);
REPLACE(0x004CAA20, &Nx::CPcTexture::AdjustHsv);
REPLACE(0x004CABE0, &Nx::CPcTexture::Offset);
REPLACE(0x004CADC0, &Nx::CPcTexture::Border);
REPLACE(0x004C8D30, &Nx::CPcTexture::Rotate);
REPLACE(0x004CAE90, Nx::LoadTexturesFromFile);
REPLACE(0x004CB420, Nx::LoadTexturesFromMemory);
REPLACE(0x004CB380, &Nx::CPcTexDict::LoadFile);
REPLACE(0x004CB7C0, &Nx::CPcTexDict::LoadMemory);
REPLACE(0x004CB760, &Nx::CPcTexDict::ConstructFromFile);
