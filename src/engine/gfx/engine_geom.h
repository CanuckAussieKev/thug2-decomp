#pragma once

#include <span>
#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/packed_colour.h"
#include "engine/maths/vec2.h"
#include "engine/maths/vec4.h"

namespace maths {
class BBox;
class Mat4;
class Vec4;
} // namespace maths

// The engine's geom (render::Geom - the platform-independent base of the PC geom, PcGeom in geom.h, its first 0x14
// bytes): what a geom keeps for itself - how it was made, its collision faces, a copy of its vertex colours, whether it
// is multicoloured - and the platform's virtual functions, all stubs here ("STUB: Plat...", vtable 0x00648EC8) that
// PcGeom overrides.
namespace render {

class RenderModel;
class SubMesh;
struct WorldScene;

// What a geom is (the level editor only works on the PC's).
enum class GeomKind : uint8_t {
    kEngine, // the engine's base (only stubs)
    kPc,     // PcGeom
};

class Geom : public memory::HeapObject {
public:
    // 0x0048DD60: nothing kept.
    Geom() = default;
    // 0x0048E130 (0x0048E220 the scalar deleting destructor, vtable slot 0): the colours' copy freed (not cleared).
    virtual ~Geom();
    Geom(const Geom&) = delete;
    Geom& operator=(const Geom&) = delete;

    // What it is (NOT ORIGINAL: the original compared the vtable).
    [[nodiscard]] virtual GeomKind kind() const { return GeomKind::kEngine; }

    // 0x0048DD80: a copy (plat_replicate) - made with `instance` 1, else 2; given this one's collision faces (when it
    // has some), no colours (the low byte of `instance` tested, its whole word passed on). The copy (or null).
    Geom* replicate(int instance, WorldScene* scene);
    // 0x0048DDC0: a copy for a model (clone_instance) - made with `instance` 1, else 2; no colours. The copy (or
    // null).
    Geom* clone_for_model(int instance, RenderModel* model);
    // 0x0048DDF0: made an instance (1) of a mesh for a model, multicoloured or not (a jump to plat_init_instance -
    // the callee's arguments: `multicolour` is its third).
    bool init_instance(SubMesh* mesh, RenderModel* model, int multicolour);
    // 0x0048DE10: the colour (plat_get_color) into `out`. Returns out.
    uint32_t* fetch_tint(uint32_t* out);
    // 0x0048DE90 (not a Ghidra function): the bounding sphere (plat_get_bounding_sphere) into `out`. Returns out.
    maths::Vec4* fetch_bound_sphere(maths::Vec4* out);
    // 0x0048DF10 (folded with other classes' setters of the word at +8): the collision faces.
    void choose_tri_source(void* faces);
    // 0x0048E150: the vertex colours (fetch_vertex_tally of them) copied to `out` - from a copy kept
    // (plat_get_render_colors, made the first time with memory::alloc_block on the front end's heap).
    void fetch_tints(std::span<uint32_t> out);

    // The platform's virtual functions (vtable slots 1 - 48; the base's bodies print their "STUB: ..." and do nothing
    // - or return what the comment says). The slots the same in both tables are plain methods below.
    // [1] / [2] 0x0049E8E0 (folded with WorldCell::plat_clone): null.
    virtual Geom* clone_instance(int instance, RenderModel* model);
    virtual Geom* plat_replicate(int instance, WorldScene* scene);
    // A UV wibble: for u and v, the scroll velocity and the wave's amplitude, frequency and phase.
    struct UvWibble {
        float u_vel_{};
        float u_amp_{};
        float u_freq_{};
        float u_phase_{};
        float v_vel_{};
        float v_amp_{};
        float v_freq_{};
        float v_phase_{};
    };

