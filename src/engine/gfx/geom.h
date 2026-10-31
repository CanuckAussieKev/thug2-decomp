#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

// The PC geom (Nx engine): a sector's or a model's renderable geometry - its meshes, or an instance of another
// geom's meshes (a model drawn with its own transform and bones) - and what the engine asks of it: colours,
// visibility, position, orientation and scale, the vertices and their colours, UV wibbles.
namespace render {

struct MeshRef;
struct SceneRef;

// An instance (0x64 bytes, constructor 0x004D1030): a mesh list drawn with a transform of its own and bone matrices,
// on the instances' list (0x007825C0).
struct Singleton {
    uint32_t flags;          // 1: owns its mesh list and bones; 2: lit (every mesh asks for it; set by the
                             // constructor); 4: coloured
    maths::Mat4 transform;   // +0x04
    maths::Mat4* bones;      // +0x44
    int bone_count_;           // +0x48
    bool active;             // +0x4C
    uint8_t color[4];        // +0x4D: RGBA (0x80 each: unchanged)
    uint8_t pad51[3];
    void* model;             // +0x54
    SceneRef* scene;           // +0x58: its mesh list (the bounding sphere at +0x64)
    Singleton* next;         // +0x5C
    uint32_t viewports;      // +0x60: a bit per viewport it shows in (all: -1)

    // 0x004D1030: construct over `scene` (its mesh list) with a copy of `transform` and the bones (not copied);
    // active, on the instances' list (0x007825C0). Lit (2) when the bones are not drawn by the vertex shader and
    // every mesh asks for lighting (+0x38 bit 0x10) and has no flag 0x20000. Returns this.
    Singleton* make_new(SceneRef* scene, const maths::Mat4* transform, int bone_count, maths::Mat4* bones);
    // 0x004D10C0: the destructor's body - its mesh list and bones freed when it owns them (1), off the list.
    void destroy_body();
    // 0x004D1130: drawn (draw_scene's flags, always with 8: no culling) - skinned by the vertex shader (bones, and
    // the scene's +0x78 not positive: the bones in 0x007825B0 / B4, the instance in B8, the lights set once per
    // set of bones), one draw per bone (up to 58: each mesh shown, 0x40, when its +0x1F is that bone; the bone
    // times the transform as the world), or once with its transform; lit with its model's lights or the scene's.
    void render(uint32_t flags);
};
static_assert(offsetof(Singleton, bones) == 0x44 && offsetof(Singleton, color) == 0x4D &&
                  offsetof(Singleton, scene) == 0x58 && sizeof(Singleton) == 0x64,
              "layout must match the original");

// 0x004D16B0: an instance drawn on its own (flag 8: even out of view or occluded), the world transform put back.
void __cdecl draw_one_instance(Singleton* instance, uint32_t flags);
// 0x004D1760: the active instances shown in `viewport`, with opaque (flags 1) or transparent (2) meshes, in view
// and not occluded: sorted (compare_instances), the vertex-shader-skinned ones first (transparent ones with 0x80),
// then the others (transparent ones with 0x100; 0x00786E60 set for those near the camera), lit by lights 0 and 1.
void __cdecl draw_instance_batch(uint32_t flags, int viewport);
// 0x004D0F40 (qsort's, over instance pointers): the vertex-shader-skinned ones first, by their bones (descending)
// then their transparent mesh counts; the others by their transparent mesh counts, then their first transparent
// meshes' materials' sort keys (+0x1C).
int __cdecl compare_instances(const void* a, const void* b);

// The nodes of a geom's load list (lists::Elem over its meshes: vtable 0x0064A434; the list's head, data -1:
// 0x0064A438), laid out as ListNode.
struct MeshListNode : memory::ListNode {
    // 0x004B5EC0: the destructor's body - out of its list (linked to itself).
    void destroy_body();
    // 0x004B5E50 (the node's virtual [0]) / 0x004B5E90 (the head's): destroyed, and freed with flags & 1. Return
    // this.
    MeshListNode* teardown(unsigned int flags);
    MeshListNode* destroy_head(unsigned int flags);
};

// The PC geom (0x64 bytes, vtable 0x0064A368; the engine's CGeom - constructor 0x0048DD60, vtable 0x00648EC8,
// destructor body 0x0048E130 - for its first 0x14 bytes).
struct PcGeom {
    void** vtable;
    uint8_t base[0x10];
    maths::BBox bounds;        // +0x14 (as loaded)
    uint8_t* scene;          // +0x34: the PC scene it's in
    memory::ListNode* load_list_;      // +0x38: the meshes while it loads (a list's head), made into `meshes` after
    MeshRef** meshes;          // +0x3C
    uint32_t mesh_count_;      // +0x40
    uint32_t visibility;     // +0x44: a viewport mask (0xDEADBEEF until set)
    bool active;             // +0x48
    uint8_t pad49[3];
    Singleton* instance;     // +0x4C: an instance's (null for a sector's geometry)
    uint8_t* source;         // +0x50: the mesh (0xC8 bytes) it's an instance of; +0xBC / +0xC0 its strip breaks
    maths::Vec4 scale;       // +0x54

