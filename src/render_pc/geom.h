#pragma once

#include <span>
#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/layout_32bit.h"
#include "engine/memory/arena_alloc.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/mesh_records.h"
#include "base/maths/shape_tests.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "base/core/packed_colour.h"
#include "base/maths/vec2.h"
#include <gsl/pointers>

// The PC geom (Nx engine): a sector's or a model's renderable geometry - its meshes, or an instance of another
// geom's meshes (a model drawn with its own transform and bones) - and what the engine asks of it: colours,
// visibility, position, orientation and scale, the vertices and their colours, UV wibbles.
namespace render {

constexpr uint32_t kViewportMaskUnset = 0xDEADBEEFU; // a geom's viewport mask until one is set

struct MeshRef;
struct SceneRef;
struct PcScene;
class PcMesh;

// An instance (0x64 bytes, constructor 0x004D1030): its record (engine/gfx/mesh_records.h: flags, transform, bones,
// colour), its model and mesh list drawn with them, on the instances' list (0x007825C0).
struct Singleton : InstanceData {
    // The fields other code reads and sets (the rest: the record's, mesh_records.h).
    [[nodiscard]] RenderModel* model() const { return model_; }
    void set_model(RenderModel* value) { model_ = value; }
    [[nodiscard]] SceneRef* scene() const { return scene_; }
    [[nodiscard]] Singleton* next() const { return next_; }
    [[nodiscard]] uint32_t viewports() const { return viewports_; }

private:
    RenderModel* model_{nullptr}; // +0x54
    SceneRef* scene_;    // +0x58: its mesh list (the bounding sphere at +0x64)
    Singleton* next_;    // +0x5C
    static constexpr uint32_t kAllViewports = 0xFFFFFFFF;
    uint32_t viewports_{kAllViewports}; // +0x60: a bit per viewport it shows in (all: -1)

public:

    // 0x004D1030: construct over `scene` (its mesh list) with a copy of `transform` and the bones (not copied);
    // active, on the instances' list (0x007825C0). Lit (2) when the bones are not drawn by the vertex shader and
    // every mesh asks for lighting (+0x38 bit 0x10) and has no flag 0x20000.
    Singleton(SceneRef* list, const maths::Mat4* matrix, int bone_count, maths::Mat4* bone_matrices);
    // 0x004D10C0: the destructor's body - its mesh list and bones freed when it owns them (1), off the list.
    ~Singleton();
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    // 0x004D1130: drawn (draw_scene's flags, always with 8: no culling) - skinned by the vertex shader (bones, and
    // the scene's +0x78 not positive: the bones in 0x007825B0 / B4, the instance in B8, the lights set once per
    // set of bones), one draw per bone (up to 58: each mesh shown, 0x40, when its +0x1F is that bone; the bone
    // times the transform as the world), or once with its transform; lit with its model's lights or the scene's.
    void render(uint32_t draw_bits);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        constexpr std::size_t kSceneOffset = 0x58;
        constexpr std::size_t kBytes = 0x64;
        static_assert(core::layout_32bit(offsetof(Singleton, scene_) == kSceneOffset && sizeof(Singleton) == kBytes),
                      "layout must match the original");
    }
};

// 0x004D16B0: an instance drawn on its own (flag 8: even out of view or occluded), the world transform put back.
void draw_one_instance(Singleton* instance, uint32_t flags);
// Which instances are drawn (the pass flags) and into which viewport.
struct BatchPass {
    uint32_t flags{};
    int viewport{};
};

// 0x004D1760: the active instances shown in `viewport`, with opaque (flags 1) or transparent (2) meshes, in view
// and not occluded: sorted (compare_instances), the vertex-shader-skinned ones first (transparent ones with 0x80),
// then the others (transparent ones with 0x100; 0x00786E60 set for those near the camera), lit by lights 0 and 1.
void draw_instance_batch(BatchPass batch_pass);
// NOT ORIGINAL - the first on the instances' list (0x007825C0; null: none), for ray tracing's record (rt_record.h).
Singleton* first_instance();
// 0x004D0F40 (qsort's, over instance pointers): the vertex-shader-skinned ones first, by their bones (descending)
// then their transparent mesh counts; the others by their transparent mesh counts, then their first transparent
// meshes' materials' sort keys (+0x1C).
int compare_instances(const void* a, const void* b);