    virtual bool plat_init_instance(SubMesh* mesh, RenderModel* model, int multicolour);     // [3] 0x0048DA90: true
    virtual void clean_instance_strips() {}                                        // [4] (do_nothing)
    virtual void change_tint(uint32_t rgba);                                       // [6] 0x0048DAC0 PlatSetColor
    virtual uint32_t* plat_get_color(uint32_t* out);                               // [7] 0x0048E040: 0
    virtual void wipe_tint();                                                      // [8] 0x0048DAD0 PlatClearColor
    virtual bool change_material_tint(uint32_t checksum, int pass, colour::Packed tint); // [9] 0x0048DAE0: false
    virtual void toggle_shown(uint32_t mask);                                      // [11] 0x0048DB00 PlatSetVisibility
    virtual uint32_t fetch_shown();                                                // [12] 0x0048DB10: 0
    virtual void change_live(bool active);                                         // [13] 0x0048DB20 PlatSetActive
    virtual bool is_operative();                                                   // [14] 0x0048DB30: false
    virtual maths::BBox* fetch_bounds();                               // [15] 0x0048DB40: a static empty box
    virtual void change_bound_sphere(const maths::Vec4* sphere);       // [16] 0x0048DC30
    virtual maths::Vec4* plat_get_bounding_sphere(maths::Vec4* out);   // [17] 0x0048E0A0: 0, radius 10000
    virtual void place_at_absolute_point(const maths::Vec4* position); // [18] 0x0048DBB0
    virtual maths::Vec4* get_absolute_point();                         // [19] 0x0048E0D0: a static vector
    virtual void change_facing(const maths::Mat4* orientation);        // [20] 0x0048DBC0
    virtual maths::Mat4* fetch_facing();                               // [21] 0x0048E1C0: a static matrix
    virtual void rotate_y(int turns);                                  // [22] 0x0048DBD0
    virtual void change_scale(const maths::Vec4* scale);               // [25] 0x0048DBF0
    virtual maths::Vec4* fetch_magnification(maths::Vec4* out);        // [26] 0x0048E100: 0
    virtual void set_model_lights(void* lights);                       // [27] 0x0048DC00
    virtual bool upload_skinning_palette(const maths::Mat4* transform, const maths::Mat4* bones, int count, int a,
                                         int b);        // [28] 0x0048DC10 PlatRenderSetup: true
    virtual void copy_bones(const maths::Mat4*, int) {} // [29] (ignores its two arguments)
    virtual void set_bones(maths::Mat4*, int) {}        // [30] (the same)
    virtual bool break_strips(uint32_t mask);           // [31] 0x0048DC40 PlatHidePolys: true
    virtual uint32_t plat_get_num_render_polys();       // [33] 0x0048DC80: 0
    virtual uint32_t fetch_vertex_tally();              // [36] 0x0048DC60: 0
    virtual void get_vertices(std::span<maths::Vec4> out); // [37] 0x0048DC70
    virtual void plat_get_render_colors(std::span<uint32_t> out); // [38] 0x0048DCA0
    virtual void set_vertices(std::span<const maths::Vec4> in); // [39] 0x0048DCB0
    virtual void change_colors(std::span<const uint32_t> in); // [40] 0x0048DCC0
    virtual void set_uv_wibble_params(const UvWibble& wibble);                             // [42] 0x0048DCE0
    virtual void enable_explicit_uv_wibble(int on);                                             // [43] 0x0048DCF0
    virtual bool set_material_uv_wibble_offsets(uint32_t checksum, int pass, float u, float v); // [44] 0x0048DD10
    virtual void set_uv_wibble_offsets(maths::Vec2 offsets);                                       // [45] 0x0048DD00
    virtual bool choose_uv_matrix(uint32_t checksum, int pass, const maths::Mat4* matrix); // [46] 0x0048DD30: false
    virtual bool create_uv_anim_params(uint32_t, int) { return true; }                     // [47] (return_true2)
    virtual void reset_texture_anims() {}                                                  // [48] (do_nothing)

    // The slots the same in both tables (the base's bodies, or shared stubs).
    uint32_t plat_get_checksum();                                                  // [5] 0x0048DAB0: 0
    uint32_t* plat_get_material_color(uint32_t* out, uint32_t checksum, int pass); // [10] 0x0048E070: 0
    void plat_set_transform(const maths::Mat4* transform);                         // [23] 0x0048DBE0
    maths::Mat4* plat_get_transform();                                             // [24] 0x0048E1F0: a static matrix
    bool set_shadow(bool) { return true; }              // [32] (SaveCard::always_true, folded)
    uint32_t plat_get_num_render_base_polys();          // [34] 0x0048DC90: 0 (not a Ghidra function)
    uint32_t get_diagnostic_count() { return 0; }       // [35] (return_zero; the scene's statistics count it)
    void play_vc_wibble_from_keyframe(int, int, int) {} // [41] (ignores its three arguments)
    void start_texture_anims(bool) {}                   // [49] (ignores its argument)
    void stop_texture_anims() {}                        // [50] (do_nothing)

    // The fields other code reads and sets.
    [[nodiscard]] void* hit_triangles() const { return hit_triangles_; }
    [[nodiscard]] uint8_t multicolour() const { return multicolour_; }

private:
    int clone_kind_ = 0;                 // +0x04: 1 an instance / a copy made with `instance`, 2 a copy without
    void* hit_triangles_ = nullptr;      // +0x08: the collision faces
    uint32_t* render_colours_ = nullptr; // +0x0C: the vertex colours' copy (fetch_tints)
    uint8_t multicolour_ = 0;            // +0x10 (init_instance's third argument's low byte)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(Geom, render_colours_) == 0xC && offsetof(Geom, multicolour_) == 0x10,
                      "layout must match the original");
    }
};

// Whether `geom` (a sector's or a model's, or null) is a PC geom (NOT ORIGINAL: the level editor's check, which
// compared the vtable).
inline bool is_pc_geom(const void* geom) { return geom && static_cast<const Geom*>(geom)->kind() == GeomKind::kPc; }


} // namespace render
