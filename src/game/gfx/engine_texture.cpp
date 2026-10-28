// The engine's texture and the folded virtual-slot dispatchers (see engine_texture.h).

#include "game/gfx/engine_texture.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kTextureVtable = 0x00649AF0;

} // namespace

// 0x0049F200
CTexture* CTexture::Construct()
{
    vtable = reinterpret_cast<void**>(kTextureVtable);
    m_flags &= 0xFFFF; // (the original clears the upper word alone)
    return this;
}

// 0x0049F210
void CTexture::DestroyBody() { vtable = reinterpret_cast<void**>(kTextureVtable); }

// 0x0049F4D0
CTexture* CTexture::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049F220 (the original sprintfs into its 512 bytes unbounded)
bool CTexture::LoadByName(const char* name, int inImages, int argument)
{
    char path[0x200];
    std::snprintf(path, sizeof(path), "%s", name);
    if (static_cast<uint8_t>(inImages)) {
        if (name[0] == '.' && name[1] == '.' && name[2] == '/')
            std::snprintf(path, sizeof(path), "%s", name + 3);
        else
            std::snprintf(path, sizeof(path), "images/%s", name);
    }
    int start = static_cast<int>(std::strlen(path));
    while (start > 0 && path[start - 1] != '\\' && path[start - 1] != '/')
        --start;
    m_checksum = Crc::GenerateCRCFromString(path + start);
    return reinterpret_cast<bool(__thiscall*)(CTexture*, const char*, int, int)>(vtable[1])(this, path, inImages,
                                                                                         argument);
}

// 0x0049F300
bool CTexture::LoadFromMemory(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                              uint32_t a5)
{
    m_checksum = checksum;
    return reinterpret_cast<bool(__thiscall*)(CTexture*, const uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t)>(
        vtable[2])(this, data, size, a3, a4, a5);
}

// 0x0049F330
bool CTexture::Create(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bitsPerPixel, uint32_t a4,
                      uint32_t a5)
{
    m_checksum = checksum;
    return reinterpret_cast<bool(__thiscall*)(CTexture*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
        vtable[3])(this, checksum, width, height, bitsPerPixel, a4, a5);
}

// 0x0049F3F0
void CTexture::SetPermanent(bool permanent)
{
    if (permanent)
        m_flags |= kTexturePermanent;
    else
        m_flags &= ~kTexturePermanent;
}

// --- the dispatchers --------------------------------------------------------------------------------------------

// 0x0049F360
__declspec(naked) void VirtualSlot17Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x44]
    }
}

// 0x0049F370
__declspec(naked) void VirtualSlot21Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x54]
    }
}

// 0x0049F380
__declspec(naked) void VirtualSlot23Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x5C]
    }
}

// 0x0049F390
__declspec(naked) void VirtualSlot24Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x60]
    }
}

// 0x0049F3A0
__declspec(naked) void VirtualSlot26Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x68]
    }
}

// 0x0049F3B0
__declspec(naked) void VirtualSlot32Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x80]
    }
}

// 0x0049F3C0
__declspec(naked) void VirtualSlot33Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x84]
    }
}

// 0x0049F3D0
__declspec(naked) void VirtualSlot34Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x88]
    }
}

// 0x0049F3E0
__declspec(naked) void VirtualSlot38Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x98]
    }
}

// 0x0049F420
__declspec(naked) void VirtualSlot41Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0xA4]
    }
}

} // namespace Nx

REPLACE(0x0049F200, &Nx::CTexture::Construct);
REPLACE(0x0049F210, &Nx::CTexture::DestroyBody);
REPLACE(0x0049F4D0, &Nx::CTexture::Destroy);
REPLACE(0x0049F220, &Nx::CTexture::LoadByName);
REPLACE(0x0049F300, &Nx::CTexture::LoadFromMemory);
REPLACE(0x0049F330, &Nx::CTexture::Create);
REPLACE(0x0049F3F0, &Nx::CTexture::SetPermanent);
REPLACE_UNCHECKED(0x0049F360, Nx::VirtualSlot17Jump); // (tail jumps: the arguments are the slot's)
REPLACE_UNCHECKED(0x0049F370, Nx::VirtualSlot21Jump);
REPLACE_UNCHECKED(0x0049F380, Nx::VirtualSlot23Jump);
REPLACE_UNCHECKED(0x0049F390, Nx::VirtualSlot24Jump);
REPLACE_UNCHECKED(0x0049F3A0, Nx::VirtualSlot26Jump);
REPLACE_UNCHECKED(0x0049F3B0, Nx::VirtualSlot32Jump);
REPLACE_UNCHECKED(0x0049F3C0, Nx::VirtualSlot33Jump);
REPLACE_UNCHECKED(0x0049F3D0, Nx::VirtualSlot34Jump);
REPLACE_UNCHECKED(0x0049F3E0, Nx::VirtualSlot38Jump);
REPLACE_UNCHECKED(0x0049F420, Nx::VirtualSlot41Jump);
