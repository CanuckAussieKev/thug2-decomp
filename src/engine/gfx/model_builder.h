#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

// The model builder (gfx::ModelAssembler, an GameObjects::Entity, 0x54 bytes, vtable 0x00648920): builds a model from an
// appearance by running a build script on itself - the script's member functions (ModelAddGeom, GeomReplaceTexture,
// ModelApplyBoneScale, ...) each do one step on the model, its skeleton and a copy of the appearance, which are held
// in globals while Build runs. The original's table has get_build in slot 8 and the steps in slots 9-25 (the script's
// names go through invoke_method_by_name); nothing derives from the builder, so they are plain methods here.
// From the disassembly of 0x004897D0-0x0048B32D.
namespace render {
class RenderModel;
}
namespace gfx {
class Rig;
class ModelLook;

class ModelAssembler final : public objects::Entity {
public:
    // 0x004897D0: an Entity with its own vtable; `via_resource_store` and the texture dictionary offset its geoms are
    // loaded with.
    ModelAssembler(bool via_resource_store, int tex_dict_offset);
    // (0x0048A2C0, the deleting destructor: Entity's destructor body - 0x00420060 - and freed with flags & 1)
    ~ModelAssembler() override = default;

    // 0x0048A2F0 (slot 8): `model` (with `skeleton`) built from `appearance` by the build script `script`: a copy of
    // the appearance made on the top-down heap, the script run on the builder, the copy deleted.
    void get_build(ModelLook* appearance, render::RenderModel* model, Rig* skeleton, uint32_t script);
    // 0x0048A3B0: the build script's member functions (their parameters read here, then the step); anything else
    // Entity's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // --- the steps (the original's vtable slots 9-25, in order; each returns whether it did what it was asked, mostly
    // true) ---
    // 0x00489820 (slot 9): the geoms of the part in the slot `part`: with a face texture, its mesh_if_facemapped
    // alone if it has one; else each of mesh, mesh1 ... mesh4 it has.
    bool add_geom(uint32_t part);
    // 0x0048A9C0 (slot 10): the slot `part` taken out of the appearance.
    bool remove_part(uint32_t part);
    // 0x00489800 (slot 11): the model's geom `geom` hidden or shown.
    bool conceal_mesh(uint32_t geom, bool hide);
    // 0x00489A50 (slot 12): the colour of the geom of the slot `params` names (part): its own h / s / v, or the part's
    // defaults (then false; none: the colour cleared, false). PaperBag and bigfoot parts aren't coloured (false).
    bool blend_tint_over(script::Struct* params);
    // 0x0048B030 (slot 13): unless the slot `part` keeps its default UVs (no use_default_uv, or non-zero: false), the
    // material's pass gets a UV matrix: turned about the middle by uv_rot, scaled by uv_scale %, moved by uv_u / uv_v %.
    bool choose_uv_shift(uint32_t part, uint32_t material, int pass);
    // 0x00489C30 (slot 14): the material's pass given UV matrix parameters.
    bool create_uv_anim_params(uint32_t material, int pass);
    // 0x00489C50 (slot 15): the part's texture replacements (replace / with / in, then 1-6). The last one's result
    // (false: none, or no part).
    bool swap_texture(uint32_t part);
    // 0x0048ADD0 (slot 16): a part that is a CreatedGraphic: its temporary texture drawn into the cag_workspace
    // texture (scaled, turned, over the deck's base texture), which replaces the model's logo texture. The
    // replacement's result (false: no such part).
    bool set_created_texture(uint32_t part);
    // 0x00489A30 / 0x00489A40 (slots 17 / 18): the model's geoms cleared / their own strips broken. True.
    bool clear_all_geoms();
    bool detach_polys();
    // 0x0048A9E0 (slot 19): the model's scale 1 and the skeleton's bone scales reset (false: no skeleton).
    bool restore_scale();
    // 0x0048AA30 (slot 20): the appearance's body_shape applied to the skeleton's bones (false: none).
    bool apply_body_shape();
    // 0x0048AA70 (slot 21): the slot `part`'s (or else `cast_part`'s option's) x / y / z % scale applied to each bone in
    // the global array `part` (local, unless the bone is in nonlocal_bones). Always false.
    bool apply_bone_sizing(uint32_t part, uint32_t cast_part);
    // 0x0048AC40 (slot 22): the model's scale from the appearance's body type, else its body_shape, else the
    // object_scaling slot's x / y / z %. Whether it was set (true with an incomplete object_scaling too).
    bool apply_object_scale();
    // 0x0048A220 (slot 23): the model's instance strips cleaned. True.
    bool seal();
    // 0x00489F10 (slot 24): the head's colour (its own, else its part's defaults) given to the torso (unless None,
    // men only), the hands (only the Hands part), the lower legs, the shoes (bare feet only) and the body. False
    // without a head.
    bool propagate_head_color();
    // 0x0048B200 (slot 25): the script `target` held in the part's option run on the appearance until it ends. True.
    bool launch_script(uint32_t part, uint32_t target);

private:
    bool via_asset_store_; // +0x4C
    int dict_shift_;       // +0x50
};

} // namespace gfx
