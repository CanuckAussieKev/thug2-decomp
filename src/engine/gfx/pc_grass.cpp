// The PC grass (a sector's grass meshes, made when the sector loads): layered grass - copies of the mesh lifted a step
// at a time, each with its "Grass-Grass_Layer<n>" material - and animated grass, its own mesh with a second texture
// coordinate set from the positions and a material that cycles through 18 frames.

#include "engine/gfx/pc_grass.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pixel_shaders.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/maths/vec4.h"
#include "engine/sys/startup_init.h"
#include "engine/gfx/engine_scene.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include "gpu/vertex_buffers.h"
#include <bit>
#include "gpu/vertex_view.h"
#include "engine/gfx/pc_scene.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace render {

namespace {
constexpr float kGrassDetailUvScale{0.010416667f};
constexpr float kGrassUvScale{0.0052083335f};
constexpr float kGrassCopyUvScale{0.020833334f};
// NOT ORIGINAL grouping - pc_grass.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_grass_state()).
struct PcGrassState {
    TextureImage grass_texture0_{}; // the grass materials' two textures (listed among the textures; see below)
    TextureImage grass_texture1_{};
    std::array<TextureImage, 17> grass_texture_animation_{}; // (17 textures)
};

PcGrassState& pc_grass_state()
{
    static core::NoDestroy<PcGrassState> state;
    return state.get();
}

constexpr uint32_t kAnimatedGrass = 0x8000000; // pass 0's flag: animated rather than layered
constexpr int kMaxLayers = 5;

BitmapTexture* look_up_texture(TextureSet* dictionary, uint32_t checksum) { return dictionary->fetch_skin(checksum); }

} // namespace

// 0x004D08A0
void set_up_animated_grass_material(MaterialRef* m)
{
    core::at(m->textures_value(), 0) = &pc_grass_state().grass_texture0_;
    core::at(m->textures_value(), 1) = &pc_grass_state().grass_texture1_;
    core::at(m->blend_modes(), 0) = 0x40000006;
    core::at(core::at(m->pass_color(), 0), 3) = 0.5f;
    m->set_pass_count(2);
    core::at(m->pass_flags(), 1) = 0x400;
    m->set_has_uv_wibble(false);
    core::at(m->mip_lod_bias(), 0) = 0;
    core::at(m->mip_lod_bias(), 1) = 0;
    core::at(m->address_modes(), 0) = 0;
    core::at(m->address_modes(), 1) = 0;
    m->set_sort_flag(0);
    m->set_sort_key(1501.0f);
    m->set_has_texture_anim(true);
    core::at(m->pass_flags(), 0) = (core::at(m->pass_flags(), 0) & ~9u) | 0xA40;
    auto* anim = static_cast<TextureAnim*>(memory::alloc(sizeof(TextureAnim)));
    m->set_texture_anim(anim);
    core::at(anim->key_counts_, 0) = 18;
    core::at(m->texture_anim()->phases, 0) = 0;
    core::at(m->texture_anim()->loops, 0) = 0;
    core::at(m->texture_anim()->keys, 0) = static_cast<TextureKey*>(memory::alloc(18 * sizeof(TextureKey)));
    core::at(m->texture_anim()->keys, 1) = nullptr;
    core::at(m->texture_anim()->keys, 2) = nullptr;
    core::at(m->texture_anim()->keys, 3) = nullptr;
    const std::span<TextureKey> keys(core::at(m->texture_anim()->keys, 0), 18);
    for (int k = 0; k < 18; ++k) { // (a frame every 1000 / 30 ms, through 17 textures)
        core::at(keys, k).time = k * 1000 / 30;
        core::at(keys, k).texture = &core::at(pc_grass_state().grass_texture_animation_, k % 17);
    }
}

