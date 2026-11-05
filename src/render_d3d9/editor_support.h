#pragma once

#include <cstdint>
#include <span>

#include <gsl/pointers>

#include "engine/gfx/render_camera.h"
#include "engine/gfx/render_device.h"

// NOT ORIGINAL - what our level editor (src/editor) asks of the Direct3D 9 backend, through render::Device
// (render_device.h, "what our level editor asks"): its old HUD's lines, a PC geom's meshes and instance, the buffers its
// ground tool makes, the Add palette's preview (once the editor's own calls into this backend, from
// editor/level_editor.cpp, _ground_game, _scenery, _preview).
namespace render::editor_support {

// The lines drawn on the device with the renderer's cached state (no texture, the screen vertex format, no pixel
// shader) and texture stage 0 taking the vertex colour, put back after (the renderer doesn't know of it).
void draw_screen_lines(std::span<const ScreenLineVertex> vertices);

// A PC geom's mesh count and meshes, its mesh array (null: none), its instance and its active flag (0 / null / false
// for another kind of geom).
uint32_t geom_mesh_count(const Geom* geom);
MeshData* geom_mesh(const Geom* geom, uint32_t index);
const void* geom_mesh_list(const Geom* geom);
InstanceData* geom_instance(const Geom* geom);
bool geom_active(const Geom* geom);

// The buffers as MeshRef::replicate makes them: a vertex buffer (0x004D5760: its copy in memory and the card's) and
// an index buffer (the C runtime's malloc 0x00624031, constructor 0x004D4270); written to the card; destroyed.
gsl::owner<VertexBufferData*> create_vertex_buffer(uint32_t bytes);
gsl::owner<IndexBufferData*> create_index_buffer(uint32_t bytes);
// The whole copy written to the card (0x004D40E0 after the inlined lock of the whole buffer, as MeshRef::move_to).
void upload_vertex_buffer(VertexBufferData* buffer);
// The whole copy written into the card's buffer, locked and unlocked directly.
void upload_index_buffer(IndexBufferData* buffer);
void release_vertex_buffer(gsl::owner<VertexBufferData*> buffer);
void release_index_buffer(gsl::owner<IndexBufferData*> buffer);

// The preview (render::Device::begin_preview_view, draw_preview_geom, end_preview_view).
void begin_preview_view(const maths::Mat4* orientation, const maths::Vec4* position, CameraLens lens);
void draw_preview_geom(Geom* geom, bool through_instance);
void end_preview_view();

} // namespace render::editor_support
