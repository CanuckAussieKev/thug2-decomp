// The skater's collision feelers: the component's own feeler check (what was hit: skatable, vert, a wall), the
// side collisions that push the skater off walls, the rail and ground probes, and its script member functions.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/maths.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/shadow_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/park_editing/park_editor.h"
#include "game/rider/skater_state.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kWallNonSkatableAngle = 0x3EEDE4D3; // Wall_Non_Skatable_Angle (degrees)
constexpr uint32_t kRight = 0x4B358AEB;                // Right
constexpr uint32_t kLedge = 0x315D9ED4;                // Ledge (a member function)
constexpr uint32_t kObjMoveToNode = 0x8819DD8B;        // Obj_MoveToNode (a member function)
constexpr uint32_t kMotion = 0x0A015E17;               // motion (a component)
constexpr uint32_t kShadow = 0x8A897DD2;               // Shadow (a component)
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;  // SkaterFlipAndRotate (a component)
constexpr uint32_t kNoReset = 0xD607E2E6;              // NoReset
constexpr uint32_t kOrient = 0x90A91232;               // orient
constexpr uint32_t kNose = 0x7F03932C;                 // nose
constexpr uint32_t kTail = 0x83C84BA2;                 // tail
constexpr uint32_t kLeft = 0x85981897;                 // left
constexpr uint32_t kAxisX = 0x7323E97C;                // x
constexpr uint32_t kAxisZ = 0x9D2D8850;                // z
constexpr uint32_t kOff = 0xD443A2BC;                  // Off
constexpr uint32_t kSideCollideHeight = 0xB45B39E2;    // Skater_side_collide_height
constexpr uint32_t kLip = 0xA549B57B;                  // Lip
constexpr uint32_t kLipHorizOffset = 0xB92CBE3B;       // SkateInAble_LipHorizOffset
constexpr uint32_t kLipDownOffset = 0xEFDFE781;        // SkateInAble_LipDownOffset
constexpr uint32_t kLipExtraHorizOffset = 0x108613CB;  // SkateInAble_LipExtraCheckHorizOffset
constexpr uint32_t kLipExtraDownOffset = 0xF9DC446D;   // SkateInAble_LipExtraCheckDownOffset
constexpr uint32_t kSidewaysShift = 0xC78EBE56;        // SkateInAble_HorizOffset
constexpr uint32_t kDownOffset = 0xFCA3378C;           // SkateInAble_DownOffset
constexpr uint32_t kCount = 0x7A26BB9D;                // count
constexpr uint32_t kAmmoMeterChange = 0x0C8E5500;      // ammo_meter_change (a script)
constexpr uint32_t kDir = 0x455485EF;                  // dir
constexpr uint32_t kUpLeft = 0xB7231A95;               // UpLeft
constexpr uint32_t kUpRight = 0xA50950C5;              // UpRight
constexpr uint32_t kDownLeft = 0xD8847EFA;             // DownLeft
constexpr uint32_t kDownRight = 0x786B8B68;            // DownRight
constexpr uint32_t kDown = 0xE3006FC4;                 // Down

// The feeler's hit (the feeler at +0xA8: its CollisionHit at +0xCC).

// Face flags (the feeler's hit).
constexpr uint16_t kFaceSkatable = 0x1;
constexpr uint16_t kFaceNotSkatable = 0x2;
constexpr uint16_t kFaceWall = 0x4;
constexpr uint16_t kFaceVert = 0x8;

constexpr float kRadiansPerDegree = 0.0174532924f; // 0x00645464
constexpr float kNearGroundReach = 16.0f;          // 0x00645D68
constexpr float kNearGroundHeight = 20.0f;         // 0x006486AC
constexpr float kRailEndDistance = 6.0f;           // 0x00646284
// The natas exit's turns about Y (radians; the code's immediates).
constexpr float kEighthTurn = 0.785398185f;     // 0x3F490FDB
constexpr float kQuarterTurn = 1.57079637f;     // 0x3FC90FDB
constexpr float kThreeEighthsTurn = 2.3561945f; // 0x4016CBE4
constexpr float kHalfTurn = 3.14159274f;        // 0x40490FDB