// (A geom's load list is a memory::ListNode list over its meshes: the node's destructors 0x004B5EC0 / 0x004B5E50 and
// the head's 0x004B5E90 are ListNode's uncouple / destroy.)

constexpr std::size_t kPcGeomBoundsOffset = 0x14;
constexpr std::size_t kPcGeomSceneOffset = 0x34;
constexpr std::size_t kPcGeomMeshesOffset = 0x3C;
constexpr std::size_t kPcGeomInstanceOffset = 0x4C;
constexpr std::size_t kPcGeomScaleOffset = 0x54;
constexpr std::size_t kPcGeomBytes = 0x64;

// The PC geom (0x64 bytes, vtable 0x0064A368; the engine's Geom - constructor 0x0048DD60, vtable 0x00648EC8,
// destructor body 0x0048E130 - for its first 0x14 bytes).
class PcGeom final : public Geom {
public:
    PcGeom(const PcGeom&) = delete;
    PcGeom& operator=(const PcGeom&) = delete;
    // 0x004B4460: no meshes, no instance, active, visible to all, scale 1.
    PcGeom();
    // 0x004B5EE0 (0x004B5FC0 the scalar deleting destructor): the meshes, then the instance, freed.
    ~PcGeom() override;

    [[nodiscard]] GeomKind kind() const override { return GeomKind::kPc; }

    // 0x004B5F60: an empty list (its head) for the meshes the load adds, on the top-down heap.
    void start_mesh_list();
    // 0x004B52F0: a mesh put at the end of that list (a node on the top-down heap).
    void append_mesh(MeshRef* mesh);
    // 0x004B5360: the list made into the mesh array (and freed); with `vertex_arena`, each mesh's first vertex buffer
    // moved into it (0x004D3E50). Nothing if there are meshes already.
    void make_mesh_table(void* vertex_arena);
    // 0x004B5440: the meshes taken out of the scene's mesh list, destroyed and freed, and the array.
    void free_mesh_table();
    // 0x004B44D0: the meshes counted into (`count`) or added to the scene's mesh list. Always true.
    bool add_to_mesh_list(bool count);
    // 0x004B4510: a new mesh list of its own meshes, sorted, with its bounds.
    SceneRef* create_mesh_list();

