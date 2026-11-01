#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace objects {

// The skeleton component (type 0x222756D5, "skeleton"; vtable 0x006464DC, 0x1C bytes; made by 0x00431F60): its
// object's skeleton (the renderer's, 0x1C bytes, not rewritten yet) - made from the skeleton asset named by skeleton=
// / skeletonName=, or copied from another object's (CopySkeletonFrom=) - and what scripts ask of its bones: their
// positions, scales and whether they're active, texture splats and composite objects spawned at them. Its script
// member functions are in the table at 0x00684DE0 (filled by 0x00644760).
//
// Vtable: 0 the scalar deleting destructor; 1 Update 0x0051B970 (shared, empty); 2 configure_from_data; 3 / 4 / 9 the
// empty 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11
// 0x0042BB20 (shared); 12 invoke_method_by_name 0x0051ADC0 (none by name).
class BoneRigModule final : public ObjectModule {
public:
    BoneRigModule(const BoneRigModule&) = delete;
    BoneRigModule& operator=(const BoneRigModule&) = delete;
    static constexpr uint32_t kType = 0x222756D5;

    // 0x00431F60 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory): the
    // base's constructor, its vtable and type, no skeleton.
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    BoneRigModule();
    // 0x00431BE0 (not a Ghidra function; registered with Create): its script member functions registered.
    static void publish_method_table();
    // 0x00431FE0: the skeleton destroyed (0x004A2CB0) and freed; the base's body.
    ~BoneRigModule() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00431CF0 (vtable slot 2): CopySkeletonFrom=<object id>: a copy (0x004A3E80) of that object's skeleton;
    // otherwise the skeleton made from the asset skeleton= (or skeletonName=, asserted), and max_bone_skip_lod= to
    // its +0x10.
    void configure_from_data(script::Struct* params) override;
    // 0x00431C70: a new skeleton (0x004A3D30) from the asset `name` (the asset manager's data; out of memory: none).
    void prepare_rig(uint32_t name);
    // 0x00431C60: the skeleton's position of `bone` (0x004A3870: its own space; whether the bone was found).
    bool get_local_bone_position(uint32_t bone, maths::Vec4* out);
    // 0x00432050: `out` = where `bone` is in the world (the skeleton's position - (0, 0, 0, 1) if the bone isn't found -
    // turned by the object's orientation and moved to its position). Always false (the original's AL is cleared).
    bool get_bone_position(uint32_t bone, maths::Vec4* out);
    // 0x004320F0 (RET 0x38): a texture splat (0x004928A0) along a line: from `start` (or the world position of `bone`
    // when not 0) towards `end` - or, with `dropdown`, `dropdown_length` (feet) down the object's up; without it, an
    // `end` of length 0 means the same length down the up - the start first raised a foot up the object's up; the end
    // scattered by up to `spread` along the object's right and at (two rand()s). `width` / `height` / `spin` / `turn`
    // / `lifetime` / `texture` / `blend` passed on, and `bone` when `follow`.
    void place_decal_splat(const maths::Vec4* start, const maths::Vec4* end, uint32_t bone, float width, float height,
                           float spin, float turn, float spread, float lifetime, float dropdown_length, void* texture,
                           uint32_t blend, bool follow, bool dropdown);
    // 0x00432490 (RET 0x1C): a composite object made of `components` (0x0045F3A0, finalised) with `params` given
    // the position, orientation (a quaternion with w > 0), velocity and rotational velocity of the skeleton's `bone`
    // (its matrix with y and z swapped, z turned round, moved by `offset` - whose w is cleared - then by the
    // object's matrix): vel = `velocity_scale` * the object's velocity + `velocity` turned by the object; rotvel =
    // `rotational_velocity` turned by the object.
    void spawn_composite_object_at_bone(uint32_t bone, script::Array* components, script::Struct* params,
                                        float velocity_scale, const maths::Vec4* velocity,
                                        const maths::Vec4* rotational_velocity, maths::Vec4* offset);

    // --- script member functions (the table at 0x00684DE0) ---
    // 0x00431C00 Obj_SetBoneActive: bone= and active= (both asserted) to the skeleton (0x004A2BB0). True.
    bool obj_set_bone_active(script::Struct* params, script::Script* script);
    // 0x00431E30 obj_set_bone_scale: bone= and scale= (asserted) to the skeleton (0x004A2D80; propagate makes it pass
    // on); with propagate_reverse, every other bone (from 1) that isn't one of the bone's (0x004A3100) instead, the
    // skeleton's flag 0x8000000 set and, with force_update, 0x004A4000. True.
    bool obj_set_bone_scale(script::Struct* params, script::Script* script);
    // 0x004327C0 obj_GetBonePosition: bone= (asserted): x, y and z of its world position added to the script's
    // parameters. Returns GetBonePosition's (always false).
    bool obj_get_bone_position(script::Struct* params, script::Script* script);
    // 0x00432860 TextureSplat: place_decal_splat from bone= (or the object's position + forward= along its at),
    // end_pos=, size=, 0x3786B657 (size by default), radius= (spread), lifetime= (5), dropdown_length= (3), the angle
    // 0x78AAF97F (degrees, 180), a random spin - or with 0x744E673F the at's angle about z (acos of its z, signed by
    // its x) -, blendmode= (blend), name= / texture= (a string or a checksum: the texture dictionary's), trail (follow
    // the bone), dropdown_vertical. True.
    bool texture_splat(script::Struct* params, script::Script* script);
    // 0x00432B90 (not a Ghidra function) Skeleton_SpawnCompositeObject: bone=, offset=, Components=, params=, vel=,
    // rotvel= (all but offset asserted) and object_vel_factor= (1) to spawn_composite_object_at_bone. True.
    bool skeleton_spawn_composite_object(script::Struct* params, script::Script* script);

    uint8_t* bone_rig_; // +0x18 the renderer's skeleton
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_skeleton_component_member_functions[36];
} // namespace objects
