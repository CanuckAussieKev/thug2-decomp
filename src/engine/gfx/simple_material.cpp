// A one-pass material made on the fly, and the chunk skippers: see simple_material.h.

#include "engine/gfx/simple_material.h"
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

namespace render {

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
    case 0x515E298E: // diffuse
    case 0x806FFF30: // none
        return 0;
    case 0x02E58C18:
        return 1; // add
    case 0xA86285A1:
        return 2;    // fixadd
    case 0xDEA7E576: // subtract
    case 0xA7FD7D23: // sub
        return 3;
    case 0x0D7A749A: // fixsubtract
    case 0x0EEA99FF: // fixsub
        return 4;
    case 0x54628ED7:
        return 5; // blend
    case 0x18B98905:
        return 6; // fixblend
    case 0x40F44B8A:
        return 7; // modulate
    case 0x90B93703:
        return 8; // fixmodulate
    case 0x68E77F40:
        return 9; // brighten
    case 0xB8AA03C9:
        return 10; // fixbrighten
    default:
        return 0;
    }
}

// 0x004D9B80
SimpleMaterial::SimpleMaterial(uint32_t /*unused0*/, uint32_t /*unused1*/, uint32_t texture_checksum,
                               uint32_t blend_checksum, int fixed_alpha, uint32_t /*unused5*/, uint32_t /*unused6*/)
{
    // (the texture entry's +0x10: its PC texture, or with flag 1 a pointer to it)
    TextureImage* texture = nullptr;
    if (const BitmapTexture* entry = find_texture(texture_checksum))
        texture = image_of(entry);
    void* memory = memory::alloc(sizeof(MaterialRef));
    MaterialRef* m = memory ? construct_material(memory) : nullptr; // (a failed allocation would crash below)
    core::at(m->pass_flags(), 0) = (texture ? 4 : 0) | 0x40;
    const int first = get_rand();
    m->set_checksum(static_cast<uint32_t>(first * get_rand())); // the sort key
    m->set_pass_count(1);                                      // one pass
    core::at(m->textures_value(), 0) = texture;
    core::at(m->blend_modes(), 0) = blend_mode_from_name(blend_checksum) | static_cast<uint32_t>(fixed_alpha) << 24;
    core::at(core::at(m->pass_color(), 0), 0) = 0.5f; // the colour
    core::at(core::at(m->pass_color(), 0), 1) = 0.5f;
    core::at(core::at(m->pass_color(), 0), 2) = 0.5f;
    core::at(core::at(m->pass_color(), 0), 3) = static_cast<float>(static_cast<double>(fixed_alpha) * (1.0 / 128)); // 0x0064A2FC
    core::at(m->address_modes(), 0) = 0;
    core::at(m->mip_lod_bias(), 0) = 0;
    m->set_alpha_ref(1);
    m->set_two_sided(1);
    m->set_has_uv_wibble(false);
    material = m;
    const uint32_t mode = core::at(m->blend_modes(), 0) & 0xFFFFFF;
    shader = core::at(pixel_shaders(), mode == 9 || mode == 10 ? kPixelShader944 : kPixelShader964);
}

// 0x004D9CD0
SimpleMaterial::~SimpleMaterial()
{
    if (MaterialRef* m = material) {
        m->~MaterialRef();
        memory::Hub::sole()->dealloc(m);
    }
}

// 0x004DA6D0
int skip_file_chunk(void* file)
{
    uint32_t id = 0, size = 0;
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
    *data = std::next(*data, static_cast<std::ptrdiff_t>(size) + 8);
    return 0;
}

} // namespace render