    // 0x004B58C0 (vtable slot 2): a copy - its meshes cloned (sharing buffers with `share`, the low byte of
    // `instance`), else, without `share`, into `scene`'s mesh list instead of these ones, which stay; with `share` an
    // instance of its own over a new mesh list.
    gsl::owner<Geom*> plat_replicate(int instance, WorldScene* into) override;
    // 0x004B5B80 (slot 1): a copy of an instance - its own scene and mesh list, the meshes cloned (sharing with the
    // low byte of `instance`), a new instance with bones for each of `model`'s (+8).
    gsl::owner<Geom*> clone_instance(int instance, RenderModel* model) override;
    // 0x004B54B0 (slot 3): made an instance of `mesh` for `model` (its bones: +8 the count, +0xC the matrices).
    bool plat_init_instance(SubMesh* mesh, RenderModel* model, int unused) override;
    // 0x004B45F0 (slot 4): the degenerate runs in the instance meshes' strips shortened (0x004D57D0).
    void clean_instance_strips() override;
    // 0x004B48B0 (vtable slot 6): a colour (RGBA); 0x80/0x81, 0x80, 0x80: none (wipe_tint).
    void change_tint(uint32_t rgba) override;
    // 0x004B4B20 (vtable slot 7): the colour (its first mesh's; alpha 0), into *out.
    uint32_t* plat_get_color(uint32_t* out) override;
    // 0x004B4970 (vtable slot 8): no colour.
    void wipe_tint() override;
    // 0x004B49C0 (vtable slot 9): the colour of a material's pass (the instance's meshes'). Whether any was set.
    bool change_material_tint(uint32_t checksum, int pass, colour::Packed tint) override;
    // 0x004B4870 (vtable slot 11) / 0x004B4120 (vtable slot 12): the viewport mask (each mesh's +0x1B gets its byte);
    // read back as 0xFFFFFF00 | its low byte.
    void toggle_shown(uint32_t mask) override;
    uint32_t fetch_shown() override;
    // 0x004B4630 (vtable slot 13) / 0x004B4690 (vtable slot 14): active (the meshes' flag 0x40), the instance's if any.
    void change_live(bool on) override;
    bool is_operative() override;
    // (vtable slot 15, folded with RankingsManager::get_personal_stats) its box as loaded.
    maths::BBox* fetch_bounds() override { return &bounds_; }
    // 0x004B4730 (vtable slot 16) / 0x004B4760 (vtable slot 17): the instance's bounding sphere (none: radius 10000).
    void change_bound_sphere(const maths::Vec4* sphere) override;
    maths::Vec4* plat_get_bounding_sphere(maths::Vec4* out) override;
    // 0x004B55D0 (vtable slot 18) / 0x004B46A0 (vtable slot 19): the position - the instance's, or every mesh moved;
    // read back from the instance or the first mesh's transform (a static vector, 0x00701610).
    void place_at_absolute_point(const maths::Vec4* position) override;
    maths::Vec4* get_absolute_point() override;
    // 0x004B5650 (vtable slot 20) / 0x004B56E0 (vtable slot 21): the instance's rotation rows; read back with no
    // position (a static matrix, 0x00701628; the identity without an instance).
    void change_facing(const maths::Mat4* orientation) override;
    maths::Mat4* fetch_facing() override;
    // 0x004B57C0 (vtable slot 22): `turns` quarter turns about y (the instance's rotation, or every mesh's vertices).
    void rotate_y(int turns) override;
    // 0x004B4DB0 (vtable slot 25) / 0x004B4ED0 (vtable slot 26): the scale - each mesh's vertices scaled about its
    // transform's position by it.
    void change_scale(const maths::Vec4* scale) override;
    maths::Vec4* fetch_magnification(maths::Vec4* out) override;
    // (vtable slot 27: a stub ignoring its argument) the model's lights.
    void set_model_lights(void* /*lights*/) override {}
    // 0x004B47B0 (vtable slot 28): the instance's transform and `count` bone matrices copied. Always true.
    bool upload_skinning_palette(const maths::Mat4* transform, std::span<const maths::Mat4> bones, int unused3,
                                 int unused4) override;
    // 0x004B47F0 (vtable slot 29): `count` bone matrices copied into the instance's.
    void copy_bones(const maths::Mat4* bones, int count) override;
    // 0x004B4820 (vtable slot 30): the instance's bones pointed at `bones`.
    void set_bones(maths::Mat4* bones, int count) override;
    // 0x004B4840 (vtable slot 31): the instance's strips broken where the source mesh says (SceneRef::break_strips).
    // Always true.
    bool break_strips(uint32_t mask) override;
    // (vtable slot 33: return_zero) no count of its polygons.
    uint32_t plat_get_num_render_polys() override { return 0; }
    // 0x004B4130 (vtable slot 36): the vertices of all the meshes.
    uint32_t fetch_vertex_tally() override;
    // 0x004B4BB0 (vtable slot 37) / 0x004B4C50 (vtable slot 39): the vertices (x, y, z, 1 each), read and written -
    // each mesh's bounding sphere made again from the box of its new vertices.
    void get_vertices(std::span<maths::Vec4> out) override;
    void set_vertices(std::span<const maths::Vec4> in) override;
    // 0x004B4160 (vtable slot 38) / 0x004B4210 (vtable slot 40): the vertex colours (RGBA; the buffers hold BGRA) read
    // from the first vertex buffers and written to all.
    void plat_get_render_colors(std::span<uint32_t> out) override;
    void change_colors(std::span<const uint32_t> in) override;
    // 0x004B4300 (vtable slot 42): the UV wibble of each mesh's first pass that has one.
    void set_uv_wibble_params(const UvWibble& wibble) override;
    // 0x004B4EF0 (vtable slot 43): every instance pass with a UV wibble set to the explicit one (flag 0x4000).
    void enable_explicit_uv_wibble(int unused) override;
    // (vtable slot 44, 0x00492DC0 - a shared "true" for four arguments) a material pass's UV offsets: nothing, true.
    bool set_material_uv_wibble_offsets(uint32_t /*checksum*/, int /*pass*/, float /*u*/, float /*v*/) override
    {
        return true;
    }
    // 0x004B43E0 (vtable slot 45): the UV offsets of each mesh's first pass with a wibble (then explicit, [43]).
    void set_uv_wibble_offsets(maths::Vec2 offsets) override;
    // 0x004B4F60 (vtable slot 46): an instance pass's UV matrix (a wibble made for it if it has none; the mesh's
    // vertex shader swapped for its UV-transforming version). Always true.
    bool choose_uv_matrix(uint32_t checksum, int pass, const maths::Mat4* matrix) override;
    // 0x004B5130 (vtable slot 47): a UV wibble made for an instance pass that has none. Whether one was.
    bool create_uv_anim_params(uint32_t checksum, int pass) override;
    // 0x004B52A0 (vtable slot 48): the meshes' texture animations started again (not for an instance).
    void reset_texture_anims() override;

