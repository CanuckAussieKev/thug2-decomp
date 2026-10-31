// The engine's texture dictionary and the loaded dictionaries (see texture_dict.h).

#include "engine/gfx/texture_dict.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/pc_texture.h"
#include "engine/lists/key_map.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/sys/startup_init.h"

namespace Engine3D {

int g_keyMultipliers[8]{2, 3, 4, 6, 12, 8, 24, -1};

// The game's texture dictionaries: the textures every level shares, and the particles'.
CTextureSet* g_gameTextureDict;
CTextureSet* g_particleTextureDict;

namespace {

extern void** const kTexDictVtable;

// The loaded dictionaries, by checksum (a static hash table).
ListKit::KeyMap g_loadedTexDicts{ListKit::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(4) at start-up)
ListKit::KeyMap& LoadedTexDicts() { return g_loadedTexDicts; }

// The multipliers FindTexture tries the name's CRC with (0x0067E520: 2, 3, 4, 6, 12, 8, 24, then -1).
const int* KeyMultipliers() { return g_keyMultipliers; }

// The CRC of what follows a name's last slash or backslash.
uint32_t FilenameKey(const char* name)
{
    int start = static_cast<int>(std::strlen(name));
    while (start > 0 && name[start - 1] != '\\' && name[start - 1] != '/')
        --start;
    return Checksums::HashFromText(name + start);
}

template <typename R, typename... A>
R Bay(CTextureSet* dict, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CTextureSet*, A...)>(dict->vtable[slot])(dict, args...);
}
template <typename R, typename... A>
R TextureSlot(CBitmapTexture* texture, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CBitmapTexture*, A...)>(texture->vtable[slot])(texture, args...);
}

// (0x0048C1B0: an empty call - compiled-out debug code - each unload makes before the platform's)
void DebugBeforeUnload() {}

// A new texture: kept on flushes or not, and added under its checksum.
CBitmapTexture* InsertTexture(CTextureSet* dict, CBitmapTexture* texture, bool permanent)
{
    if (texture) {
        texture->PinResident(permanent);
        uint32_t key = texture->m_checksum;
        dict->m_textures->StoreEntry(key, texture);
    }
    return texture;
}

// A new table of 32 buckets (none without memory).
ListKit::KeyMap* NewTextureTable()
{
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    return memory ? new (memory) ListKit::KeyMap(5) : nullptr;
}

} // namespace

// 0x0049F830
CTextureSet* CTextureSet::MakeNew(uint32_t checksum, bool makeTable)
{
    m_checksum = checksum;
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    m_shareCount = 1;
    m_textures = makeTable ? NewTextureTable() : nullptr;
    return this;
}

// 0x0049F8F0
CTextureSet* CTextureSet::ConstructNamed(const char* name, bool makeTable)
{
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    m_checksum = Checksums::HashFromText(name);
    m_shareCount = 1;
    m_textures = makeTable ? NewTextureTable() : nullptr;
    return this;
}

// 0x0049FC70
void CTextureSet::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kTexDictVtable);
    DropTextures(true);
    if (ListKit::KeyMap* table = m_textures) {
        if (table->m_buckets) {
            table->EmptyTable();
            MemSys::Dealloc(table->m_buckets);
            table->m_buckets = nullptr;
        }
        MemSys::Dealloc(table);
    }
}

// 0x0049FCC0
CTextureSet* CTextureSet::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x0049F540
CBitmapTexture* CTextureSet::FetchSkin(uint32_t checksum)
{
    if (!m_textures)
        return nullptr;
    return static_cast<CBitmapTexture*>(m_textures->FetchEntry(checksum, true));
}

// 0x0049F560
CBitmapTexture* CTextureSet::GetTextureByName(const char* name)
{
    uint32_t checksum = FilenameKey(name);
    if (!m_textures)
        return nullptr;
    return static_cast<CBitmapTexture*>(m_textures->FetchEntry(checksum, true));
}

// 0x0049F660 (the original copies the name into 512 bytes unbounded)
CBitmapTexture* CTextureSet::FindTexture(const char* name)
{
    auto find = [this](uint32_t checksum) -> CBitmapTexture* {
        for (const int* multiplier = KeyMultipliers(); *multiplier > 0; ++multiplier) {
            uint32_t key = static_cast<uint32_t>(*multiplier) * checksum;
            CBitmapTexture* texture = m_textures ? static_cast<CBitmapTexture*>(m_textures->FetchEntry(key)) : nullptr;
            if (texture)
                return texture;
        }
        return nullptr;
    };
    if (CBitmapTexture* texture = find(Checksums::HashFromText(name)))
        return texture;
    char other[0x200];
    std::snprintf(other, sizeof(other), "%s", name);
    size_t length = std::strlen(other);
    if (length > 4 && length - 4 + 14 <= sizeof(other)) // "_auto32m0.png" and its 0 in place of the last four
        std::memcpy(other + length - 4, "_auto32m0.png", 14);
    return find(Checksums::HashFromText(other));
}

