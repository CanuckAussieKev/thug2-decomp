#pragma once

#include <cstddef>
#include <cstdint>

// The engine's texture (Nx::CTexture; the PC texture CPcTexture - pc_texture.h - extends it), and the folded
// dispatchers the engine's classes call their virtual slots through.
namespace Nx {

// CTexture::m_flags
enum : uint32_t {
    kTexturePermanent = 0x10000, // kept when its dictionary's textures are flushed (FlushTextures without `all`)
};

// The texture (8 bytes of the engine's; vtable 0x00649AF0 - its own slots are stubs; PC 0x0064A770). Its virtual
// slots (CPcTexture's): [0] destroy, [1] loaded from a file, [2] from memory, [3] made, [17] rotated, [21]
// stretched, [23] brightness, [24] hue / saturation / value, [26] the pixel data's size, [33] / [34] the image's
// width / height, [38] its +0x0E, [41] another texture drawn over it.
class CTexture {
public:
    // 0x0049F200: construct - the upper half of the flags cleared. Returns this.
    CTexture* Construct();
    // 0x0049F210: the destructor's body.
    void DestroyBody();
    // 0x0049F4D0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CTexture* Destroy(unsigned int freeFlags);

    // 0x0049F220: loaded from an image file (slot 1) - the name as given, or with `inImages` (its low byte) under
    // "images/" (a "../" taken off instead); named by the CRC of the name after its last slash. Whether it was.
    bool LoadByName(const char* name, int inImages, int argument);
    // 0x0049F300: loaded from an image in memory (slot 2 with the other arguments), named `checksum`. Whether it was.
    bool LoadFromMemory(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4, uint32_t a5);
    // 0x0049F330 (not a function in Ghidra): made (slot 3 with all the arguments), named `checksum`. Whether it was.
    bool Create(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bitsPerPixel, uint32_t a4, uint32_t a5);
    // 0x0049F3F0: kept or not when its dictionary is flushed.
    void SetPermanent(bool permanent);

    void** vtable;
    uint32_t m_checksum; // +0x04
    uint32_t m_flags;    // +0x08 (the platform's low half: CPcTexture's)
};
static_assert(sizeof(CTexture) == 0xC, "layout must match the original");

// The folded dispatchers: a tail jump to the object's virtual slot (its arguments left as they are) - written for
// the texture's slots, shared by other classes' calls of the same slot (the geom's 32 and 41, ...).
// 0x0049F360 [17], 0x0049F370 [21], 0x0049F380 [23], 0x0049F390 [24], 0x0049F3A0 [26], 0x0049F3B0 [32],
// 0x0049F3C0 [33], 0x0049F3D0 [34], 0x0049F3E0 [38], 0x0049F420 [41].
void VirtualSlot17Jump();
void VirtualSlot21Jump();
void VirtualSlot23Jump();
void VirtualSlot24Jump();
void VirtualSlot26Jump();
void VirtualSlot32Jump();
void VirtualSlot33Jump();
void VirtualSlot34Jump();
void VirtualSlot38Jump();
void VirtualSlot41Jump();

// The texture's image size (slots 33 / 34).
inline uint16_t TextureWidth(void* texture)
{
    return reinterpret_cast<uint16_t(__thiscall*)(void*)>((*static_cast<void***>(texture))[33])(texture);
}
inline uint16_t TextureHeight(void* texture)
{
    return reinterpret_cast<uint16_t(__thiscall*)(void*)>((*static_cast<void***>(texture))[34])(texture);
}

} // namespace Nx