    // The fields other code reads and sets.
    maths::BBox& bounds() { return bounds_; }
    [[nodiscard]] const maths::BBox& bounds() const { return bounds_; }
    [[nodiscard]] PcScene* scene() const { return scene_; }
    void set_scene(PcScene* value) { scene_ = value; }
    [[nodiscard]] memory::ListNode* load_list() const { return load_list_; }
    [[nodiscard]] MeshRef** meshes() const { return meshes_; }
    [[nodiscard]] uint32_t mesh_count() const { return mesh_count_; }
    void set_mesh_count(uint32_t value) { mesh_count_ = value; }
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] Singleton* instance() const { return instance_; }

private:
    maths::BBox bounds_;           // +0x14 (as loaded)
    PcScene* scene_{};             // +0x34: the PC scene it's in
    memory::ListNode* load_list_{nullptr}; // +0x38: the meshes while it loads (a list's head), made into `meshes` after
    MeshRef** meshes_{nullptr};             // +0x3C
    uint32_t mesh_count_{};       // +0x40

public:
    // NOT ORIGINAL - the meshes as a run of mesh_count_.
    [[nodiscard]] std::span<MeshRef*> mesh_run() const { return {meshes_, mesh_count_}; }
private:
    uint32_t visibility{kViewportMaskUnset}; // +0x44: a viewport mask (0xDEADBEEF until set)
    bool active_{true};               // +0x48
    std::array<uint8_t, 3> pad49{};
    Singleton* instance_{nullptr}; // +0x4C: an instance's (null for a sector's geometry)
    PcMesh* source{};                // +0x50: the mesh it's an instance of (its strip breaks)
    maths::Vec4 scale{}; // +0x54
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(PcGeom, bounds_) == kPcGeomBoundsOffset && offsetof(PcGeom, scene_) == kPcGeomSceneOffset && offsetof(PcGeom, meshes_) == kPcGeomMeshesOffset &&
                          offsetof(PcGeom, instance_) == kPcGeomInstanceOffset && offsetof(PcGeom, scale) == kPcGeomScaleOffset && sizeof(PcGeom) == kPcGeomBytes),
                      "layout must match the original");
    }
};

// The bone matrices of the instance being drawn by the vertex shader (its bones counted; empty between instances).
std::span<maths::Mat4> bone_palette();

} // namespace render
