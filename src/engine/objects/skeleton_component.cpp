// The skeleton component (engine/objects/skeleton_component.h).

#include "engine/objects/skeleton_component.h"
#include "platform/d3dx.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "platform/crt.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/engine_parts.h"
#include <utility>

namespace objects {

std::array<script::MemberFunctionEntry, 6>& skeleton_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, 6> skeleton_component_member_functions{{
        {.callsign_ = 0x20F7B992, .owner_kind_ = 0x222756D5}, {.callsign_ = 0x613831F2, .owner_kind_ = 0x222756D5}, {.callsign_ = 0xBE1C58CA, .owner_kind_ = 0x222756D5}, {.callsign_ = 0xB244C9CF, .owner_kind_ = 0x222756D5},
        {.callsign_ = 0xF99F9FAF, .owner_kind_ = 0x222756D5}, {.callsign_ = 0, .owner_kind_ = 0x222756D5}}};
    return skeleton_component_member_functions;
}

namespace {

constexpr uint32_t kBone = 0xCAB94088;             // bone
constexpr uint32_t kCopySkeletonFrom = 0xA81F615C; // CopySkeletonFrom
constexpr uint32_t kSkeletonName = 0x09794932;     // skeletonName
constexpr uint32_t kMaxBoneSkipLod = 0xD3982061;   // max_bone_skip_lod

constexpr float kInchesPerFoot = 12.0f;                    // 0x0064546C
constexpr float kOneOverRandMax = 3.0518509447574615e-05f; // 0x00646510 (1 / 32767)
constexpr float kTwoPi = 6.2831854820251465f;              // 0x0064545C
constexpr float kRadiansPerDegree = 0.01745329238474369f;  // 0x00645464

// The renderer's skeleton (0x1C bytes; not rewritten yet): +0x00 flags, +0x08 its skeleton data (+0x04 the bone
// count, +0x08 the bones' names), +0x10 the bone-skipping level of detail.
bool skeleton_bone_position(gfx::Rig* skeleton, uint32_t bone, maths::Vec4* out)
{
    return skeleton->get_bone_position(bone, out);
}
bool skeleton_bone_matrix(gfx::Rig* skeleton, uint32_t bone, maths::Mat4* out)
{
    return skeleton->read_joint_matrix(bone, out);
}

// The texture dictionary's texture by name / checksum (0x0049F560 / 0x0049F540).
render::BitmapTexture* texture_named(const char* name) { return render::engine_parts().particle_texture_dict()->get_texture_by_name(name); }
render::BitmapTexture* texture_by_checksum(uint32_t checksum) { return render::engine_parts().particle_texture_dict()->fetch_skin(checksum); }

// The game's C runtime rand().
int get_rand() { return crt::get_rand(); }

script::VectorValue to_value(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z, .w = v.w}; }

} // namespace

// 0x00431F60
gsl::owner<ObjectModule*> BoneRigModule::fabricate() { return new BoneRigModule; }

// (the constructor: inlined in fabricate in the original)
BoneRigModule::BoneRigModule() { kind_tag_ = kType; }

// 0x00431BE0
void BoneRigModule::publish_method_table()
{
    script::register_member_function_table(skeleton_component_member_functions());
}

// 0x00431FE0 (0x00432790 the deleting one)
BoneRigModule::~BoneRigModule()
{
    if (gfx::Rig* skeleton = bone_rig_) {
        skeleton->~Rig();
        memory::Hub::get_pull()->dealloc(skeleton);
    }
}

// 0x00431C70
void BoneRigModule::prepare_rig(uint32_t name)
{
    void* asset = assets::asset_manager()->get_asset_data(name, 0);
    void* memory = memory::alloc(0x1C);
    bone_rig_ = memory ? memory::construct_in<gfx::Rig>(memory, static_cast<gfx::RigData*>(asset)) : nullptr;
}

// 0x00431CF0
void BoneRigModule::configure_from_data(script::Struct* params)
{
    uint32_t from = 0;
    if (params->read_hash_value(kCopySkeletonFrom, &from, 0)) {
        auto* other = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(from));
        auto* other_skeleton = core::down_cast<BoneRigModule>(other->fetch_module(kType));
        void* memory = memory::alloc(0x1C);
        bone_rig_ = memory ? static_cast<gfx::Rig*>(memory)->construct_copy(other_skeleton->bone_rig_) : nullptr;
        return;
    }
    // (the name is read into the parameters' own argument slot: neither given leaves the pointer's value)
    uint32_t name = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    if (!params->read_hash_value(kType, &name, 0))
        params->read_hash_value(kSkeletonName, &name, 1);
    prepare_rig(name);
    int skip = 0;
    if (params->fetch_int_value(kMaxBoneSkipLod, &skip, 0) && bone_rig_)
        bone_rig_->max_lod_ = static_cast<uint32_t>(skip);
}

