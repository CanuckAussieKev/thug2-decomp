// The PC materials' loaders (surface_material.h): a scene's material list - from memory or from a file, the same records -
// into a new hash table by checksum, their textures looked up in the scene's texture table.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/scene_file_stream.h"
#include "formats/scene_format.h"
#include "engine/gfx/surface_material.h"
#include "engine/lists/key_map.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/render_state.h"
#include "engine/core/same_bits.h"
#include <array>
#include <gsl/assert>

namespace render {

constexpr int kMaxMaterialTableBits = 12;
constexpr int kMaxDepthBias = 0x10;
constexpr int kAlphaShift = 24;
constexpr int kAlphaScale = 128;
constexpr int kAddressVShift = 16;       // an address mode: U in the low half, V in the high
constexpr int kTextureKeyBytes = 8;
constexpr int kPassFlagNoColourChange = 0x10000000;
constexpr int kPassFlagSpecular = 0x100;

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
    while (count * 2 >= size && bits < kMaxMaterialTableBits) {
        size <<= 1;
        ++bits;
    }
    return bits;
}

// A material made from its record (the original's reads, in order, into the same fields).
MaterialRef* make_material(const scene_format::Material& record, const lists::KeyMap* textures)
{
    void* at = memory::alloc(sizeof(MaterialRef));
    MaterialRef* material = at ? memory::construct_in<MaterialRef>(at) : nullptr;
    Expects(material != nullptr);
    material->set_checksum(record.checksum);
    material->set_name_hash(record.name_hash);
    material->set_pass_count(record.pass_count);
    material->set_alpha_ref(static_cast<uint8_t>(record.alpha_ref));
    material->set_sort_flag(record.sort_flag);
    material->set_sort_key(record.sort_key);
    const bool keep_culling = record.keep_culling != 0; // (else a blended pass 0 turns culling off)
    material->set_two_sided(record.two_sided);
    material->set_depth_bias(
        static_cast<uint8_t>(record.depth_bias > kMaxDepthBias ? kMaxDepthBias : record.depth_bias));
    if (record.has_extra) {
        material->set_unknown14(core::at(record.extra, 0));
        material->set_unknown18(core::at(record.extra, 1));
    }
    material->set_specular_power(record.specular_power);
    if (material->specular_power() > 0.0F) {
        std::memcpy(material->specular().data(), record.specular.data(), sizeof(material->specular()));
    }
    bool all_half = true;
    for (uint32_t pass = 0; pass < material->pass_count(); ++pass) {
        const scene_format::MaterialPass& in = core::at(record.passes, pass);
        core::at(material->pass_flags(), pass) = in.flags;
        std::memcpy(core::at(material->pass_color(), pass).data(), in.color.data(), sizeof(in.color));
        const uint32_t half = 0x3F000000;
        const std::array<float, 4>& colour = core::at(material->pass_color(), pass);
        if (all_half && (!core::has_bits(core::at(colour, 0), half) || !core::has_bits(core::at(colour, 1), half) ||
                         !core::has_bits(core::at(colour, 2), half))) {
            all_half = false;
        }
        core::at(material->blend_modes(), pass) = (in.blend_mode & kBlendModeMask) | in.fixed_alpha << kAlphaShift;
        core::at(core::at(material->pass_color(), pass), 3) =
            static_cast<float>(static_cast<double>(in.fixed_alpha) * (1.0F / kAlphaScale)); // the fixed alpha
        if (pass == 0 && !keep_culling && ((core::at(material->blend_modes(), 0) & kBlendModeMask) != 0U)) {
            material->set_two_sided(1);
        }
        core::at(material->address_modes(), pass) = in.address_v << kAddressVShift | in.address_u;
        std::memcpy(core::at(material->reflection_tiling(), pass).data(), in.reflection_tiling.data(),
                    sizeof(in.reflection_tiling));
        core::at(material->unknownc0(), pass) = in.unknown_c0;
        if ((in.flags & scene_format::kPassUvWibble) != 0U) {
            material->set_has_uv_wibble(true);
            void* w = memory::alloc(sizeof(UvOscillator));
            UvOscillator* wibble = w ? memory::construct_in<UvOscillator>(w) : nullptr;
            core::at(material->uv_wibble(), pass) = wibble;
            std::memcpy(wibble, in.uv_wibble.data(), sizeof(in.uv_wibble));
        }
        if (pass == 0 && ((in.flags & scene_format::kPassColorAnims) != 0U)) {
            auto anims = static_cast<uint32_t>(record.color_anims.size());
            material->set_color_anim_count(anims);
            material->set_color_anims(static_cast<ColorAnim*>(memory::alloc(anims * sizeof(ColorAnim))));
            material->set_anim_colors(static_cast<uint32_t*>(memory::alloc(anims * 4)));
            for (uint32_t i = 0; i < anims; ++i) {
                const scene_format::ColorAnimTrack& track = core::at(record.color_anims, i);
                ColorAnim& anim = core::at(material->color_anim_run(), i);
                anim.key_count_ = track.key_count;
                anim.phase_ = track.phase;
                anim.keys = static_cast<ColorKey*>(memory::alloc(track.key_count * sizeof(ColorKey)));
                std::memcpy(anim.keys, track.keys.data(), track.keys.size());
            }
        }
        if ((in.flags & scene_format::kPassTextureAnim) != 0U) {
            if (!material->texture_anim()) {
                material->set_texture_anim(static_cast<TextureAnim*>(memory::alloc(sizeof(TextureAnim))));
                std::memset(material->texture_anim(), 0, sizeof(TextureAnim));
                material->set_has_texture_anim(true);
            }
            const int keys = in.texture_key_count;
            core::at(material->texture_anim()->key_counts_, pass) = static_cast<uint32_t>(keys);
            core::at(material->texture_anim()->phases, pass) = in.texture_phase;
            core::at(material->texture_anim()->loops, pass) = in.texture_loops;
            core::at(material->texture_anim()->keys, pass) =
                static_cast<TextureKey*>(memory::alloc(static_cast<uint32_t>(keys) * kTextureKeyBytes));
            for (int k = 0; k < keys; ++k) {
                const scene_format::TextureFrame& frame = core::at(in.texture_keys, static_cast<size_t>(k));
                TextureKey& key = core::at(key_run(*(material->texture_anim()), static_cast<size_t>(pass)), k);
                key.time = frame.time;
                key.texture = find_texture(textures, frame.texture);
            }
        }
        if (in.texture == 0U) { // (with a texture animation, its first key's)
            core::at(material->textures(), pass) = nullptr;
            continue;
        }
        core::at(material->textures(), pass) = find_texture(textures, in.texture);
        const double lod = static_cast<double>(in.mip_lod_bias) + 8.0F;
        auto stored = static_cast<float>(lod);
        std::memcpy(&core::at(material->mip_lod_bias(), pass), &stored, 4);
        if (lod > 0.0) {
            core::at(material->mip_lod_bias(), pass) = 0; // (no more than 0)
        }
    }
    if (all_half) {
        core::at(material->pass_flags(), 0) |= kPassFlagNoColourChange; // (no pass changes its colour)
    }
    if (material->specular_power() > 0.0F) {
        core::at(material->pass_flags(), 0) |= kPassFlagSpecular;
    }
    return material;
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
        MaterialRef* material = make_material(record, textures);
        if (!table->fetch_entry(material->checksum())) {
            table->store_entry(material->checksum(), material);
        }
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
