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
// given again (update_mesh) the frame it is next recorded; one whose vertices were written (moved by the editor) is
// reported changed (mesh_changed) then. A mesh skinned on the CPU is not registered itself: each instance drawing it has
// a pose of its own, skinned at the record with that instance's bones - the pose the draw makes this frame
// (engine/gfx/rt_poses.h). A device reset forgets every mesh (removed; registered again under new ids). Materials are
// given (set_material) when first used and whenever their first pass changes (texture animations).
//
// begin_frame comes at the first viewport's world camera, before the world is drawn: the record's instances and lights
// are complete then, its cameras those of the viewports drawn so far (the others' are added as they are drawn; the
// record stays unchanged otherwise until the next frame).
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace render {

struct MeshRef;
struct MaterialRef;
struct VertexBufferData;
struct Singleton;
struct GpuTexture;
namespace rt {
struct ObjectLight;
struct Geometry;
}
namespace texel {
struct Image;
}

namespace rt_record {

// (added by the sky's parts - sky_scenery.h) A mesh's first level of detail as its buffers hold it (rt_scene.h Geometry:
// every attribute where its vertex format puts it, the indices in memory) and its vertex buffer's bytes in memory; false:
// nothing to read (no indices, no buffer, not three floats of position, neither a strip nor a list). What the ray
// tracing registers and the samplers read.
bool mesh_geometry(const MeshRef* mesh, rt::Geometry* out, const VertexBufferData** source);
// A texture's first level decoded on the processor (texel_decode.h; read through a read-only lock) - `out` empty when
// its format can't be read. Returns its format (0: no texture).
uint32_t decode_first_level(GpuTexture* texture, texel::Image* out);

// A RayTracing is installed (rt::ray_tracing()).
[[nodiscard]] bool on();

// (round FG - one lamp-texel test) The light store's code for a lamp drawn in `mesh`'s texture (rt_lights.h
// lamp_texel_code): the floor the bake tells its lamp texels by - kTexelOverTexture x its texture's median, the texture
// decoded once - and, for a blended surface, its texels drawn solid alone. A mesh with no texture to read: the floor at
// the most (no lamp texels; the bake found none there either).
[[nodiscard]] uint8_t lamp_texel_code_of(const MeshRef* mesh);
// (round EMIT2 - an emitter's flux conserved) What a light store's emission is multiplied by over a triangle of a mesh,
// averaged over the triangle: the colour as drawn (the texel x the material's colour, linear - ff_shaders.cpp
// surface_texel, the rays' store_emission) where the renderers let it emit - its mesh's lamp-texel code
// (lamp_texel_code_of) when `lamp_texels`, every texel otherwise -, 0 elsewhere. {1, 1, 1} for a mesh not registered or
// unreadable (the emission as the store gives it).
struct EmissionTexelQuery {
    uint32_t mesh = 0; // (rt::MeshId)
    std::array<uint32_t, 3> corner{};
    bool lamp_texels = false;
};
[[nodiscard]] std::vector<std::array<float, 3>> emission_texels(std::span<const EmissionTexelQuery> queries);
// (round EMIT) Whether a material is a light card (rt_lights.h light_card: an additive first pass whose texture doesn't
// move - a halo, light cone, pool or starburst the artists drew round a lamp).
[[nodiscard]] bool light_card_material(const MaterialRef* material);

// draw_world, before anything is drawn: the record cleared and filled - instances (meshes and materials registered,
// changed ones reported) and lights.
void record_frame();
// draw_viewport, the world's camera chosen: the viewport's camera added; the frame's first: begin_frame.
void camera_chosen(int viewport);
// (the one lighting path - the objects' shadows) An instance drawn lit by the game's lights (Singleton::render, as it
// sends them - pc_instance.cpp): its lights kept for its instances in the next record (rt_scene.h ObjectLight; the
// record is made before the frame's draws, so with the lights of the draw before - a frame's lag, as the skinned
// meshes' geometry).
void object_lit(const Singleton* instance, const rt::ObjectLight& light);
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
// (added for the relit level's writes) A vertex buffer destroyed (VertexBuffer::teardown): whatever was kept for it
// forgotten - another buffer made later at the same address is a new one.
void vertex_buffer_released(const VertexBufferData* buffer);
// The device's buffers released (release_buffers, before a reset): every mesh removed.
void forget_meshes();

// (the console's `rt probe <x> <y>`) A ray from the first viewport's camera through the point of its picture (0 - 1
// across and down) traced: what it hits - the instance's kind, its mesh, its material (passes, reflectivity, class) -
// and the relit world's vertices round the hit (rt_relit_world::describe_near).
[[nodiscard]] std::string describe_screen_point(float across, float down);
// (the light energy audit's census - the console's `rt census`) Every mesh of the loaded scenes by what lights it: the
// sky's, hidden, not traced, or what the relit mode makes of it (rt_relit_world::census_bucket) - with its material's
// kind (opaque, cut-out, blended, additive): meshes, vertices and example materials per bucket, a line each.
[[nodiscard]] std::string world_census();
// (round P10 - objects relit) The frame's objects by how they are drawn: lit by the game's lights, with their own colours
// (animated, moving, standing still - placed once or drawn at several places) - the relit bake logs it.
// `detail`: a line for each placed object (where, its meshes' materials, vertices and colours' brightness).
[[nodiscard]] std::string object_census(bool detail);

// While one lives, vertex buffer writes are colour-only: the positions are as they were, so no mesh's BLAS is built
// again - the rule for every write that changes only colours (MeshRef::apply_vertex_colors' colour animations, the relit
// mode's writes, the scene lights' PcGeom::change_colors). `relit` kTell: the game's own new colours over a buffer the
// relit mode may own - it is told (rt_relit_world::buffer_written: written again) - unlike the relit mode's own writes
// (they must never queue themselves again) and the colour animations (vertices the relit mode leaves alone).
class ColourWrite {
public:
    enum class Relit : uint8_t { kQuiet, kTell };
    explicit ColourWrite(Relit relit = Relit::kQuiet);
    ~ColourWrite();
    ColourWrite(const ColourWrite&) = delete;
    ColourWrite& operator=(const ColourWrite&) = delete;
    ColourWrite(ColourWrite&&) = delete;
    ColourWrite& operator=(ColourWrite&&) = delete;

private:
    Relit relit_;
};

} // namespace rt_record
} // namespace render