// 0x00431C60
bool BoneRigModule::get_local_bone_position(uint32_t bone, maths::Vec4* out)
{
    return skeleton_bone_position(bone_rig_, bone, out);
}

// 0x00432050
bool BoneRigModule::get_bone_position(uint32_t bone, maths::Vec4* out)
{
    if (!skeleton_bone_position(bone_rig_, bone, out))
        *out = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::Mat4 placed = entity_->basis_frame_;
    const maths::Vec4& pos = entity_->at_point_;
    core::at(placed.row, 3) = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0f};
    maths::Vec4 world;
    placed.xform(world, *out);
    *out = world;
    return false;
}

// 0x004320F0
void BoneRigModule::place_decal_splat(const maths::Vec4* start, const maths::Vec4* end, uint32_t bone, float width,
                                      float height, float spin, float turn, float spread, float lifetime,
                                      float dropdown_length, render::BitmapTexture* texture, uint32_t blend, bool follow, bool dropdown)
{
    maths::Vec4 from;
    if (!bone)
        from = *start;
    else
        get_bone_position(bone, &from);
    const float length = dropdown_length * kInchesPerFoot;
    const maths::Vec4& up = core::at(entity_->basis_frame_.row, 1);
    maths::Vec4 to;
    bool raise = true;
    if (dropdown) {
        // Straight down (in the world) by the length.
        const float zero = length * 0.0f;
        to = {.x = from.x - zero, .y = from.y - length, .z = from.z - zero, .w = from.w - length};
    } else {
        const maths::Vec4& d = *end;
        const double length_squared =
            (static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y) + static_cast<double>(d.z) * d.z;
        if (length_squared > 0.0) {
            to = d;
            raise = false;
        } else {
            // Down the object's up by the length (x's product unrounded, as the x87 leaves it).
            to = {.x = static_cast<float>(from.x - static_cast<double>(length) * up.x), .y = from.y - length * up.y,
                  .z = from.z - length * up.z, .w = from.w - length * up.w};
        }
    }
    if (raise) {
        // The start a foot up the object's up (x and y's products unrounded).
        from.x = static_cast<float>(static_cast<double>(up.x) * kInchesPerFoot + from.x);
        from.y = static_cast<float>(static_cast<double>(up.y) * kInchesPerFoot + from.y);
        from.z = up.z * kInchesPerFoot + from.z;
        from.w = up.w * kInchesPerFoot + from.w;
    }
    if (spread > 0.0f) {
        // Scattered along the object's right and at by up to the spread each (the second offset unrounded).
        const float twice = spread + spread;
        const auto across = static_cast<float>(static_cast<double>(get_rand()) * twice * kOneOverRandMax - spread);
        const double along = static_cast<double>(get_rand()) * twice * kOneOverRandMax - spread;
        const maths::Vec4& right = core::at(entity_->basis_frame_.row, 0);
        const maths::Vec4& at = core::at(entity_->basis_frame_.row, 2);
        to.x = static_cast<float>(to.x + static_cast<double>(across) * right.x);
        to.y = static_cast<float>(to.y + static_cast<double>(across) * right.y);
        to.z = to.z + across * right.z;
        to.w = to.w + across * right.w;
        to.x = static_cast<float>(to.x + along * at.x);
        to.y = to.y + static_cast<float>(along * at.y);
        to.z = to.z + static_cast<float>(along * at.z);
        to.w = to.w + static_cast<float>(along * at.w);
    }
    render::engine::create_texture_splat_at(&from, &to, width, height, spin, turn, lifetime, nullptr, blend,
                                            follow ? bone : 0, 0, texture);
}