// 0x004D0B60
bool make_animated_grass(PcGeom* /*geom*/, MeshRef* mesh)
{
    TextureSet* dictionary = get_sky_scene()->texture_set(); // the sky's
    const BitmapTexture* entry = look_up_texture(dictionary, 0x96D0BF08);
    if (!entry)
        return false;
    pc_grass_state().grass_texture1_ = *image_of(entry); // (a copy of the record, as the original)
    if ((mesh->stride_value() - mesh->uv_offset()) / 8 == 1) { // (one texture coordinate set: room made for a second)
        gsl::owner<VertexBuffer*> wider = new_vertex_buffer((mesh->stride_value() + 8u) * mesh->vertex_count(), 0, 0);
        uint8_t* from = nullptr;
        uint8_t* to = nullptr;
        mesh->fetch_vertex_store(0)->get_lock(0, 0, &from, D3DLOCK_READONLY);
        wider->get_lock(0, 0, &to, 0);
        const std::span<const uint8_t> source_vertices(from, size_t{mesh->vertex_count()} * mesh->stride_value());
        const std::span<uint8_t> wide_vertices(to, size_t{mesh->vertex_count()} * (mesh->stride_value() + 8));
        for (size_t i = 0; i < mesh->vertex_count(); ++i)
            std::memcpy(wide_vertices.subspan(i * (mesh->stride_value() + 8)).data(), source_vertices.subspan(i * mesh->stride_value()).data(), mesh->stride_value());
        mesh->fetch_vertex_store(0)->teardown();
        mesh->set_buffer_usage(0);
        mesh->set_vertex_buffer(0, wider);
        mesh->set_stride(static_cast<uint8_t>(mesh->stride_value() + 8));
        mesh->set_fvf((mesh->fvf_value() & ~0x100u) | 0x200); // (D3DFVF_TEX1 -> TEX2)
    }
    set_up_animated_grass_material(mesh->material_value());
    mesh->flags_value() |= 0x402;
    mesh->pixel_shader() = core::at(pixel_shaders(), kPixelShader924);
    uint8_t* vertices = nullptr;
    mesh->fetch_vertex_store(0)->get_lock(0, 0, &vertices, 0); // (not unlocked: the copy is what's changed)
    const gpu::VertexWriter grass(vertices, mesh->stride_value(), mesh->vertex_count());
    for (uint32_t i = 0; i < mesh->vertex_count(); ++i) {
        const auto v = grass.view().point<maths::Vec4>(i);
        const float x = v.x;
        const float z = v.z;
        grass.set_floats<4>(i, mesh->uv_offset(),
                            {static_cast<float>(static_cast<double>(x) * kGrassUvScale),
                             static_cast<float>(static_cast<double>(z) * kGrassUvScale),
                             static_cast<float>(static_cast<double>(x) * kGrassDetailUvScale),
                             static_cast<float>(static_cast<double>(z) * kGrassDetailUvScale)});
    }
    return true;
}

// 0x004D0D30
bool make_grass(PcGeom* geom, MeshRef* mesh)
{
    MaterialRef* m = mesh->material_value();
    if (!m)
        return false;
    if (core::at(m->pass_flags(), 0) & kAnimatedGrass)
        return make_animated_grass(geom, mesh);
    int layers = static_cast<int>(m->unknown18_value());
    if (!layers)
        return false;
    if (layers > kMaxLayers)
        layers = kMaxLayers;
    auto step = static_cast<float>(std::bit_cast<float>(m->unknown14_value()) / static_cast<double>(layers));
    for (int layer = 0; layer < layers; ++layer) {
        memory::Hub::get_pull()->enter_heap_scope(memory::bottom_up_heap());
        MeshRef* copy = mesh->replicate(false);
        memory::Hub::get_pull()->leave_heap_scope();
        copy->flags_value() |= 0x182;
        std::array<char, 64> name{};
        text::format_to(name, "Grass-Grass_Layer%d", layer);
        const uint32_t checksum = checksums::generate_crc_from_buffer(name.data(), static_cast<int>(std::strlen(name.data())));
        const SceneRef* scene = geom->scene->meshes;
        auto* material = static_cast<MaterialRef*>(scene->find_material(checksum));
        copy->set_material(material);
        material_pixel_shader(material, &copy->pixel_shader());
        auto height = static_cast<float>(layer) * 0.1f + 1100.0f; // (drawn after the ground, in order)
        core::at(material->pass_flags(), 0) |= 0x40;
        core::at(material->mip_lod_bias(), 0) = 0xC0000000; // (-2)
        material->set_sort_flag(0);
        material->set_sort_key(height);
        uint8_t* vertices = nullptr;
        copy->fetch_vertex_store(0)->get_lock(0, 0, &vertices, 0); // (not unlocked)
        const gpu::VertexWriter layer_vertices(vertices, copy->stride_value(), copy->vertex_count());
        const double lift = static_cast<double>(layer + 1) * step;
        for (uint32_t i = 0; i < copy->vertex_count(); ++i) {
            auto v = layer_vertices.view().point<maths::Vec4>(i);
            v.y = static_cast<float>(lift + v.y);
            layer_vertices.set_point(i, 0, v);
            const float x = v.x;
            const float z = v.z;
            layer_vertices.set_floats<2>(i, copy->uv_offset(),
                                         {static_cast<float>(static_cast<double>(x) * kGrassCopyUvScale),
                                          static_cast<float>(static_cast<double>(z) * kGrassCopyUvScale)});
        }
        geom->append_mesh(copy);
    }
    mesh->flags_value() |= 0x80; // (the ground mesh itself isn't drawn as grass)
    return true;
}

} // namespace render

namespace render {

// 0x00640D90 (a static initialiser): the grass materials' two textures, empty. (The original constructed them as
// listed textures; the list is only walked for render targets, which these never are.)
void construct_grass_textures()
{
    pc_grass_state().grass_texture0_ = TextureImage{};
    pc_grass_state().grass_texture1_ = TextureImage{};
}

// 0x00640DC0 (a static initialiser): the grass animation's 17 textures, empty (as above).
void construct_grass_texture_animation() { pc_grass_state().grass_texture_animation_.fill(TextureImage{}); }

} // namespace render