    // 0x004B4460: construct - no meshes, no instance, active, visible to all, scale 1. Returns this.
    PcGeom* make_new();
    // 0x004B5EE0: the destructor's body (the meshes, then the instance, freed).
    void destroy_body();
    // 0x004B5FC0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    PcGeom* teardown(unsigned int flags);

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

    // 0x004B58C0 (virtual [2]): a copy - its meshes cloned (sharing buffers with `share`), else, without `share`,
    // into `scene`'s mesh list instead of these ones, which stay; with `share` an instance of its own over a new
    // mesh list.
    PcGeom* replicate(bool share, uint8_t* scene);
    // 0x004B5B80 (virtual [1]): a copy of an instance - its own scene and mesh list, the meshes cloned, a new
    // instance with bones for each of `model`'s (+8).
    PcGeom* clone_instance(bool share, uint8_t* model);
    // 0x004B54B0 (virtual [3]): made an instance of `mesh` for `model` (its bones: +8 the count, +0xC the matrices).
    bool init_instance(uint8_t* mesh, uint8_t* model, int unused);
    // 0x004B45F0 (virtual [4]): the degenerate runs in the instance meshes' strips shortened (0x004D57D0).
    void clean_instance_strips();
    // 0x004B48B0 (virtual [6]): a colour (RGBA); 0x80/0x81, 0x80, 0x80: none (wipe_tint).
    void change_tint(uint32_t rgba);
    // 0x004B4B20 (virtual [7]): the colour (its first mesh's; alpha 0), into *out.
    uint32_t* fetch_tint(uint32_t* out);
    // 0x004B4970 (virtual [8]): no colour.
    void wipe_tint();
    // 0x004B49C0 (virtual [9]): the colour of a material's pass (the instance's meshes'). Whether any was set.
    bool change_material_tint(uint32_t checksum, int pass, uint32_t rgba);
    // 0x004B4870 (virtual [11]) / 0x004B4120 (virtual [12]): the viewport mask (each mesh's +0x1B gets its byte);
    // read back as 0xFFFFFF00 | its low byte.
    void toggle_shown(uint32_t mask);
    uint32_t fetch_shown();
    // 0x004B4630 (virtual [13]) / 0x004B4690 (virtual [14]): active (the meshes' flag 0x40), the instance's if any.
    void change_live(bool active);
    bool is_operative();
    // 0x004B4730 (virtual [16]) / 0x004B4760 (virtual [17]): the instance's bounding sphere (none: radius 10000).
    void change_bound_sphere(const maths::Vec4* sphere);
    maths::Vec4* fetch_bound_sphere(maths::Vec4* out);
    // 0x004B55D0 (virtual [18]) / 0x004B46A0 (virtual [19]): the position - the instance's, or every mesh moved;
    // read back from the instance or the first mesh's transform (a static vector, 0x00701610).
    void place_at_absolute_point(const maths::Vec4* position);
    maths::Vec4* get_absolute_point();
    // 0x004B5650 (virtual [20]) / 0x004B56E0 (virtual [21]): the instance's rotation rows; read back with no
    // position (a static matrix, 0x00701628; the identity without an instance).
    void change_facing(const maths::Mat4* orientation);
    maths::Mat4* fetch_facing();
    // 0x004B57C0 (virtual [22]): `turns` quarter turns about y (the instance's rotation, or every mesh's vertices).
    void rotate_y(int turns);
    // 0x004B4DB0 (virtual [25]) / 0x004B4ED0 (virtual [26]): the scale - each mesh's vertices scaled about its
    // transform's position by it.
    void change_scale(const maths::Vec4* scale);
    maths::Vec4* fetch_magnification(maths::Vec4* out);
    // 0x004B47B0 (virtual [28]): the instance's transform and `count` bone matrices copied. Always true.
    bool upload_skinning_palette(const maths::Mat4* transform, const maths::Mat4* bones, int count, int unused3,
                         int unused4);
    // 0x004B47F0 (virtual [29]): `count` bone matrices copied into the instance's.
    void copy_bones(const maths::Mat4* bones, int count);
    // 0x004B4820 (virtual [30]): the instance's bones pointed at `bones`.
    void set_bones(maths::Mat4* bones, int count);
    // 0x004B4840 (virtual [31]): the instance's strips broken where the source mesh says (SceneRef::break_strips).
    // Always true.
    bool break_strips(uint32_t mask);
    // 0x004B4130 (virtual [36]): the vertices of all the meshes.
    uint32_t fetch_vertex_tally();
    // 0x004B4BB0 (virtual [37]) / 0x004B4C50 (virtual [39]): the vertices (x, y, z, 1 each), read and written -
    // each mesh's bounding sphere made again from the box of its new vertices.
    void get_vertices(maths::Vec4* out);
    void set_vertices(const maths::Vec4* in);
    // 0x004B4160 (virtual [38]) / 0x004B4210 (virtual [40]): the vertex colours (RGBA; the buffers hold BGRA) read
    // from the first vertex buffers and written to all.
    void fetch_tints(uint8_t* out);
    void change_colors(const uint8_t* in);
    // 0x004B4300 (virtual [42]): the UV wibble of each mesh's first pass that has one.
    void set_uv_wibble_params(float u_vel, float u_amp, float u_freq, float u_phase, float v_vel, float v_amp, float v_freq,
                           float v_phase);
    // 0x004B4EF0 (virtual [43]): every instance pass with a UV wibble set to the explicit one (flag 0x4000).
    void enable_explicit_uv_wibble(int unused);
    // 0x004B43E0 (virtual [45]): the UV offsets of each mesh's first pass with a wibble (then explicit, [43]).
    void set_uv_wibble_offsets(float u, float v);
    // 0x004B4F60 (virtual [46]): an instance pass's UV matrix (a wibble made for it if it has none; the mesh's
    // vertex shader swapped for its UV-transforming version). Always true.
    bool choose_uv_matrix(uint32_t checksum, int pass, const maths::Mat4* matrix);
    // 0x004B5130 (virtual [47]): a UV wibble made for an instance pass that has none. Whether one was.
    bool create_uv_anim_params(uint32_t checksum, int pass);
    // 0x004B52A0 (virtual [48]): the meshes' texture animations started again (not for an instance).
    void reset_texture_anims();
};
static_assert(offsetof(PcGeom, bounds) == 0x14 && offsetof(PcGeom, scene) == 0x34 &&
                  offsetof(PcGeom, meshes) == 0x3C && offsetof(PcGeom, instance) == 0x4C &&
                  offsetof(PcGeom, scale) == 0x54 && sizeof(PcGeom) == 0x64,
              "layout must match the original");

extern maths::Mat4* g_palette_base;
extern Singleton* g_current_instance;

} // namespace Nx
