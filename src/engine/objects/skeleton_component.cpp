// The skeleton component (engine/objects/skeleton_component.h).

#include "engine/objects/skeleton_component.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

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

namespace objects {

uint32_t g_skeleton_component_member_functions[36]{
    0x20F7B992, 0x222756D5, 0, 0, 0, 0, 0x613831F2, 0x222756D5, 0, 0, 0, 0, 0xBE1C58CA, 0x222756D5, 0, 0, 0, 0,
    0xB244C9CF, 0x222756D5, 0, 0, 0, 0, 0xF99F9FAF, 0x222756D5, 0, 0, 0, 0, 0,          0x222756D5, 0, 0, 0, 0};

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
bool skeleton_bone_position(uint8_t* skeleton, uint32_t bone, maths::Vec4* out)
{
    return reinterpret_cast<gfx::Rig*>(skeleton)->get_bone_position(bone, out);
}
bool skeleton_bone_matrix(uint8_t* skeleton, uint32_t bone, maths::Mat4* out)
{
    return reinterpret_cast<gfx::Rig*>(skeleton)->read_joint_matrix(bone, out);
}
void change_bone_sizing(uint8_t* skeleton, uint32_t bone, const maths::Vec4* scale, bool alone)
{
    reinterpret_cast<gfx::Rig*>(skeleton)->change_bone_sizing(bone, scale, alone);
}
// The skeleton data's index of the bone named `bone` (0x004A2C70), and whether bone `index` is `parent` or under it
// (0x004A3100).
uint32_t bone_index(uint8_t* data, uint32_t bone)
{
    return static_cast<uint32_t>(reinterpret_cast<gfx::RigData*>(data)->fetch_slot_no(bone));
}
bool is_bone_under(uint8_t* data, uint32_t index, uint32_t parent)
{
    return reinterpret_cast<gfx::RigData*>(data)->is_descendant(index, parent);
}
uint32_t get_bone_count(uint8_t* data) { return *reinterpret_cast<uint32_t*>(data + 0x04); }
uint32_t bone_name(uint8_t* data, uint32_t index) { return reinterpret_cast<uint32_t*>(data + 0x08)[index]; }

// The texture dictionary's texture by name / checksum (0x0049F560 / 0x0049F540).
void* texture_named(const char* name) { return render::g_particle_texture_dict->get_texture_by_name(name); }
void* texture_by_checksum(uint32_t checksum) { return render::g_particle_texture_dict->fetch_skin(checksum); }

// The game's C runtime rand().
int get_rand() { return crt::get_rand(); }

script::VectorValue to_value(const maths::Vec4& v) { return {v.x, v.y, v.z, v.w}; }

} // namespace

// 0x00431F60
memory::owner<ObjectModule*> BoneRigModule::fabricate() { return new BoneRigModule; }

// (the constructor: inlined in fabricate in the original)
BoneRigModule::BoneRigModule()
{
    kind_tag_ = kType;
    bone_rig_ = nullptr;
}

// 0x00431BE0
void BoneRigModule::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_skeleton_component_member_functions, 0x18);
}

// 0x00431FE0 (0x00432790 the deleting one)
BoneRigModule::~BoneRigModule()
{
    if (uint8_t* skeleton = bone_rig_) {
        reinterpret_cast<gfx::Rig*>(skeleton)->~Rig();
        memory::Hub::get_pull()->dealloc(skeleton);
    }
}

// 0x00431C70
void BoneRigModule::prepare_rig(uint32_t name)
{
    void* asset = assets::g_asset_manager->get_asset_data(name, 0);
    void* memory = memory::alloc(0x1C);
    bone_rig_ = memory ? reinterpret_cast<uint8_t*>(new (memory) gfx::Rig(static_cast<gfx::RigData*>(asset))) : nullptr;
}

// 0x00431CF0
void BoneRigModule::configure_from_data(script::Struct* params)
{
    uint32_t from = 0;
    if (params->read_hash_value(kCopySkeletonFrom, &from, 0)) {
        auto* other = reinterpret_cast<GameEntity*>(object_hub_slot()->look_up_entity(from));
        auto* other_skeleton = reinterpret_cast<BoneRigModule*>(other->fetch_module(kType));
        void* memory = memory::alloc(0x1C);
        bone_rig_ = memory ? reinterpret_cast<uint8_t*>(static_cast<gfx::Rig*>(memory)->construct_copy(
                                 reinterpret_cast<const gfx::Rig*>(other_skeleton->bone_rig_)))
                           : nullptr;
        return;
    }
    // (the name is read into the parameters' own argument slot: neither given leaves the pointer's value)
    uint32_t name = reinterpret_cast<uint32_t>(params);
    if (!params->read_hash_value(kType, &name, 0))
        params->read_hash_value(kSkeletonName, &name, 1);
    prepare_rig(name);
    int skip = 0;
    if (params->fetch_int_value(kMaxBoneSkipLod, &skip, 0) && bone_rig_)
        reinterpret_cast<gfx::Rig*>(bone_rig_)->max_lod_ = static_cast<uint32_t>(skip);
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
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Mat4 placed = entity_->basis_frame_;
    const maths::Vec4& pos = entity_->at_point_;
    placed.row[3] = {pos.x, pos.y, pos.z, 1.0f};
    maths::Vec4 world;
    placed.xform(world, *out);
    *out = world;
    return false;
}

