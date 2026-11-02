#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/mem.h"

namespace lists {
class KeyMap;
}

// The engine's texture dictionary (render::TextureSet; the PC's PcTexDict - pc_texture.h - extends it): a hash table of
// textures by checksum, loaded, made, copied, replaced and flushed through the platform's slots; and the loaded
// dictionaries themselves, shared by name and counted (the table at 0x00700F04).
namespace render {

class BitmapTexture;

// The dictionary (0x10 bytes of the engine's; vtable 0x00649BB4 - its own slots are stubs; PC 0x0064A824). Slots 4, 6
// and 7 are the same stub in both tables and never called: left out.
class TextureSet : public memory::HeapObject {
public:
    // 0x0049F830: named `checksum`, used once, with an empty table of 32 buckets when `make_table` (none without
    // memory) or none.
    TextureSet(uint32_t checksum, bool make_table);
    // 0x0049F8F0: the same, named by the CRC of `name`.
    TextureSet(const char* name, bool make_table);
    // 0x0049FC70 (0x0049FCC0 the scalar deleting destructor, vtable slot 0): every texture flushed, the table emptied
    // and freed (its buckets too).
    virtual ~TextureSet();
    TextureSet(const TextureSet&) = delete;
    TextureSet& operator=(const TextureSet&) = delete;

    // The platform's (the engine's: none / false): vtable slot 1, a texture loaded from a file; slot 2, one loaded
    // from memory; slot 3, one made; slot 5, a texture destroyed (true if it was); slot 8, a copy of a texture.
    virtual BitmapTexture* load_texture_file(const char* /*name*/, uint32_t /*a1*/, uint32_t /*a2*/) { return nullptr; }
    virtual BitmapTexture* load_texture_data(const uint8_t* /*data*/, uint32_t /*size*/, uint32_t /*checksum*/,
                                             uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a6*/)
    {
        return nullptr;
    }
    virtual BitmapTexture* make_texture(uint32_t /*checksum*/, uint32_t /*width*/, uint32_t /*height*/,
                                        uint32_t /*bits_per_pixel*/, uint32_t /*a4*/, uint32_t /*a5*/)
    {
        return nullptr;
    }
    virtual bool destroy_texture(BitmapTexture* /*texture*/) { return false; }
    virtual BitmapTexture* duplicate_texture(uint32_t /*checksum*/, BitmapTexture* /*source*/) { return nullptr; }

    // 0x0049F540: the texture `checksum`, or null (no table: null).
    BitmapTexture* fetch_skin(uint32_t checksum);
    // 0x0049F560: the texture named by the CRC of `name` after its last slash or backslash.
    BitmapTexture* get_texture_by_name(const char* name);
    // 0x0049F660: the texture `name` under any of the keys its CRC times 2, 3, 4, 6, 12, 8 and 24 (the list at
    // 0x0067E520, up to its -1) - else the same for the name with its last four characters replaced by
    // "_auto32m0.png" (when it's longer than four). Null if none.
    BitmapTexture* find_texture(const char* name);
    // 0x0049F9B0: a texture loaded from a file (slot 1), kept on flushes with `permanent`, added under its checksum.
    // Returns it (null if the platform made none).
    BitmapTexture* ingest_skin(const char* name, uint32_t a1, uint32_t a2, bool permanent);
    // 0x0049FA20: a texture loaded from memory (slot 2 with all the arguments but `permanent`), kept and added as
    // ingest_skin's.
    BitmapTexture* read_texture_buffer(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3, uint32_t a4,
                                       bool permanent, uint32_t a6);
    // 0x0049FAA0: a texture made (slot 3 with the first six arguments), kept and added as ingest_skin's.
    BitmapTexture* create_texture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel,
                                  uint32_t a4, uint32_t a5, bool permanent);
    // 0x0049F500: a texture destroyed by the platform (slot 5) and, if it was, taken out of the table. Whether it was.
    bool unload_texture(BitmapTexture* texture);
    // 0x0049F5C0: every texture destroyed and taken out - but the permanent ones unless `all`.
    void drop_textures(bool all);
    // 0x0049F800: the texture find_texture finds made a copy of `source` (its slot 4). False if there's none.
    bool swap_texture(const char* name, BitmapTexture* source);
    // 0x0049FB20: the same from a file - loaded (permanent) on the top-down heap, copied, then unloaded again. False
    // if there's no such texture to replace.
    bool replace_texture_from_file(const char* name, const char* file);
    // 0x0049FBE0: a copy of `source` (slot 8) named `checksum` and added. Returns it.
    BitmapTexture* copy_texture(uint32_t checksum, BitmapTexture* source);
    // 0x0049FC20: the same, then `overlay` drawn over it (the copy's slot 41 with `argument`).
    BitmapTexture* copy_texture_with_overlay(uint32_t checksum, BitmapTexture* source, BitmapTexture* overlay,
                                             uint32_t argument);

    uint32_t checksum_;    // +0x04
    uint16_t share_count_; // +0x08: its loads (unload_tex_dict deletes it at 0)
    std::array<uint8_t, 2> pada_{};
    lists::KeyMap* textures_; // +0x0C
};
static_assert(offsetof(TextureSet, textures_) == 0xC && sizeof(TextureSet) == 0x10, "layout must match the original");

// The loaded dictionaries (the hash table 0x00700F04), by checksum.
// 0x0049F010: the dictionary `checksum`, or null.
TextureSet* find_tex_dict(uint32_t checksum);
// 0x0049F0C0: a dictionary loaded from "<path>.<platform extension>" (0x004C7270 with the other arguments) - or the one
// loaded already, counted once more - under the CRC of the path plus `offset`.
TextureSet* load_tex_dict(const char* path, int level, uint32_t offset, uint32_t a3, uint32_t a4);
// 0x0049F050: one loaded from memory (0x004C72E0) under `checksum + offset` - or the one loaded already (not counted
// again).
TextureSet* load_tex_dict_from_memory(uint32_t checksum, const uint8_t* data, uint32_t size, uint32_t a3,
                                      uint32_t offset, uint32_t a5, uint32_t a6);
// 0x0049F180: a model's textures - "<base>.tex" of its name (0x0048BFE0 splits it: whether it's a skin) loaded as
// load_tex_dict's (level 0; the skin flag as the third argument).
TextureSet* load_model_textures(const char* name, uint32_t offset, uint32_t a4);
// 0x0049EFD0: a dictionary used once less - taken out and deleted (0x004B6920) at none. Whether it was.
bool unload_tex_dict(TextureSet* dict);
// 0x0049EF90: "<name>.<platform extension>" into `out`.
void tex_dict_file_name(char* out, const char* name);
// 0x0049EFB0: the particle texture dictionary's (0x00700F20) textures flushed (drop_textures).
void drop_particle_textures(bool all);

TextureSet* game_texture_dict();
TextureSet* particle_texture_dict();
} // namespace render
