// A mesh's buffers by index (the buffers themselves: render_d3d9/vertex_buffers.cpp), its move into a vertex arena, its
// coloured pixel shader; the skinned meshes' scaling while they load; the pixel shaders by a file's index.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <array>

#include "render_d3d9/d3d9_handles.h"
#include "render_d3d9/sub_mesh.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/render_device.h"
#include "render_d3d9/render_state.h"
#include "render_d3d9/shader_slots.h"
#include "render_d3d9/vertex_buffers.h"
#include "base/core/checked_index.h"
#include "engine/gfx/renderer.h"
#include "render_d3d9/mesh_pass.h"

namespace render {

constexpr std::size_t kPixelShaderConstantFloats = 16;
constexpr int kBufferAlignSlack = 0x1F; // vertex buffers are laid out on 32 bytes
constexpr unsigned kBufferAlignMask = 0x1FU;
constexpr int kColourScale = 128;        // a colour byte of 128 is 1.0
constexpr int kPassFlagKeepColour = 0x80;
constexpr int kLastPixelShaderIndex = 0x12;

namespace {
std::array<float, kPixelShaderConstantFloats>& saved_pixel_shader_constants()
{
    static std::array<float, kPixelShaderConstantFloats> saved_pixel_shader_constants{}; // (4 passes of 16 bytes: the adapter identifier follows)
    return saved_pixel_shader_constants;
}
} // namespace

// 0x004D3FC0
VertexBuffer* MeshRef::fetch_vertex_store(int index)
{
    return static_cast<VertexBuffer*>(core::at(vertex_buffers_, index));
}

// 0x004D3FD0
void MeshRef::set_vertex_buffer(int index, VertexBuffer* vb) { core::at(vertex_buffers_, index) = vb; }

// 0x004D3E50
void MeshRef::move_into_arena(VertexArena* arena)
{
    auto* vb = static_cast<VertexBuffer*>(core::at(vertex_buffers_, 0));
    if (vb->buffer()) {
        return; // (the original adds 0x1F & ~0x1F - nothing - to the arena's offset)
    }
    vb->set_buffer(arena->buffer);
    vb->set_arena_offset(arena->used);
    const std::span<uint8_t> locked(static_cast<uint8_t*>(arena->data), arena->size); // (the buffer, locked)
    std::memcpy(locked.subspan(arena->used, vb->size()).data(), vb->data(), vb->size());
    d3d9::object(arena->buffer)->AddRef();
    arena->used += (vb->size() + kBufferAlignSlack) & ~kBufferAlignMask;
}

// 0x004D3D10
void MeshRef::set_pixel_shader_colored()
{
    const uint32_t bytes = material_->pass_count() << 4; // 16 a pass
    auto& saved = saved_pixel_shader_constants();
    std::memcpy(saved.data(), renderer().ps_constants().data(), bytes);
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    if (renderer().instance_colour_on() != 0U) {
        red = core::at(renderer().instance_colour(), 0) * static_cast<double>(1.0F / kColourScale);
        green = core::at(renderer().instance_colour(), 1) * static_cast<double>(1.0F / kColourScale);
        blue = core::at(renderer().instance_colour(), 2) * static_cast<double>(1.0F / kColourScale);
    } else {
        red = static_cast<double>(core::at(color_, 0)) + core::at(color_, 0);
        green = static_cast<double>(core::at(color_, 1)) + core::at(color_, 1);
        blue = static_cast<double>(core::at(color_, 2)) + core::at(color_, 2);
    }
    for (uint32_t pass = 0; pass < material_->pass_count(); ++pass) {
        if ((core::at(material_->pass_flags(), pass) & kPassFlagKeepColour) != 0U) { // (the colour isn't to be changed)
            continue;
        }
        renderer().scale_pass_colour(pass, {.r = red, .g = green, .b = blue});
    }
    renderer().set_pixel_shader_changed(1); // (the constants changed)
    set_pixel_shader_with_constants(pixel_shader_, material_->pass_count());
    renderer().load_ps_constants(std::span<const float>(saved).first(bytes / 4));
    renderer().set_pixel_shader_changed(1);
}

// 0x004D3CA0
void set_skin_scaling(const SkinScaling* settings)
{
    mesh_pass().set_skin_scaling_on(1);
    mesh_pass().skin_settings() = *settings;
}

// 0x004D3CE0
void clear_skin_scaling()
{
    mesh_pass().set_skin_scaling_on(0);
    mesh_pass().skin_settings() = {};
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
    if (index > kLastPixelShaderIndex) {
        return nullptr; // (NOT ORIGINAL: none - the original returned the index itself as the shader)
    }
    return core::at(kShaders, index) >= 0 ? core::at(pixel_shaders(), core::at(kShaders, index)) : nullptr;
}

} // namespace render
