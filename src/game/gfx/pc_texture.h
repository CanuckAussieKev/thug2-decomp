#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/texture.h"

namespace Lst {
class HashTable;
}

// The PC texture and texture dictionary (Nx engine: the engine's CTexture - constructor 0x0049F200, destructor body
// 0x0049F210 - and CTexDict - constructor 0x0049F830, destructor body 0x0049FC70 - over them), the image helpers
// their editing uses (a sub-image copy, RGB <-> HSV) and the platform's dictionary makers.
namespace Nx {

// A texture being edited (16 bytes): its texture, a copy of the pixels as they were (or null), the pixels (32-bit,
// rows top down) and a texture of its own kept with them (or null).
struct sTextureEdit {
    sTexture* texture;
    uint32_t* original;
    uint32_t* pixels;
    sTexture* kept;
};

// The PC texture (0x14 bytes, vtable 0x0064A770).
struct CPcTexture {
    void** vtable;
    uint32_t checksum;  // +0x04 (the engine's)
    uint32_t flags;     // +0x08: 1 being edited (`engine` is an sTextureEdit*)
    uint8_t unknownC;   // +0x0C
    uint8_t unknownD;   // +0x0D
    uint8_t padE[2];
    void* engine;       // +0x10: the sTexture, or the edit holding it

    sTexture* Texture() const;

