// NOT ORIGINAL - the level editor's calls into the Direct3D 9 backend (editor_support.h).

#include "render_pc/editor_support.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include <gsl/pointers>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "base/core/layout_32bit.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/render_camera.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/surface_material.h"
#include "engine/memory/mem.h"
#include "platform/crt.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "render_pc/draw_frame.h"
#include "render_pc/geom.h"
#include "render_pc/pc_billboards.h"
#include "render_pc/pc_instance.h"
#include "render_pc/render_state.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace render::editor_support {

namespace {

uint32_t get_stage_state(int type)
{
    uint32_t value = 0;
    gpu().get_stage_state(0, static_cast<GpuStageState>(type), &value);
    return value;
}
void set_stage_state(int type, uint32_t value)
{
    gpu().set_stage_state(0, static_cast<GpuStageState>(type), value);
}

} // namespace

void draw_screen_lines(std::span<const ScreenLineVertex> vertices)
{
    const int count = static_cast<int>(vertices.size());
    const ScreenLineVertex* line_data = vertices.data();
    // The renderer's own (cached) state: no texture, pre-transformed vertices with a colour.
    set_texture(0, nullptr);
    set_vertex_format(kScreenVertexFormat); // XYZRHW | DIFFUSE | TEX1
    set_pixel_shader(nullptr, nullptr);     // no pixel shader
    // Stage 0 takes the vertex colour (put back after: the renderer doesn't know of this).
    const std::array<int, 4> states = {{1, 2, 4, 5}};      // colour op / arg 1, alpha op / arg 1
    const std::array<uint32_t, 4> values = {{2, 0, 2, 0}}; // select arg 1, diffuse
    std::array<uint32_t, 4> saved{};
    for (int i = 0; i < 4; ++i) {
        core::at(saved, i) = get_stage_state(core::at(states, i));
        set_stage_state(core::at(states, i), core::at(values, i));
    }
    gpu().draw_from_memory(GpuPrimitive::kLineList, static_cast<uint32_t>(count / 2), line_data, sizeof(ScreenLineVertex));
    for (int i = 0; i < 4; ++i) {
        set_stage_state(core::at(states, i), core::at(saved, i));
    }
}

// --- a PC geom ----------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kIndexStoreBytes = 0x18; // the index store object's size
constexpr uint32_t kIndexStoreMode = 0x65;  // the index store's mode argument

// The PC geom `geom` is (null: another kind, or none).
const PcGeom* pc_geom(const Geom* geom)
{
    return (geom != nullptr && geom->kind() == GeomKind::kPc) ? core::down_cast<const PcGeom>(geom) : nullptr;
}

} // namespace

uint32_t geom_mesh_count(const Geom* geom)
{
    const PcGeom* pc = pc_geom(geom);
    return pc != nullptr ? pc->mesh_count() : 0U;
}

MeshData* geom_mesh(const Geom* geom, uint32_t index) { return core::at(pc_geom(geom)->mesh_run(), index); }

const void* geom_mesh_list(const Geom* geom)
{
    const PcGeom* pc = pc_geom(geom);
    return pc != nullptr ? static_cast<const void*>(pc->meshes()) : nullptr;
}

InstanceData* geom_instance(const Geom* geom)
{
    const PcGeom* pc = pc_geom(geom);
    return pc != nullptr ? pc->instance() : nullptr;
}

bool geom_active(const Geom* geom)
{
    const PcGeom* pc = pc_geom(geom);
    return pc != nullptr && pc->active();
}

gsl::owner<VertexBufferData*> create_vertex_buffer(uint32_t bytes) { return new_vertex_buffer(bytes, 0, 0); }

gsl::owner<IndexBufferData*> create_index_buffer(uint32_t bytes)
{
    static_assert(core::layout_32bit(sizeof(IdxStore) == kIndexStoreBytes), "the original allocates this many bytes");
    gsl::owner<void*> memory = crt::alloc_block(sizeof(IdxStore));
    if (!memory) {
        return nullptr;
    }
    return memory::construct_in<IdxStore>(memory, bytes, 0, kIndexStoreMode);
}

