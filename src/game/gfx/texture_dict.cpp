// The engine's texture dictionary and the loaded dictionaries (see texture_dict.h).

#include "game/gfx/texture_dict.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/pc_texture.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kTexDictVtable = 0x00649BB4;

// The loaded dictionaries, by checksum (a static hash table).
Lst::HashTable& LoadedTexDicts() { return GameVariable<Lst::HashTable>(0x00700F04); }

// The multipliers FindTexture tries the name's CRC with (0x0067E520: 2, 3, 4, 6, 12, 8, 24, then -1).
const int* KeyMultipliers() { return &GameVariable<int>(0x0067E520); }

// The CRC of what follows a name's last slash or backslash.
uint32_t FileNameChecksum(const char* name)
{
    int start = static_cast<int>(std::strlen(name));
    while (start > 0 && name[start - 1] != '\\' && name[start - 1] != '/')
        --start;
    return Crc::GenerateCRCFromString(name + start);
}

template <typename R, typename... A>
R Slot(CTexDict* dict, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CTexDict*, A...)>(dict->vtable[slot])(dict, args...);
}
template <typename R, typename... A>
R TextureSlot(CTexture* texture, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CTexture*, A...)>(texture->vtable[slot])(texture, args...);
}

// (0x0048C1B0: an empty call - compiled-out debug code - each unload makes before the platform's)
void DebugBeforeUnload() {}

// A new texture: kept on flushes or not, and added under its checksum.
CTexture* AddTexture(CTexDict* dict, CTexture* texture, bool permanent)
{
    if (texture) {
        texture->SetPermanent(permanent);
        uint32_t key = texture->m_checksum;
        dict->mp_textures->PutItem(key, texture);
    }
    return texture;
}

// A new table of 32 buckets (none without memory).
Lst::HashTable* NewTextureTable()
{
    void* memory = Mem::New(sizeof(Lst::HashTable));
    return memory ? new (memory) Lst::HashTable(5) : nullptr;
}

} // namespace

// 0x0049F830
CTexDict* CTexDict::Construct(uint32_t checksum, bool makeTable)
{
    m_checksum = checksum;
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    m_refCount = 1;
    mp_textures = makeTable ? NewTextureTable() : nullptr;
    return this;
}

// 0x0049F8F0
CTexDict* CTexDict::ConstructNamed(const char* name, bool makeTable)
{
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    m_checksum = Crc::GenerateCRCFromString(name);
    m_refCount = 1;
    mp_textures = makeTable ? NewTextureTable() : nullptr;
    return this;
}

// 0x0049FC70
void CTexDict::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    FlushTextures(true);
    if (Lst::HashTable* table = mp_textures) {
        if (table->m_buckets) {
            table->FlushAllItems();
            Mem::Free(table->m_buckets);
            table->m_buckets = nullptr;
        }
        Mem::Free(table);
    }
}

// 0x0049FCC0
CTexDict* CTexDict::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049F540
CTexture* CTexDict::GetTexture(uint32_t checksum)
{
    if (!mp_textures)
        return nullptr;
    return static_cast<CTexture*>(mp_textures->GetItem(checksum, true));
}

// 0x0049F560
CTexture* CTexDict::GetTextureByName(const char* name)
{
    uint32_t checksum = FileNameChecksum(name);
    if (!mp_textures)
        return nullptr;
    return static_cast<CTexture*>(mp_textures->GetItem(checksum, true));
}

// 0x0049F660 (the original copies the name into 512 bytes unbounded)
CTexture* CTexDict::FindTexture(const char* name)
{
    auto find = [this](uint32_t checksum) -> CTexture* {
        for (const int* multiplier = KeyMultipliers(); *multiplier > 0; ++multiplier) {
            uint32_t key = static_cast<uint32_t>(*multiplier) * checksum;
            CTexture* texture = mp_textures ? static_cast<CTexture*>(mp_textures->GetItem(key)) : nullptr;
            if (texture)
                return texture;
        }
        return nullptr;
    };
    if (CTexture* texture = find(Crc::GenerateCRCFromString(name)))
        return texture;
    char other[0x200];
    std::snprintf(other, sizeof(other), "%s", name);
    size_t length = std::strlen(other);
    if (length > 4 && length - 4 + 14 <= sizeof(other)) // "_auto32m0.png" and its 0 in place of the last four
        std::memcpy(other + length - 4, reinterpret_cast<const char*>(0x00649BA4), 14);
    return find(Crc::GenerateCRCFromString(other));
}