    // 0x004C7ED0: construct (no texture). Returns this.
    CPcTexture* Construct();
    // 0x004C7EF0: its texture (and checksum, the texture's).
    void SetTexture(sTexture* texture);
    // 0x004C8450: construct a copy of `source`: its texture copied, and an edit's copy of the pixels and kept texture
    // (never its pixels: the copy's own, still null, are tested for them, as the original). Returns this.
    CPcTexture* ConstructCopy(const CPcTexture* source);
    // 0x004C9D00: the destructor's body: an edit ended, the texture destroyed.
    void DestroyBody();
    // 0x004CAE60 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcTexture* Destroy(unsigned int freeFlags);
    // 0x004C7F00 (virtual [1]): loaded from "<name>.img.xbx". Whether it was.
    bool LoadFile(const char* name, int unused1, int unused2);
    // 0x004C7F80 (virtual [2]): loaded from an image in memory. Whether it was.
    bool LoadMemory(const uint8_t* data, int size, int unused1, int unused2, int unused3);
    // 0x004C8830 (virtual [3]): a new texture of the size (one level per the device: the levels' 0), P8 for up to
    // 8 bits a pixel, X1R5G5B5 for 16, X8R8G8B8 for 32 (and below 8); the old one's Direct3D texture released. The
    // texture's size from the level's. True.
    bool Create(uint32_t checksum, uint32_t width, uint32_t height, int bitsPerPixel, int unused1, int unused2);
    // 0x004C86A0 (virtual [4]): its Direct3D texture made again as a copy of `source`'s, every level (exit() if it
    // can't be made). True.
    bool CopyFrom(const CPcTexture* source);
    // 0x004C8A60 (virtual [8]): an edit begun: level 0's pixels read (32 bits only, bottom row first), a copy of them
    // kept and a texture of them made when asked. True.
    bool BeginEdit(bool keepTexture, bool keepCopy);
    // 0x004C9EC0 (virtual [9]): the edit's pixels written back into level 0, bottom row first.
    bool WriteBack(int unused1, int unused2);
    // 0x004C98B0 (virtual [10]): the edit ended: its buffers and kept texture freed, the texture back in place.
    bool EndEdit();
    // 0x004C97D0: the palette entry nearest a colour (alpha's difference counted twice).
    uint8_t NearestPaletteIndex(uint32_t colour);
    // 0x004C9950 (virtual [12]): the edit's pixels tinted by an RGBA colour (x / 128, alpha too), from the copy
    // unless `cumulative`; the palette too for up to 8 bits a pixel, unless `cumulative`. True.
    bool Modulate(uint32_t colour, bool cumulative);
    // 0x004C9D80 (virtual [15]): the edit's pixels resized from another edit's (both edits: else false).
    bool ResizeFrom(const CPcTexture* source, int unused);
    // 0x004C9DE0 (virtual [16]): the edit's pixels zoomed about the centre. True.
    bool Zoom(float xZoom, float yZoom);
    // 0x004C8D30 (virtual [17]): the edit's pixels turned by `angle` about a point (below 0: the middle). True.
    bool Rotate(float angle, float centreX, float centreY);
    // 0x004CABE0 (virtual [18]): the edit's pixels moved; what's uncovered filled from the nearest edge unless
    // `keepEdges`. True.
    bool Offset(int dx, int dy, bool keepEdges, int unused);
    // 0x004CADC0 (virtual [19]): a one-pixel border of the colour round the edit's pixels. True.
    bool Border(uint32_t colour);
    // 0x004CA220 (virtual [21]): the part one side of column / row `position` stretched by `amount`. True.
    bool Stretch(uint16_t position, int vertical, int amount);
    // 0x004CA410 (virtual [22]): the part one side squashed by `amount`, the space left filled with the colour or
    // its edge. True.
    bool Squash(uint16_t position, int vertical, int amount, bool fill, uint32_t colour);
    // 0x004CA7B0 (virtual [13]): the edit's pixels mirrored left to right. True.
    bool Mirror();
    // 0x004CAA20 (virtual [24]): the edit's pixels' hue turned, saturation and value scaled (up to 1), from the copy
    // unless `cumulative`. True.
    bool AdjustHsv(float hue, float saturation, float value, bool cumulative);
    // 0x004CA8E0 (virtual [41]): another texture's level 0 drawn over the edit's pixels by its alpha. False.
    bool Overlay(CPcTexture* over, int unused);
    // 0x004C9F90: a rectangle of the edit's pixels read out (a palettized texture's whole image instead).
    void ReadRect(uint32_t* out, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    // 0x004CA0D0: a rectangle written in (a palettized texture's by nearest palette entry).
    void WriteRect(const uint32_t* in, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    // 0x004C7FB0 / 0x004C7FE0 (virtual [33] / [34]): the image's width and height (0 without a texture).
    uint16_t GetWidth();
    uint16_t GetHeight();
    // 0x004C8010 / 0x004C8030 (virtual [35] / [38]): its bits per pixel and its +0x0E (0 without one).
    uint8_t GetDepth();
    uint8_t GetUnknownE();
    // 0x004C8050 (virtual [39], folded with other classes' getters of a byte at +0x0C) / 0x004C8060 (virtual [40]).
    uint8_t GetUnknownC();
    uint8_t GetUnknownD();
};
static_assert(offsetof(CPcTexture, flags) == 8 && offsetof(CPcTexture, engine) == 0x10 && sizeof(CPcTexture) == 0x14,
              "layout must match the original");

// The PC texture dictionary (0x14 bytes, vtable 0x0064A824).
struct CPcTexDict {
    void** vtable;
    uint8_t engine04[8];
    void* textures;  // +0x0C: Lst::HashTable* by checksum
    uint8_t engine10[4];

    // 0x004C8070: construct (the engine's constructor with this checksum, 1). Returns this.
    CPcTexDict* Construct(uint32_t checksum);
    // 0x004CB760: construct (the engine's constructor with the name, 1) and load the file. Returns this.
    CPcTexDict* ConstructFromFile(const char* name);
    // 0x004CB380 / 0x004CB7C0: the textures of a .tex file / image in memory added - the table remade to fit when it
    // has none yet. True.
    bool LoadFile(const char* name);
    bool LoadMemory(const uint8_t* data);
    // 0x004C9AF0: the destructor's body (a jump to the engine's).
    void DestroyBody();
    // 0x004C9F60 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcTexDict* Destroy(unsigned int freeFlags);
    // 0x004C9B00 (virtual [1]): a new texture loaded from a file (the engine's 0x0049F220 with 1). Returns it.
    CPcTexture* LoadTexture(const char* name, int a2, int unused);
    // 0x004C89B0 (virtual [2]): a new texture made (the engine's 0x0049F300 with 1). Returns it.
    CPcTexture* CreateTexture(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t unused);
    // 0x004C9BA0 (virtual [3]): the dictionary's texture of this checksum, or a new one, loaded from memory (the
    // engine's 0x0049F330). Returns it.
    CPcTexture* ReloadTexture(uint32_t checksum, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
    // 0x004C8090 (virtual [5]): a texture destroyed (through its vtable). True.
    bool UnloadTexture(CPcTexture* texture);
    // 0x004C9C80 (virtual [8]): a new texture copied from `source` (0x004C8450) under `checksum`. Returns it (the
    // original leaves it in EAX, and the engine's CopyTexture hands it on - the skater's graffiti texture is one).
    CPcTexture* CopyTexture(uint32_t checksum, const CPcTexture* source);
};
static_assert(offsetof(CPcTexDict, textures) == 0xC && sizeof(CPcTexDict) == 0x14, "layout must match the original");

// 0x004CAE90 / 0x004CB420: a .tex file's / image's textures (each: checksum, size, levels, depth, palette depth,
// compression, palette size, then its levels) made and put in `table` - remade to fit first with `remake` (in the
// heap the old one was in). Returns the table. A palettized texture becomes an A8R8G8B8 one with its texels not
// filled (its palette and levels read and dropped, as the original); the memory loader reads no palette and
// leaves the cursor where it was.
Lst::HashTable* __cdecl LoadTexturesFromFile(const char* name, Lst::HashTable* table, bool remake);
Lst::HashTable* __cdecl LoadTexturesFromMemory(const uint8_t** cursor, Lst::HashTable* table, bool remake);

// 0x004C7270: a dictionary loaded from a file. 0x004C72E0: one made with a checksum and loaded from memory (called
// on null without memory, as the original). 0x004C7360: an empty one.
CPcTexDict* __cdecl LoadTexDictFile(const char* name);
CPcTexDict* __cdecl LoadTexDictMemory(uint32_t checksum, const uint8_t* data);
CPcTexDict* __cdecl CreateTexDict(uint32_t checksum);

// 0x004C80B0: an image's rows copied into another's at (x, y), clipped to it (`bitsPerPixel` 4 copies half bytes).
void __cdecl CopySubImage(uint8_t* into, uint16_t x, uint16_t y, uint16_t intoWidth, uint16_t intoHeight,
                          const uint8_t* from, uint16_t fromWidth, uint16_t fromHeight, uint16_t bitsPerPixel);
// 0x004C9200: an image resized - 8 bits a pixel by nearest (the bytes cleared first: as many as the source has),
// else 32 bits bilinear in 1024ths.
void __cdecl ResizeImage(const uint8_t* from, uint16_t fromWidth, uint16_t fromHeight, uint8_t* into, uint16_t intoWidth,
                         uint16_t intoHeight, uint16_t bitsPerPixel);
// 0x004C9520: a 32-bit image zoomed about its centre (bilinear, clamped to the edges).
void __cdecl ZoomImage(const uint8_t* from, uint16_t width, uint16_t height, uint8_t* into, float xZoom, float yZoom);
// 0x004C8180: RGB (0-1) to hue (degrees; -1 when black), saturation and value.
void __cdecl RgbToHsv(float r, float g, float b, float* h, float* s, float* v);
// 0x004C82D0: and back.
void __cdecl HsvToRgb(float* r, float* g, float* b, float h, float s, float v);

} // namespace Nx
