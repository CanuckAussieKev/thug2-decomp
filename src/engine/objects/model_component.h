#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"

namespace render {
class ProbeRay;
class RenderModel;
} // namespace render
namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// One turn of a model's display about an axis (RotateDisplay): from `base` by `range` degrees over `period`
// milliseconds - linearly (power 0), eased in (power > 0: sin(pi/2 t) squared power - 1 times) or out (power < 0:
// 1 - sin(pi/2 - pi/2 t)) -, then off (or held at its end).
struct ModelSpin {
    // 0x00420590: its angle now (degrees; 0 when off; unrounded, as the original leaves it in ST0). Past its period:
    // switched off (0), or held at base + range. The time is the game clock's (slow motion counted), or the real one
    // (kModelSpinRealTime).
    double bearing();

    float period_;      // +0x00 milliseconds
    uint32_t outset_;   // +0x04 when it started (the game clock's time, or the real time)
    float base_;        // +0x08 degrees
    float range_;       // +0x0C degrees
    int power_;         // +0x10 SinePower
    uint8_t flag_bits_; // +0x14 MODEL_SPIN_*
    uint8_t pad15[3];
};
static_assert(sizeof(ModelSpin) == 0x18, "layout must match the original");

// ModelSpin::flag_bits_
constexpr uint8_t kModelSpinOn = 0x1;
constexpr uint8_t kModelSpinHold = 0x2;     // HoldOnLastAngle
constexpr uint8_t kModelSpinRealTime = 0x4; // dontpause: the real time, not the game clock's

// A model's turns about one axis: up to six at once, added up.
struct ModelSpinAxis {
    // 0x004204F0: every turn off, inactive.
    void get_clear();
    // 0x00420520: a new turn in the first free slot (none free: nothing), the axis active.
    void append_one(float period, uint32_t start, float base, float range, int power, bool hold, bool real_time);
    // 0x004213E0: the angles of the turns still on, added up (degrees). Inactive once none is.
    float bearing();

    ModelSpin spins_[6]; // +0x00
    bool active_;        // +0x90
    uint8_t pad91[3];
};
static_assert(sizeof(ModelSpinAxis) == 0x94, "layout must match the original");

