// The lockobj component's construction, destruction and the lock's update (GameObjects::PinnedObjectModule; lock_obj.cpp has its
// parameters and the update that calls this one). From the disassembly of 0x0041F400-0x0041F51F, 0x0041F760-0x0041FF14
// and 0x0041FF20. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the
// FPU stack are worked in double.

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/gfx/d3dx.h"
#include "engine/core/mem.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity_hub.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

float g_lock_obj_update_wobble_step{0.0061359233f};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kFirstNonSkaterId = 8; // (objects with smaller ids are the skaters)

// The wobble's step: 2 pi / 1024 as the update's own float (0x0064611C; configure_from_data's, 0x00646118, is one bit
// smaller).
float update_wobble_step() { return g_lock_obj_update_wobble_step; }

// x87 FSIN, as the original takes the wobble's sines.
double x87_sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// A skeleton component's skeleton (0x005480F0: its +0x18).
void* skeleton_of(ObjectModule* component) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18); }
// The skeleton's matrix of `bone` (0x004A36E0; false if it hasn't the bone), its bone index (0x004A2B60) and, from its
// data (+0x08), a bone's parent (0x004A2C50).
bool skin_matrix_ptr(void* skeleton, uint32_t bone, maths::Mat4* out)
{
    return static_cast<gfx::Rig*>(skeleton)->read_joint_matrix(bone, out);
}
int bone_index(void* skeleton, uint32_t bone) { return static_cast<gfx::Rig*>(skeleton)->fetch_bone_slot(bone); }
uint32_t parent_bone(void* skeleton, int index)
{
    void* data = *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 8);
    return static_cast<gfx::RigData*>(data)->fetch_parent_hash(index);
}
// The model component's 0x004215F0: its matrix into `out`.
void compose_object_transform(ObjectModule* model, maths::Mat4* out)
{
    static_cast<MeshInstanceModule*>(model)->get_draw_matrix(out);
}
// Whether the target's model is to be followed: a model with its +0x234 set, or a skater.
bool follow_model(ObjectModule* model, GameEntity* target)
{
    return (model && reinterpret_cast<uint8_t*>(model)[0x234]) || target->ident_ < kFirstNonSkaterId;
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

void set_product(maths::Mat4& out, const maths::Mat4& a, const maths::Mat4& b)
{
    maths::MatrixProduct product = {&a, &b};
    out = product;
}

} // namespace

// 0x0041FF20
ObjectModule* __cdecl PinnedObjectModule::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(PinnedObjectModule));
    return memory ? static_cast<PinnedObjectModule*>(memory)->make_new() : nullptr;
}

// 0x0041F500
PinnedObjectModule* PinnedObjectModule::make_new()
{
    ObjectModule::make_new();
    lock_flags_ &= ~kLockFlagLocked;
    vtable_ = kVtable;
    kind_tag_ = kType;
    return this;
}