// 0x0049F9B0
CBitmapTexture* CTextureSet::IngestSkin(const char* name, uint32_t a1, uint32_t a2, bool permanent)
{
    MemSys::PushMemProfile(name);
    auto* texture = Bay<CBitmapTexture*>(this, 1, name, a1, a2);
    InsertTexture(this, texture, permanent);
    MemSys::PopMemProfile();
    return texture;
}

// 0x0049FA20
CBitmapTexture* CTextureSet::ReadTextureBuffer(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3,
                                          uint32_t a4, bool permanent, uint32_t a6)
{
    MemSys::PushMemProfile("texture from buffer"); // "texture from buf"
    auto* texture = Bay<CBitmapTexture*>(this, 2, data, size, checksum, a3, a4, a6);
    InsertTexture(this, texture, permanent);
    MemSys::PopMemProfile();
    return texture;
}

// 0x0049FAA0
CBitmapTexture* CTextureSet::CreateTexture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bitsPerPixel,
                                  uint32_t a4, uint32_t a5, bool permanent)
{
    MemSys::PushMemProfile("created texture"); // "created texture"
    auto* texture = Bay<CBitmapTexture*>(this, 3, checksum, width, height, bitsPerPixel, a4, a5);
    InsertTexture(this, texture, permanent);
    MemSys::PopMemProfile();
    return texture;
}

// 0x0049F500
bool CTextureSet::UnloadTexture(CBitmapTexture* texture)
{
    uint32_t key = texture->m_checksum;
    DebugBeforeUnload();
    if (!Bay<bool>(this, 5, texture))
        return false;
    m_textures->EraseEntry(key);
    return true;
}

// 0x0049F5C0 (the next texture found before this one goes)
void CTextureSet::DropTextures(bool all)
{
    ListKit::KeyMap* table = m_textures;
    if (!table)
        return;
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    if (!table->StepIterator() || !table->m_iterItem)
        return;
    auto* texture = static_cast<CBitmapTexture*>(table->m_iterItem->value);
    while (texture) {
        CBitmapTexture* next = nullptr;
        if (m_textures->StepIterator() && m_textures->m_iterItem)
            next = static_cast<CBitmapTexture*>(m_textures->m_iterItem->value);
        if (all || !(texture->m_flagBits & kTexturePermanent)) {
            uint32_t key = texture->m_checksum;
            TextureSlot<void*>(texture, 0, 1u);
            m_textures->EraseEntry(key);
        }
        texture = next;
    }
}

// 0x0049F800
bool CTextureSet::SwapTexture(const char* name, CBitmapTexture* source)
{
    CBitmapTexture* texture = FindTexture(name);
    if (!texture)
        return false;
    return TextureSlot<bool>(texture, 4, source);
}

// 0x0049FB20 (no texture loaded: the original reads through null)
bool CTextureSet::ReplaceTextureFromFile(const char* name, const char* file)
{
    CBitmapTexture* texture = FindTexture(name);
    if (!texture)
        return false;
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    MemSys::PushMemProfile(file);
    auto* loaded = Bay<CBitmapTexture*>(this, 1, file, 0u, 0u);
    InsertTexture(this, loaded, true);
    MemSys::PopMemProfile();
    bool replaced = TextureSlot<bool>(texture, 4, loaded);
    uint32_t key = loaded->m_checksum;
    DebugBeforeUnload();
    if (Bay<bool>(this, 5, loaded))
        m_textures->EraseEntry(key);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return replaced;
}

// 0x0049FBE0
CBitmapTexture* CTextureSet::CopyTexture(uint32_t checksum, CBitmapTexture* source)
{
    auto* texture = Bay<CBitmapTexture*>(this, 8, checksum, source);
    if (texture) {
        texture->m_checksum = checksum;
        m_textures->StoreEntry(checksum, texture);
    }
    return texture;
}

// 0x0049FC20
CBitmapTexture* CTextureSet::CopyTextureWithOverlay(uint32_t checksum, CBitmapTexture* source, CBitmapTexture* overlay, uint32_t argument)
{
    auto* texture = Bay<CBitmapTexture*>(this, 8, checksum, source);
    if (texture) {
        texture->m_checksum = checksum;
        m_textures->StoreEntry(checksum, texture);
        TextureSlot<bool>(texture, 41, overlay, argument);
    }
    return texture;
}

// --- the loaded dictionaries --------------------------------------------------------------------------------------

// 0x0049F010
CTextureSet* __cdecl FindTexDict(uint32_t checksum) { return static_cast<CTextureSet*>(LoadedTexDicts().FetchEntry(checksum)); }

