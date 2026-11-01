// A mesh's buffers by index (the buffers themselves: gpu/vertex_buffers.cpp), its move into a vertex arena, its
// coloured pixel shader; the skinned meshes' scaling while they load; the pixel shaders by a file's index.

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

uint8_t g_skin_scaling_on;
std::array<uint32_t, 5> g_skin_settings;
namespace {
std::array<uint8_t, 64> g_saved_pixel_shader_constants; // (4 passes of 16 bytes: the adapter identifier follows)
} // namespace

// 0x004D3FC0
VertexBuffer* MeshRef::fetch_vertex_store(int index) { return core::at(vertex_buffers_, index); }

// 0x004D3FD0
void MeshRef::set_vertex_buffer(int index, VertexBuffer* vb) { core::at(vertex_buffers_, index) = vb; }

// 0x004D3E50
void MeshRef::move_into_arena(VertexArena* arena)
{
    VertexBuffer* vb = core::at(vertex_buffers_, 0);
    if (vb->buffer)
        return; // (the original adds 0x1F & ~0x1F - nothing - to the arena's offset)
    vb->buffer = arena->buffer;
    vb->arena_offset_ = arena->used;
    std::memcpy(static_cast<uint8_t*>(arena->data) + arena->used, vb->data, vb->size);
    static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->AddRef();
    arena->used += (vb->size + 0x1F) & ~0x1Fu;
}

// 0x004D3D10
void MeshRef::set_pixel_shader_colored()
{
    const uint32_t bytes = material->pass_count_ << 4; // 16 a pass
    auto* constants = g_ps_constant_bank.data();
    auto* saved = g_saved_pixel_shader_constants.data();
    std::memcpy(saved, constants, bytes);
    double r = 0.0, g = 0.0, b = 0.0;
    if (g_instance_colour_on) {
        r = core::at(g_instance_colour, 0) * static_cast<double>(1.0f / 128);
        g = core::at(g_instance_colour, 1) * static_cast<double>(1.0f / 128);
        b = core::at(g_instance_colour, 2) * static_cast<double>(1.0f / 128);
    } else {
        r = static_cast<double>(core::at(color, 0)) + core::at(color, 0);
        g = static_cast<double>(core::at(color, 1)) + core::at(color, 1);
        b = static_cast<double>(core::at(color, 2)) + core::at(color, 2);
    }
    for (uint32_t pass = 0; pass < material->pass_count_; ++pass) {
        if (core::at(material->pass_flags_, pass) & 0x80) // (the colour isn't to be changed)
            continue;
        float* c = constants + 4 * pass;
        c[0] = static_cast<float>(r * c[0]);
        c[1] = static_cast<float>(g * c[1]);
        c[2] = static_cast<float>(b * c[2]);
    }
    g_pixel_shader_changed = 1; // (the constants changed)
    set_pixel_shader_with_constants(pixel_shader_, material->pass_count_);
    std::memcpy(constants, saved, bytes);
    g_pixel_shader_changed = 1;
}

// 0x004D3CA0
void set_skin_scaling(const uint32_t* settings)
{
    g_skin_scaling_on = 1;
    core::at(g_skin_settings, 1) = settings[1];
    core::at(g_skin_settings, 0) = settings[0];
    core::at(g_skin_settings, 3) = settings[3];
    core::at(g_skin_settings, 2) = settings[2];
    core::at(g_skin_settings, 4) = 0;
}

// 0x004D3CE0
void clear_skin_scaling()
{
    g_skin_scaling_on = 0;
    core::at(g_skin_settings, 1) = 0;
    core::at(g_skin_settings, 0) = 0;
    core::at(g_skin_settings, 3) = 0;
    core::at(g_skin_settings, 2) = 0;
    core::at(g_skin_settings, 4) = 0;
}

// 0x004D3ED0
void* pixel_shader_from_index(uint32_t index)
{
    // (kPixelShader948 is skipped: no index names it)
    static const std::array<int, 0x13> kShaders = {{-1,
                                       -1,
                                       kPixelShader964,
                                       kPixelShader960,
                                       kPixelShader95c,
                                       kPixelShader958,
                                       kPixelShader954,
                                       kPixelShader950,
                                       kPixelShader94c,
                                       kPixelShader944,
                                       kPixelShader940,
                                       kPixelShader93c,
                                       kPixelShader938,
                                       kPixelShader934,
                                       kPixelShader930,
                                       kPixelShader92c,
                                       kPixelShader928,
                                       kPixelShader924,
                                       kPixelShader920}};
    if (index > 0x12)
        return cell::pointer<void>(index); // (the original returns the index itself)
    return core::at(kShaders, index) >= 0 ? core::at(g_pixel_shaders, core::at(kShaders, index)) : nullptr;
}

} // namespace render
