#pragma once

#include <cstddef>
#include <cstdint>

// The engine's texture (render::BitmapTexture; the PC texture PcTexture - pc_texture.h - extends it), and the folded
// dispatchers the engine's classes call their virtual slots through.
namespace render {

// BitmapTexture::flag_bits_
enum : uint32_t {
    kTexturePermanent = 0x10000, // kept when its dictionary's textures are flushed (drop_textures without `all`)
};

// The texture (8 bytes of the engine's; vtable 0x00649AF0 - its own slots are stubs; PC 0x0064A770). Its virtual
// slots (PcTexture's): [0] destroy, [1] loaded from a file, [2] from memory, [3] made, [17] rotated, [21]
// stretched, [23] brightness, [24] hue / saturation / value, [26] the pixel data's size, [33] / [34] the image's
// width / height, [38] its +0x0E, [41] another texture drawn over it.
class BitmapTexture {
public:
    // 0x0049F200: construct - the upper half of the flags cleared. Returns this.
    BitmapTexture* make_new();
    // 0x0049F210: the destructor's body.
    void destroy_body();
    // 0x0049F4D0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    BitmapTexture* teardown(unsigned int free_flags);

    // 0x0049F220: loaded from an image file (slot 1) - the name as given, or with `in_images` (its low byte) under
    // "images/" (a "../" taken off instead); named by the CRC of the name after its last slash. Whether it was.
    bool load_by_name(const char* name, int in_images, int argument);
    // 0x0049F300: loaded from an image in memory (slot 2 with the other arguments), named `checksum`. Whether it was.
    bool read_from_ram_block(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                             uint32_t a5);
    // 0x0049F330 (not a function in Ghidra): made (slot 3 with all the arguments), named `checksum`. Whether it was.
    bool fabricate(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel, uint32_t a4,
                   uint32_t a5);
    // 0x0049F3F0: kept or not when its dictionary is flushed.
    void pin_resident(bool permanent);

    void** vtable;
    uint32_t checksum_;  // +0x04
    uint32_t flag_bits_; // +0x08 (the platform's low half: PcTexture's)
};
static_assert(sizeof(BitmapTexture) == 0xC, "layout must match the original");

// The folded dispatchers: a tail jump to the object's virtual slot (its arguments left as they are) - written for
// the texture's slots, shared by other classes' calls of the same slot (the geom's 32 and 41, ...).
// 0x0049F360 [17], 0x0049F370 [21], 0x0049F380 [23], 0x0049F390 [24], 0x0049F3A0 [26], 0x0049F3B0 [32],
// 0x0049F3C0 [33], 0x0049F3D0 [34], 0x0049F3E0 [38], 0x0049F420 [41].
void virtual_slot17_jump();
void virtual_slot26_jump();
void virtual_slot32_jump();
void virtual_slot33_jump();
void virtual_slot34_jump();
void virtual_slot41_jump();

// The texture's image size (slots 33 / 34).
inline uint16_t texels_across(void* texture)
{
    return reinterpret_cast<uint16_t(__thiscall*)(void*)>((*static_cast<void***>(texture))[33])(texture);
}
inline uint16_t texels_down(void* texture)
{
    return reinterpret_cast<uint16_t(__thiscall*)(void*)>((*static_cast<void***>(texture))[34])(texture);
}

} // namespace render