// 0x0049F0C0
CTextureSet* __cdecl LoadTexDict(const char* path, int /*level*/, uint32_t offset, uint32_t /*a3*/, uint32_t /*a4*/)
{
    char file[0x80];
    std::snprintf(file, sizeof(file), "%s.%s", path, PlatformSuffix());
    MemSys::PushMemProfile(path);
    uint32_t key = Checksums::HashFromText(path) + offset;
    CTextureSet* dict = FindTexDict(key);
    if (dict) {
        ++dict->m_shareCount;
    } else {
        // (the original passes level, offset, a3 and a4 too: the PC's reads the name alone)
        dict = reinterpret_cast<CTextureSet*>(LoadTexDictFile(file));
        dict->m_checksum = key;
        LoadedTexDicts().StoreEntry(key, dict);
    }
    MemSys::PopMemProfile();
    return dict;
}

// 0x0049F050
CTextureSet* __cdecl LoadTexDictFromMemory(uint32_t checksum, const uint8_t* data, uint32_t /*size*/, uint32_t /*a3*/,
                                        uint32_t offset, uint32_t /*a5*/, uint32_t /*a6*/)
{
    MemSys::PushMemProfile("TEX_data_buffer"); // "TEX_data_buffer"
    uint32_t key = checksum + offset;
    CTextureSet* dict = FindTexDict(key);
    if (!dict) {
        // (the original passes size, a3, offset, a5 and a6 too: the PC's reads the checksum and data alone)
        dict = reinterpret_cast<CTextureSet*>(LoadTexDictMemory(key, data));
        dict->m_checksum = key;
        LoadedTexDicts().StoreEntry(key, dict);
    }
    MemSys::PopMemProfile();
    return dict;
}

// 0x0049F180 (the skin flag is pushed as a whole word in the original - stack noise above its byte)
CTextureSet* __cdecl LoadModelTextures(const char* name, uint32_t offset, uint32_t a4)
{
    char base[0x200];
    bool skin = Engine::SplitSkinName(name, base);
    char file[0x200];
    std::snprintf(file, sizeof(file), "%s.tex", base);
    return LoadTexDict(file, 0, offset, skin, a4);
}

// 0x0049EFD0
bool __cdecl UnloadTexDict(CTextureSet* dict)
{
    if (--dict->m_shareCount != 0)
        return false;
    uint32_t key = dict->m_checksum;
    LoadedTexDicts().EraseEntry(key);
    DeleteModelLights(reinterpret_cast<CPcModelLights*>(dict)); // (deleted through its vtable)
    return true;
}

// 0x0049EF90 (the original sprintfs unbounded)
void __cdecl TexDictFileName(char* out, const char* name)
{
    std::snprintf(out, 256, "%s.%s", name, PlatformSuffix());
}

// 0x0049EFB0
void __cdecl DropParticleTextures(bool all) { g_particleTextureDict->DropTextures(all); }

namespace {
static VtableSlot kTexDictVtable_slots[] = {&Engine3D::CTextureSet::Teardown, Stubs::ReturnZero3, Stubs::ReturnZero6,
                Stubs::ReturnZero6, Stubs::ReturnZero1, Stubs::ReturnFalse1, Stubs::IgnoreArgument, Stubs::ReturnFalse1,
                Stubs::ReturnZero2};
void** const kTexDictVtable = reinterpret_cast<void**>(kTexDictVtable_slots);
} // namespace

} // namespace Nx

namespace Engine3D {

namespace {

// A table (2 buckets) nothing uses but its static initialiser and exit function.
ListKit::KeyMap g_spareTexDictTable{ListKit::KeyMap::Unconstructed{}};

// The loaded dictionary named `name` (one more use of it), or a new one, made and listed.
CTextureSet* SharedTexDict(const char* name)
{
    uint32_t checksum = Checksums::HashFromText(name);
    if (CTextureSet* dict = FindTexDict(checksum)) {
        ++dict->m_shareCount;
        return dict;
    }
    auto* dict = reinterpret_cast<CTextureSet*>(CreateTexDict(checksum));
    LoadedTexDicts().StoreEntry(checksum, dict);
    return dict;
}

} // namespace

// 0x00640BF0 (a static initialiser): the loaded dictionaries' table, 16 buckets.
void ConstructLoadedTexDicts() { new (&g_loadedTexDicts) ListKit::KeyMap(4); }

// 0x00640C10, 0x00640C60 (static initialisers): the dictionaries of the sprites and the particles.
void SetUpGameTextureDict() { g_gameTextureDict = SharedTexDict("sprite"); }
void SetUpParticleTextureDict() { g_particleTextureDict = SharedTexDict("particle"); }

// 0x00640CB0 (a static initialiser)
void ConstructSpareTexDictTable() { new (&g_spareTexDictTable) ListKit::KeyMap(1); }

} // namespace Nx