// The model component (type 0x286A8D26 "model", vtable 0x00646128, 0x238 bytes; made by 0x00421490): an object's
// renderable model (render::RenderModel, +0x208) - made from its structure (Obj_InitModel: a level object's sector, a clone of
// another object's model, a model file or an asset), placed every frame at its object's display matrix (turned by
// RotateDisplay's spins, flipped by EnableDisplayFlip, moved by the display offset) - and the script members that
// change it (visibility of its geoms, colours, lights, scaling, textures).
//
// Vtable: 0 the scalar deleting destructor (0x00420270), 1 Update, 2 configure_from_data, 3 Finalize, 8 Hide, 9
// Teleport, 12 invoke_method_by_name, 13 set_up_mesh_from_profile, 14 change_mesh; the rest the base's / shared.
class MeshInstanceModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x286A8D26;

    // 0x00421490 (registered with the composite object manager): a new one (Construct; 0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x00420BF0: its type, a new model (0x0048BFA0), the spins off, no display
    // offset (w 1), scale 1, no colour values, the pivot (0, 0, 0, 1).
    MeshInstanceModule();
    // 0x00420000: the model deleted (0x0048C3C0), then the base's body.
    ~MeshInstanceModule() override;
    // 0x004214F0 (vtable slot 2): UseModelLights (the model's lights made - their position the object's, their
    // count Max_Scene_Lights - or, without it, taken away), the skeleton given to the model, ShadowVolume, then
    // set_up_mesh.
    void configure_from_data(script::Struct* params) override;
    // 0x004204B0 (vtable slot 3): its object's skeleton, animation blender and suspend components.
    void seal() override;
    // 0x004219F0 (vtable slot 1): a level object (set_up_mesh's refObjectName) follows its sector's model; otherwise an
    // active, shown model goes to the display matrix (with the skeleton and whether the blender's animation is
    // ready).
    void tick_frame() override;
    // 0x00420070 (vtable slot 8)
    void conceal(bool hide) override;
    // 0x00420480 (vtable slot 9): Update now, then the model's lights' brightness.
    void jump_to() override;
    // 0x00421AE0 (vtable slot 12): its script members (a switch on the name); 2 when it's not one of them.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x00421230 (vtable slot 13): the model built from a profile (Gfx: 0x004897D0 / 0x0048A2F0 - the build script,
    // 0x91C1EC93 when 0), then as set_up_mesh ends.
    void set_up_mesh_from_profile(void* profile, bool via_resource_store, int tex_dict_offset, uint32_t build_script);
    // 0x00421360 (vtable slot 14): another model; its lights' brightness and bounding sphere taken.
    void change_mesh(render::RenderModel* model);

    // 0x00420D10: the model from the structure (see the class comment), then (with the object's
    // kCompositeFlagHidden?) Update and the lights, the bounding sphere into enclosing_ball_ and the bone volumes.
    void set_up_mesh(script::Struct* params);
    // 0x0041FFA0: the level sector `name`'s geometry cloned into the model (a level object).
    void init_from_sector(uint32_t name);
    // 0x00420350: the skeleton's bone volumes (shared by every model with that skeleton) given to the model.
    void init_bone_volumes();
    // 0x004215F0: the display matrix: the object's, turned by the spins about X, Y and Z (about the pivot when only
    // one... see the source), flipped (EnableDisplayFlip), at the object's position plus the spins' offset and the
    // display offset.
    void get_draw_matrix(maths::Mat4* out);
    // 0x00421960: a line against the model (the skeleton's bones): the hit, 0 without a skeleton or bone volumes.
    int test_collision(maths::Vec4* start, uint32_t bone, uint32_t a, uint32_t b, uint32_t c);

    // 0x00420090: colour value `index` (1 to the count) - nothing without a model or outside.
    void set_color_value(int index, float value);
    // 0x004200C0: the model's lights' brightness.
    void get_update_brightness();
    // 0x004200E0: set_up_mesh_from_profile(profile, true, 0, buildScript) through the vtable, when there's a model.
    void rebuild_from_profile(void* profile, uint32_t build_script);
    // 0x00420100
    uint32_t get_linked_entity_name() const { return linked_entity_label_; }
    // 0x00420110: the model's texture `src` replaced by `dest` in `mesh` (All: in every mesh); false without a model.
    bool swap_texture(uint32_t mesh, const char* src, const char* dest);
    // 0x004202A0: geom `name` hidden or shown (sent to the server first when `send` and its player is this machine's).
    // True.
    bool conceal_mesh(uint32_t name, bool hide, bool send);
    // 0x00420670: the brightness of what a feeler hit, doubled, into m_brightness and the lights (lerped by `rate`
    // when asked); without the model's lights, the global brightness.
    void apply_brightness(render::ProbeRay* feeler, bool lerp, float rate);
    // 0x00420700: the colour of geom 0 (init_from_sector's model only).
    void change_tint(uint32_t color);
    // 0x00420730: turns of `duration` frames about the axes asked (each from `start` to `end` degrees), with the pivot
    // (0, 30, 0, 1); sent to the server (message 0x76) when its player is this machine's.
    void rotate_display(float duration, float start, float end, int power, bool hold, bool x, bool y, bool z);
    // 0x004208D0: every turn off; sent (message 0x8C).
    void cancel_rotate_display();

    maths::Vec4 enclosing_ball_;  // +0x18 the model's (0x00492F30)
    maths::Vec4 pivot_;           // +0x28 RotationOffset
    ModelSpinAxis twist_rate_[3]; // +0x38 about X, Y, Z
    bool flip_;                   // +0x1F4 EnableDisplayFlip
    uint8_t pad1f5_[3];
    maths::Vec4 shift_;                  // +0x1F8 added to the display position (w 1)
    render::RenderModel* mesh_instance_; // +0x208
    ObjectModule* bone_rig_;             // +0x20C its object's skeleton component (Finalize)
    ObjectModule* anim_blender_ptr_;     // +0x210
    ObjectModule* suspend_;              // +0x214
    float color_values_[4];              // +0x218 (1 to color_value_count_)
    int color_value_count_;              // +0x228
    float brightness_;                   // +0x22C
    uint32_t linked_entity_label_;       // +0x230 refObjectName: a level object's sector
    bool level_object_;                  // +0x234
    bool sector_model_;                  // +0x235 init_from_sector's
    uint8_t pad236[2];
};
static_assert(offsetof(MeshInstanceModule, enclosing_ball_) == 0x18 && offsetof(MeshInstanceModule, pivot_) == 0x28 &&
                  offsetof(MeshInstanceModule, twist_rate_) == 0x38 && offsetof(MeshInstanceModule, flip_) == 0x1F4 &&
                  offsetof(MeshInstanceModule, shift_) == 0x1F8 &&
                  offsetof(MeshInstanceModule, mesh_instance_) == 0x208 &&
                  offsetof(MeshInstanceModule, color_values_) == 0x218 &&
                  offsetof(MeshInstanceModule, color_value_count_) == 0x228 &&
                  offsetof(MeshInstanceModule, linked_entity_label_) == 0x230 && sizeof(MeshInstanceModule) == 0x238,
              "layout must match the original (0x00421490 allocates 0x238 bytes)");

} // namespace objects