// 0x00432490
void BoneRigModule::spawn_composite_object_at_bone(uint32_t bone, script::Array* components, script::Struct* params,
                                                   float velocity_scale, const maths::Vec4* velocity,
                                                   const maths::Vec4* rotational_velocity, maths::Vec4* offset)
{
    auto* skeleton = core::down_cast<BoneRigModule>(entity_->fetch_module(kType));
    maths::Mat4 joint_mat;
    skeleton_bone_matrix(skeleton->bone_rig_, bone, &joint_mat);
    // The object's matrix at its position (the rows' w cleared).
    maths::Mat4 placed = entity_->basis_frame_;
    core::at(placed.row, 3) = entity_->at_point_;
    // The bone's matrix in the game's axes: y and z swapped, the new z turned round; moved by the offset.
    const maths::Vec4 up = core::at(joint_mat.row, 1);
    core::at(joint_mat.row, 1) = core::at(joint_mat.row, 2);
    core::at(joint_mat.row, 2) = {.x = -up.x, .y = -up.y, .z = -up.z, .w = -up.w};
    offset->w = 0.0f;
    core::at(joint_mat.row, 3).x = core::at(joint_mat.row, 3).x + offset->x;
    core::at(joint_mat.row, 3).y = core::at(joint_mat.row, 3).y + offset->y;
    core::at(joint_mat.row, 3).z = core::at(joint_mat.row, 3).z + offset->z;
    core::at(placed.row, 0).w = 0.0f;
    core::at(placed.row, 1).w = 0.0f;
    core::at(placed.row, 2).w = 0.0f;
    d3dx::matrix_multiply(&joint_mat, &joint_mat, &placed);

    params->put_vec3(0x7F261953, to_value(core::at(joint_mat.row, 3))); // pos
    maths::QuatRotation rotation{};
    rotation.from_matrix(joint_mat);
    const maths::Vec4 quat = {.x = rotation.x, .y = rotation.y, .z = rotation.z, .w = rotation.w};
    if (quat.w > 0.0f)
        params->put_vec3(0xC97F3AA9, to_value(quat)); // Orientation (w kept positive)
    else
        params->put_vec3(0xC97F3AA9, {.x = -quat.x, .y = -quat.y, .z = -quat.z, .w = -quat.w});
    const maths::Vec4& object_vel = entity_->vel_;
    const maths::Vec4 carried = {.x = velocity_scale * object_vel.x, .y = velocity_scale * object_vel.y,
                                 .z = velocity_scale * object_vel.z, .w = velocity_scale * object_vel.w};
    maths::Vec4 turned;
    entity_->basis_frame_.rotate_vector(turned, *velocity);
    params->put_vec3(0x0C4C809E,
                     {.x = carried.x + turned.x, .y = carried.y + turned.y, .z = carried.z + turned.z, .w = carried.w + turned.w}); // vel
    entity_->basis_frame_.rotate_vector(turned, *rotational_velocity);
    params->put_vec3(0xFB1A83B2, to_value(turned)); // rotvel
    object_hub_slot()->spawn_entity_from_node(components, params, true);
}

// 0x00431C00
bool BoneRigModule::obj_set_bone_active(script::Struct* params, script::Script* /*script*/)
{
    // (missing, the original's bone is the slot PUSH ECX made - the component's address, a name no bone has - and
    // `active` the parameters' address: never 0, on)
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 1);
    int active = 1;
    params->fetch_int_value(0xB4E103FD, &active, 1); // active
    bone_rig_->enable_bone(bone, active != 0);
    return true;
}

// 0x00431E30
bool BoneRigModule::obj_set_bone_scale(script::Struct* params, script::Script* /*script*/)
{
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 1);
    float scale = 0.0f;
    params->fetch_float(0x13B9DA7B, &scale, 1); // scale
    const bool propagate = params->has_bare_flag(0x4CC196D1);
    const bool reverse = params->has_bare_flag(0xBA05E2DE); // propagate_reverse
    const maths::Vec4 scales = {.x = scale, .y = scale, .z = scale, .w = 0.0f};
    if (!reverse) {
        bone_rig_->change_bone_sizing(bone, &scales, !propagate);
        return true;
    }
    // Every bone but this one and those under it.
    gfx::RigData* data = bone_rig_->payload_;
    const auto index = static_cast<uint32_t>(data->fetch_slot_no(bone));
    for (uint32_t i = 1; std::cmp_less(i, data->num_); ++i) {
        if (!data->is_descendant(i, index) && i != index)
            bone_rig_->change_bone_sizing(core::at(data->names_, i), &scales, true);
    }
    bone_rig_->flag_bits_ |= 0x8000000;
    if (params->has_bare_flag(0x734AD918)) // force_update
        bone_rig_->collapse_zero_scaled_bones();
    return true;
}

// 0x004327C0
bool BoneRigModule::obj_get_bone_position(script::Struct* params, script::Script* script)
{
    maths::Vec4 pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    // (missing, the original's bone is the parameters' address - a name no bone has)
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 1);
    const bool found = get_bone_position(bone, &pos);
    script->args()->push_float(0x7323E97C, pos.x); // x
    script->args()->push_float(0x0424D9EA, pos.y); // y
    script->args()->push_float(0x9D2D8850, pos.z); // z
    return found;
}

