#pragma once

#include <cstddef>
#include <cstdint>

namespace maths {
class BBox;
class Mat4;
class Vec4;
} // namespace maths

// The engine's geom (render::CGeom - the platform-independent base of the PC geom, PcGeom in geom.h, its first 0x14
// bytes): what a geom keeps for itself - how it was made, its collision faces, a copy of its vertex colours, whether it
// is multicoloured - and the base's platform calls, all stubs ("STUB: Plat...", vtable 0x00648EC8) that PcGeom
// replaces. Its public calls are the dispatchers below (one jump to a slot each, linker-folded: other engine classes
// call the same addresses for their own slots).
namespace render {

class Geom {
public:
    // 0x0048DD60: construct - nothing kept. Returns this.
    Geom* make_new();
    // 0x0048E130: the destructor's body - the colours' copy freed (not cleared).
    void destroy_body();
    // 0x0048E220 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    Geom* teardown(unsigned int flags);

    // 0x0048DD80: a copy (virtual [2]) - made with `instance` 1, else 2; given this one's collision faces (when it has
    // some), no colours (the low byte of `instance` tested, its whole word passed on). The copy (or null).
    Geom* replicate(int instance, void* scene);
    // 0x0048DDC0: a copy for a model (virtual [1]) - made with `instance` 1, else 2; no colours. The copy (or null).
    Geom* clone_for_model(int instance, void* model);
    // 0x0048DDF0: made an instance (1) of a mesh for a model, multicoloured or not (a jump to virtual [3] - the
    // callee's arguments: `multicolour` is its third).
    bool init_instance(void* mesh, void* model, int multicolour);
    // 0x0048DE10: the colour (virtual [7]) into `out`. Returns out.
    uint32_t* fetch_tint(uint32_t* out);
    // 0x0048DE90 (not a Ghidra function): the bounding sphere (virtual [17]) into `out`. Returns out.
    maths::Vec4* fetch_bound_sphere(maths::Vec4* out);
    // 0x0048DF10 (folded with other classes' setters of the word at +8): the collision faces.
    void choose_tri_source(void* faces);
    // 0x0048E150: the vertex colours (virtual [36] of them) copied to `out` - from a copy kept (virtual [38], made
    // the first time with memory::alloc_block on the front end's heap).
    void fetch_tints(uint32_t* out);

