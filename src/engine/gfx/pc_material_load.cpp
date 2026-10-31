// The PC materials' loaders (surface_material.h): a scene's material list - from memory or from a file, the same records -
// into a new hash table by checksum, their textures looked up in the scene's texture table.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"

namespace render {

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
    file::Hdl file;
    void TakeIn(void* out, int size, int count) { file::get_take_in(out, size, count, file); }
    void Skip(int bytes) { file::seek_to(file, bytes, 1); }
};

// A texture table's entry for the checksum (its texture at +0x10, or behind it with flag 1), or null.
uint8_t* find_texture(const lists::KeyMap* textures, uint32_t checksum)
{
    auto* entry = static_cast<uint8_t*>(textures->fetch_entry(checksum));
    if (!entry)
        return nullptr;
    auto* texture = *reinterpret_cast<uint8_t**>(entry + 0x10);
    if (entry[8] & 1)
        texture = *reinterpret_cast<uint8_t**>(texture);
    return texture;
}

template <typename T>
T get_pull(auto& reader, int size = sizeof(T))
{
    T value{};
    reader.TakeIn(&value, size, 1);
    return value;
}

// The table's size: 2^bits buckets, from 4 up to 4096, at least twice the count.
int table_bits(uint32_t count)
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
lists::KeyMap* read_materials(Reader& reader, const lists::KeyMap* textures)
{
    uint32_t count = get_pull<uint32_t>(reader);
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    auto* table = memory ? new (memory) lists::KeyMap(table_bits(count)) : nullptr;
    for (uint32_t index = 0; index < count; ++index) {
        void* at = memory::alloc(sizeof(MaterialRef));
        MaterialRef* m = at ? static_cast<MaterialRef*>(at)->make_new() : nullptr;
        reader.TakeIn(&m->checksum, 4, 1);
        reader.TakeIn(&m->name_hash_, 4, 1);
        reader.TakeIn(&m->pass_count_, 4, 1);
        m->alpha_ref_ = static_cast<uint8_t>(get_pull<uint32_t>(reader));
        reader.TakeIn(&m->sort_flag_, 1, 1);
        reader.TakeIn(&m->sort_key_, 4, 1);
        uint8_t keep_culling = get_pull<uint8_t>(reader); // (else a blended pass 0 turns culling off)
        reader.TakeIn(&m->two_sided_, 1, 1);
        int bias = get_pull<int>(reader);
        m->depth_bias_ = static_cast<uint8_t>(bias > 0x10 ? 0x10 : bias);
        if (get_pull<uint8_t>(reader)) {
            reader.TakeIn(&m->unknown14, 4, 1);
            reader.TakeIn(&m->unknown18, 4, 1);
        }
        reader.TakeIn(&m->specular_power_, 4, 1);
        if (m->specular_power_ > 0.0f)
            reader.TakeIn(m->specular, 12, 1);
        bool all_half = true;
        for (uint32_t pass = 0; pass < m->pass_count_; ++pass) {
            uint32_t texture_checksum = get_pull<uint32_t>(reader);
            reader.TakeIn(&m->pass_flags_[pass], 4, 1);
            get_pull<uint8_t>(reader);
            reader.TakeIn(m->pass_color_[pass], 12, 1);
            const uint32_t half = 0x3F000000;
            if (all_half && (std::memcmp(&m->pass_color_[pass][0], &half, 4) != 0 ||
                             std::memcmp(&m->pass_color_[pass][1], &half, 4) != 0 ||
                             std::memcmp(&m->pass_color_[pass][2], &half, 4) != 0))
                all_half = false;
            uint32_t blend[2];
            reader.TakeIn(blend, 8, 1);
            m->blend_modes_[pass] = (blend[0] & 0xFFFFFF) | blend[1] << 24;
            m->pass_color_[pass][3] =
                static_cast<float>(static_cast<double>(blend[1]) * (1.0f / 128)); // the fixed alpha
            if (pass == 0 && !keep_culling && (m->blend_modes_[0] & 0xFFFFFF))
                m->two_sided_ = 1;
            uint32_t u = get_pull<uint32_t>(reader);
            uint32_t v = get_pull<uint32_t>(reader);
            m->address_modes_[pass] = v << 16 | u;
            reader.TakeIn(m->reflection_tiling_[pass], 8, 1);
            reader.TakeIn(&m->unknownc0_[pass], 4, 1);
            if (m->pass_flags_[pass] & 1) {
                m->has_uv_wibble_ = true;
                void* w = memory::alloc(sizeof(UvOscillator));
                UvOscillator* wibble = w ? static_cast<UvOscillator*>(w)->make_new() : nullptr;
                m->uv_wibble_[pass] = wibble;
                reader.TakeIn(wibble, 0x20, 1);
            }
            if (pass == 0 && (m->pass_flags_[0] & 2)) {
                uint32_t anims = get_pull<uint32_t>(reader);
                m->color_anim_count_ = anims;
                m->color_anims_ = static_cast<ColorAnim*>(memory::alloc(anims * sizeof(ColorAnim)));
                m->anim_colors_ = static_cast<uint32_t*>(memory::alloc(anims * 4));
                for (uint32_t i = 0; i < anims; ++i) {
                    uint32_t keys = get_pull<uint32_t>(reader);
                    int phase = get_pull<int>(reader);
                    m->color_anims_[i].key_count_ = keys;
                    m->color_anims_[i].phase = phase;
                    m->color_anims_[i].keys = static_cast<ColorKey*>(memory::alloc(keys * sizeof(ColorKey)));
                    reader.TakeIn(m->color_anims_[i].keys, 8, static_cast<int>(keys));
                }
            }
            if (m->pass_flags_[pass] & 0x800) {
                if (!m->texture_anim_) {
                    m->texture_anim_ = static_cast<TextureAnim*>(memory::alloc(sizeof(TextureAnim)));
                    std::memset(m->texture_anim_, 0, sizeof(TextureAnim));
                    m->has_texture_anim_ = true;
                }
                int keys = get_pull<int>(reader);
                get_pull<uint32_t>(reader);
                int loops = get_pull<int>(reader);
                uint32_t phase = get_pull<uint32_t>(reader);
                m->texture_anim_->key_counts_[pass] = static_cast<uint32_t>(keys);
                m->texture_anim_->phases[pass] = phase;
                m->texture_anim_->loops[pass] = loops;
                m->texture_anim_->keys[pass] = static_cast<TextureKey*>(memory::alloc(static_cast<uint32_t>(keys) * 8));
                for (int k = 0; k < keys; ++k) {
                    TextureKey& key = m->texture_anim_->keys[pass][k];
                    reader.TakeIn(&key.time, 4, 1);
                    uint32_t checksum = get_pull<uint32_t>(reader);
                    if (k == 0)
                        texture_checksum = checksum; // (the pass's own texture: the first key's)
                    key.texture = find_texture(textures, checksum);
                }
            }
            if (!texture_checksum) {
                m->textures[pass] = nullptr;
                reader.Skip(0x10);
                continue;
            }
            m->textures[pass] = find_texture(textures, texture_checksum);
            get_pull<uint32_t>(reader);
            get_pull<uint32_t>(reader);
            float lod_bias = get_pull<float>(reader);
            get_pull<uint32_t>(reader);
            double lod = static_cast<double>(lod_bias) + 8.0f;
            float stored = static_cast<float>(lod);
            std::memcpy(&m->mip_lod_bias_[pass], &stored, 4);
            if (lod > 0.0)
                m->mip_lod_bias_[pass] = 0; // (no more than 0)
        }
        if (all_half)
            m->pass_flags_[0] |= 0x10000000; // (no pass changes its colour)
        if (m->specular_power_ > 0.0f)
            m->pass_flags_[0] |= 0x100;
        if (!table->fetch_entry(m->checksum))
            table->store_entry(m->checksum, m);
    }
    return table;
}

} // namespace

// 0x004D2CB0
lists::KeyMap* __cdecl read_materials_from_buffer(uint8_t** cursor, const lists::KeyMap* textures)
{
    MemoryReader reader{*cursor};
    lists::KeyMap* table = read_materials(reader, textures);
    *cursor = reader.cursor;
    return table;
}

// 0x004D3390
lists::KeyMap* __cdecl load_materials_from_file(file::Hdl file, const lists::KeyMap* textures)
{
    FileReader reader{file};
    return read_materials(reader, textures);
}

} // namespace render