objects::GameEntity* skater_object(SkaterPhysicsModule* component)
{
    return static_cast<objects::GameEntity*>(object_of(component));
}

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C): out = a * b.
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// 0x0057CB60 (GameObjects::GrindRailHub::get_absolute_pos): a rail node's position.
maths::Vec4 rail_node_position(void* manager, const void* node)
{
    maths::Vec4 position;
    static_cast<objects::GrindRailHub*>(manager)->get_absolute_pos(&position,
                                                                   static_cast<const objects::GrindPoint*>(node));
    return position;
}

} // namespace

// 0x005003A0
bool SkaterPhysicsModule::probe_touches_zone(uint32_t ignore, uint32_t require)
{
    auto* feeler = &feeler_;
    feeler->change_strand(&probe_start_, &probe_end_);
    feeler->set_excluded(static_cast<uint16_t>(ignore), static_cast<uint16_t>(require));
    uint8_t hit = static_cast<uint8_t>(feeler->get_hit_shape(true, false));
    if (!hit)
        return false;
    if (feeler_.hit_.object_checksum_ && feeler_.hit_.face_flag40_)
        feeler_.hit_.unknown38 = 0;
    uint16_t flags = feeler_.hit_.face_flags_;
    float normal_y = feeler_.surface_dir_.y;
    hit_wall_ = 0;
    hit_vert_ = (flags & kFaceVert) ? 1 : 0;
    bool skatable;
    if (flags & kFaceSkatable) {
        skatable = true;
    } else if (flags & kFaceNotSkatable) {
        skatable = false;
    } else if (flags & kFaceVert) {
        skatable = true;
    } else if (flags & kFaceWall) {
        hit_skatable_ = 0;
        hit_wall_ = 1;
        return true;
    } else {
        // Neither: skatable unless steeper than Wall_Non_Skatable_Angle (its sine above the normal's y).
        double limit = std::sin(static_cast<double>(script::fetch_float(kWallNonSkatableAngle)) * kRadiansPerDegree);
        skatable = !(limit > normal_y);
    }
    hit_skatable_ = skatable ? 1 : 0;
    return true;
}

// 0x00500020
bool SkaterPhysicsModule::can_grind_rail(const void* rail, const void*, bool any_time)
{
    struct RailNode {
        uint8_t unknown[0x40];
        const RailNode* previous; // +0x40
        const RailNode* next;     // +0x44
    };
    const auto* node = static_cast<const RailNode*>(rail);
    objects::RiderPoseState* flags = state_;
    const auto* last = reinterpret_cast<const RailNode*>(rail_node_);
    auto same_rail = [&] { return last == node || last == node->previous || last == node->next; };
    // In the air after a grind: not the same rail (or its neighbours) again.
    if (flags->cur_phase_ == 1 && flags->flag(PoseFlag::kFromAcidDrop).on && flags->flag(PoseFlag::kJumpedOffRail).on &&
        same_rail())
        return false;

    bool new_rail = false;
    if (!last) {
        new_rail = true;
    } else if (!same_rail()) {
        // Another rail. In a created park, only if its ends are all more than 6 from the last one's (pieces' rails
        // meet end to end).
        if (park_editing::g_park_editor->cur_phase_ == kOff || !last->previous || !node->previous) {
            new_rail = true;
        } else {
            // (Each pair in the original's order: this rail's end first, then the last one's.)
            auto between = [this](const void* from, const void* to) {
                maths::Vec4 a = rail_node_position(rail_manager_, from);
                maths::Vec4 b = rail_node_position(rail_manager_, to);
                return b - a;
            };
            maths::Vec4 d1 = between(node, reinterpret_cast<const RailNode*>(rail_node_));
            maths::Vec4 d2 = between(node->previous, reinterpret_cast<const RailNode*>(rail_node_));
            const RailNode* last_previous = reinterpret_cast<const RailNode*>(rail_node_)->previous;
            maths::Vec4 d3 = between(node, last_previous);
            last_previous = reinterpret_cast<const RailNode*>(rail_node_)->previous;
            maths::Vec4 d4 = between(node->previous, last_previous);
            new_rail = std::fabs(length_unrounded(d1)) > kRailEndDistance &&
                       std::fabs(length_unrounded(d2)) > kRailEndDistance &&
                       std::fabs(length_unrounded(d3)) > kRailEndDistance &&
                       std::fabs(length_unrounded(d4)) > kRailEndDistance;
        }
    }
    if (new_rail)
        set_timed_flag(this, PoseFlag::kLipToAir, 1);

    // The same rail: only after the wait since leaving it.
    if (!state_->flag(PoseFlag::kLipToAir).on &&
        get_since(static_cast<uint32_t>(rail_time_)) <= static_cast<uint32_t>(rail_wait_))
        return false;
    if (any_time)
        return true;
    flags = state_;
    if (flags->cur_phase_ == 4)
        return false;
    // (On a vert wall, only while moving up.)
    if (flags->flag(PoseFlag::kVertWall).on && !(entity_->vel_.y > 0.0f))
        return false;
    return true;
}

