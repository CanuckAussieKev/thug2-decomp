#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/pc_model.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace script {
class Struct;
}

// The engine's model (render::RenderModel; the PC model PcModel extends it): a list of geoms - each loaded from a model file
// (with its own scene and textures), taken from the asset manager or added as it is - drawn together with a display
// matrix and a skeleton's bones, shown or hidden as a whole or by geom, coloured, scaled, given a shadow, lights and
// bone volumes.
namespace render {

// One of a model's geoms (0x18 bytes; what a node of the model's geom list holds).
struct ModelGeom {
    void* geom;            // +0x00: the engine's geom (CGeom; on PC a PcGeom)
    uint8_t* textures;     // +0x04: the scene's texture dictionary (its +4; null for a geom added as it is)
    uint32_t name;         // +0x08: the geom's name (a checksum; 0: none)
    uint8_t* scene;        // +0x0C: the scene loaded from a model file (the model's own; null otherwise)
    uint32_t asset_name_;    // +0x10: the asset manager's scene (its checksum; 0 otherwise)
    uint8_t* bone_volumes_;  // +0x14: the bone volumes (the first geom's only)
};
static_assert(offsetof(ModelGeom, asset_name_) == 0x10 && sizeof(ModelGeom) == 0x18,
              "layout must match the original");

// RenderModel::flag_bits_ (the flag setters take a byte and keep its low bit - the original's shifts and masks)
enum : uint8_t {
    kModelSphereValid = 0x01, // enclosing_ball_ holds the platform's
    kModelEnabledBit = 0x02,
    kModelHidden = 0x04,
    kModelScaled = 0x08,       // size_factor_ isn't (1, 1, 1)
    kModelShadow = 0x10,       // its geoms cast a shadow (each geom told as it comes)
    kModelShadowVolume = 0x20, // its model files loaded with their shadow volumes
};

// The checksum of "all" - a geom name matching every geom.
constexpr uint32_t kAllGeoms = 0xC4E78E22;

// The model (0x4C bytes; vtable 0x00649348 - PcModel's 0x0064A4B4 replaces [0], [12] and [13]). Its virtual slots:
// [0] destroy, [1] the skeleton's bones allotted (0x005BC7F0: true), [2] a bone flag for upload_skinning_palette (0x0049F4A0:
// 0), [3] whether a display mode may be set (0x005BC7F0: true), [4] 0x00492DC0 (true; nothing calls it), [5] - [7]
// 0x005BC7F0, [8] 0x0048DD50, [9] 0x004549C0 (all true), [10] / [11] before / after its textures are replaced
// (0x005B9760: true), [12] the platform's bounding sphere, [13] the platform's bounding sphere set (0x005BAB90:
// nothing).
class RenderModel {
public:
    // 0x00494C40: construct: an empty geom list, display mode 6 (if the platform agrees), active, not hidden,
    // scale 1, no lights, the file name "none". Returns this. (visible_geoms_ and the geom counts by kind are left
    // as they were.)
    RenderModel* make_new();
    // 0x00494D00: the destructor's body: the lights deleted, the bone volumes released (0x0048B400), the geoms
    // cleared, the list's nodes erased and its head freed.
    void destroy_body();
    // 0x00494DC0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    RenderModel* teardown(unsigned int flags);

    // 0x00492DC0 (virtual [4]): true (the four arguments unused).
    bool virtual_true4(int a, int b, int c, int d);
    // 0x00492EB0 (virtual [12]): the default bounding sphere: (0, 36, 0) with a radius of 48 for a model with bones,
    // else 1200. Returns out.
    maths::Vec4* plat_get_bounding_sphere(maths::Vec4* out);