// 0x0049F9B0
CTexture* CTexDict::LoadTexture(const char* name, uint32_t a1, uint32_t a2, bool permanent)
{
    Mem::PushMemProfile(name);
    auto* texture = Slot<CTexture*>(this, 1, name, a1, a2);
    AddTexture(this, texture, permanent);
    Mem::PopMemProfile();
    return texture;
}

// 0x0049FA20
CTexture* CTexDict::LoadTextureFromBuffer(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3,
                                          uint32_t a4, bool permanent, uint32_t a6)
{
    Mem::PushMemProfile(reinterpret_cast<const char*>(0x00649BD8)); // "texture from buf"
    auto* texture = Slot<CTexture*>(this, 2, data, size, checksum, a3, a4, a6);
    AddTexture(this, texture, permanent);
    Mem::PopMemProfile();
    return texture;
}

// 0x0049FAA0
CTexture* CTexDict::CreateTexture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bitsPerPixel,
                                  uint32_t a4, uint32_t a5, bool permanent)
{
    Mem::PushMemProfile(reinterpret_cast<const char*>(0x00649BEC)); // "created texture"
    auto* texture = Slot<CTexture*>(this, 3, checksum, width, height, bitsPerPixel, a4, a5);
    AddTexture(this, texture, permanent);
    Mem::PopMemProfile();
    return texture;
}

// 0x0049F500
bool CTexDict::UnloadTexture(CTexture* texture)
{
    uint32_t key = texture->m_checksum;
    DebugBeforeUnload();
    if (!Slot<bool>(this, 5, texture))
        return false;
    mp_textures->FlushItem(key);
    return true;
}

// 0x0049F5C0 (the next texture found before this one goes)
void CTexDict::FlushTextures(bool all)
{
    Lst::HashTable* table = mp_textures;
    if (!table)
        return;
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    if (!table->IterateNext() || !table->m_iterItem)
        return;
    auto* texture = static_cast<CTexture*>(table->m_iterItem->value);
    while (texture) {
        CTexture* next = nullptr;
        if (mp_textures->IterateNext() && mp_textures->m_iterItem)
            next = static_cast<CTexture*>(mp_textures->m_iterItem->value);
        if (all || !(texture->m_flags & kTexturePermanent)) {
            uint32_t key = texture->m_checksum;
            TextureSlot<void*>(texture, 0, 1u);
            mp_textures->FlushItem(key);
        }
        texture = next;
    }
}

// 0x0049F800
bool CTexDict::ReplaceTexture(const char* name, CTexture* source)
{
    CTexture* texture = FindTexture(name);
    if (!texture)
        return false;
    return TextureSlot<bool>(texture, 4, source);
}

// 0x0049FB20 (no texture loaded: the original reads through null)
bool CTexDict::ReplaceTextureFromFile(const char* name, const char* file)
{
    CTexture* texture = FindTexture(name);
    if (!texture)
        return false;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    Mem::PushMemProfile(file);
    auto* loaded = Slot<CTexture*>(this, 1, file, 0u, 0u);
    AddTexture(this, loaded, true);
    Mem::PopMemProfile();
    bool replaced = TextureSlot<bool>(texture, 4, loaded);
    uint32_t key = loaded->m_checksum;
    DebugBeforeUnload();
    if (Slot<bool>(this, 5, loaded))
        mp_textures->FlushItem(key);
    Mem::Manager::Get()->PopContext();
    return replaced;
}

// 0x0049FBE0
CTexture* CTexDict::CopyTexture(uint32_t checksum, CTexture* source)
{
    auto* texture = Slot<CTexture*>(this, 8, checksum, source);
    if (texture) {
        texture->m_checksum = checksum;
        mp_textures->PutItem(checksum, texture);
    }
    return texture;
}

// 0x0049FC20
CTexture* CTexDict::CopyTextureWithOverlay(uint32_t checksum, CTexture* source, CTexture* overlay, uint32_t argument)
{
    auto* texture = Slot<CTexture*>(this, 8, checksum, source);
    if (texture) {
        texture->m_checksum = checksum;
        mp_textures->PutItem(checksum, texture);
        TextureSlot<bool>(texture, 41, overlay, argument);
    }
    return texture;
}

// --- the loaded dictionaries --------------------------------------------------------------------------------------

// 0x0049F010
CTexDict* __cdecl FindTexDict(uint32_t checksum) { return static_cast<CTexDict*>(LoadedTexDicts().GetItem(checksum)); }

