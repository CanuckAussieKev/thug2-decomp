// Drawing a scene's meshes: see scene_render.h.

#include "base/core/checked_index.h"
#include "render_d3d9/scene_render.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "render_d3d9/device.h"
#include "engine/gfx/surface_material.h"
#include "render_d3d9/sub_mesh.h"
#include "engine/gfx/occlusion.h"
#include "render_d3d9/pc_billboards.h"
#include "render_d3d9/render_state.h"
#include "render_d3d9/shadow_base.h"
#include "render_d3d9/view_mode.h"
#include "base/maths/vec4.h"
#include "platform/crt.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/view_setup.h"

namespace render {

constexpr std::size_t kDrawnMeshSlots = 4096;
constexpr std::size_t kSortedMeshSlots = 1000;
constexpr int kMeshFlagActive = 0x40;
constexpr int kMeshFlagsHidden = 0x210;
constexpr int kLastDrawnSlot = 0xFFF;
constexpr int kNoTransparentMeshes = 0x1000; // first_transparent() at or past this: no transparent meshes in the scene
constexpr int kSceneFlagShadows = 0x100;

namespace {

constexpr float kNear = 600.0F; // 0x0064AA98

// The meshes drawn (or to be drawn) this pass, for the shadows (0x00792640, 4096 at most - the last slot reused).
std::array<MeshRef*, kDrawnMeshSlots>& get_drawn()
{
    static std::array<MeshRef*, kDrawnMeshSlots> drawn_meshes{};
    return drawn_meshes;
}
struct DepthOrderedMesh {
    MeshRef* mesh;
    float depth;
    uint32_t unused;
};
std::array<DepthOrderedMesh, kSortedMeshSlots>& get_sorted()
{
    static core::NoDestroy<std::array<DepthOrderedMesh, kSortedMeshSlots>> sorted_meshes;
    return sorted_meshes.get();
}

// A mesh's fields and calls (not rewritten yet; thiscall).
uint32_t mesh_traits(const MeshRef* mesh) { return mesh->flags(); }
MaterialRef* get_mesh_material(const MeshRef* mesh) { return mesh->material(); }
void set_material(MaterialRef* material) { material->hand_in(); }
void draw_mesh(MeshRef* mesh) { mesh->render(); }
// The render statistics (0x00775B80; thiscall).
void count_pass(int which) { billboard_pool().render(which); }

// The game's qsort (equal entries keep the game's order).
void get_qsort(void* base, uint32_t count, uint32_t size, int (*compare)(const void*, const void*))
{
    crt::get_qsort(base, count, size, compare);
}

// One scene's drawing pass: its flags, the viewport's bit, the material set last and the meshes drawn so far.
class Lap {
public:
    explicit Lap(uint32_t flags) : flags_(flags), no_culling_((flags & kRenderNoCulling) != 0) {}
    void set_flags(uint32_t flags) { flags_ = flags; }
    void set_viewport(int viewport) { viewport_bit_ = static_cast<uint8_t>(1U << viewport); }
    [[nodiscard]] int drawn() const { return drawn_; }
    void forget_drawn() { drawn_ = 0; }