    // 0x00492DD0: the skeleton's bones taken once (their count, the platform told - virtual [1] -, the matrices).
    // Always true.
    bool choose_rig(uint8_t* skeleton);
    // 0x00492E10: the display mode (0 normal, 1 the skeleton drawn, 5 a debug box; 6 when made) set if it's new and
    // the platform agrees (virtual [3]). Whether it was set.
    bool set_display_mode(int mode);
    // 0x00492E40: how many geoms.
    int fetch_geom_total();
    // 0x00492E50 / 0x00493460: the bone matrices; set (each geom pointed at them, with the bone count).
    uint8_t* fetch_skinning_palette();
    void upload_skinning_palette(uint8_t* matrices);
    // 0x00492E60: drawn scaled or not.
    void allow_resizing(uint8_t on);
    // 0x00492E80: the bounding sphere set (the platform's, virtual [13]); the cached one forgotten.
    void change_bound_sphere(const maths::Vec4* sphere);
    // 0x00492E90: model files loaded with their shadow volumes or not.
    void activate_shadow_volume(uint8_t on);
    // 0x00492EE0: the scale (scaled unless it's exactly 1, 1, 1). Always true.
    bool change_scale(const maths::Vec4* scale);
    // 0x00492F30: the bounding sphere (the platform's, virtual [12], cached). Returns out.
    maths::Vec4* fetch_bound_sphere(maths::Vec4* out);
    // 0x00492F90 / 0x00492FC0: the bone volumes (the first geom's): set unless it has some; read (null if none).
    void set_bone_volumes(uint8_t* volumes);
    uint8_t* get_bone_volumes();
    // 0x00492FF0 / 0x00493040: with exactly one geom from the asset manager, that scene's +0xB8 (0x00490AC0) / +0xB4
    // (0x00490AB0) - the bone count and the hierarchy; 0 otherwise.
    uint32_t get_asset_bone_count();
    uint32_t get_asset_hierarchy();
    // 0x00493090: each geom made active (the model's display mode 0, active and the geom visible) and given the
    // display matrix (scaled when scaling is on) - with the skeleton's bones in mode 0 unless there's no skeleton or
    // `no_bones`; mode 1 draws the skeleton (0x004A31E0), mode 5 / above 6 a debug box (compiled out). Always true.
    bool render(maths::Mat4* matrix, bool no_bones, uint8_t* skeleton);
    // 0x00493410: the skeleton's bone matrices copied to each geom. Always true.
    bool copy_bones(uint8_t* skeleton);
    // 0x004934A0: a geom made from the asset manager's scene `assetName` (named `name`) and added. The node comes
    // from the default heap with `default_heap`, else the pool heap (0x00658555: none - false); the geom made the
    // same way (none - false, only without `default_heap`). True when added.
    bool add_asset_geom(uint32_t asset_name, uint32_t name, bool multicolor, bool default_heap);
    // 0x004935E0: a geom made from a model file `fileName` and added: through the asset manager (`via_resource_store`:
    // its .tex then the file, waited for unless `by_name` - then only looked up), else the scene loaded with its own
    // textures (0x0049F180 / 0x0048C3D0). Nodes and geom as add_asset_geom. True when added.
    bool add_file_geom(const char* file_name, uint32_t name, bool via_resource_store, int tex_dict_offset, bool force_tex_dict,
                     bool multicolor, bool default_heap, bool by_name);
    // 0x004938E0: an existing geom added as it is (named `name`). True when added (false: no node).
    bool add_geom(void* geom, uint32_t name, bool default_heap);
    // 0x004939E0: a line tested against the bone volumes of each geom that has some (0x0048B580) - the hit nearest
    // (within 0.01 of nothing: the first, then nearer ones); its extra result into *out (when given). The hit.
    int test_bone_volumes(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t* out);
    // 0x00493AB0: the first geom whose bone volumes give a value for the bone (0x0048BC40), that value; else 0.
    float get_bone_volume(uint32_t a, uint32_t b, uint32_t bone);
    // 0x00493B30 / 0x00493B90 / 0x00494B30: the strip-break flags of the geoms' scenes (+0xB0) together; each geom's
    // strips broken by a mask; by the scenes' own flags (always true).
    uint32_t get_strip_break_flags();
    void break_strips(uint32_t mask);
    // 0x00494700 (not a Ghidra function; what 0x0048A220 calls on the model at 0x006FA2F8): each geom's instance
    // strips cleaned (the geom's [4], through 0x00487D50).
    void clean_instance_strips();
    bool break_own_strips();
    // 0x00493BD0 / 0x00493C10: lights made (0x0048EA80) and given to each geom; deleted (0x0048C080) and taken away.
    // Always true.
    bool create_lights();
    bool destroy_lights();
    // 0x00493C60: active or not (each geom told when it changes); not while hidden. False only for hidden + active.
    bool change_live(uint8_t active);
    // 0x00493CD0 / 0x00494640 / 0x00494680 / 0x004946C0: the geom named `name`, geom `index`, its name and its
    // textures (null / 0 past the end).
    void* get_geom_by_name(uint32_t name);
    void* fetch_geom(int index);
    uint32_t fetch_part_label(int index);
    uint8_t* get_geom_textures(int index);
    // 0x00493D10: texture `source` (".png" added when missing) replaced by `dest` (cut at ".png") in the textures of
    // the geom `name` (all, or ones without a name) - (0x0049FB20). Whether any was; script integer 0x2A648514 read
    // when none (a debug leftover).
    bool swap_texture(uint32_t name, const char* source, const char* dest);
    // 0x00493E80: the same with the texture dictionary's texture `dest` (0x0049F540, 0x0049F800); only geoms named
    // `name` (or all).
    bool replace_texture_with(uint32_t name, const char* source, uint32_t dest);
    // 0x00493FB0: the colour of the geom `name` (or all) cleared. Whether any.
    bool clear_geom_color(uint32_t name);
    // 0x00494010: the geoms `name` (or all) that aren't multicolour coloured from hue / saturation / value. Whether
    // any.
    bool set_geom_color_hsv(uint32_t name, float h, float s, float v);
    // 0x00494110 / 0x00494160 / 0x004941B0: each geom's material pass given UV offsets, a UV matrix, UV matrix
    // parameters (0x0048DFE0 / 0x0048DFF0 / 0x0048E000). Whether any geom did.
    bool choose_uv_shift(uint32_t material, int pass, float u, float v);
    bool choose_uv_matrix(uint32_t material, int pass, const maths::Mat4* matrix);
    bool create_uv_anim_params(uint32_t material, int pass);
    // 0x00494200: a script's colour (h, s, v): the geoms `name` (or all) that aren't multicolour coloured; the
    // multicolour ones' material passes (an array of structures of a material and a pass - materials=, else
    // material= with an array or one pass=) instead. Whether any.
    bool set_color_from_script(script::Struct* params, float h, float s, float v);
    // 0x00494450: the same, the colour cleared - the multicolour passes given 0x80808080.
    bool clear_color_from_script(script::Struct* params);
    // 0x00494730: the geom named `name` deleted and taken out of the list (its scene too, if it's the model's).
    void detach_geom(uint32_t name);
    // 0x00494800 / 0x00494880: the geom `name` (or all) hidden or shown (visible_geoms_; whether any matched); whether
    // it's hidden (false when a geom of that name is shown).
    bool conceal_mesh(uint32_t name, bool hide);
    bool is_part_concealed(uint32_t name);
    // 0x004948D0: every geom coloured. Always true.
    bool change_tint(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    // 0x00494930 / 0x00494970: the geoms' viewport masks together; each geom's set. (Always true.)
    uint32_t fetch_shown();
    bool toggle_shown(uint32_t mask);
    // 0x004949B0: shadows on or off (each geom told).
    void activate_shadow(uint8_t on);
    // 0x00494A00: the face texture's image (0xB00B0DC0) and overlay made into textures, the overlay applied in the
    // profile's skin colour (0x00486F70), then the image replaces `source` in the geom `name`; both textures taken out
    // again. Whether it was replaced.
    bool replace_face_texture(void* face, const char* source, uint32_t name);
    // 0x00494B70: hidden or not - and made inactive or active (change_live).
    bool conceal(uint8_t hide);
    // 0x00494BA0: every geom deleted (their scenes too, if the model's) and the list emptied.
    void drop_geoms();

    void** vtable;                // +0x00
    int display_mode_;            // +0x04
    int bone_total_;               // +0x08
    uint8_t* skin_palette_;      // +0x0C
    ModelGeomNode* geoms_;      // +0x10: the list's head (a ring of nodes: std::list)
    uint32_t geom_list_size_;      // +0x14
    uint32_t visible_geoms_;      // +0x18: a bit per geom (in list order, modulo 32)
    maths::Vec4 size_factor_;          // +0x1C
    uint8_t* lights_ptr_;           // +0x2C
    uint32_t source_path_;          // +0x30: the first geom's (a checksum; "none" when made)
    maths::Vec4 enclosing_ball_; // +0x34 (with kModelSphereValid)
    uint8_t flag_bits_;              // +0x44
    uint8_t pad45;
    int16_t geom_total_;           // +0x46
    int16_t num_file_geoms_;       // +0x48: geoms with a scene of their own
    int16_t num_asset_geoms_;      // +0x4A: geoms with the asset manager's scene
};
static_assert(offsetof(RenderModel, geoms_) == 0x10 && offsetof(RenderModel, size_factor_) == 0x1C &&
                  offsetof(RenderModel, lights_ptr_) == 0x2C && offsetof(RenderModel, enclosing_ball_) == 0x34 &&
                  offsetof(RenderModel, flag_bits_) == 0x44 && offsetof(RenderModel, num_asset_geoms_) == 0x4A &&
                  sizeof(RenderModel) == 0x4C,
              "layout must match the original");

} // namespace Nx
