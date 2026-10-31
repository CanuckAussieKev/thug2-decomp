// A one-pass material made on the fly, and the chunk skippers: see simple_material.h.

#include "engine/gfx/simple_material.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/texture_dict.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/shader_slots.h"
#include "platform/crt.h"

namespace render {

namespace {

// The texture dictionary's lookup (not rewritten yet; thiscall): the texture entry for a checksum, or null.
uint8_t* find_texture(uint32_t checksum)
{
    return reinterpret_cast<uint8_t*>(static_cast<TextureSet*>(reinterpret_cast<void*>(g_particle_texture_dict))->fetch_skin(checksum));
}
// A material's constructor and destructor body (not rewritten yet; thiscall).
uint8_t* construct_material(void* memory) { return reinterpret_cast<uint8_t*>(static_cast<MaterialRef*>(memory)->make_new()); }
void destroy_material(void* material) { static_cast<MaterialRef*>(material)->destroy_body(); }
// The C runtime's rand (the game's: its sequence).
int get_rand() { return crt::get_rand(); }

template <typename T>
T& get_at(uint8_t* p, uint32_t offset)
{
    return *reinterpret_cast<T*>(p + offset);
}

} // namespace

// 0x004D9CF0
uint32_t __cdecl blend_mode_from_name(uint32_t checksum)
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
SimpleMaterial* SimpleMaterial::make_new(uint32_t /*unused0*/, uint32_t /*unused1*/, uint32_t texture_checksum,
                                            uint32_t blend_checksum, int fixed_alpha, uint32_t /*unused5*/,
                                            uint32_t /*unused6*/)
{
    uint32_t texture = 0;
    if (uint8_t* entry = find_texture(texture_checksum))
        texture = (entry[8] & 1) ? **reinterpret_cast<uint32_t**>(entry + 0x10) : get_at<uint32_t>(entry, 0x10);
    void* memory = memory::alloc(0x110);
    uint8_t* m = memory ? construct_material(memory) : nullptr; // (a failed allocation would crash below)
    get_at<uint32_t>(m, 0x20) = (texture ? 4 : 0) | 0x40;
    int first = get_rand();
    get_at<int>(m, 0) = first * get_rand(); // the sort key
    get_at<uint32_t>(m, 8) = 1;         // one pass
    get_at<uint32_t>(m, 0x30) = texture;
    get_at<uint32_t>(m, 0x80) = blend_mode_from_name(blend_checksum) | static_cast<uint32_t>(fixed_alpha) << 24;
    get_at<float>(m, 0x40) = 0.5f; // the colour
    get_at<float>(m, 0x44) = 0.5f;
    get_at<float>(m, 0x48) = 0.5f;
    get_at<float>(m, 0x4C) = static_cast<float>(static_cast<double>(fixed_alpha) * (1.0 / 128)); // 0x0064A2FC
    get_at<uint32_t>(m, 0x90) = 0;
    get_at<uint32_t>(m, 0xE0) = 0;
    get_at<uint8_t>(m, 0x10) = 1;
    get_at<uint8_t>(m, 0xD) = 1;
    get_at<uint8_t>(m, 0xE) = 0;
    material = m;
    uint32_t mode = get_at<uint32_t>(m, 0x80) & 0xFFFFFF;
    shader = g_pixel_shaders[mode == 9 || mode == 10 ? kPixelShader944 : kPixelShader964];
    return this;
}

// 0x004D9CD0
void SimpleMaterial::destroy_body()
{
    if (uint8_t* m = material) {
        destroy_material(m);
        memory::Hub::sole()->dealloc(m);
    }
}

// 0x004DA6D0
int __cdecl skip_file_chunk(void* file)
{
    uint32_t id, size;
    file::get_take_in(&id, 4, 1, file);
    file::get_take_in(&size, 4, 1, file);
    file::seek_to(file, static_cast<int>(size), SEEK_CUR);
    return 0;
}

// 0x004DA710
int __cdecl skip_memory_chunk(uint8_t** data)
{
    *data = *data + *reinterpret_cast<uint32_t*>(*data + 4) + 8;
    return 0;
}

} // namespace Nx
