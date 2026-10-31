// The PC grass (a sector's grass meshes, made when the sector loads): layered grass - copies of the mesh lifted a step
// at a time, each with its "Grass-Grass_Layer<n>" material - and animated grass, its own mesh with a second texture
// coordinate set from the positions and a material that cycles through 18 frames.

#include "engine/gfx/pc_grass.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

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
#include "engine/sys/startup_init.h"

namespace render {

float g_grass_detail_uv_scale{0.010416667f};
float g_grass_uv_scale{0.0052083335f};
float g_grass_copy_uv_scale{0.020833334f};
uint8_t g_grass_texture0[0x20]; // the grass materials' texture slots (0x20 bytes each)
uint8_t g_grass_texture1[0x20];
uint8_t g_grass_texture_animation[17 * 0x20]; // (17 slots of 0x20)

namespace {

constexpr uint32_t kAnimatedGrass = 0x8000000; // pass 0's flag: animated rather than layered
constexpr int kMaxLayers = 5;

void* look_up_texture(void* dictionary, uint32_t checksum)
{
    return static_cast<TextureSet*>(dictionary)->fetch_skin(checksum);
}

} // namespace

// 0x004D08A0
void __cdecl set_up_animated_grass_material(MaterialRef* m)
{
    m->textures[0] = g_grass_texture0;
    m->textures[1] = g_grass_texture1;
    m->blend_modes_[0] = 0x40000006;
    m->pass_color_[0][3] = 0.5f;
    m->pass_count_ = 2;
    m->pass_flags_[1] = 0x400;
    m->has_uv_wibble_ = false;
    m->mip_lod_bias_[0] = 0;
    m->mip_lod_bias_[1] = 0;
    m->address_modes_[0] = 0;
    m->address_modes_[1] = 0;
    m->sort_flag_ = 0;
    m->sort_key_ = 1501.0f;
    m->has_texture_anim_ = true;
    m->pass_flags_[0] = (m->pass_flags_[0] & ~9u) | 0xA40;
    auto* anim = static_cast<TextureAnim*>(memory::alloc(sizeof(TextureAnim)));
    m->texture_anim_ = anim;
    anim->key_counts_[0] = 18;
    m->texture_anim_->phases[0] = 0;
    m->texture_anim_->loops[0] = 0;
    m->texture_anim_->keys[0] = static_cast<TextureKey*>(memory::alloc(18 * sizeof(TextureKey)));
    m->texture_anim_->keys[1] = nullptr;
    m->texture_anim_->keys[2] = nullptr;
    m->texture_anim_->keys[3] = nullptr;
    for (int k = 0; k < 18; ++k) { // (a frame every 1000 / 30 ms, through 17 textures)
        m->texture_anim_->keys[0][k].time = k * 1000 / 30;
        m->texture_anim_->keys[0][k].texture = g_grass_texture_animation + (k % 17) * 0x20;
    }
}

// 0x004D0B60
bool __cdecl make_animated_grass(PcGeom* /*geom*/, MeshRef* mesh)
{
    void* dictionary = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(get_sky_scene()) + 0x88); // the sky's
    auto* entry = static_cast<uint8_t*>(look_up_texture(dictionary, 0x96D0BF08));
    if (!entry)
        return false;
    auto* texture = *reinterpret_cast<uint8_t**>(entry + 0x10);
    if (entry[8] & 1)
        texture = *reinterpret_cast<uint8_t**>(texture);
    std::memcpy(g_grass_texture1, texture, 0x20);
    if ((mesh->stride - mesh->unknown1c_[2]) / 8 == 1) { // (one texture coordinate set: room made for a second)
        VertexBuffer* wider = new_vertex_buffer((mesh->stride + 8u) * mesh->vertex_count_, 0, 0);
        uint8_t* from = nullptr;
        uint8_t* to = nullptr;
        mesh->fetch_vertex_store(0)->get_lock(0, 0, &from, D3DLOCK_READONLY);
        wider->get_lock(0, 0, &to, 0);
        for (int i = 0; i < mesh->vertex_count_; ++i) {
            std::memcpy(to, from, mesh->stride);
            to += mesh->stride + 8;
            from += mesh->stride;
        }
        mesh->fetch_vertex_store(0)->teardown();
        mesh->buffer_usage_ = 0;
        mesh->set_vertex_buffer(0, wider);
        mesh->stride = static_cast<uint8_t>(mesh->stride + 8);
        mesh->fvf = (mesh->fvf & ~0x100u) | 0x200; // (D3DFVF_TEX1 -> TEX2)
    }
    set_up_animated_grass_material(reinterpret_cast<MaterialRef*>(mesh->material));
    mesh->flags |= 0x402;
    mesh->pixel_shader_ = g_pixel_shaders[kPixelShader924];
    uint8_t* vertices = nullptr;
    mesh->fetch_vertex_store(0)->get_lock(0, 0, &vertices, 0); // (not unlocked: the copy is what's changed)
    for (uint32_t i = 0; i < mesh->vertex_count_; ++i) {
        auto* v = reinterpret_cast<float*>(vertices + mesh->stride * i);
        auto* uv = reinterpret_cast<float*>(vertices + mesh->stride * i + mesh->unknown1c_[2]);
        float x = v[0], z = v[2];
        uv[0] = static_cast<float>(static_cast<double>(x) * g_grass_uv_scale);
        uv[1] = static_cast<float>(static_cast<double>(z) * g_grass_uv_scale);
        uv[2] = static_cast<float>(static_cast<double>(x) * g_grass_detail_uv_scale);
        uv[3] = static_cast<float>(static_cast<double>(z) * g_grass_detail_uv_scale);
    }
    return true;
}

