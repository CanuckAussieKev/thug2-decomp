// The motion component (GameObjects::KinematicsModule, motion.h): its update, moving to points, following paths of nodes and
// following a leader's trail.

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/random.h"
#include "engine/objects/emitter_object.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_internal.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/path_follower.h"
#include "engine/objects/path_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"

namespace objects {

using namespace motion_detail;

namespace {

constexpr uint32_t kSpawnObjScript = 0x3AEFE377;  // SpawnObjScript
constexpr uint32_t kSwitchObjScript = 0x86125749; // switchobjscript
constexpr uint32_t kParamsName = 0x7031F10C;      // Params
constexpr uint32_t kOriginalNode = 0x422E1FCE;    // originalnode
constexpr uint32_t kRange = 0x6C78A5B6;           // Range
constexpr uint32_t kOb = 0xFFFF9A1C;              // ob
constexpr uint32_t kInches = 0xF915B4F3;          // inches
constexpr uint32_t kDistance = 0xE36D657E;        // distance
constexpr uint32_t kOrientY = 0xE19E310A;         // OrientY
constexpr uint32_t kPedLogic = 0x6EFDDFB8;        // the pedlogic component

// The speed along a path (motion_path.h).
void update_path_speed(KinematicsModule* motion) { reinterpret_cast<MotionPathMembers*>(motion)->update_path_speed(); }

// The velocity of a step from where the object was (`back`: minus that position) to where it is now: x from the
// unrounded difference and reciprocal, the others from the rounded ones (as the original's x87 code).
void set_step_velocity(GameEntity* object, const float back[4], float frame_time)
{
    const maths::Vec4& pos = object->at_point_;
    double x = static_cast<double>(back[0]) + pos.x;
    auto y = static_cast<float>(static_cast<double>(back[1]) + pos.y);
    auto z = static_cast<float>(static_cast<double>(back[2]) + pos.z);
    auto w = static_cast<float>(static_cast<double>(back[3]) + pos.w);
    double inverse = 1.0 / static_cast<double>(frame_time);
    auto rounded = static_cast<float>(inverse);
    maths::Vec4& vel = object->vel_;
    vel.z = static_cast<float>(static_cast<double>(z) * rounded);
    vel.w = static_cast<float>(static_cast<double>(w) * rounded);
    vel.x = static_cast<float>(x * inverse);
    vel.y = static_cast<float>(static_cast<double>(y) * rounded);
}

// Minus the object's position (the start of a step).
void get_back(const GameEntity* object, float back[4])
{
    back[0] = -object->at_point_.x;
    back[1] = -object->at_point_.y;
    back[2] = -object->at_point_.z;
    back[3] = -object->at_point_.w;
}

// The node a path follower is headed for (kMotionFollowingPath and a follower), or 0; and its last node.
uint32_t path_node(const KinematicsModule* motion)
{
    if (!(motion->motion_flags_ & kMotionFollowingPath) || !motion->path_follower_)
        return 0;
    return motion->path_follower_->list_link_;
}
uint32_t path_last_node(const KinematicsModule* motion)
{
    if (!(motion->motion_flags_ & kMotionFollowingPath) || !motion->path_follower_)
        return 0;
    return motion->path_follower_->last_node_;
}

} // namespace

// --- the update ---

// 0x004270E0
void KinematicsModule::tick_frame()
{
    step_seconds_ = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    if (motion_flags_) {
        if (motion_flags_ & kMotionHovering) {
            entity_->at_point_.y = hover_base_;
            update_hover();
        }
        if (motion_flags_ & kMotionRotateAxes)
            update_rotations();
        if (motion_flags_ & kMotionTurning)
            update_turn();
        if (motion_flags_ & kMotionMoving) {
            if (update_move_to())
                motion_flags_ &= ~kMotionMoving;
        } else if (motion_flags_ & kMotionFollowingPath) {
            update_path();
        } else if (motion_flags_ & kMotionFollowingLeader) {
            update_follow_leader();
        }
        if (motion_flags_ & kMotionJumping) {
            update_jump();
            entity_->at_point_ = jump_pos_;
        }
    }
    // The ground offset (Obj_StickToGround's height to make up), eased in.
    if (!(std::fabs(ground_offset_) > 0.0f)) {
        ground_speed_ = 0.0f;
    } else {
        bool up = ground_offset_ > 0.0f;
        float acceleration = up ? 384.0f : -384.0f;
        if (up != (ground_speed_ > 0.0f))
            ground_speed_ = 0.0f;
        double change = static_cast<double>(acceleration) * step_seconds_;
        auto step = static_cast<float>((0.5 * change + ground_speed_) * step_seconds_);
        ground_speed_ = static_cast<float>(change + ground_speed_);
        // (past the offset: the rest of it, and no more speed; a NaN step goes as it is)
        if (up ? step > ground_offset_ : step < ground_offset_) {
            ground_speed_ = 0.0f;
            step = ground_offset_;
        }
        entity_->at_point_.y = static_cast<float>(static_cast<double>(step) + entity_->at_point_.y);
        ground_offset_ = static_cast<float>(static_cast<double>(ground_offset_) - step);
    }
    if (entity_->ident_ > 15)
        entity_->display_matrix_ = entity_->basis_frame_;
}

// 0x00424200
bool KinematicsModule::update_move_to()
{
    float back[4];
    get_back(entity_, back);
    if (!(move_acceleration_ == 0.0f)) {
        double speed = frame_clock::get_slomo_frame_seconds() * 60.0 * move_acceleration_ + move_speed_;
        move_speed_ = static_cast<float>(speed);
        if (speed >= move_max_speed_) { // (not with a NaN)
            move_speed_ = move_max_speed_;
            move_acceleration_ = 0.0f;
        }
    }
    double done = static_cast<double>(move_speed_) * step_seconds_ + move_done_;
    move_done_ = static_cast<float>(done);
    maths::Vec4& pos = entity_->at_point_;
    if (done >= move_distance_) {
        pos = move_target_;
        if (move_arrive_node_)
            arrive_at_node(move_arrive_node_);
        entity_->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
        return true;
    }
    // (the fraction of the way rounded to a float; x and y through rounded products, z through an unrounded one)
    double dx = static_cast<double>(move_target_.x) - move_start_.x;
    double dy = static_cast<double>(move_target_.y) - move_start_.y;
    auto dz = static_cast<float>(static_cast<double>(move_target_.z) - move_start_.z);
    auto dw = static_cast<float>(static_cast<double>(move_target_.w) - move_start_.w);
    auto t = static_cast<float>(done / move_distance_);
    auto x = static_cast<float>(dx * t);
    auto y = static_cast<float>(dy * t);
    auto w = static_cast<float>(static_cast<double>(dw) * t);
    pos.z = static_cast<float>(static_cast<double>(dz) * t + move_start_.z);
    pos.w = static_cast<float>(static_cast<double>(w) + move_start_.w);
    pos.x = static_cast<float>(static_cast<double>(x) + move_start_.x);
    pos.y = static_cast<float>(static_cast<double>(y) + move_start_.y);
    set_step_velocity(entity_, back, step_seconds_);
    return false;
}

// 0x00426E70
void KinematicsModule::update_path()
{
    if (entity_->fetch_module(kPedLogic))
        return;
    float back[4];
    get_back(entity_, back);
    update_path_speed(this);
    if (!(path_speed_ == 0.0f)) {
        maths::Mat4& matrix = entity_->basis_frame_;
        const maths::Vec4* along = (motion_flags2_ & kMotion2NoPathHeading) ? nullptr : &matrix.row[2];
        if (path_follower_->tick_frame(static_cast<float>(static_cast<double>(step_seconds_) * path_speed_), along)) {
            motion_flags_ &= ~kMotionFollowingPath;
            path_speed_ = 0.0f;
        }
        if (!(motion_flags2_ & kMotion2NoPathHeading)) {
            path_follower_->get_heading(&matrix.row[2]);
            if ((motion_flags2_ & kMotion2LevelHeading) && !(matrix.row[2].y == 0.0f)) {
                matrix.row[2].y = 0.0f;
                matrix.row[2].make_unit();
            }
            if (!(motion_flags2_ & kMotion2StickToGround)) {
                matrix.row[0] = maths::cross_vec(matrix.row[1], matrix.row[2]);
                matrix.row[0].make_unit();
                matrix.row[2] = maths::cross_vec(matrix.row[0], matrix.row[1]);
                matrix.row[2].make_unit();
            }
        }
        float height = entity_->at_point_.y;
        entity_->at_point_ = path_follower_->at_point_;
        if (motion_flags2_ & kMotion2StickToGround) {
            entity_->at_point_.y = height;
            stick_to_ground();
        } else if (motion_flags2_ & kMotion2ConstantHeight) {
            entity_->at_point_.y = height;
        }
    }
    set_step_velocity(entity_, back, step_seconds_);
}

// 0x004246A0
void KinematicsModule::update_follow_leader()
{
    trail_->tick_frame();
    float height = entity_->at_point_.y;
    trail_->fetch_place(&entity_->at_point_, &entity_->basis_frame_);
    if (leave_y_unaffected_)
        entity_->at_point_.y = height;
}

// --- paths ---

// 0x00423FC0
void KinematicsModule::start_path(uint32_t node)
{
    if (path_group_)
        path_group_->drop_out(entity_);
    path_group_ = PathManager::sole()->add_to_path(entity_, node);
    init_path_follower(entity_);
    path_follower_->kickoff(node, &entity_->at_point_);
}

// 0x00424020
void KinematicsModule::arrive_at_node(uint32_t node)
{
    script::Struct* node_struct = level_nodes::fetch_vertex(node, false);
    list_link_ = node;
    uint32_t script;
    if (node_struct->read_hash_value(kSpawnObjScript, &script, 0)) {
        script::Struct* params = nullptr;
        node_struct->read_sub_struct(kParamsName, &params, 0);
        entity_->spawn_script(script, params, 0, false);
        return;
    }
    if (node_struct->read_hash_value(kSwitchObjScript, &script, 0)) {
        script::Struct* params = nullptr;
        node_struct->read_sub_struct(kParamsName, &params, 0);
        entity_->replace_script(script, params);
    }
}

// 0x00424180
void KinematicsModule::follow_path_linked(script::Struct* params, script::Script*)
{
    uint32_t node = params->has_bare_flag(kOriginalNode) ? own_node_name_ : list_link_;
    uint32_t links = level_nodes::get_link_count(node);
    if (!links)
        return;
    uint32_t next;
    if (static_cast<int>(links) > 1 && (motion_flags2_ & kMotion2RandomLinks))
        next = level_nodes::fetch_link(node, maths::rnd(static_cast<int>(links)));
    else
        next = level_nodes::fetch_link(node, 0);
    start_path(next);
    motion_flags_ = (motion_flags_ & ~kMotionMoving) | kMotionFollowingPath;
}

// 0x004244C0
GameEntity* KinematicsModule::get_next_obj_on_path(float range)
{
    if (!path_group_)
        return nullptr;
    SharedRef<Entity>* slot = path_group_->slots_;
    int count = path_group_->extent_;
    GameEntity* nearest = nullptr;
    float nearest_distance = 0.0f; // (only read once there is a nearest)
    for (; count > 0; --count, ++slot) {
        auto* other = static_cast<GameEntity*>(slot->owner_object_);
        if (!other || other == entity_)
            continue;
        const maths::Vec4& at = entity_->basis_frame_.row[2];
        const maths::Vec4& other_at = other->basis_frame_.row[2];
        // (facing the same way, and ahead)
        double facing = (static_cast<double>(at.z) * other_at.z + static_cast<double>(at.y) * other_at.y) +
                        static_cast<double>(at.x) * other_at.x;
        if (!(facing >= 0.0))
            continue;
        double ahead = ((static_cast<double>(other->at_point_.z) - entity_->at_point_.z) * at.z +
                        (static_cast<double>(other->at_point_.y) - entity_->at_point_.y) * at.y) +
                       (static_cast<double>(other->at_point_.x) - entity_->at_point_.x) * at.x;
        if (!(ahead >= 0.0))
            continue;
        // (both on the same segment: headed for the same node - and if that's the same, from the same one)
        auto* other_motion = static_cast<KinematicsModule*>(other->fetch_module(kType));
        if (path_node(other_motion) == path_node(this)) {
            other_motion = static_cast<KinematicsModule*>(other->fetch_module(kType));
            if (path_last_node(other_motion) != path_last_node(this))
                continue;
        }
        double dx = static_cast<double>(entity_->at_point_.x) - other->at_point_.x;
        double dy = static_cast<double>(entity_->at_point_.y) - other->at_point_.y;
        double dz = static_cast<double>(entity_->at_point_.z) - other->at_point_.z;
        double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
        if (!(distance <= range))
            continue;
        if (nearest && !(distance < nearest_distance))
            continue;
        nearest_distance = static_cast<float>(distance);
        nearest = other;
    }
    return nearest;
}

// 0x004246F0
void KinematicsModule::trail_behind_lead(script::Struct* params)
{
    if (params->has_bare_flag(kOff)) {
        motion_flags_ &= ~kMotionFollowingLeader;
        if (FollowTrail* trail = trail_) {
            // (its destructor: the empty 0x005480A0)
            memory::Hub::get_pull()->dealloc(trail);
            trail_ = nullptr;
        }
        return;
    }
    leave_y_unaffected_ = params->has_bare_flag("LeaveYUnaffected");
    motion_flags_ = (motion_flags_ & ~kMotionMoving) | kMotionFollowingLeader;
    static_cast<PinnedObjectModule*>(entity_->fetch_module(kLockObj))->lock_flags_ &= ~kLockFlagLocked;
    if (!trail_) {
        void* memory = memory::alloc_zeroed(sizeof(FollowTrail));
        trail_ = memory ? new (memory) FollowTrail : nullptr;
    }
    trail_->get_reset();
    float distance = 0.0f;
    params->fetch_float(kDistance, &distance, 0);
    trail_->reach_ = distance;
    uint32_t leader = 0;
    params->read_hash_value(kName, &leader, 0);
    trail_->entity_key_ = leader;
    if (params->has_bare_flag(kOrientY))
        trail_->free_facing_ = true;
}

// --- script member functions ---

// 0x00425150
bool KinematicsModule::obj_get_next_obj_on_path(script::Struct* params, script::Script* script)
{
    float range = 100.0f;
    params->fetch_float(kRange, &range, 0);
    range = static_cast<float>(static_cast<double>(range) * 12.0f); // (feet)
    script->args_->erase_module(kOb);
    if (GameEntity* next = get_next_obj_on_path(range))
        script->args_->put_name_key(kOb, next->ident_);
    return true;
}

// 0x004251C0
bool KinematicsModule::obj_set_path_turn_dist(script::Struct* params, script::Script*)
{
    float distance = 0.0f; // (the original's is whatever its stack slot held: only read once found)
    if (params->fetch_float(0u, &distance, 0)) {
        if (!params->has_bare_flag(kInches))
            distance = static_cast<float>(static_cast<double>(distance) * 12.0f);
        init_path_follower(entity_);
        path_follower_->corner_distance_ = distance;
    }
    return true;
}

// 0x00425220
bool KinematicsModule::obj_follow_path(script::Struct* params, script::Script*)
{
    motion_flags2_ &= ~kMotion2StoppedOnPath;
    uint32_t node;
    if (params->read_hash_value(kName, &node, 0)) {
        start_path(node);
        motion_flags_ = (motion_flags_ & ~kMotionMoving) | kMotionFollowingPath;
    }
    return true;
}

// 0x00425270
bool KinematicsModule::obj_follow_path_linked(script::Struct* params, script::Script* script)
{
    motion_flags2_ &= ~kMotion2StoppedOnPath;
    follow_path_linked(params, script);
    return true;
}

// 0x00425290
bool KinematicsModule::obj_follow_path_stored(script::Struct*, script::Script*)
{
    start_path(saved_node_id_);
    motion_flags_ = (motion_flags_ & ~kMotionMoving) | kMotionFollowingPath;
    return true;
}

// 0x004252C0
bool KinematicsModule::obj_follow_leader(script::Struct* params, script::Script*)
{
    trail_behind_lead(params);
    return true;
}

} // namespace objects

