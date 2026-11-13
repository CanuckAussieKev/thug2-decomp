#pragma once

// NOT ORIGINAL - ray tracing's frame as data (engine/gfx/rt_scene.h, docs/design/vulkan.md R1): render_pc keeps the
// installed RayTracing told of the meshes and materials it draws from and fills the frame record each frame. Nothing
// is drawn differently, with Direct3D 9 or Vulkan; with no RayTracing installed every call here is a test of one
// pointer and nothing more.
//
// What the record holds:
// - instances: every mesh the world's and the objects' passes may draw this frame - the loaded scenes' meshes but the
//   sky's (kWorld) and every active instance's (kObject; kSkinned when skinned on the CPU), shown and not hidden -,
//   gathered before the world is drawn and without the view's culling (what is behind the camera still casts
//   shadows); each with its mesh's id, its material's and the transform it is drawn with;
// - lights: the level's ambient colour, its directional lights and the dynamic point lights that are on;
// - cameras: each viewport's view and projection as it is drawn.
//
// Meshes are registered (add_mesh) the first frame they are in the record, with their first level of detail; removed
// when destroyed. A mesh whose buffers moved (a skinned mesh's vertex buffers take turns, a device reset, an edit) is
// given again (update_mesh) the frame it is next recorded; one whose vertices were written (skinned on the CPU, moved
// by the editor) is reported changed (mesh_changed) then - so the geometry of a skinned mesh is the one it was drawn
// with the frame before. A device reset forgets every mesh (removed; registered again under new ids). Materials are
// given (set_material) when first used and whenever their first pass changes (texture animations).
//
// begin_frame comes at the first viewport's world camera, before the world is drawn: the record's instances and lights
// are complete then, its cameras those of the viewports drawn so far (the others' are added as they are drawn; the
// record stays unchanged otherwise until the next frame).
namespace render {

struct MeshRef;
struct MaterialRef;
struct VertexBufferData;

namespace rt_record {

// A RayTracing is installed (rt::ray_tracing()).
[[nodiscard]] bool on();

// draw_world, before anything is drawn: the record cleared and filled - instances (meshes and materials registered,
// changed ones reported) and lights.
void record_frame();
// draw_viewport, the world's camera chosen: the viewport's camera added; the frame's first: begin_frame.
void camera_chosen(int viewport);
// draw_viewport: the world's and the objects' passes begin / end (the sky's are outside them).
void set_world_passes(bool inside);
// A mesh drawn (MeshRef::render), with the world transform it is drawn with: reported to a check that wants draws,
// when inside the world's passes.
void mesh_drawn(const MeshRef* mesh);
// The destructors: the mesh / material removed if it was registered.
void mesh_released(const MeshRef* mesh);
void material_released(const MaterialRef* material);
// A vertex buffer's contents written to the card (VertexBuffer::get_unlock): its mesh changed, unless the write is a
// colour-only one (ColourWrite).
void vertex_buffer_written(const VertexBufferData* buffer);
// The device's buffers released (release_buffers, before a reset): every mesh removed.
void forget_meshes();

// While one lives, vertex buffer writes are colour-only (MeshRef::apply_vertex_colors).
class ColourWrite {
public:
    ColourWrite();
    ~ColourWrite();
    ColourWrite(const ColourWrite&) = delete;
    ColourWrite& operator=(const ColourWrite&) = delete;
    ColourWrite(ColourWrite&&) = delete;
    ColourWrite& operator=(ColourWrite&&) = delete;
};

} // namespace rt_record
} // namespace render