// 0x005012B0
int SkaterPhysicsModule::near_ground(script::Struct* params, script::Script*)
{
    bool right = params->find_component(kRight) != nullptr;
    objects::GameEntity* object = object_of(this);
    const maths::Vec4& side = object->basis_frame_.row[0];
    const maths::Vec4& position = object->at_point_;
    double x = static_cast<double>(side.x) * kNearGroundReach; // (left unrounded)
    float y = side.y * kNearGroundReach, z = side.z * kNearGroundReach, w = side.w * kNearGroundReach;
    maths::Vec4& start = probe_start_;
    maths::Vec4& end = probe_end_;
    // A feeler 20 up and down, a little to the side (Right: the other way).
    float origin_coord_x, origin_coord_y;
    if (right) {
        origin_coord_x = static_cast<float>(position.x - x);
        origin_coord_y = position.y - y;
        start.z = position.z - z;
        start.w = position.w - w;
    } else {
        origin_coord_x = static_cast<float>(x + position.x);
        origin_coord_y = y + position.y;
        start.z = z + position.z;
        start.w = w + position.w;
    }
    start.x = origin_coord_x;
    start.y = origin_coord_y;
    start.y = start.y + kNearGroundHeight;
    end = start;
    end.y = end.y - kNearGroundHeight;
    return probe_touches_zone(0x10, 0) ? 1 : 0;
}

// 0x00500F10
int SkaterPhysicsModule::skate_in_able(script::Struct* params, script::Script*)
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& start = probe_start_;
    maths::Vec4& end = probe_end_;
    // (each line's x - and across the skater its y - from an unrounded product, as the original's FPU code has them)
    if (params->has_bare_flag(kLip)) {
        // Down from just behind the position (back along up), then from a little further.
        float back = tuning_float_lookup(kLipHorizOffset, object);
        float down = tuning_float_lookup(kLipDownOffset, object);
        end = entity_->at_point_;
        end.y = end.y - down;
        const maths::Vec4& up = entity_->basis_frame_.row[1];
        start.x = static_cast<float>(end.x - static_cast<double>(back) * up.x);
        start.y = end.y - back * up.y;
        start.z = end.z - back * up.z;
        start.w = end.w - back * up.w;
        if (!probe_touches_zone(0x10, 0)) {
            back = tuning_float_lookup(kLipExtraHorizOffset, object);
            down = tuning_float_lookup(kLipExtraDownOffset, object);
            const maths::Vec4& position = entity_->at_point_;
            const maths::Vec4& up2 = entity_->basis_frame_.row[1];
            start.x = static_cast<float>(position.x - static_cast<double>(back) * up2.x);
            start.y = position.y - back * up2.y;
            start.z = position.z - back * up2.z;
            start.w = position.w - back * up2.w;
            end = start;
            end.y = end.y - down;
            if (!probe_touches_zone(0x10, 0))
                return 0;
        }
        return hit_vert_ ? 1 : 0;
    }
    // Across the skater: from `across` to one side (lowered) to as far to the other.
    float across = tuning_float_lookup(kSidewaysShift, object);
    const maths::Vec4& side = entity_->basis_frame_.row[0];
    const maths::Vec4& position = entity_->at_point_;
    maths::Vec4 near_side;
    near_side.x = static_cast<float>(static_cast<double>(across) * side.x + position.x);
    near_side.y = static_cast<float>(static_cast<double>(across) * side.y + position.y);
    near_side.z = across * side.z + position.z;
    near_side.w = across * side.w + position.w;
    float down = tuning_float_lookup(kDownOffset, object);
    near_side.y = near_side.y - down;
    const maths::Vec4& side2 = entity_->basis_frame_.row[0];
    const double twice = static_cast<double>(across) + across;
    maths::Vec4 far_side;
    far_side.x = static_cast<float>(near_side.x - twice * side2.x);
    far_side.y = near_side.y - static_cast<float>(twice * side2.y);
    far_side.z = near_side.z - static_cast<float>(twice * side2.z);
    far_side.w = near_side.w - static_cast<float>(twice * side2.w);
    if (params->has_bare_flag(kLeft)) {
        start = near_side;
        end = far_side;
    } else {
        start = far_side;
        end = near_side;
    }
    if (!probe_touches_zone(0x10, 0))
        return 0;
    return hit_vert_ ? 1 : 0;
}

