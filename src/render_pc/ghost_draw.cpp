// NOT ORIGINAL - the ghosts a UI handed over drawn in the 3D view (ghost_draw.h).

#include "render_pc/ghost_draw.h"

#include <cstdint>
#include <span>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/ghost_draws.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/mesh_formats.h"
#include "engine/gfx/surface_material.h"
#include "render_pc/geom.h"
#include "render_pc/render_state.h"
#include "render_pc/sub_mesh.h"

namespace render::ghost_draw {

namespace {

constexpr uint32_t kMeshKeptOutFlags = 0x210;   // the mesh flags that keep a mesh out of every view (as the preview)
constexpr uint32_t kMeshScanLimit = 4096;       // the meshes looked at, at most (the editor's limit)
constexpr uint32_t kPassFlagTransparent = 0x40; // a material's first pass: drawn after the opaque meshes
constexpr int kAlphaShift = 24;                 // a blend word's fixed alpha (its top byte)

const PcGeom* pc_geom(const Geom* geom)
{
    return (geom != nullptr && geom->kind() == GeomKind::kPc) ? core::down_cast<const PcGeom>(geom) : nullptr;
}

// One ghost's meshes: opaque ones first (at its opacity), then the transparent (their own blend).
void draw_ghost(const ghost_draws::Ghost& ghost)
{
    const PcGeom* geom = pc_geom(ghost.geom);
    if (geom == nullptr || geom->meshes() == nullptr) {
        return;
    }
    gpu().set_transform(GpuTransform::kWorld, ghost.world.data());
    set_world_transform(ghost.world.data()); // (the distance clipping's and culling's)
    const std::span<MeshRef*> meshes = geom->mesh_run();
    for (int pass = 0; pass < 2; ++pass) {
        forced_blend() = pass == 0 ? (static_cast<uint32_t>(kBlendModeFixBlend) |
                                      (static_cast<uint32_t>(ghost.opacity) << kAlphaShift))
                                   : 0U;
        for (uint32_t i = 0; i < meshes.size() && i < kMeshScanLimit; ++i) {
            MeshRef* mesh = core::at(meshes, i);
            if (mesh == nullptr || (mesh->flags() & kMeshKeptOutFlags) != 0U) {
                continue;
            }
            MaterialRef* material = mesh->material();
            if (material == nullptr) {
                continue;
            }
            const bool transparent = (core::at(material->pass_flags(), 0) & kPassFlagTransparent) != 0U;
            if (transparent != (pass == 1)) {
                continue;
            }
            material->hand_in(); // (each time: the forced blend is the material's blend word's stand-in)
            mesh->render();
        }
    }
}

} // namespace

void draw(bool keep)
{
    const std::span<const ghost_draws::Ghost> ghosts = ghost_draws::pending();
    if (ghosts.empty()) {
        return;
    }
    GpuMatrix world{};
    gpu().get_transform(GpuTransform::kWorld, world.data());
    const uint32_t forced = forced_blend();
    change_phase(RenderStateId::kAlphaBlend, 1);
    change_phase(RenderStateId::kZWrite, 1);
    change_phase(RenderStateId::kZTest, 1);
    for (const ghost_draws::Ghost& ghost : ghosts) {
        draw_ghost(ghost);
    }
    forced_blend() = forced;
    gpu().set_transform(GpuTransform::kWorld, world.data());
    set_world_transform(nullptr);
    if (!keep) {
        ghost_draws::clear();
    }
}

void drop() { ghost_draws::clear(); }

} // namespace render::ghost_draw