// 0x004320F0
void BoneRigModule::place_decal_splat(const maths::Vec4* start, const maths::Vec4* end, uint32_t bone, float width,
                                      float height, float spin, float turn, float spread, float lifetime,
                                      float dropdown_length, void* texture, uint32_t blend, bool follow, bool dropdown)
{
    maths::Vec4 from;
    if (!bone)
        from = *start;
    else
        get_bone_position(bone, &from);
    const float length = dropdown_length * kInchesPerFoot;
    const maths::Vec4& up = entity_->basis_frame_.row[1];
    maths::Vec4 to;
    bool raise = true;
    if (dropdown) {
        // Straight down (in the world) by the length.
        const float zero = length * 0.0f;
        to = {from.x - zero, from.y - length, from.z - zero, from.w - length};
    } else {
        const maths::Vec4& d = *end;
        double length_squared =
            (static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y) + static_cast<double>(d.z) * d.z;
        if (length_squared > 0.0) {
            to = d;
            raise = false;
        } else {
            // Down the object's up by the length (x's product unrounded, as the x87 leaves it).
            to = {static_cast<float>(from.x - static_cast<double>(length) * up.x), from.y - length * up.y,
                  from.z - length * up.z, from.w - length * up.w};
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
        const float across = static_cast<float>(static_cast<double>(get_rand()) * twice * kOneOverRandMax - spread);
        const double along = static_cast<double>(get_rand()) * twice * kOneOverRandMax - spread;
        const maths::Vec4& right = entity_->basis_frame_.row[0];
        const maths::Vec4& at = entity_->basis_frame_.row[2];
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
                                            follow ? bone : 0, 0, static_cast<uint8_t*>(texture));
}

// 0x00432490
void BoneRigModule::spawn_composite_object_at_bone(uint32_t bone, script::Array* components, script::Struct* params,
                                                   float velocity_scale, const maths::Vec4* velocity,
                                                   const maths::Vec4* rotational_velocity, maths::Vec4* offset)
{
    auto* skeleton = reinterpret_cast<BoneRigModule*>(entity_->fetch_module(kType));
    maths::Mat4 joint_mat;
    skeleton_bone_matrix(skeleton->bone_rig_, bone, &joint_mat);
    // The object's matrix at its position (the rows' w cleared).
    maths::Mat4 placed = entity_->basis_frame_;
    placed.row[3] = entity_->at_point_;
    // The bone's matrix in the game's axes: y and z swapped, the new z turned round; moved by the offset.
    maths::Vec4 up = joint_mat.row[1];
    joint_mat.row[1] = joint_mat.row[2];
    joint_mat.row[2] = {-up.x, -up.y, -up.z, -up.w};
    offset->w = 0.0f;
    joint_mat.row[3].x = joint_mat.row[3].x + offset->x;
    joint_mat.row[3].y = joint_mat.row[3].y + offset->y;
    joint_mat.row[3].z = joint_mat.row[3].z + offset->z;
    placed.row[0].w = 0.0f;
    placed.row[1].w = 0.0f;
    placed.row[2].w = 0.0f;
    d3dx::matrix_multiply(&joint_mat, &joint_mat, &placed);

    params->put_vec3(0x7F261953, to_value(joint_mat.row[3])); // pos
    maths::Vec4 quat;
    reinterpret_cast<maths::QuatRotation*>(&quat)->from_matrix(joint_mat);
    if (quat.w > 0.0f)
        params->put_vec3(0xC97F3AA9, to_value(quat)); // Orientation (w kept positive)
    else
        params->put_vec3(0xC97F3AA9, {-quat.x, -quat.y, -quat.z, -quat.w});
    const maths::Vec4& object_vel = entity_->vel_;
    maths::Vec4 carried = {velocity_scale * object_vel.x, velocity_scale * object_vel.y, velocity_scale * object_vel.z,
                           velocity_scale * object_vel.w};
    maths::Vec4 turned;
    entity_->basis_frame_.rotate_vector(turned, *velocity);
    params->put_vec3(0x0C4C809E,
                     {carried.x + turned.x, carried.y + turned.y, carried.z + turned.z, carried.w + turned.w}); // vel
    entity_->basis_frame_.rotate_vector(turned, *rotational_velocity);
    params->put_vec3(0xFB1A83B2, to_value(turned)); // rotvel
    object_hub_slot()->spawn_entity_from_node(components, params, true);
}

// 0x00431C00
bool BoneRigModule::obj_set_bone_active(script::Struct* params, script::Script* /*script*/)
{
    uint32_t bone = reinterpret_cast<uint32_t>(this); // (the slot PUSH ECX made: missing, the component's address)
    params->read_hash_value(kBone, &bone, 1);
    // (read into the parameters' own argument slot: missing, the pointer's value)
    int active = static_cast<int>(reinterpret_cast<uint32_t>(params));
    params->fetch_int_value(0xB4E103FD, &active, 1); // active
    reinterpret_cast<gfx::Rig*>(bone_rig_)->enable_bone(bone, active != 0);
    return true;
}

// 0x00431E30
bool BoneRigModule::obj_set_bone_scale(script::Struct* params, script::Script* /*script*/)
{
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 1);
    float scale = 0.0f;
    params->fetch_float(0x13B9DA7B, &scale, 1); // scale
    bool propagate = params->has_bare_flag(0x4CC196D1);
    bool reverse = params->has_bare_flag(0xBA05E2DE); // propagate_reverse
    maths::Vec4 scales = {scale, scale, scale, 0.0f};
    if (!reverse) {
        change_bone_sizing(bone_rig_, bone, &scales, !propagate);
        return true;
    }
    // Every bone but this one and those under it.
    auto* data = reinterpret_cast<uint8_t*>(reinterpret_cast<gfx::Rig*>(bone_rig_)->payload_);
    uint32_t index = bone_index(data, bone);
    for (uint32_t i = 1; i < get_bone_count(data); ++i) {
        if (!is_bone_under(data, i, index) && i != index)
            change_bone_sizing(bone_rig_, bone_name(data, i), &scales, true);
    }
    *reinterpret_cast<uint32_t*>(bone_rig_) |= 0x8000000;
    if (params->has_bare_flag(0x734AD918)) // force_update
        reinterpret_cast<gfx::Rig*>(bone_rig_)->collapse_zero_scaled_bones();
    return true;
}

