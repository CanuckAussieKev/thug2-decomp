// A one-pass material made on the fly, and the chunk skippers: see simple_material.h.

#include "game/gfx/simple_material.h"

#include <cstdint>
#include <cstdio>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// The texture dictionary's lookup (not rewritten yet; thiscall): the texture entry for a checksum, or null.
uint8_t* FindTexture(uint32_t checksum)
{
    return GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0049F540)(GameVariable<void*>(0x00700F20), checksum);
}
// A material's constructor and destructor body (not rewritten yet; thiscall).
uint8_t* ConstructMaterial(void* memory) { return GameFunction<uint8_t*(__thiscall*)(void*)>(0x004D1A90)(memory); }
void DestroyMaterial(void* material) { GameFunction<void(__thiscall*)(void*)>(0x004D2BD0)(material); }
// The C runtime's rand (the game's: its sequence).
int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); }

template <typename T>
T& At(uint8_t* p, uint32_t offset)
{
    return *reinterpret_cast<T*>(p + offset);
}

} // namespace

// 0x004D9CF0
uint32_t __cdecl BlendModeFromName(uint32_t checksum)
{
    switch (checksum) {
    case 0x515E298E: // diffuse
    case 0x806FFF30: // none
        return 0;
    case 0x02E58C18: return 1; // add
    case 0xA86285A1: return 2; // fixadd
    case 0xDEA7E576:           // subtract
    case 0xA7FD7D23:           // sub
        return 3;
    case 0x0D7A749A: // fixsubtract
    case 0x0EEA99FF: // fixsub
        return 4;
    case 0x54628ED7: return 5;  // blend
    case 0x18B98905: return 6;  // fixblend
    case 0x40F44B8A: return 7;  // modulate
    case 0x90B93703: return 8;  // fixmodulate
    case 0x68E77F40: return 9;  // brighten
    case 0xB8AA03C9: return 10; // fixbrighten
    default: return 0;
    }
}

// 0x004D9B80
sSimpleMaterial* sSimpleMaterial::Construct(uint32_t /*unused0*/, uint32_t /*unused1*/, uint32_t textureChecksum,
                                            uint32_t blendChecksum, int fixedAlpha, uint32_t /*unused5*/,
                                            uint32_t /*unused6*/)
{
    uint32_t texture = 0;
    if (uint8_t* entry = FindTexture(textureChecksum))
        texture = (entry[8] & 1) ? **reinterpret_cast<uint32_t**>(entry + 0x10) : At<uint32_t>(entry, 0x10);
    void* memory = Mem::New(0x110);
    uint8_t* m = memory ? ConstructMaterial(memory) : nullptr; // (a failed allocation would crash below)
    At<uint32_t>(m, 0x20) = (texture ? 4 : 0) | 0x40;
    int first = Rand();
    At<int>(m, 0) = first * Rand(); // the sort key
    At<uint32_t>(m, 8) = 1;         // one pass
    At<uint32_t>(m, 0x30) = texture;
    At<uint32_t>(m, 0x80) = BlendModeFromName(blendChecksum) | static_cast<uint32_t>(fixedAlpha) << 24;
    At<float>(m, 0x40) = 0.5f; // the colour
    At<float>(m, 0x44) = 0.5f;
    At<float>(m, 0x48) = 0.5f;
    At<float>(m, 0x4C) = static_cast<float>(static_cast<double>(fixedAlpha) * (1.0 / 128)); // 0x0064A2FC
    At<uint32_t>(m, 0x90) = 0;
    At<uint32_t>(m, 0xE0) = 0;
    At<uint8_t>(m, 0x10) = 1;
    At<uint8_t>(m, 0xD) = 1;
    At<uint8_t>(m, 0xE) = 0;
    material = m;
    uint32_t mode = At<uint32_t>(m, 0x80) & 0xFFFFFF;
    shader = GameVariable<void*>(mode == 9 || mode == 10 ? 0x007CA944 : 0x007CA964);
    return this;
}

// 0x004D9CD0
void sSimpleMaterial::DestroyBody()
{
    if (uint8_t* m = material) {
        DestroyMaterial(m);
        Mem::Manager::Instance()->Free(m);
    }
}

// 0x004DA6D0
int __cdecl SkipFileChunk(void* file)
{
    uint32_t id, size;
    File::Read(&id, 4, 1, file);
    File::Read(&size, 4, 1, file);
    File::Seek(file, static_cast<int>(size), SEEK_CUR);
    return 0;
}

// 0x004DA710
int __cdecl SkipMemoryChunk(uint8_t** data)
{
    *data = *data + *reinterpret_cast<uint32_t*>(*data + 4) + 8;
    return 0;
}

} // namespace Nx

REPLACE(0x004D9CF0, Nx::BlendModeFromName);
REPLACE(0x004D9B80, &Nx::sSimpleMaterial::Construct);
REPLACE(0x004D9CD0, &Nx::sSimpleMaterial::DestroyBody);
REPLACE(0x004DA6D0, Nx::SkipFileChunk);
REPLACE(0x004DA710, Nx::SkipMemoryChunk);
