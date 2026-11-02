// Drawing a scene's meshes: see scene_render.h.

#include "engine/core/checked_index.h"
#include "engine/gfx/scene_render.h"

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/gfx/device.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/shadow_base.h"
#include "engine/gfx/view_mode.h"
#include "engine/maths/vec4.h"
#include "platform/crt.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/shared_state.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
uint8_t& get_z_write() { return g_z_write; }
float sphere_depth() { return occlusion_shared().last_sphere_distance_; }
constexpr float kNear = 600.0f; // 0x0064AA98

// The meshes drawn (or to be drawn) this pass, for the shadows (0x00792640, 4096 at most - the last slot reused).
std::array<MeshRef*, 4096>& get_drawn()
{
    static std::array<MeshRef*, 4096> drawn_meshes{};
    return drawn_meshes;
}
struct DepthOrderedMesh {
    MeshRef* mesh;
    float depth;
    uint32_t unused;
};
std::array<DepthOrderedMesh, 1000>& get_sorted()
{
    static core::NoDestroy<std::array<DepthOrderedMesh, 1000>> sorted_meshes;
    return sorted_meshes.get();
}

// A mesh's fields and calls (not rewritten yet; thiscall).
uint32_t mesh_traits(const MeshRef* mesh) { return mesh->flags; }
MaterialRef* get_mesh_material(const MeshRef* mesh) { return mesh->material; }
void set_material(MaterialRef* material) { material->hand_in(); }
void draw_mesh(MeshRef* mesh) { mesh->render(); }
// The render statistics (0x00775B80; thiscall).
void count_pass(int which) { g_billboard_pool.render(which); }

// The game's qsort (equal entries keep the game's order).
void get_qsort(void* base, uint32_t count, uint32_t size, int (*compare)(const void*, const void*))
{
    crt::get_qsort(base, count, size, compare);
}

struct Lap {
    SceneRef* scene{};
    uint32_t flags{};
    uint8_t viewport_bit_{};
    bool no_culling_{};
    MaterialRef* material = nullptr; // the current one
    int drawn = 0;

    // Whether a mesh is drawn this pass: shown (0x40) and not hidden (0x210); unless culling is off, in this
    // viewport, in view and (with occlusion) not hidden by an occluder.
    bool get_visible(MeshRef* mesh) const
    {
        const uint32_t mesh_flags = mesh_traits(mesh);
        if (!(mesh_flags & 0x40) || (mesh_flags & 0x210))
            return false;
        if (no_culling_)
            return true;
        if (!(mesh->viewports & viewport_bit_))
            return false;
        const maths::Vec4 sphere = sphere_of(mesh->centre, mesh->radius);
        const maths::Vec4* centre = &sphere;
        const float radius = mesh->radius;
        if (!sphere_in_view(centre, radius))
            return false;
        if ((flags & kRenderOcclusion) && sphere_occluded(centre, radius, 1))
            return false;
        return true;
    }
    void blit(MeshRef* mesh)
    {
        MaterialRef* mesh_material = get_mesh_material(mesh);
        if (mesh_material != material) {
            material = mesh_material;
            set_material(material);
        }
        draw_mesh(mesh);
    }
    void remember(MeshRef* mesh)
    {
        core::at(get_drawn(), drawn) = mesh;
        if (drawn < 0xFFF)
            ++drawn;
    }
};

} // namespace

// 0x004DA690
int compare_distances(const void* a, const void* b)
{
    const float first = static_cast<const DepthOrderedMesh*>(a)->depth;
    const float second = static_cast<const DepthOrderedMesh*>(b)->depth;
    if (first < second)
        return -1;
    if (first > second)
        return 1;
    return 0;
}

