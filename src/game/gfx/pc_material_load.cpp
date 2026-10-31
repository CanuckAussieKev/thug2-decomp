// The PC materials' loaders (surface_material.h): a scene's material list - from memory or from a file, the same records -
// into a new hash table by checksum, their textures looked up in the scene's texture table.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/mem.h"
#include "game/file/file_load.h"
#include "game/gfx/surface_material.h"
#include "game/lists/key_map.h"

namespace Engine3D {

namespace {

// The records come through one of these: from memory (a cursor moved on) or from a file.
struct MemoryReader {
    uint8_t* cursor;
    void TakeIn(void* out, int size, int count)
    {
        std::memcpy(out, cursor, static_cast<size_t>(size) * count);
        cursor += size * count;
    }
    void Skip(int bytes) { cursor += bytes; }
};
struct FileReader {
    FileLoad::Hdl file;
    void TakeIn(void* out, int size, int count) { FileLoad::TakeIn(out, size, count, file); }
    void Skip(int bytes) { FileLoad::SeekTo(file, bytes, 1); }
};

// A texture table's entry for the checksum (its texture at +0x10, or behind it with flag 1), or null.
uint8_t* FindTexture(const ListKit::KeyMap* textures, uint32_t checksum)
{
    auto* entry = static_cast<uint8_t*>(textures->FetchEntry(checksum));
    if (!entry)
        return nullptr;
    auto* texture = *reinterpret_cast<uint8_t**>(entry + 0x10);
    if (entry[8] & 1)
        texture = *reinterpret_cast<uint8_t**>(texture);
    return texture;
}

template <typename T>
T Pull(auto& reader, int size = sizeof(T))
{
    T value{};
    reader.TakeIn(&value, size, 1);
    return value;
}

// The table's size: 2^bits buckets, from 4 up to 4096, at least twice the count.
int TableBits(uint32_t count)
{
    int bits = 2;
    uint32_t size = 4;
    while (count * 2 >= size && bits < 12) {
        size <<= 1;
        ++bits;
    }
    return bits;
}

template <typename Reader>
ListKit::KeyMap* ReadMaterials(Reader& reader, const ListKit::KeyMap* textures)
{
    uint32_t count = Pull<uint32_t>(reader);
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    auto* table = memory ? new (memory) ListKit::KeyMap(TableBits(count)) : nullptr;
    for (uint32_t index = 0; index < count; ++index) {
        void* at = MemSys::Alloc(sizeof(materialRef));
        materialRef* m = at ? static_cast<materialRef*>(at)->MakeNew() : nullptr;
        reader.TakeIn(&m->checksum, 4, 1);
        reader.TakeIn(&m->nameHash, 4, 1);
        reader.TakeIn(&m->passCount, 4, 1);
        m->alphaRef = static_cast<uint8_t>(Pull<uint32_t>(reader));
        reader.TakeIn(&m->sortFlag, 1, 1);
        reader.TakeIn(&m->sortKey, 4, 1);
        uint8_t keepCulling = Pull<uint8_t>(reader); // (else a blended pass 0 turns culling off)
        reader.TakeIn(&m->twoSided, 1, 1);
        int bias = Pull<int>(reader);
        m->depthBias = static_cast<uint8_t>(bias > 0x10 ? 0x10 : bias);
        if (Pull<uint8_t>(reader)) {
            reader.TakeIn(&m->unknown14, 4, 1);
            reader.TakeIn(&m->unknown18, 4, 1);
        }
        reader.TakeIn(&m->specularPower, 4, 1);
        if (m->specularPower > 0.0f)
            reader.TakeIn(m->specular, 12, 1);
        bool allHalf = true;
        for (uint32_t pass = 0; pass < m->passCount; ++pass) {
            uint32_t textureChecksum = Pull<uint32_t>(reader);
            reader.TakeIn(&m->passFlags[pass], 4, 1);
            Pull<uint8_t>(reader);
            reader.TakeIn(m->passColor[pass], 12, 1);
            const uint32_t half = 0x3F000000;
            if (allHalf && (std::memcmp(&m->passColor[pass][0], &half, 4) != 0 ||
                            std::memcmp(&m->passColor[pass][1], &half, 4) != 0 ||
                            std::memcmp(&m->passColor[pass][2], &half, 4) != 0))
                allHalf = false;
            uint32_t blend[2];
            reader.TakeIn(blend, 8, 1);
            m->blendModes[pass] = (blend[0] & 0xFFFFFF) | blend[1] << 24;
            m->passColor[pass][3] = static_cast<float>(static_cast<double>(blend[1]) * (1.0f / 128)); // the fixed alpha
            if (pass == 0 && !keepCulling && (m->blendModes[0] & 0xFFFFFF))
                m->twoSided = 1;
            uint32_t u = Pull<uint32_t>(reader);
            uint32_t v = Pull<uint32_t>(reader);
            m->addressModes[pass] = v << 16 | u;
            reader.TakeIn(m->reflectionTiling[pass], 8, 1);
            reader.TakeIn(&m->unknownC0[pass], 4, 1);
            if (m->passFlags[pass] & 1) {
                m->hasUVWibble = true;
                void* w = MemSys::Alloc(sizeof(sUVOscillator));
                sUVOscillator* wibble = w ? static_cast<sUVOscillator*>(w)->MakeNew() : nullptr;
                m->uvWibble[pass] = wibble;
                reader.TakeIn(wibble, 0x20, 1);
            }
            if (pass == 0 && (m->passFlags[0] & 2)) {
                uint32_t anims = Pull<uint32_t>(reader);
                m->colorAnimCount = anims;
                m->colorAnims = static_cast<sColorAnim*>(MemSys::Alloc(anims * sizeof(sColorAnim)));
                m->animColors = static_cast<uint32_t*>(MemSys::Alloc(anims * 4));
                for (uint32_t i = 0; i < anims; ++i) {
                    uint32_t keys = Pull<uint32_t>(reader);
                    int phase = Pull<int>(reader);
                    m->colorAnims[i].keyCount = keys;
                    m->colorAnims[i].phase = phase;
                    m->colorAnims[i].keys = static_cast<sColorKey*>(MemSys::Alloc(keys * sizeof(sColorKey)));
                    reader.TakeIn(m->colorAnims[i].keys, 8, static_cast<int>(keys));
                }
            }
            if (m->passFlags[pass] & 0x800) {
                if (!m->textureAnim) {
                    m->textureAnim = static_cast<sTextureAnim*>(MemSys::Alloc(sizeof(sTextureAnim)));
                    std::memset(m->textureAnim, 0, sizeof(sTextureAnim));
                    m->hasTextureAnim = true;
                }
                int keys = Pull<int>(reader);
                Pull<uint32_t>(reader);
                int loops = Pull<int>(reader);
                uint32_t phase = Pull<uint32_t>(reader);
                m->textureAnim->keyCounts[pass] = static_cast<uint32_t>(keys);
                m->textureAnim->phases[pass] = phase;
                m->textureAnim->loops[pass] = loops;
                m->textureAnim->keys[pass] = static_cast<sTextureKey*>(MemSys::Alloc(static_cast<uint32_t>(keys) * 8));
                for (int k = 0; k < keys; ++k) {
                    sTextureKey& key = m->textureAnim->keys[pass][k];
                    reader.TakeIn(&key.time, 4, 1);
                    uint32_t checksum = Pull<uint32_t>(reader);
                    if (k == 0)
                        textureChecksum = checksum; // (the pass's own texture: the first key's)
                    key.texture = FindTexture(textures, checksum);
                }
            }
            if (!textureChecksum) {
                m->textures[pass] = nullptr;
                reader.Skip(0x10);
                continue;
            }
            m->textures[pass] = FindTexture(textures, textureChecksum);
            Pull<uint32_t>(reader);
            Pull<uint32_t>(reader);
            float lodBias = Pull<float>(reader);
            Pull<uint32_t>(reader);
            double lod = static_cast<double>(lodBias) + 8.0f;
            float stored = static_cast<float>(lod);
            std::memcpy(&m->mipLodBias[pass], &stored, 4);
            if (lod > 0.0)
                m->mipLodBias[pass] = 0; // (no more than 0)
        }
        if (allHalf)
            m->passFlags[0] |= 0x10000000; // (no pass changes its colour)
        if (m->specularPower > 0.0f)
            m->passFlags[0] |= 0x100;
        if (!table->FetchEntry(m->checksum))
            table->StoreEntry(m->checksum, m);
    }
    return table;
}

} // namespace

// 0x004D2CB0
ListKit::KeyMap* __cdecl ReadMaterialsFromBuffer(uint8_t** cursor, const ListKit::KeyMap* textures)
{
    MemoryReader reader{*cursor};
    ListKit::KeyMap* table = ReadMaterials(reader, textures);
    *cursor = reader.cursor;
    return table;
}

// 0x004D3390
ListKit::KeyMap* __cdecl LoadMaterialsFromFile(FileLoad::Hdl file, const ListKit::KeyMap* textures)
{
    FileReader reader{file};
    return ReadMaterials(reader, textures);
}

} // namespace Nx
