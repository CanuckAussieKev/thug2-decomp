// The engine's texture and the folded virtual-slot dispatchers (see engine_texture.h).

#include "engine/gfx/engine_texture.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/gfx/viewport.h"

namespace render {

// 0x0049F220 (the original sprintfs into its 512 bytes unbounded)
bool BitmapTexture::load_by_name(const char* name, int in_images, int argument)
{
    char path[0x200];
    std::snprintf(path, sizeof(path), "%s", name);
    if (static_cast<uint8_t>(in_images)) {
        if (name[0] == '.' && name[1] == '.' && name[2] == '/')
            std::snprintf(path, sizeof(path), "%s", name + 3);
        else
            std::snprintf(path, sizeof(path), "images/%s", name);
    }
    int start = static_cast<int>(std::strlen(path));
    while (start > 0 && path[start - 1] != '\\' && path[start - 1] != '/')
        --start;
    checksum_ = checksums::hash_from_text(path + start);
    return ingest_file(path, in_images, argument);
}

// 0x0049F300
bool BitmapTexture::read_from_ram_block(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                                        uint32_t a5)
{
    checksum_ = checksum;
    return load_memory(data, size, a3, a4, a5);
}

// 0x0049F330
bool BitmapTexture::fabricate(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel, uint32_t a4,
                              uint32_t a5)
{
    checksum_ = checksum;
    return create_image(checksum, width, height, static_cast<int>(bits_per_pixel), static_cast<int>(a4),
                        static_cast<int>(a5));
}

// 0x0049F3F0
void BitmapTexture::pin_resident(bool permanent)
{
    if (permanent)
        flag_bits_ |= kTexturePermanent;
    else
        flag_bits_ &= ~kTexturePermanent;
}

// --- the dispatchers --------------------------------------------------------------------------------------------

// 0x0049F3C0
__declspec(naked) void virtual_slot33_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x84]
    }
}

// 0x0049F420
__declspec(naked) void virtual_slot41_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0xA4]
    }
}

} // namespace render