// 0x004D0D30
bool __cdecl make_grass(PcGeom* geom, MeshRef* mesh)
{
    auto* m = reinterpret_cast<MaterialRef*>(mesh->material);
    if (!m)
        return false;
    if (m->pass_flags_[0] & kAnimatedGrass)
        return make_animated_grass(geom, mesh);
    int layers = static_cast<int>(m->unknown18);
    if (!layers)
        return false;
    if (layers > kMaxLayers)
        layers = kMaxLayers;
    float step = static_cast<float>(*reinterpret_cast<const float*>(&m->unknown14) / static_cast<double>(layers));
    for (int layer = 0; layer < layers; ++layer) {
        memory::Hub::get_pull()->enter_heap_scope(memory::bottom_up_heap());
        MeshRef* copy = mesh->replicate(false);
        memory::Hub::get_pull()->leave_heap_scope();
        copy->flags |= 0x182;
        char name[64];
        std::snprintf(name, sizeof(name), "Grass-Grass_Layer%d", layer);
        uint32_t checksum = checksums::generate_crc_from_buffer(name, static_cast<int>(std::strlen(name)));
        auto* scene = *reinterpret_cast<SceneRef**>(geom->scene + 0x280);
        auto* material = static_cast<MaterialRef*>(scene->find_material(checksum));
        copy->material = reinterpret_cast<uint8_t*>(material);
        material_pixel_shader(material, &copy->pixel_shader_);
        float height = static_cast<float>(layer * 0.1f + 1100.0f); // (drawn after the ground, in order)
        material->pass_flags_[0] |= 0x40;
        material->mip_lod_bias_[0] = 0xC0000000; // (-2)
        material->sort_flag_ = 0;
        material->sort_key_ = height;
        uint8_t* vertices = nullptr;
        copy->fetch_vertex_store(0)->get_lock(0, 0, &vertices, 0); // (not unlocked)
        double lift = static_cast<double>(layer + 1) * step;
        for (uint32_t i = 0; i < copy->vertex_count_; ++i) {
            auto* v = reinterpret_cast<float*>(vertices + copy->stride * i);
            v[1] = static_cast<float>(lift + v[1]);
            auto* uv = reinterpret_cast<float*>(vertices + copy->stride * i + copy->unknown1c_[2]);
            float x = v[0], z = v[2];
            uv[0] = static_cast<float>(static_cast<double>(x) * g_grass_copy_uv_scale);
            uv[1] = static_cast<float>(static_cast<double>(z) * g_grass_copy_uv_scale);
        }
        geom->append_mesh(copy);
    }
    mesh->flags |= 0x80; // (the ground mesh itself isn't drawn as grass)
    return true;
}

} // namespace render

namespace render {

// 0x00640D90 (a static initialiser): the grass materials' two textures (each listed among the textures).
void construct_grass_textures()
{
    reinterpret_cast<TextureRef*>(g_grass_texture0)->make_new();
    reinterpret_cast<TextureRef*>(g_grass_texture1)->make_new();
}

// 0x00640DC0 (a static initialiser): the grass animation's 17 textures.
void construct_grass_texture_animation()
{
    for (int i = 0; i < 17; ++i)
        reinterpret_cast<TextureRef*>(g_grass_texture_animation + 0x20 * i)->make_new();
}

} // namespace render
