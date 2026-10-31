// The skater's core physics component: the wall ride state's update.

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/ride_tracker.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/trick_component.h"
#include "engine/objects/trigger_events.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kWallRideGravity = 0x0C617CAF;   // Wall_Ride_Gravity
constexpr uint32_t kWallRideTurnSpeed = 0x64A48A64; // Wall_Ride_Turn_Speed
constexpr uint32_t kForwardLength = 0x20102726;     // Skater_First_Forward_Collision_Length
constexpr uint32_t kGroundSnapUp = 0xE4D79235;      // Physics_Ground_Snap_Up
constexpr uint32_t kDownCheckLength = 0x438BA168;   // Wall_Ride_Down_Collision_Check_Length
constexpr uint32_t kJumpOutSpeed = 0x349D02D4;      // Wall_Ride_Jump_Out_Speed
constexpr uint32_t kJumpUpSpeed = 0x5A9DE35A;       // Wall_Ride_Jump_Up_Speed
constexpr uint32_t kMatrixQueries = 0xBF518E27;     // SkaterMatrixQueries (a component)
constexpr uint32_t kLanded = 0x532B16EF;            // Landed (event)
constexpr uint32_t kOffEdge = 0xD96F01F1;           // SkaterOffEdge (event)
constexpr uint32_t kGroundGone = 0x3B1001B6;        // GroundGone (event)

constexpr uint32_t kWallFaceVector = 0x38C; // the wall's normal

constexpr float kHeadingUp = 0.68f; // 0x0064BDB4: heading up the wall this much: no turning
constexpr float kTopFacesUp = 0.9f; // 0x00649FE4: a hit ahead facing up this much: the wall's top
constexpr float kPushOff = 18.0f;   // 0x0064AAAC
constexpr float kPushAway = 100.0f; // 0x006463D4

const maths::Vec4 kUpAxis = {0.0f, 1.0f, 0.0f, 1.0f};

void change_state(void* component, int state)
{
    static_cast<SkaterPhysicsModule*>(component)->set_physics_state(state);
}
void set_up(void* component, const maths::Vec4& up)
{
    static_cast<SkaterPhysicsModule*>(component)->turn_to_normal(up);
}
// velocity or position += normal * scale, x and y from the unrounded products (as the original).
void add_scaled(maths::Vec4& to, const maths::Vec4& normal, float scale)
{
    float z = normal.z * scale, w = normal.w * scale;
    to.x = static_cast<float>(static_cast<double>(normal.x) * scale + to.x);
    to.y = static_cast<float>(static_cast<double>(normal.y) * scale + to.y);
    to.z = z + to.z;
    to.w = w + to.w;
}

} // namespace

// (Part of 0x0050F830) Off the wall's edge: the up straight, pushed out 18 from the wall, into the air.
void SkaterPhysicsModule::off_wall_edge(bool reset_display)
{
    set_up(this, kUpAxis);
    if (reset_display)
        matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
    add_scaled(vector_at(object_of(this), kPosition), vector_at(this, kWallFaceVector), kPushOff);
    change_state(this, kAir);
    static_cast<objects::Entity*>(object_of(this))->announce(kOffEdge, nullptr, 0.0f);
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kGroundGone, nullptr);
}