// 0x00432860
bool BoneRigModule::texture_splat(script::Struct* params, script::Script* /*script*/)
{
    const bool trail = params->has_bare_flag(0x4D977A70);             // trail
    const bool dropdown_vertical = params->has_bare_flag(0x7EE6C892); // dropdown_vertical
    // Not original: the size starts at 0 - the original's is left uninitialised (stack garbage) when not given.
    float size = 0.0f;
    params->fetch_float(0x083FDB95, &size, 0); // size
    float height = size;
    params->fetch_float(0x3786B657, &height, 0);
    float radius = 0.0f;
    params->fetch_float(0xC48391A5, &radius, 0); // radius
    float lifetime = 5.0f;
    params->fetch_float(0xC218CF77, &lifetime, 0); // lifetime
    float dropdown_length = 3.0f;
    params->fetch_float(0x12C62572, &dropdown_length, 0); // dropdown_length
    float angle = 180.0f;
    params->fetch_float(0x78AAF97F, &angle, 0);
    uint32_t blend = 0x54628ED7;                    // blend
    params->read_hash_value(0xDD6BB3D5, &blend, 0); // blendmode
    angle = angle * kRadiansPerDegree;

    // The texture: by name or by checksum, from name= or texture=.
    render::BitmapTexture* texture = nullptr;
    const char* image_label = nullptr;
    uint32_t texture_checksum = 0; // (read into the parameters' own argument slot, cleared first)
    if (params->fetch_text(0xA1DC81F9u, &image_label, 0) || params->fetch_text(0x7D99F28Du, &image_label, 0))
        texture = texture_named(image_label);
    else if (params->read_hash_value(0xA1DC81F9u, &texture_checksum, 0) ||
             params->read_hash_value(0x7D99F28Du, &texture_checksum, 0))
        texture = texture_by_checksum(texture_checksum);

    // From the bone, or `forward` along the object's at (x's product unrounded).
    uint32_t bone = 0;
    // Not original: with neither, the original passes its uninitialised stack as the start; ours the object's position.
    maths::Vec4 start = entity_->at_point_;
    if (!params->hash_from_name_or_text(kBone, &bone, 0)) {
        float forward = 0.0f;
        if (params->fetch_float(0x186ED079, &forward, 0)) { // forward
            const maths::Vec4& pos = entity_->at_point_;
            const maths::Vec4& at = core::at(entity_->basis_frame_.row, 2);
            start = {.x = static_cast<float>(static_cast<double>(forward) * at.x + pos.x), .y = forward * at.y + pos.y,
                     .z = forward * at.z + pos.z, .w = forward * at.w + pos.w};
        }
    }
    script::VectorValue end = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    params->fetch_vec(0x09A7DC2E, &end, 0); // end_pos
    auto spin = static_cast<float>(static_cast<double>(get_rand()) * kTwoPi * kOneOverRandMax);
    if (params->has_bare_flag(0x744E673F)) {
        // The object's at's angle about z: acos of its z, on the side of its x (the C runtime's acos, 0x00623630).
        const maths::Vec4& at = core::at(entity_->basis_frame_.row, 2);
        const float side = at.x < 0.0f ? -1.0f : 1.0f;
        spin = static_cast<float>(std::acos(static_cast<double>(at.z)) * side);
    }
    const maths::Vec4 end_vector = {.x = end.x, .y = end.y, .z = end.z, .w = end.w};
    place_decal_splat(&start, &end_vector, bone, size, height, spin, angle, radius, lifetime, dropdown_length, texture,
                      blend, trail, dropdown_vertical);
    return true;
}

// 0x00432B90
bool BoneRigModule::skeleton_spawn_composite_object(script::Struct* params, script::Script* /*script*/)
{
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 1);
    script::VectorValue offset = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    params->fetch_vec(0xA6F5352F, &offset, 0); // offset
    script::Array* components = nullptr;
    params->fetch_list(0x11B70A02, &components, 1); // Components
    script::Struct* object_params = nullptr;
    params->read_sub_struct(0x7031F10C, &object_params, 1); // params
    script::VectorValue vel = {};
    params->fetch_vec(0x0C4C809E, &vel, 1); // vel
    script::VectorValue rotvel = {};
    params->fetch_vec(0xFB1A83B2, &rotvel, 1);   // rotvel
    float factor = 1.0f;                         // (read into the parameters' own argument slot)
    params->fetch_float(0x8C6A6E08, &factor, 0); // object_vel_factor
    const maths::Vec4 velocity = {.x = vel.x, .y = vel.y, .z = vel.z, .w = vel.w};
    const maths::Vec4 rotation = {.x = rotvel.x, .y = rotvel.y, .z = rotvel.z, .w = rotvel.w};
    maths::Vec4 at = {.x = offset.x, .y = offset.y, .z = offset.z, .w = offset.w};
    spawn_composite_object_at_bone(bone, components, object_params, factor, &velocity, &rotation, &at);
    return true;
}

} // namespace objects