    // The base's platform calls (virtual [3] - [47]; each prints its "STUB: ..." and does nothing):
    bool plat_init_instance(void* mesh, void* model, int multicolour);             // 0x0048DA90 [3]: true
    uint32_t plat_get_checksum();                                                  // 0x0048DAB0 [5]: 0
    void plat_set_color(uint32_t rgba);                                            // 0x0048DAC0 [6]
    uint32_t* plat_get_color(uint32_t* out);                                       // 0x0048E040 [7]: 0
    void plat_clear_color();                                                       // 0x0048DAD0 [8]
    bool plat_set_material_color(uint32_t checksum, int pass, uint32_t rgba);      // 0x0048DAE0 [9]: false
    uint32_t* plat_get_material_color(uint32_t* out, uint32_t checksum, int pass); // 0x0048E070 [10]: 0
    void plat_set_visibility(uint32_t mask);                                       // 0x0048DB00 [11]
    uint32_t plat_get_visibility();                                                // 0x0048DB10 [12]: 0
    void plat_set_active(bool active);                                             // 0x0048DB20 [13]
    bool plat_is_active();                                                         // 0x0048DB30 [14]: false
    maths::BBox* plat_get_bounding_box();                      // 0x0048DB40 [15]: a static empty box
    void plat_set_bounding_sphere(const maths::Vec4* sphere);  // 0x0048DC30 [16]
    maths::Vec4* plat_get_bounding_sphere(maths::Vec4* out);   // 0x0048E0A0 [17]: 0, radius 10000
    void plat_set_world_position(const maths::Vec4* position); // 0x0048DBB0 [18]
    maths::Vec4* plat_get_world_position();                    // 0x0048E0D0 [19]: a static vector
    void plat_set_orientation(const maths::Mat4* orientation); // 0x0048DBC0 [20]
    maths::Mat4* plat_get_orientation();                       // 0x0048E1C0 [21]: a static matrix
    void plat_rotate_y(int turns);                             // 0x0048DBD0 [22]
    void plat_set_transform(const maths::Mat4* transform);     // 0x0048DBE0 [23]
    maths::Mat4* plat_get_transform();                         // 0x0048E1F0 [24]: a static matrix
    void plat_set_scale(const maths::Vec4* scale);             // 0x0048DBF0 [25]
    maths::Vec4* plat_get_scale(maths::Vec4* out);             // 0x0048E100 [26]: 0
    void plat_set_model_lights(void* lights);                  // 0x0048DC00 [27]
    bool plat_render_setup(const maths::Mat4* transform, const maths::Mat4* bones, int count, int a,
                           int b);                     // 0x0048DC10 [28]: true
    bool plat_hide_polys(uint32_t mask);               // 0x0048DC40 [31]: true
    uint32_t plat_get_num_render_verts();              // 0x0048DC60 [36]: 0
    void plat_get_render_verts(maths::Vec4* out);      // 0x0048DC70 [37] (not a Ghidra function)
    uint32_t plat_get_num_render_polys();              // 0x0048DC80 [33]: 0
    uint32_t plat_get_num_render_base_polys();         // 0x0048DC90 [34]: 0 (not a Ghidra function)
    void plat_get_render_colors(uint32_t* out);        // 0x0048DCA0 [38] (not a Ghidra function)
    void plat_set_render_verts(const maths::Vec4* in); // 0x0048DCB0 [39]
    void plat_set_render_colors(const uint32_t* in);   // 0x0048DCC0 [40]
    void plat_set_uv_wibble_params(float u_vel, float u_amp, float u_freq, float u_phase, float v_vel, float v_amp,
                                   float v_freq,
                                   float v_phase);                                           // 0x0048DCE0 [42]
    void plat_use_explicit_uv_wibble(int on);                                                // 0x0048DCF0 [43]
    bool plat_set_material_uv_wibble_offsets(uint32_t checksum, int pass, float u, float v); // 0x0048DD10 [44]: false
    void plat_set_uv_wibble_offsets(float u, float v);                                       // 0x0048DD00 [45]
    bool plat_set_uv_matrix(uint32_t checksum, int pass, const maths::Mat4* matrix);         // 0x0048DD30 [46]: false
    // ([29] / [30] are stubs::ignore_arguments2, [41] stubs::ignore_arguments3, [47] stubs::return_true2 - folded.)

    void** vtable_;
    int clone_kind_;           // +0x04: 1 an instance / a copy made with `instance`, 2 a copy without
    void* hit_triangles_;      // +0x08: the collision faces
    uint32_t* render_colours_; // +0x0C: the vertex colours' copy (fetch_tints)
    uint8_t multicolour_;      // +0x10 (init_instance's third argument's low byte)
};
static_assert(offsetof(Geom, render_colours_) == 0xC && offsetof(Geom, multicolour_) == 0x10,
              "layout must match the original");

// The dispatchers: `MOV EAX,[ECX]; JMP [EAX + 4 * slot]` - the callee's arguments, whatever the class (the geom's
// slots named; the same addresses serve other engine classes: e.g. 0x0048D060 is the font's string size and the
// geom's wipe_tint, 0x0048DE80 a texture's slot 15). Naked, registered unchecked.
void virtual_slot8_jump();  // 0x0048D060 (the geom's wipe_tint, the font's read_string_param)
void virtual_slot9_jump();  // 0x0048DE30 change_material_tint
void virtual_slot10_jump(); // 0x0048E4C0
void virtual_slot13_jump(); // 0x0048DE60 change_live
void virtual_slot14_jump(); // 0x0048DE70 is_operative
void virtual_slot15_jump(); // 0x0048DE80 fetch_bounds
void virtual_slot16_jump(); // 0x0048DEB0 change_bound_sphere
void virtual_slot18_jump(); // 0x0048DEC0 place_at_absolute_point
void virtual_slot19_jump(); // 0x0048DED0 get_absolute_point
void virtual_slot25_jump(); // 0x0048DF00 change_scale
void virtual_slot27_jump(); // 0x0048DF20 SetModelLights
void virtual_slot28_jump(); // 0x0048DF30 upload_skinning_palette

} // namespace render
