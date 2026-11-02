// The PC materials' loaders (surface_material.h): a scene's material list - from memory or from a file, the same records -
// into a new hash table by checksum, their textures looked up in the scene's texture table.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/scene_file_stream.h"
#include "formats/scene_format.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"

namespace render {

namespace {

// A texture table's texture's image for the checksum, or null.
TextureImage* find_texture(const lists::KeyMap* textures, uint32_t checksum)
{
    const auto* entry = static_cast<const BitmapTexture*>(textures->fetch_entry(checksum));
    return entry ? image_of(entry) : nullptr;
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

// A material made from its record (the original's reads, in order, into the same fields).
MaterialRef* make_material(const scene_format::Material& record, const lists::KeyMap* textures)
{
    void* at = memory::alloc(sizeof(MaterialRef));
    MaterialRef* m = at ? memory::construct_in<MaterialRef>(at) : nullptr;
    m->checksum = record.checksum;
    m->name_hash_ = record.name_hash;
    m->pass_count_ = record.pass_count;
    m->alpha_ref_ = static_cast<uint8_t>(record.alpha_ref);
    m->sort_flag_ = record.sort_flag;
    m->sort_key_ = record.sort_key;
    const bool keep_culling = record.keep_culling != 0; // (else a blended pass 0 turns culling off)
    m->two_sided_ = record.two_sided;
    m->depth_bias_ = static_cast<uint8_t>(record.depth_bias > 0x10 ? 0x10 : record.depth_bias);
    if (record.has_extra) {
        m->unknown14 = core::at(record.extra, 0);
        m->unknown18 = core::at(record.extra, 1);
    }
    m->specular_power_ = record.specular_power;
    if (m->specular_power_ > 0.0f)
        std::memcpy(m->specular.data(), record.specular.data(), sizeof(m->specular));
    bool all_half = true;
    for (uint32_t pass = 0; pass < m->pass_count_; ++pass) {
        const scene_format::MaterialPass& in = core::at(record.passes, pass);
        core::at(m->pass_flags_, pass) = in.flags;
        std::memcpy(core::at(m->pass_color_, pass).data(), in.color.data(), sizeof(in.color));
        const uint32_t half = 0x3F000000;
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        if (all_half && (std::memcmp(&core::at(core::at(m->pass_color_, pass), 0), &half, 4) != 0 ||
                         // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
                         std::memcmp(&core::at(core::at(m->pass_color_, pass), 1), &half, 4) != 0 ||
                         // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
                         std::memcmp(&core::at(core::at(m->pass_color_, pass), 2), &half, 4) != 0))
            all_half = false;
        core::at(m->blend_modes_, pass) = (in.blend_mode & 0xFFFFFF) | in.fixed_alpha << 24;
        core::at(core::at(m->pass_color_, pass), 3) =
            static_cast<float>(static_cast<double>(in.fixed_alpha) * (1.0f / 128)); // the fixed alpha
        if (pass == 0 && !keep_culling && (core::at(m->blend_modes_, 0) & 0xFFFFFF))
            m->two_sided_ = 1;
        core::at(m->address_modes_, pass) = in.address_v << 16 | in.address_u;
        std::memcpy(core::at(m->reflection_tiling_, pass).data(), in.reflection_tiling.data(), sizeof(in.reflection_tiling));
        core::at(m->unknownc0_, pass) = in.unknown_c0;
        if (in.flags & scene_format::kPassUvWibble) {
            m->has_uv_wibble_ = true;
            void* w = memory::alloc(sizeof(UvOscillator));
            UvOscillator* wibble = w ? memory::construct_in<UvOscillator>(w) : nullptr;
            core::at(m->uv_wibble_, pass) = wibble;
            std::memcpy(wibble, in.uv_wibble.data(), sizeof(in.uv_wibble));
        }
        if (pass == 0 && (in.flags & scene_format::kPassColorAnims)) {
            auto anims = static_cast<uint32_t>(record.color_anims.size());
            m->color_anim_count_ = anims;
            m->color_anims_ = static_cast<ColorAnim*>(memory::alloc(anims * sizeof(ColorAnim)));
            m->anim_colors_ = static_cast<uint32_t*>(memory::alloc(anims * 4));
            for (uint32_t i = 0; i < anims; ++i) {
                const scene_format::ColorAnimTrack& track = core::at(record.color_anims, i);
                ColorAnim& anim = core::at(m->color_anim_run(), i);
                anim.key_count_ = track.key_count;
                anim.phase = track.phase;
                anim.keys = static_cast<ColorKey*>(memory::alloc(track.key_count * sizeof(ColorKey)));
                std::memcpy(anim.keys, track.keys.data(), track.keys.size());
            }
        }
        if (in.flags & scene_format::kPassTextureAnim) {
            if (!m->texture_anim_) {
                m->texture_anim_ = static_cast<TextureAnim*>(memory::alloc(sizeof(TextureAnim)));
                std::memset(m->texture_anim_, 0, sizeof(TextureAnim));
                m->has_texture_anim_ = true;
            }
            const int keys = in.texture_key_count;
            core::at(m->texture_anim_->key_counts_, pass) = static_cast<uint32_t>(keys);
            core::at(m->texture_anim_->phases, pass) = in.texture_phase;
            core::at(m->texture_anim_->loops, pass) = in.texture_loops;
            core::at(m->texture_anim_->keys, pass) = static_cast<TextureKey*>(memory::alloc(static_cast<uint32_t>(keys) * 8));
            for (int k = 0; k < keys; ++k) {
                const scene_format::TextureFrame& frame = core::at(in.texture_keys, static_cast<size_t>(k));
                TextureKey& key = core::at(m->texture_anim_->key_run(static_cast<size_t>(pass)), k);
                key.time = frame.time;
                key.texture = find_texture(textures, frame.texture);
            }
        }
        if (!in.texture) { // (with a texture animation, its first key's)
            core::at(m->textures, pass) = nullptr;
            continue;
        }
        core::at(m->textures, pass) = find_texture(textures, in.texture);
        const double lod = static_cast<double>(in.mip_lod_bias) + 8.0f;
        auto stored = static_cast<float>(lod);
        std::memcpy(&core::at(m->mip_lod_bias_, pass), &stored, 4);
        if (lod > 0.0)
            core::at(m->mip_lod_bias_, pass) = 0; // (no more than 0)
    }
    if (all_half)
        core::at(m->pass_flags_, 0) |= 0x10000000; // (no pass changes its colour)
    if (m->specular_power_ > 0.0f)
        core::at(m->pass_flags_, 0) |= 0x100;
    return m;
}

template <typename Stream>
lists::KeyMap* read_materials(Stream& in, const lists::KeyMap* textures)
{
    auto count = scene_format::take<uint32_t>(in);
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    auto* table = memory ? memory::construct_in<lists::KeyMap>(memory, table_bits(count)) : nullptr;
    scene_format::Material record;
    for (uint32_t index = 0; index < count; ++index) {
        // Not original - each record read whole and checked first (scene_format.h): one cut short, with more than
        // the 4 passes the material holds (the original wrote further passes past its arrays) or with animations past
        // the end ends the list, the stream failed.
        if (!scene_format::read_material(in, &record)) {
            debug::printf("scene materials damaged: material %u of %u (%08x) cut short or doesn't fit\n", index, count,
                          record.checksum);
            in.fail();
            break;
        }
        MaterialRef* m = make_material(record, textures);
        if (!table->fetch_entry(m->checksum))
            table->store_entry(m->checksum, m);
    }
    return table;
}

} // namespace

// 0x004D2CB0
lists::KeyMap* read_materials_from_buffer(scene_format::MemoryStream& in, const lists::KeyMap* textures)
{
    return read_materials(in, textures);
}

// 0x004D3390
lists::KeyMap* load_materials_from_file(scene_format::FileStream& in, const lists::KeyMap* textures)
{
    return read_materials(in, textures);
}

} // namespace render
