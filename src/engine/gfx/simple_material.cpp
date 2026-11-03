// A one-pass material made on the fly, and the chunk skippers: see simple_material.h.

#include "engine/gfx/simple_material.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/texture_dict.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/shader_slots.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/engine_parts.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace render {

constexpr int kPassFlagLit = 0x40;
constexpr int kAlphaShift = 24;
constexpr float kPassGrey = 0.5F;
constexpr int kHalfByte = 128;
constexpr int kBlockHeaderBytes = 8;

// Checksums used in this file (the text each hashes).
constexpr uint32_t kADD = 0x02E58C18U;         // ADD
constexpr uint32_t kBlend = 0x54628ED7U;       // Blend
constexpr uint32_t kBrighten = 0x68E77F40U;    // Brighten
constexpr uint32_t kDIFFUSE = 0x515E298EU;     // DIFFUSE
constexpr uint32_t kFixadd = 0xA86285A1U;      // fixadd
constexpr uint32_t kFixblend = 0x18B98905U;    // fixblend
constexpr uint32_t kFixbrighten = 0xB8AA03C9U; // fixbrighten
constexpr uint32_t kFixmodulate = 0x90B93703U; // fixmodulate
constexpr uint32_t kFixsub = 0x0D7A749AU;      // fixsub
constexpr uint32_t kFixsubtract = 0x0EEA99FFU; // fixsubtract
constexpr uint32_t kModulate = 0x40F44B8AU;    // modulate
constexpr uint32_t kNONE = 0x806FFF30U;        // NONE
constexpr uint32_t kSub = 0xA7FD7D23U;         // Sub
constexpr uint32_t kSubtract = 0xDEA7E576U;    // subtract

namespace {

// The texture dictionary's lookup (not rewritten yet; thiscall): the texture entry for a checksum, or null.
const BitmapTexture* find_texture(uint32_t checksum) { return engine_parts().particle_texture_dict()->fetch_skin(checksum); }
// A material's constructor and destructor body (not rewritten yet; thiscall).
gsl::owner<MaterialRef*> construct_material(void* memory) { return new (memory) MaterialRef; }
// The C runtime's rand (the game's: its sequence).
int get_rand() { return crt::get_rand(); }

} // namespace

// 0x004D9CF0
uint32_t blend_mode_from_name(uint32_t checksum)
{
    switch (checksum) {
    case kDIFFUSE: // diffuse
    case kNONE: // none
        return 0;
    case kADD:
        return 1; // add
    case kFixadd:
        return 2;    // fixadd
    case kSubtract: // subtract
    case kSub: // sub
        return 3;
    case kFixsub: // fixsubtract
    case kFixsubtract: // fixsub
        return 4;
    case kBlend:
        return kBlendModeAlpha; // blend
    case kFixblend:
        return kBlendModeFixBlend; // fixblend
    case kModulate:
        return kBlendModeModulate; // modulate
    case kFixmodulate:
        return kBlendModeFixModulate; // fixmodulate
    case kBrighten:
        return kBlendModeBrighten; // brighten
    case kFixbrighten:
        return kBlendModeFixBrighten; // fixbrighten
    default:
        return 0;
    }
}

// 0x004D9B80
SimpleMaterial::SimpleMaterial(uint32_t /*unused0*/, uint32_t /*unused1*/, MaterialNames names, int fixed_alpha, uint32_t /*unused5*/, uint32_t /*unused6*/)
{
    const auto texture_checksum = names.texture;
    const auto blend_checksum = names.blend;
    // (the texture entry's +0x10: its PC texture, or with flag 1 a pointer to it)
    TextureImage* texture = nullptr;
    if (const BitmapTexture* entry = find_texture(texture_checksum)) {
        texture = image_of(entry);
    }
    void* memory = memory::alloc(sizeof(MaterialRef));
    MaterialRef* m = memory ? construct_material(memory) : nullptr; // (a failed allocation would crash below)
    Expects(m != nullptr);
    core::at(m->pass_flags(), 0) = (texture ? 4 : 0) | kPassFlagLit;
    const int first = get_rand();
    m->set_checksum(static_cast<uint32_t>(first * get_rand())); // the sort key
    m->set_pass_count(1);                                      // one pass
    core::at(m->textures(), 0) = texture;
    core::at(m->blend_modes(), 0) = blend_mode_from_name(blend_checksum) | static_cast<uint32_t>(fixed_alpha) << kAlphaShift;
    core::at(core::at(m->pass_color(), 0), 0) = kPassGrey; // the colour
    core::at(core::at(m->pass_color(), 0), 1) = kPassGrey;
    core::at(core::at(m->pass_color(), 0), 2) = kPassGrey;
    core::at(core::at(m->pass_color(), 0), 3) = static_cast<float>(static_cast<double>(fixed_alpha) * (1.0 / kHalfByte)); // 0x0064A2FC
    core::at(m->address_modes(), 0) = 0;
    core::at(m->mip_lod_bias(), 0) = 0;
    m->set_alpha_ref(1);
    m->set_two_sided(1);
    m->set_has_uv_wibble(false);
    material_ = m;
    const uint32_t mode = core::at(m->blend_modes(), 0) & 0xFFFFFF;
    shader_ = core::at(pixel_shaders(), mode == kBlendModeBrighten || mode == kBlendModeFixBrighten ? kPixelShader944 : kPixelShader964);
}

// 0x004D9CD0
SimpleMaterial::~SimpleMaterial()
{
    if (MaterialRef* m = material_) {
        m->~MaterialRef();
        memory::Hub::dealloc(m);
    }
}

// 0x004DA6D0
int skip_file_chunk(void* file)
{
    uint32_t id = 0;
    uint32_t size = 0;
    file::get_take_in(&id, 4, 1, file);
    file::get_take_in(&size, 4, 1, file);
    file::seek_to(file, static_cast<int>(size), SEEK_CUR);
    return 0;
}

// 0x004DA710
int skip_memory_chunk(uint8_t** data)
{
    uint32_t size = 0; // (the chunk's: its second word)
    std::memcpy(&size, std::next(*data, 4), sizeof(size));
    *data = std::next(*data, static_cast<std::ptrdiff_t>(size) + kBlockHeaderBytes);
    return 0;
}

} // namespace render