void upload_vertex_buffer(VertexBufferData* buffer)
{
    buffer->set_lock_offset(0);
    buffer->set_lock_size(buffer->size());
    buffer->set_lock_flags(0);
    static_cast<VertexBuffer*>(buffer)->get_unlock();
}

void upload_index_buffer(IndexBufferData* buffer)
{
    GpuIndexBuffer* const index_buffer = buffer->buffer();
    void* locked = nullptr;
    if (!index_buffer || gpu().lock_index_buffer(index_buffer, 0, 0, &locked, 0) < 0) {
        return;
    }
    std::memcpy(locked, buffer->indices(), buffer->size());
    gpu().unlock_index_buffer(index_buffer);
}

void release_vertex_buffer(gsl::owner<VertexBufferData*> buffer) { static_cast<VertexBuffer*>(buffer)->teardown(); }

void release_index_buffer(gsl::owner<IndexBufferData*> buffer) { static_cast<IdxStore*>(buffer)->teardown(); }

// --- the preview --------------------------------------------------------------------------------------------------

namespace {

constexpr int kNotCulledFlag = 8;               // draw_one_instance's flag: not culled
constexpr uint32_t kMeshDrawnFlag = 0x40;       // the mesh flag a drawn mesh has
constexpr uint32_t kMeshSkipFlags = 0x210;      // the mesh flags that keep a mesh out of the preview
constexpr uint32_t kMeshScanLimit = 4096;       // the meshes looked at, at most (the editor's limit)
constexpr uint32_t kPassFlagTransparent = 0x40; // a material's first pass: drawn after the opaque meshes

// A geometry's meshes as the world pass draws them: shown and not hidden; opaque ones first, then the transparent.
void draw_meshes(const PcGeom* geom)
{
    const std::span<MeshRef*> meshes = geom->mesh_run();
    const uint32_t count = geom->mesh_count();
    if (!geom->meshes()) {
        return;
    }
    const MaterialRef* current = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < count && i < kMeshScanLimit; ++i) {
            MeshRef* mesh = core::at(meshes, i);
            if (!mesh) {
                continue;
            }
            const uint32_t flags = mesh->flags();
            if (((flags & kMeshDrawnFlag) == 0U) || ((flags & kMeshSkipFlags) != 0U)) {
                continue;
            }
            MaterialRef* material = mesh->material();
            const bool transparent =
                (material != nullptr) && ((core::at(material->pass_flags(), 0) & kPassFlagTransparent) != 0U);
            if (transparent != (pass == 1)) {
                continue;
            }
            if (material != current) {
                current = material;
                material->hand_in();
            }
            mesh->render();
        }
    }
}

} // namespace

void begin_preview_view(const maths::Mat4* orientation, const maths::Vec4* position, CameraLens lens)
{
    choose_camera(orientation, position, lens, false);
    set_world_transform(nullptr);
    change_phase(RenderStateId::kZWrite, 1);
    change_phase(RenderStateId::kZTest, 1);
}

void draw_preview_geom(Geom* geom, bool through_instance)
{
    const PcGeom* pc = pc_geom(geom);
    if (pc == nullptr) {
        return;
    }
    if (Singleton* instance = through_instance ? pc->instance() : nullptr) {
        // 0x004D16B0: one instance drawn (flag 8: not culled) - its world matrix, its bones, its lights.
        draw_one_instance(instance, kNotCulledFlag);
    } else {
        draw_meshes(pc);
    }
}

void end_preview_view()
{
    // 0x004CDAA0 (thiscall on 0x00775B80): the view frustum from the camera's globals; no instance's bones taken as
    // set (0x007825BC: the next instance sets its own).
    billboard_pool().setup_axes();
    set_last_bones(nullptr);
}

} // namespace render::editor_support