// 0x005013E0
int SkaterPhysicsModule::set_ammo(script::Struct* params, script::Script*)
{
    int& ammo = ammo_;
    int before = ammo;
    params->fetch_int_value(uint32_t{0}, &ammo, 0); // (the unnamed integer)
    int change = ammo - before;
    script::Struct* changed = script::spawn_param_bag();
    changed = changed ? new (changed) script::Struct : nullptr;
    changed->put_int(kCount, change); // (on a null structure if out of memory, as the original)
    script::end_child_scripts_of_entity_by_name(skater_object(this), kAmmoMeterChange, nullptr);
    skater_object(this)->spawn_script(kAmmoMeterChange, changed, 0, false);
    if (changed) {
        changed->get_clear();
        script::delete_struct(changed);
    }
    return 1;
}

// 0x005014E0 (some turns by Mat4::RotateY, the others by a rotation matrix and D3DXMatrixMultiply, as the original
// has them)
int SkaterPhysicsModule::check_natas_exit_direction(script::Struct* params, script::Script*)
{
    uint32_t dir = 0; // (left as it was on the stack without one, in the original)
    params->read_hash_value(kDir, &dir, 1);
    maths::Mat4 m;
    m = static_cast<objects::GameEntity*>(static_cast<objects::Rider*>(skater_object(this))->get_skater_camera())
            ->basis_frame_;
    switch (dir) {
    case kDownRight:
        m.rotate_y(kEighthTurn);
        break;
    case kRight:
        m.rotate_y(kQuarterTurn);
        break;
    case kLeft:
        m.rotate_y(-kQuarterTurn);
        break;
    case kUpRight:
        m.rotate_y(kThreeEighthsTurn);
        break;
    case kDown:
        m.rotate_y(0.0f);
        break;
    default: { // UpLeft, DownLeft; Up and anything else half a turn
        float turn = dir == kUpLeft ? -kThreeEighthsTurn : dir == kDownLeft ? -kEighthTurn : kHalfTurn;
        maths::Mat4 rotation;
        matrix_multiply(&m, &m, &maths::make_yaw_matrix(rotation, turn));
        break;
    }
    }
    entity_->basis_frame_ = m;
    return 1;
}