// 0x004327C0
bool BoneRigModule::obj_get_bone_position(script::Struct* params, script::Script* script)
{
    maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
    // (the bone is read into the parameters' own argument slot)
    uint32_t bone = reinterpret_cast<uint32_t>(params);
    params->read_hash_value(kBone, &bone, 1);
    bool found = get_bone_position(bone, &pos);
    script->args_->push_float(0x7323E97C, pos.x); // x
    script->args_->push_float(0x0424D9EA, pos.y); // y
    script->args_->push_float(0x9D2D8850, pos.z); // z
    return found;
}

// 0x00432860
bool BoneRigModule::texture_splat(script::Struct* params, script::Script* /*script*/)
{
    bool trail = params->has_bare_flag(0x4D977A70);             // trail
    bool dropdown_vertical = params->has_bare_flag(0x7EE6C892); // dropdown_vertical
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
    void* texture = nullptr;
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
            const maths::Vec4& at = entity_->basis_frame_.row[2];
            start = {static_cast<float>(static_cast<double>(forward) * at.x + pos.x), forward * at.y + pos.y,
                     forward * at.z + pos.z, forward * at.w + pos.w};
        }
    }
    script::VectorValue end = {0.0f, 0.0f, 0.0f, 1.0f};
    params->fetch_vec(0x09A7DC2E, &end, 0); // end_pos
    float spin = static_cast<float>(static_cast<double>(get_rand()) * kTwoPi * kOneOverRandMax);
    if (params->has_bare_flag(0x744E673F)) {
        // The object's at's angle about z: acos of its z, on the side of its x (the C runtime's acos, 0x00623630).
        const maths::Vec4& at = entity_->basis_frame_.row[2];
        float side = at.x < 0.0f ? -1.0f : 1.0f;
        spin = static_cast<float>(std::acos(static_cast<double>(at.z)) * side);
    }
    const maths::Vec4 end_vector = {end.x, end.y, end.z, end.w};
    place_decal_splat(&start, &end_vector, bone, size, height, spin, angle, radius, lifetime, dropdown_length, texture,
                      blend, trail, dropdown_vertical);
    return true;
}

// 0x00432B90
bool BoneRigModule::skeleton_spawn_composite_object(script::Struct* params, script::Script* /*script*/)
{
    uint32_t bone = 0;
    params->read_hash_value(kBone, &bone, 1);
    script::VectorValue offset = {0.0f, 0.0f, 0.0f, 1.0f};
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
    maths::Vec4 velocity = {vel.x, vel.y, vel.z, vel.w};
    maths::Vec4 rotation = {rotvel.x, rotvel.y, rotvel.z, rotvel.w};
    maths::Vec4 at = {offset.x, offset.y, offset.z, offset.w};
    spawn_composite_object_at_bone(bone, components, object_params, factor, &velocity, &rotation, &at);
    return true;
}

} // namespace objects
