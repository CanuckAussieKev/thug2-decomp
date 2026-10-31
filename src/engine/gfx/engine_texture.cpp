// The engine's texture and the folded virtual-slot dispatchers (see engine_texture.h).

#include "engine/gfx/engine_texture.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/gfx/viewport.h"

namespace render {

namespace {

extern void** const kTextureVtable;

} // namespace

// 0x0049F200
BitmapTexture* BitmapTexture::make_new()
{
    vtable = reinterpret_cast<void**>(kTextureVtable);
    flag_bits_ &= 0xFFFF; // (the original clears the upper word alone)
    return this;
}

// 0x0049F210
void BitmapTexture::destroy_body() { vtable = reinterpret_cast<void**>(kTextureVtable); }

// 0x0049F4D0
BitmapTexture* BitmapTexture::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::dealloc(this);
    return this;
}

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
    return reinterpret_cast<bool(__thiscall*)(BitmapTexture*, const char*, int, int)>(vtable[1])(this, path, in_images,
                                                                                                 argument);
}

// 0x0049F300
bool BitmapTexture::read_from_ram_block(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                                        uint32_t a5)
{
    checksum_ = checksum;
    return reinterpret_cast<bool(__thiscall*)(BitmapTexture*, const uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t)>(
        vtable[2])(this, data, size, a3, a4, a5);
}

// 0x0049F330
bool BitmapTexture::fabricate(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel, uint32_t a4,
                              uint32_t a5)
{
    checksum_ = checksum;
    return reinterpret_cast<bool(__thiscall*)(BitmapTexture*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                              uint32_t)>(vtable[3])(this, checksum, width, height, bits_per_pixel, a4,
                                                                    a5);
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

// 0x0049F360
__declspec(naked) void virtual_slot17_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x44]
    }
}

// 0x0049F3A0
__declspec(naked) void virtual_slot26_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x68]
    }
}

// 0x0049F3B0
__declspec(naked) void virtual_slot32_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x80]
    }
}

// 0x0049F3C0
__declspec(naked) void virtual_slot33_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x84]
    }
}

// 0x0049F3D0
__declspec(naked) void virtual_slot34_jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x88]
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

namespace {
static VtableSlot g_texture_vtable_slots[] = {&render::BitmapTexture::teardown,
                                              stubs::return_false3,
                                              stubs::return_false5,
                                              stubs::return_false6,
                                              stubs::return_false1,
                                              stubs::return_false4,
                                              stubs::return_false1,
                                              stubs::return_false1,
                                              stubs::return_false2,
                                              stubs::return_false2,
                                              stubs::return_false,
                                              stubs::return_false5,
                                              stubs::return_false2,
                                              stubs::return_false,
                                              stubs::return_false,
                                              stubs::return_false2,
                                              stubs::return_false2,
                                              stubs::return_false3,
                                              stubs::return_false4,
                                              stubs::return_false1,
                                              stubs::return_false7,
                                              stubs::return_false3,
                                              stubs::return_false5,
                                              stubs::return_false2,
                                              stubs::return_false4,
                                              stubs::return_zero,
                                              stubs::return_zero,
                                              stubs::return_zero,
                                              stubs::return_zero,
                                              stubs::ignore_arguments2,
                                              stubs::return_false1,
                                              stubs::return_false,
                                              stubs::return_false,
                                              stubs::return_zero_word,
                                              stubs::return_zero_word,
                                              stubs::return_false,
                                              stubs::return_zero,
                                              stubs::ignore_argument,
                                              stubs::return_false,
                                              stubs::return_false,
                                              stubs::return_false,
                                              stubs::return_false2};
void** const kTextureVtable = reinterpret_cast<void**>(g_texture_vtable_slots);
} // namespace

} // namespace render