// 0x00501600
int SkaterPhysicsModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name == kLedge)
        return on_ledge_ ? 1 : 0;
    if (name != kObjMoveToNode) {
        // Not original - THAW's bike member functions (SkaterOnBike, IsSkaterOnBike...: game/rider/thaw_bmx.h)
        // while [features] thaw_bmx is on.
        // Original behaviour: any other name isn't this component's (2).
        if (thaw_bmx::is_switched_on())
            return thaw_bmx::invoke_method_by_name(this, name, params, script);
        return 2;
    }
    // Obj_MoveToNode: the motion component moves the skater; then everything that follows the position is put there.
    objects::GameEntity* object = skater_object(this);
    objects::ObjectModule* motion = object->fetch_module(kMotion);
    bool moved = motion->invoke_method_by_name(kObjMoveToNode, params, script) != 0;
    display_matrix_ = entity_->basis_frame_;
    auto* shadow = static_cast<objects::GroundShadowModule*>(skater_object(this)->fetch_module(kShadow));
    shadow->change_place(&entity_->at_point_);
    shadow->set_normal(&entity_->basis_frame_.row[1]);
    safe_pos_ = entity_->at_point_;
    entity_->old_pos_ = entity_->at_point_;
    skater_object(this)->mark_jumped_ahead(true);
    if (entity_->is_local_skater_) {
        // (the skater's camera follows at once)
        void* camera = static_cast<objects::Rider*>(object_of(this))->get_skater_camera();
        if (camera)
            static_cast<objects::GameEntity*>(camera)->tick_frame();
    }
    if (!params->has_bare_flag(kNoReset)) {
        floor_feeler_.hit_.face_flag40_ = 0;
        entity_->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
        physics::change_phase(this, 0);
        set_timed_flag(this, PoseFlag::kFlipped, !static_cast<objects::Rider*>(entity_)->goofy_);
        static_cast<objects::RiderSpinAndFlip*>(skater_object(this)->fetch_module(kSkaterFlipAndRotate))
            ->apply_flip_to_anims();
    } else if (params->has_bare_flag(kOrient)) {
        // The horizontal speed kept, along the new facing.
        maths::Vec4& velocity = entity_->vel_;
        float upward = velocity.y;
        velocity.y = 0.0f;
        objects::GameEntity* o = object_of(this);
        float speed = static_cast<float>(length_unrounded(o->vel_));
        o->vel_ = o->basis_frame_.row[2] * speed;
        entity_->vel_.y = upward;
    }
    return moved ? 1 : 0;
}

// 0x005009C0
void SkaterPhysicsModule::find_nearest_wall_side()
{
    render::ProbeRay feeler;
    nearest_wall_side_ = 0;
    feeler.line_.start = entity_->at_point_;
    float nearest = 999.0f;
    // Each end and side of the board: 12 along the forward or side row.
    static const struct {
        uint32_t side;
        uint32_t axis;
        float distance;
    } kSides[4] = {{kNose, kAxisZ, 12.0f}, {kTail, kAxisZ, -12.0f}, {kLeft, kAxisX, 12.0f}, {kRight, kAxisX, -12.0f}};
    for (const auto& side : kSides) {
        const maths::Vec4& row = side.axis == kAxisX ? entity_->basis_frame_.row[0] : entity_->basis_frame_.row[2];
        const maths::Vec4& start = feeler.line_.start;
        double distance = side.distance;
        maths::Vec4& end = feeler.line_.end;
        end.x = static_cast<float>(distance * row.x + start.x);
        end.y = static_cast<float>(distance * row.y + start.y);
        end.z = static_cast<float>(distance * row.z + start.z);
        end.w = static_cast<float>(distance * row.w + start.w);
        if (feeler.get_hit_shape(true, false) && feeler.reach_ < nearest) {
            nearest = feeler.reach_;
            nearest_wall_side_ = side.side;
        }
    }
}

// 0x00501C90
void SkaterPhysicsModule::check_side_feeler(const maths::Vec4* start, const maths::Vec4* end, uint32_t side)
{
    // (The original's loop over the other three sides checks the opposite side's time each time round.)
    for (uint32_t other = 0; other < 4; ++other) {
        if (other != side && get_since(static_cast<uint32_t>(side_hit_times_[side ^ 1])) > 500)
            return;
    }
    probe_start_ = *start;
    probe_end_ = *end;
    if (!probe_touches_zone(0x10, 0))
        return;
    maths::Vec4& sum = side_normal_sum_;
    const maths::Vec4& normal = feeler_.surface_dir_;
    sum.x = normal.x + sum.x;
    sum.y = normal.y + sum.y;
    sum.z = normal.z + sum.z;
    sum.w = normal.w + sum.w;
    if (feeler_.reach_ < side_nearest_)
        side_nearest_ = feeler_.reach_;
    side_hit_times_[side] = get_now();
}