// 0x004DC700
void draw_scene(SceneRef* scene, uint32_t flags, int viewport)
{
    Lap pass{.scene = scene, .flags = flags, .viewport_bit_ = 0, .no_culling_ = (flags & kRenderNoCulling) != 0};
    if (scene->unknown40)
        return;
    // Not original - the level editor's Collision only view (engine/gfx/view_mode.h): no scene drawn in the 3D passes.
    // Original behaviour: drawn.
    if (view_mode::world_hidden())
        return;
    if (flags & kDrawStencilOnly) {
        draw_shadow_volumes(scene, viewport);
        return;
    }
    if (scene->first_transparent_ >= 0x1000)
        pass.flags = flags &= ~kRenderNearFirst;
    pass.viewport_bit_ = static_cast<uint8_t>(1u << viewport);

    if (flags & kRenderOpaque) {
        for (int i = 0; i < scene->first_transparent_; ++i) {
            MeshRef* mesh = core::at(scene->mesh_run(), i);
            if (!pass.get_visible(mesh))
                continue;
            if (!(flags & kRenderNearFirst)) {
                pass.blit(mesh);
            } else {
                // The near ones now; their depth kept for the far ones after. (Not a number: never drawn.)
                const float depth = sphere_depth();
                if (depth < kNear || depth == kNear)
                    pass.blit(mesh);
                mesh->depth = sphere_depth();
            }
            pass.remember(mesh);
        }
        if (flags & kRenderNearFirst) {
            const int count = pass.drawn;
            for (int j = 0; j < count; ++j) {
                MeshRef* mesh = core::at(get_drawn(), j);
                if (mesh->depth > kNear)
                    pass.blit(mesh);
            }
        }
        if (scene->flags & 0x100) { // shadows on the meshes drawn
            if (get_z_write() == 1) {
                render::set_render_state(D3DRS_ZWRITEENABLE, FALSE);
                get_z_write() = 0;
            }
            DWORD min_filter = 0;
            input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
            if (min_filter == D3DTEXF_ANISOTROPIC)
                input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            draw_shadows(scene, std::span<MeshRef* const>(get_drawn()).first(static_cast<size_t>(pass.drawn)));
            if (min_filter == D3DTEXF_ANISOTROPIC)
                input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            if (!get_z_write()) {
                render::set_render_state(D3DRS_ZWRITEENABLE, TRUE);
                get_z_write() = 1;
            }
        }
        pass.drawn = 0;
        if (flags & kRenderStats)
            count_pass(1);
    }

    if (!(flags & kRenderTransparent))
        return;
    // The transparent meshes up to the flagged ones.
    int i = scene->first_transparent_;
    for (; i < scene->first_flagged_; ++i) {
        MeshRef* mesh = core::at(scene->mesh_run(), i);
        if (!pass.get_visible(mesh))
            continue;
        pass.blit(mesh);
        pass.remember(mesh);
    }
    if (scene->flagged_count_ > 0) {
        // The flagged ones, sorted by depth.
        const int end = scene->first_flagged_ + scene->flagged_count_;
        int sorted = 0;
        for (; i < end; ++i) {
            MeshRef* mesh = core::at(scene->mesh_run(), i);
            if (!pass.get_visible(mesh))
                continue;
            pass.remember(mesh);
            core::at(get_sorted(), sorted).mesh = mesh;
            core::at(get_sorted(), sorted).depth = sphere_depth();
            ++sorted;
        }
        if (sorted > 0) {
            get_qsort(get_sorted().data(), sorted, sizeof(DepthOrderedMesh), &compare_distances);
            for (int s = 0; s < sorted; ++s)
                pass.blit(core::at(get_sorted(), s).mesh);
        }
        // Then the rest.
        for (; i < scene->mesh_count_; ++i) {
            MeshRef* mesh = core::at(scene->mesh_run(), i);
            if (!pass.get_visible(mesh))
                continue;
            pass.blit(mesh);
            pass.remember(mesh);
        }
    }
    if (scene->flags & 0x100)
        draw_shadows(scene, std::span<MeshRef* const>(get_drawn()).first(static_cast<size_t>(pass.drawn)));
    if (flags & kRenderStats)
        count_pass(2);
}

} // namespace render