// 0x0049F0C0
CTexDict* __cdecl LoadTexDict(const char* path, int /*level*/, uint32_t offset, uint32_t /*a3*/, uint32_t /*a4*/)
{
    char file[0x80];
    std::snprintf(file, sizeof(file), "%s.%s", path, PlatformExtension());
    Mem::PushMemProfile(path);
    uint32_t key = Crc::GenerateCRCFromString(path) + offset;
    CTexDict* dict = FindTexDict(key);
    if (dict) {
        ++dict->m_refCount;
    } else {
        // (the original passes level, offset, a3 and a4 too: the PC's reads the name alone)
        dict = reinterpret_cast<CTexDict*>(LoadTexDictFile(file));
        dict->m_checksum = key;
        LoadedTexDicts().PutItem(key, dict);
    }
    Mem::PopMemProfile();
    return dict;
}

// 0x0049F050
CTexDict* __cdecl LoadTexDictFromMemory(uint32_t checksum, const uint8_t* data, uint32_t /*size*/, uint32_t /*a3*/,
                                        uint32_t offset, uint32_t /*a5*/, uint32_t /*a6*/)
{
    Mem::PushMemProfile(reinterpret_cast<const char*>(0x00649AC8)); // "TEX_data_buffer"
    uint32_t key = checksum + offset;
    CTexDict* dict = FindTexDict(key);
    if (!dict) {
        // (the original passes size, a3, offset, a5 and a6 too: the PC's reads the checksum and data alone)
        dict = reinterpret_cast<CTexDict*>(LoadTexDictMemory(key, data));
        dict->m_checksum = key;
        LoadedTexDicts().PutItem(key, dict);
    }
    Mem::PopMemProfile();
    return dict;
}

// 0x0049F180 (the skin flag is pushed as a whole word in the original - stack noise above its byte)
CTexDict* __cdecl LoadModelTextures(const char* name, uint32_t offset, uint32_t a4)
{
    char base[0x200];
    bool skin = GameFunction<bool(__cdecl*)(const char*, char*)>(0x0048BFE0)(name, base);
    char file[0x200];
    std::snprintf(file, sizeof(file), "%s.tex", base);
    return LoadTexDict(file, 0, offset, skin, a4);
}

// 0x0049EFD0
bool __cdecl UnloadTexDict(CTexDict* dict)
{
    if (--dict->m_refCount != 0)
        return false;
    uint32_t key = dict->m_checksum;
    LoadedTexDicts().FlushItem(key);
    GameFunction<bool(__cdecl*)(CTexDict*)>(0x004B6920)(dict); // (deleted through its vtable)
    return true;
}

// 0x0049EF90 (the original sprintfs unbounded)
void __cdecl TexDictFileName(char* out, const char* name)
{
    std::snprintf(out, 256, "%s.%s", name, PlatformExtension());
}

// 0x0049EFB0
void __cdecl FlushParticleTextures(bool all) { GameVariable<CTexDict*>(0x00700F20)->FlushTextures(all); }

} // namespace Nx

REPLACE(0x0049F830, &Nx::CTexDict::Construct);
REPLACE(0x0049F8F0, &Nx::CTexDict::ConstructNamed);
REPLACE(0x0049FC70, &Nx::CTexDict::DestroyBody);
REPLACE(0x0049FCC0, &Nx::CTexDict::Destroy);
REPLACE(0x0049F540, &Nx::CTexDict::GetTexture);
REPLACE(0x0049F560, &Nx::CTexDict::GetTextureByName);
REPLACE(0x0049F660, &Nx::CTexDict::FindTexture);
REPLACE(0x0049F9B0, &Nx::CTexDict::LoadTexture);
REPLACE(0x0049FA20, &Nx::CTexDict::LoadTextureFromBuffer);
REPLACE(0x0049FAA0, &Nx::CTexDict::CreateTexture);
REPLACE(0x0049F500, &Nx::CTexDict::UnloadTexture);
REPLACE(0x0049F5C0, &Nx::CTexDict::FlushTextures);
REPLACE(0x0049F800, &Nx::CTexDict::ReplaceTexture);
REPLACE(0x0049FB20, &Nx::CTexDict::ReplaceTextureFromFile);
REPLACE(0x0049FBE0, &Nx::CTexDict::CopyTexture);
REPLACE(0x0049FC20, &Nx::CTexDict::CopyTextureWithOverlay);
REPLACE(0x0049F010, Nx::FindTexDict);
REPLACE(0x0049F0C0, Nx::LoadTexDict);
REPLACE(0x0049F050, Nx::LoadTexDictFromMemory);
REPLACE(0x0049F180, Nx::LoadModelTextures);
REPLACE(0x0049EFD0, Nx::UnloadTexDict);
REPLACE(0x0049EF90, Nx::TexDictFileName);
REPLACE(0x0049EFB0, Nx::FlushParticleTextures);