    // Whether a mesh is drawn this pass: shown (0x40) and not hidden (0x210); unless culling is off, in this
    // viewport, in view and (with occlusion) not hidden by an occluder.
    bool get_visible(MeshRef* mesh) const
    {
        const uint32_t mesh_flags = mesh_traits(mesh);
        if (((mesh_flags & kMeshFlagActive) == 0U) || ((mesh_flags & kMeshFlagsHidden) != 0U)) {
            return false;
        }
        if (no_culling_) {
            return true;
        }
        if ((mesh->viewports() & viewport_bit_) == 0) {
            return false;
        }
        const maths::Vec4 sphere = sphere_of(mesh->centre(), mesh->radius());
        const maths::Vec4* centre = &sphere;
        const float radius = mesh->radius();
        if (!sphere_in_view(centre, radius)) {
            return false;
        }
        if (((flags_ & kRenderOcclusion) != 0U) && sphere_occluded(centre, {.radius = radius, .weight = 1})) {
            return false;
        }
        return true;
    }
    void blit(MeshRef* mesh)
    {
        MaterialRef* mesh_material = get_mesh_material(mesh);
        if (mesh_material != material_) {
            material_ = mesh_material;
            set_material(material_);
        }
        draw_mesh(mesh);
    }
    void remember(MeshRef* mesh)
    {
        core::at(get_drawn(), drawn_) = mesh;
        if (drawn_ < kLastDrawnSlot) {
            ++drawn_;
        }
    }

private:
    uint32_t flags_;
    uint8_t viewport_bit_{};
    bool no_culling_;
    MaterialRef* material_ = nullptr; // the current one
    int drawn_ = 0;
};

} // namespace

// 0x004DA690
int compare_distances(const void* a, const void* b)
{
    const float first = static_cast<const DepthOrderedMesh*>(a)->depth;
    const float second = static_cast<const DepthOrderedMesh*>(b)->depth;
    if (first < second) {
        return -1;
    }
    if (first > second) {
        return 1;
    }
    return 0;
}

namespace {

// The shadows on the meshes a pass drew: no depth writes, no anisotropic filtering meanwhile.
void draw_pass_shadows(SceneRef* scene, Lap& pass)
{
    if (renderer().z_write() == 1) {
        render::set_render_state(GpuRenderState::kZWriteEnable, 0);
        renderer().set_z_write(0);
    }
    uint32_t min_filter = 0;
    gpu().get_sampler_state(0, GpuSamplerState::kMinFilter, &min_filter);
    if (min_filter == gpu_value::kFilterAnisotropic) {
        gpu().set_sampler_state(0, GpuSamplerState::kMinFilter, gpu_value::kFilterLinear);
    }
    draw_shadows(scene, std::span<MeshRef* const>(get_drawn()).first(static_cast<size_t>(pass.drawn())));
    if (min_filter == gpu_value::kFilterAnisotropic) {
        gpu().set_sampler_state(0, GpuSamplerState::kMinFilter, gpu_value::kFilterAnisotropic);
    }
    if (renderer().z_write() == 0U) {
        render::set_render_state(GpuRenderState::kZWriteEnable, 1);
        renderer().set_z_write(1);
    }
}

// A scene's flagged transparent meshes sorted by depth, from `index` on; then the rest.
void draw_flagged_and_rest(SceneRef* scene, Lap& pass, int& index)
{
    // The flagged ones, sorted by depth.
    const int end = scene->first_flagged() + scene->flagged_count();
    int sorted = 0;
    for (; index < end; ++index) {
        MeshRef* mesh = core::at(scene->mesh_run(), index);
        if (!pass.get_visible(mesh)) {
            continue;
        }
        pass.remember(mesh);
        core::at(get_sorted(), sorted).mesh = mesh;
        core::at(get_sorted(), sorted).depth = view_setup().last_sphere_distance();
        ++sorted;
    }
    if (sorted > 0) {
        get_qsort(get_sorted().data(), sorted, sizeof(DepthOrderedMesh), &compare_distances);
        for (int sorted_index = 0; sorted_index < sorted; ++sorted_index) {
            pass.blit(core::at(get_sorted(), sorted_index).mesh);
        }
    }
    // Then the rest.
    for (; index < scene->mesh_count(); ++index) {
        MeshRef* mesh = core::at(scene->mesh_run(), index);
        if (!pass.get_visible(mesh)) {
            continue;
        }
        pass.blit(mesh);
        pass.remember(mesh);
    }
}

} // namespace

// 0x004DC700
void draw_scene(SceneRef* scene, ScenePass scene_pass)
{
    auto flags = scene_pass.flags;
    const auto viewport = scene_pass.viewport;
    Lap pass(flags);
    if (scene->unknown40() != 0U) {
        return;
    }
    // Not original - the level editor's Collision only view (render_d3d9/view_mode.h): no scene drawn in the 3D passes.
    // Original behaviour: drawn.
    if (view_mode::world_hidden()) {
        return;
    }
    if ((flags & kDrawStencilOnly) != 0U) {
        draw_shadow_volumes(scene, viewport);
        return;
    }
    if (scene->first_transparent() >= kNoTransparentMeshes) {
        flags &= ~kRenderNearFirst;
        pass.set_flags(flags);
    }
    pass.set_viewport(viewport);

    if ((flags & kRenderOpaque) != 0U) {
        for (int i = 0; i < scene->first_transparent(); ++i) {
            MeshRef* mesh = core::at(scene->mesh_run(), i);
            if (!pass.get_visible(mesh)) {
                continue;
            }
            if ((flags & kRenderNearFirst) == 0U) {
                pass.blit(mesh);
            } else {
                // The near ones now; their depth kept for the far ones after. (Not a number: never drawn.)
                const float depth = view_setup().last_sphere_distance();
                if (depth < kNear || depth == kNear) {
                    pass.blit(mesh);
                }
                mesh->set_depth(view_setup().last_sphere_distance());
            }
            pass.remember(mesh);
        }
        if ((flags & kRenderNearFirst) != 0U) {
            const int count = pass.drawn();
            for (int j = 0; j < count; ++j) {
                MeshRef* mesh = core::at(get_drawn(), j);
                if (mesh->depth() > kNear) {
                    pass.blit(mesh);
                }
            }
        }
        if ((scene->flags() & kSceneFlagShadows) != 0U) { // shadows on the meshes drawn
            draw_pass_shadows(scene, pass);
        }
        pass.forget_drawn();
        if ((flags & kRenderStats) != 0U) {
            count_pass(1);
        }
    }

    if ((flags & kRenderTransparent) == 0U) {
        return;
    }
    // The transparent meshes up to the flagged ones.
    int index = scene->first_transparent();
    for (; index < scene->first_flagged(); ++index) {
        MeshRef* mesh = core::at(scene->mesh_run(), index);
        if (!pass.get_visible(mesh)) {
            continue;
        }
        pass.blit(mesh);
        pass.remember(mesh);
    }
    if (scene->flagged_count() > 0) {
        draw_flagged_and_rest(scene, pass, index);
    }
    if ((scene->flags() & kSceneFlagShadows) != 0U) {
        draw_shadows(scene, std::span<MeshRef* const>(get_drawn()).first(static_cast<size_t>(pass.drawn())));
    }
    if ((flags & kRenderStats) != 0U) {
        count_pass(2);
    }
}

} // namespace render