// 0x005055B0
void SkaterPhysicsModule::check_side_feelers()
{
    if (side_reach_ == 0.0f || side_push_ == 0.0f)
        return;
    side_normal_sum_ = {0.0f, 0.0f, 0.0f, 1.0f};
    side_nearest_ = 1.0f;
    // The feelers start at the side collision height up the skater.
    objects::GameEntity* object = object_of(this);
    double height = script::fetch_float(kSideCollideHeight);
    const maths::Vec4& up = object->basis_frame_.row[1];
    const maths::Vec4& position = entity_->at_point_;
    maths::Vec4 base;
    base.x = static_cast<float>(height * up.x + position.x);
    base.y = static_cast<float>(height * up.y) + position.y;
    base.z = static_cast<float>(height * up.z) + position.z;
    base.w = static_cast<float>(height * up.w) + position.w;
    maths::Vec4 start = base;
    // Sides 0 and 1: along the side row and back; 2 and 3: along the forward row and back.
    for (uint32_t side = 0; side < 4; ++side) {
        const maths::Vec4& row = side < 2 ? entity_->basis_frame_.row[0] : entity_->basis_frame_.row[2];
        double reach = side_reach_;
        float x = static_cast<float>(reach * row.x), y = static_cast<float>(reach * row.y),
              z = static_cast<float>(reach * row.z);
        double w = reach * row.w; // (left unrounded)
        maths::Vec4 end;
        if (side % 2 == 0) {
            end.x = x + base.x;
            end.y = y + base.y;
            end.z = z + base.z;
            end.w = static_cast<float>(w + base.w);
        } else {
            end.x = base.x - x;
            end.y = base.y - y;
            end.z = base.z - z;
            end.w = static_cast<float>(base.w - w);
        }
        check_side_feeler(&start, &end, side);
    }
    if (bits_as<uint32_t>(side_nearest_) == 0x3F800000u) // (nothing hit: still 1)
        return;
    // Pushed off by the walls hit, the nearer the harder; turned along a wall it faces into.
    maths::Vec4& push = side_normal_sum_;
    set_length(push, static_cast<float>((1.0 - side_nearest_) * side_push_));
    maths::Vec4& velocity = entity_->vel_;
    velocity.x = push.x + velocity.x;
    velocity.y = push.y + velocity.y;
    velocity.z = push.z + velocity.z;
    velocity.w = push.w + velocity.w;
    entity_->vel_.align_to_plane(floor_normal_);
    object = object_of(this);
    const maths::Vec4& forward = object->basis_frame_.row[2];
    const maths::Vec4& normal = feeler_.surface_dir_;
    double into = static_cast<double>(forward.z) * normal.z + static_cast<double>(forward.y) * normal.y +
                  static_cast<double>(forward.x) * normal.x;
    if (!(into < 0.0))
        return;
    maths::Vec4 along = forward;
    along.align_to_plane(normal);
    float angle = static_cast<float>(
        maths::get_turn_around_axis(entity_->basis_frame_.row[2], along, entity_->basis_frame_.row[1]));
    double turn = static_cast<double>(maths::get_sign(angle)) * side_turn_ * frame_length_;
    float step = static_cast<float>(turn);
    if (std::fabs(static_cast<double>(angle)) < std::fabs(turn))
        step = angle;
    entity_->basis_frame_.yaw_locally(step);
    display_matrix_.yaw_locally(step);
}

// 0x005054A0
bool SkaterPhysicsModule::apply_model_rotations()
{
    auto* model = static_cast<objects::MeshInstanceModule*>(skater_object(this)->fetch_module(0x286A8D26)); // (Model)
    if (!model->twist_rate_[0].active_ && !model->twist_rate_[1].active_ && !model->twist_rate_[2].active_)
        return false;
    objects::GameEntity* object = object_of(this);
    maths::Vec4 axis = object->basis_frame_.row[1];
    model->get_draw_matrix(&object->basis_frame_);
    objects::ModelSpinAxis* y_rotation = &model->twist_rate_[1];
    float angle = y_rotation->bearing();
    if (angle != 0.0f) {
        // The velocity turned with the model about the up axis.
        maths::Vec4& velocity = entity_->vel_;
        alignas(16) uint8_t rotation[0x10]; // (a quaternion, it seems)
        auto* quat = reinterpret_cast<maths::QuatRotation*>(rotation);
        quat->init_from_axis_angle(axis, angle);
        velocity = *quat->spin_by(&axis, velocity);
    }
    display_matrix_ = entity_->basis_frame_;
    model->twist_rate_[0].get_clear();
    y_rotation->get_clear();
    model->twist_rate_[2].get_clear();
    return true;
}

} // namespace skater