// 0x0050F830
void SkaterPhysicsModule::step_wall_ride()
{
    set_flag(this, 0x78, 0);
    set_flag(this, 0xA8, 0);
    set_flag(this, 0x80, 0);
    set_flag(this, 0xB0, 0);
    get_at<uint8_t>(this, 0x2E4) = 0;
    get_at<void*>(this, kRailNode) = nullptr;
    get_at<uint32_t>(this, kLastRailScript) = 0;
    get_at<int>(this, 0x39C) = get_now();

    // What the skater rides on carries it (its turn too).
    maths::Vec4 carried = {0.0f, 0.0f, 0.0f, 1.0f};
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity_.owner_object_ || !tracker->tick_frame(&vector_at(object_of(this), kPosition))) {
            stop_riding(this);
        } else {
            carried = get_tracker(this)->carried_;
            if (get_tracker(this)->turned_) {
                auto multiply = d3dx::matrix_multiply;
                maths::Mat4& m = matrix_at(object_of(this), kMatrix);
                multiply(&m, &m, &get_tracker(this)->turn_);
                multiply(&matrix_at(this, kDisplayMatrix), &matrix_at(this, kDisplayMatrix), &get_tracker(this)->turn_);
            }
        }
    }

    // The move: velocity * t + (0, gravity, 0, 1) * t * t / 2 (rounded as the original), plus the carry; then the
    // velocity += (0, gravity, 0, 1) * t.
    void* object = object_of(this);
    float gravity = tuning_float_lookup(kWallRideGravity, object);
    {
        float t = get_at<float>(this, kFrameLength);
        double half = static_cast<double>(t) * t * 0.5f;
        double zero_half = 0.0f * half;
        float zero_half_rounded = static_cast<float>(zero_half);
        float gravity_half = static_cast<float>(half * gravity);
        float half_rounded = static_cast<float>(half);
        maths::Vec4& v = vector_at(object_of(this), kVelocity);
        double tx = static_cast<double>(t) * v.x;
        float ty = t * v.y, tz = t * v.z, tw = t * v.w;
        float mx = static_cast<float>(tx + zero_half_rounded);
        float my = ty + gravity_half;
        double mz = zero_half + tz;
        double mw = static_cast<double>(tw) + half_rounded;
        maths::Vec4& p = vector_at(object_of(this), kPosition);
        p.x = mx + p.x;
        p.y = my + p.y;
        p.z = static_cast<float>(mz + p.z);
        p.w = static_cast<float>(mw + p.w);
        p.x = carried.x + p.x;
        p.y = carried.y + p.y;
        p.z = carried.z + p.z;
        p.w = carried.w + p.w;
        float tt = get_at<float>(this, kFrameLength);
        double zero_t = 0.0f * static_cast<double>(tt);
        float fall = tt * gravity;
        maths::Vec4& u = vector_at(object_of(this), kVelocity);
        u.x = static_cast<float>(zero_t) + u.x;
        u.y = fall + u.y;
        u.z = static_cast<float>(zero_t + u.z);
        u.w = tt + u.w;
    }

    // Up the wall: the wall's level tangent (normalised) crossed with its normal.
    const maths::Vec4& wall = vector_at(this, kWallFaceVector);
    double tangent_z = wall.z, tangent_x = -static_cast<double>(wall.x);
    float tangent_y = 0.0f;
    double tangent_length = std::sqrt(tangent_z * tangent_z + tangent_x * tangent_x);
    if (tangent_length > 0.0) {
        double inverse = 1.0 / tangent_length;
        tangent_z *= inverse;
        tangent_y = static_cast<float>(0.0 * inverse);
        tangent_x *= inverse;
    }
    maths::Vec4 up_wall = carried;
    up_wall.x = static_cast<float>(tangent_y * wall.z - tangent_x * wall.y);
    up_wall.y = static_cast<float>(tangent_x * wall.x - tangent_z * wall.z);
    up_wall.z = static_cast<float>(tangent_z * wall.y - tangent_y * wall.x);

    // Turned towards up the wall when not heading up it enough (a turn's cosine at most).
    float turn = script::fetch_float(kWallRideTurnSpeed);
    const maths::Vec4& facing = vector_at(object_of(this), kFacing);
    double heading = (static_cast<double>(up_wall.z) * facing.z + static_cast<double>(up_wall.y) * facing.y) +
                     static_cast<double>(up_wall.x) * facing.x;
    bool turning = !(heading > kHeadingUp) && !(heading > 0.0 && heading > std::cos(static_cast<double>(turn)));
    if (turning) {
        const maths::Vec4& right = vector_at(object_of(this), kRight);
        double side = (static_cast<double>(up_wall.z) * right.z + static_cast<double>(up_wall.y) * right.y) +
                      static_cast<double>(up_wall.x) * right.x;
        if (side <= 0.0)
            turn = -turn;
        void* o = object_of(this);
        vector_at(o, kVelocity).w = 1.0f;
        vector_at(object_of(this), kVelocity)
            .rotate_about_axis(vector_at(this, kWallFaceVector), turn); // (about the wall)
        o = object_of(this);
        vector_at(o, kFacing) = vector_at(o, kVelocity);
        vector_at(object_of(this), kFacing).make_unit();
        o = object_of(this);
        vector_at(o, kUp) = vector_at(this, kWallFaceVector);
        const maths::Vec4& f = vector_at(o, kFacing);
        const maths::Vec4& u = vector_at(o, kUp);
        maths::Vec4& r = vector_at(o, kRight);
        float rx = static_cast<float>(static_cast<double>(f.z) * u.y - static_cast<double>(f.y) * u.z);
        float ry = static_cast<float>(static_cast<double>(f.x) * u.z - static_cast<double>(f.z) * u.x);
        float rz = static_cast<float>(static_cast<double>(f.y) * u.x - static_cast<double>(f.x) * u.y);
        r.w = 0.0f;
        r.z = rz;
        r.x = rx;
        r.y = ry;
        vector_at(object_of(this), kRight).make_unit();
        matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
    }

    // A feeler ahead along the move (Skater_First_Forward_Collision_Length past the new position).
    {
        void* o = object_of(this);
        const maths::Vec4& p = vector_at(o, kPosition);
        const maths::Vec4& old = vector_at(o, kOldPosition);
        maths::Vec4 move = {p.x - old.x, p.y - old.y, p.z - old.z, p.w - old.w};
        double moved = std::sqrt((static_cast<double>(move.z) * move.z + static_cast<double>(move.y) * move.y) +
                                 static_cast<double>(move.x) * move.x);
        if (moved > 0.0) {
            double inverse = 1.0 / moved;
            move.x = static_cast<float>(move.x * inverse);
            move.y = static_cast<float>(move.y * inverse);
            move.z = static_cast<float>(inverse * move.z);
        }
        vector_at(this, kProbeOrigin) = vector_at(o, kOldPosition);
        maths::Vec4& end = vector_at(this, kProbeTip);
        end = vector_at(object_of(this), kPosition);
        float reach = tuning_float_lookup(kForwardLength, object_of(this));
        double ahead_x = static_cast<double>(move.x) * reach;
        float ahead_y = reach * move.y, ahead_z = reach * move.z, ahead_w = reach * move.w;
        end.x = static_cast<float>(ahead_x + end.x);
        end.y = ahead_y + end.y;
        end.z = ahead_z + end.z;
        end.w = ahead_w + end.w;
    }
    if (get_feel(this, 0x10, 0)) {
        const maths::Vec4 hit = vector_at(this, kHitNormal);
        const maths::Vec4& w = vector_at(this, kWallFaceVector);
        double along_wall =
            (static_cast<double>(hit.z) * w.z + static_cast<double>(hit.y) * w.y) + static_cast<double>(hit.x) * w.x;
        const maths::Vec4& point = vector_at(this, kHitPoint);
        maths::Vec4& p = vector_at(object_of(this), kPosition);
        p.z = hit.z + point.z;
        p.w = hit.w + point.w;
        p.x = hit.x + point.x;
        p.y = hit.y + point.y;
        if (get_at<uint8_t>(this, 0x22A) && !(std::fabs(along_wall) < 0.01f)) {
            // Round a corner: the new wall.
            vector_at(this, kWallFaceVector) = hit;
            set_up(this, vector_at(this, kWallFaceVector));
            matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
            vector_at(object_of(this), kVelocity).align_to_plane(vector_at(this, kWallFaceVector));
        } else if (hit.y > kTopFacesUp) {
            // Onto the top: the ground (a grind first), upright on it, heading along the velocity.
            void* o = object_of(this);
            vector_at(o, kPosition) = vector_at(this, kHitPoint);
            vector_at(o, kFacing) = vector_at(o, kVelocity);
            vector_at(object_of(this), kFacing).flatten_onto_plane(hit);
            vector_at(object_of(this), kFacing).make_unit();
            vector_at(object_of(this), kUp) = hit;
            void* s = object_of(this);
            vector_at(s, kRight) = maths::cross_vec(vector_at(s, kUp), vector_at(s, kFacing));
            vector_at(object_of(this), kRight).make_unit();
            matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
            auto* queries = static_cast<objects::GameEntity*>(object_of(this))->fetch_module(kMatrixQueries);
            matrix_at(queries, 0x18) = matrix_at(get_at<void*>(queries, 0xC), kMatrix);
            vector_at(this, 0x78) = vector_at(object_of(this), kUp);
            vector_at(this, 0x58) = vector_at(object_of(this), kUp);
            vector_at(this, kFloorUpVector) = vector_at(object_of(this), kUp);
            try_grind(true);
            if (state_of(this) == kRail)
                return;
            change_state(this, kGround);
            static_cast<objects::Entity*>(object_of(this))->post_own_event(kLanded, nullptr);
            return;
        } else {
            off_wall_edge(false);
        }
    }
    if (keep_off_ceiling_at_wall())
        off_wall_edge(false);

    // A feeler down the skater's up: still on the wall?
    {
        void* o = object;
        float snap = tuning_float_lookup(kGroundSnapUp, o);
        const maths::Vec4& up = vector_at(object_of(this), kUp);
        maths::Vec4& start = vector_at(this, kProbeOrigin);
        start = {snap * up.x, snap * up.y, snap * up.z, snap * up.w};
        const maths::Vec4& p = vector_at(object_of(this), kPosition);
        start.x = start.x + p.x, start.y = p.y + start.y, start.z = p.z + start.z, start.w = p.w + start.w;
        float down = script::fetch_float(kDownCheckLength);
        const maths::Vec4& up2 = vector_at(object_of(this), kUp);
        maths::Vec4& end = vector_at(this, kProbeTip);
        end = {down * up2.x, down * up2.y, down * up2.z, down * up2.w};
        const maths::Vec4& q = vector_at(object_of(this), kPosition);
        end.x = end.x + q.x, end.y = q.y + end.y, end.z = q.z + end.z, end.w = q.w + end.w;
    }
    if (get_feel(this, 0x10, 0)) {
        // At the wall (just off it), along it.
        vector_at(this, kWallFaceVector) = vector_at(this, kHitNormal);
        void* o = object_of(this);
        vector_at(o, kPosition) = vector_at(this, kHitPoint);
        maths::Vec4& p = vector_at(object_of(this), kPosition);
        const maths::Vec4& w = vector_at(this, kWallFaceVector);
        p.x = p.x + w.x, p.y = w.y + p.y, p.z = w.z + p.z, p.w = w.w + p.w;
        vector_at(object_of(this), kVelocity).align_to_plane(vector_at(this, kWallFaceVector));
        set_up(this, vector_at(this, kWallFaceVector));
        matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
        if (!get_at<uint8_t>(this, 0x22A)) {
            // Not a wall to ride: pushed away from it into the air.
            add_scaled(vector_at(object_of(this), kVelocity), vector_at(this, kWallFaceVector), kPushAway);
            set_up(this, kUpAxis);
            matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
            change_state(this, kAir);
            static_cast<objects::Entity*>(object_of(this))->announce(kOffEdge, nullptr, 0.0f);
            static_cast<objects::Entity*>(object_of(this))->post_own_event(kGroundGone, nullptr);
            trigger_feeler(this, kOtherFeelerLeaveEvents, kOtherFeeler);
            if (trigger_took_over(this))
                return;
        } else {
            if (get_at<int>(this, 0xFC) != get_at<int>(this, 0x1AC)) {
                trigger_feeler(this, kOtherFeelerLeaveEvents, kOtherFeeler);
                if (trigger_took_over(this))
                    return;
                trigger_feeler(this, kSkaterEvents0064bBb0, kOtherFeeler);
                if (trigger_took_over(this))
                    return;
            }
            void* skater = get_at<void*>(this, kSkaterComponent);
            if (!get_at<uint8_t>(skater, 0x13B0))
                static_cast<objects::MoveExecutor*>(skater)->set_trick_objects(false);
            assign_feeler(&get_at<uint8_t>(this, kOtherFeeler), &get_at<uint8_t>(this, kFeeler));
            get_at<uint8_t>(get_at<void*>(this, kSfxModuleSlot), 0x1D) = get_at<uint8_t>(this, 0x1AA);
        }
    } else {
        off_wall_edge(true);
        trigger_feeler(this, kOtherFeelerLeaveEvents, kOtherFeeler);
        if (trigger_took_over(this))
            return;
    }

    if (!bit_mark(this, 0x20) && input_of(this)[0x1C0])
        set_flag(this, 0x20, 1);
    // Jumping off: out from the wall and up, upright.
    if (jump_released()) {
        set_flag(this, 0xD8, 1);
        trigger_feeler(this, kJumpTriggerEvents, kOtherFeeler);
        if (trigger_took_over(this))
            return;
        float out = script::fetch_float(kJumpOutSpeed);
        maths::Vec4& v = vector_at(object_of(this), kVelocity);
        const maths::Vec4& w = vector_at(this, kWallFaceVector);
        float y = out * w.y, z = out * w.z, ww = out * w.w;
        v.x = static_cast<float>(static_cast<double>(out) * w.x + v.x);
        v.y = y + v.y;
        v.z = z + v.z;
        v.w = ww + v.w;
        float& vy = vector_at(object_of(this), kVelocity).y;
        vy = script::fetch_float(kJumpUpSpeed) + vy;
        vector_at(object_of(this), kUp) = kUpAxis;
        matrix_at(object_of(this), kMatrix).square_up_axes(1);
        matrix_at(this, kDisplayMatrix) = matrix_at(object_of(this), kMatrix);
        get_at<int>(this, 0x88) = 0;
        vector_at(this, 0x58) = vector_at(object_of(this), kUp);
        vector_at(this, kFloorUpVector) = vector_at(this, 0x58);
        vector_at(this, 0x78) = vector_at(this, 0x58);
    }
    static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
        ->pass_to_trick_objects(get_at<uint32_t>(this, 0x1B0));
}

} // namespace skater