// 0x0041F400
void PinnedObjectModule::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x0041F440
PinnedObjectModule* PinnedObjectModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0041F760
void PinnedObjectModule::update_lock()
{
    EntityHub* manager = object_hub_slot();
    auto* target = static_cast<GameEntity*>(manager->look_up_entity(target_id_));
    if (!target) {
        target = static_cast<GameEntity*>(object_hub_slot()->look_up_entity(entity_label_));
        if (!target)
            return;
    }
    maths::Vec4 pos = entity_->at_point_;     // where it goes
    maths::Vec4 extra = {0.0f, 0.0f, 0.0f, 1.0f}; // added to it when there's no lag
    maths::Vec4 local;
    if (bone_) {
        // On a bone: its orientation, and its "at" towards its parent.
        ObjectModule* skeleton_component = target->fetch_module(kSkeleton);
        void* skeleton = skeleton_component ? skeleton_of(target->fetch_module(kSkeleton)) : nullptr;
        if (skeleton && skin_matrix_ptr(skeleton, bone_, &entity_->basis_frame_)) {
            maths::Mat4 parent;
            skin_matrix_ptr(skeleton, parent_bone(skeleton, bone_index(skeleton, bone_)), &parent);
            GameEntity* object = entity_;
            maths::Vec4 joint_world = object->basis_frame_.row[3];
            maths::Vec4 owner_pos = parent.row[3];
            object->basis_frame_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            parent.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            ObjectModule* model = target->fetch_module(kModel);
            if (model && follow_model(model, target)) {
                maths::Mat4 frame;
                compose_object_transform(model, &frame);
                extra = frame.row[3] - target->at_point_;
                frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
                set_product(entity_->basis_frame_, entity_->basis_frame_, frame);
                set_product(parent, parent, frame);
                pos = frame.xform(local, joint_world);
                pos += target->at_point_ + shift_ * entity_->basis_frame_;
                owner_pos = frame.xform(local, owner_pos);
                owner_pos += target->at_point_ + shift_ * parent;
            } else {
                const maths::Mat4& frame = target->display_matrix_;
                set_product(entity_->basis_frame_, entity_->basis_frame_, frame);
                set_product(parent, parent, frame);
                pos = frame.xform(local, joint_world);
                pos += target->at_point_ + shift_ * entity_->basis_frame_;
                owner_pos = frame.xform(local, owner_pos);
                owner_pos += target->at_point_ + shift_ * parent;
            }
            object = entity_;
            maths::Vec4& at = object->basis_frame_.row[2];
            at = {owner_pos.x - pos.x, owner_pos.y - pos.y, owner_pos.z - pos.z, owner_pos.w - pos.w};
            entity_->basis_frame_.row[2].make_unit();
            maths::Vec4 up = target->display_matrix_.row[1];
            object = entity_;
            object->basis_frame_.row[0] = maths::cross_vec(object->basis_frame_.row[2], up);
            object = entity_;
            object->basis_frame_.row[1] = maths::cross_vec(object->basis_frame_.row[2], object->basis_frame_.row[0]);
            entity_->display_matrix_ = entity_->basis_frame_;
        }
    } else {
        // At the target (or its model): its orientation, and the offset turned by it.
        ObjectModule* model = target->fetch_module(kModel);
        if (model && follow_model(model, target)) {
            maths::Mat4 frame;
            compose_object_transform(model, &frame);
            pos = shift_ * frame;
            frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            entity_->basis_frame_ = frame;
        } else {
            entity_->basis_frame_ = target->display_matrix_;
            maths::Vec4 turned = shift_ * entity_->basis_frame_;
            pos = {turned.x + target->at_point_.x, target->at_point_.y + turned.y, target->at_point_.z + turned.z,
                   target->at_point_.w + turned.w};
        }
    }
    GameEntity* object = entity_;
    if (lock_flags_ & kLockFlagLag) {
        // Part of the way there (the roundings where the original stores).
        float lag = lag_;
        double dx = static_cast<double>(pos.x) - object->at_point_.x;
        double dy = static_cast<double>(pos.y) - object->at_point_.y;
        float dz = pos.z - object->at_point_.z;
        float dw = pos.w - object->at_point_.w;
        float step_x = static_cast<float>(dx * lag);
        float step_y = static_cast<float>(dy * lag);
        double step_z = static_cast<double>(dz) * lag;
        float step_w = dw * lag;
        float x = step_x + object->at_point_.x;
        float y = step_y + object->at_point_.y;
        object->at_point_.z = static_cast<float>(step_z + object->at_point_.z);
        object->at_point_.w = step_w + object->at_point_.w;
        object->at_point_.x = x;
        object->at_point_.y = y;
        // The lag for the next update: as configure_from_data's, with the update's step.
        int t = static_cast<int>((frame_clock::get_time() + static_cast<uint32_t>(wobble_seed_)) & 0x3FF);
        double time = static_cast<double>(t);
        double step = update_wobble_step();
        double wobble = x87_sin(time * static_cast<double>(wobble_[1]) * step) *
                        x87_sin(time * static_cast<double>(wobble_[0]) * step);
        lag_ = static_cast<float>((wobble + 1.0) * (static_cast<double>(lag_max_) - lag_min_) * 0.5 + lag_min_);
    } else {
        object->at_point_ = {extra.x + pos.x, extra.y + pos.y, extra.z + pos.z, extra.w + pos.w};
    }
    entity_->vel_ = target->vel_;
    if (lock_flags_ & kLockFlagAngles)
        matrix_multiply(&entity_->basis_frame_, &entity_->basis_frame_, &orientation_turn_);
    entity_->display_matrix_ = entity_->basis_frame_;
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::PinnedObjectModule::teardown,
         &objects::PinnedObjectModule::tick_frame,
         &objects::PinnedObjectModule::configure_from_data,
         stubs::do_nothing,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         stubs::ignore_argument,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::PinnedObjectModule::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace Obj
