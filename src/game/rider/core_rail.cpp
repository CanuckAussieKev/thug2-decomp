// The skater's core physics component: grinding - the rail state's update and moving on from a rail's end.

#include <cmath>
#include <cstdint>

#include "engine/maths/maths.h"
#include "engine/gfx/d3dx.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/walk_hang.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "game/skate/point_tally.h"
#include "game/park_editing/park_editor.h"
#include "game/skate/skate_module.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/trigger_events.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15; // Rail_Corner_Leave_Angle
constexpr uint32_t kRailRegrabDelay = 0x2B4645AD;       // Rail_minimum_rerail_time
constexpr uint32_t kJumpRerailTime = 0x0BF35053;   // Rail_jump_rerail_time
constexpr uint32_t kRailGravity = 0xD1F46992;      // Physics_Rail_Gravity
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;  // RailManager (a component)
constexpr uint32_t kGrind = 0x255ED86F;            // Grind (the balance trick's kind)
constexpr uint32_t kSlide = 0x8D10119D;            // Slide
constexpr uint32_t kPerfectRail = 0xCD09E062;      // CHEAT_PERFECT_RAIL
constexpr uint32_t kOffRail = 0xAFAA46BA;          // OffRail (event)
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kParkEditorLevel = 0xD443A2BC;

constexpr float kDegrees = 0.0174532924f;
constexpr float kRaise = 6.0f;      // 0x00646284: the transfer line's start raised
constexpr float kRaiseEnd = 6.1f;   // 0x0064BDA8: its end
constexpr float kFeelerAhead = 6.0f; // (the feeler past the move: 6 along it)
constexpr float kTurnFacing = 0.3f; // (how far the display facing turns to the rail each frame)

bool in_park_editor() { return get_at<uint32_t>(reinterpret_cast<uint8_t*>(park_editing::g_park_editor), 0x40) == kParkEditorLevel; }
void change_state(void* component, int state) { static_cast<SkaterPhysicsModule*>(component)->set_physics_state(state); }

maths::Vec4 grind_point_in_world(void* component, const objects::GrindPoint* node)
{
    maths::Vec4 out;
    return *get_at<objects::GrindRailHub*>(component, kRailManager)->get_absolute_pos(&out, node);
}

// -1 for a dot product below 0, else 1 (NaN: 1).
float sign_of(double dot) { return dot < 0.0 ? -1.0f : 1.0f; }
double dot3(const maths::Vec4& a, const maths::Vec4& v)
{
    return (static_cast<double>(a.z) * v.z + static_cast<double>(a.y) * v.y) + static_cast<double>(a.x) * v.x;
}

// A node rails go on from (flag 4 - its >> 2 & 1).
bool continues(const objects::GrindPoint* node) { return (node->flag_bits_ >> 2) & 1; }

// The corner from segment `a` (to - from) into segment `b` (level): turning less than Rail_Corner_Leave_Angle.
bool gentle_corner(maths::Vec4 a, maths::Vec4 b)
{
    a.y = 0.0f;
    b.y = 0.0f;
    a.make_unit();
    b.make_unit();
    float turn = static_cast<float>((static_cast<double>(b.z) * a.z + static_cast<double>(b.y) * a.y) +
                                    static_cast<double>(b.x) * a.x);
    return !(std::cos(static_cast<double>(script::fetch_float(kCornerLeaveAngle)) * kDegrees) > turn);
}

} // namespace

// 0x0050C200
void SkaterPhysicsModule::transfer_or_leave_rail(const maths::Vec4* end)
{
    // The line looked along: from `end` (in the park editor: back along the velocity) to the skater, raised.
    void* object = object_of(this);
    maths::Vec4 start;
    if (!in_park_editor()) {
        start = *end;
    } else {
        const maths::Vec4& p = vector_at(object, kPosition);
        const maths::Vec4& v = vector_at(object, kVelocity);
        float t = get_at<float>(this, kFrameLength);
        float bx = t * v.x, by = t * v.y, bz = t * v.z;
        double bw = static_cast<double>(t) * v.w;
        start.x = p.x - bx;
        start.y = p.y - by;
        start.z = p.z - bz;
        start.w = static_cast<float>(p.w - bw);
    }
    start.y = start.y + kRaise;
    maths::Vec4 stop = vector_at(object_of(this), kPosition);
    stop.y = stop.y + kRaiseEnd;
    float angle = script::fetch_float(kCornerLeaveAngle);
    auto* node = get_at<objects::GrindPoint*>(this, kRailNode);
    int side = node->get_wall_side(&vector_at(object_of(this), kVelocity));
    float alignment = static_cast<float>(std::cos(static_cast<double>(angle) * kDegrees));
    maths::Vec4 point;
    objects::GrindPoint* found = nullptr;
    bool onto = get_at<objects::GrindRailHub*>(this, kRailManager)
                    ->find_grind_rail(&start, &stop, &point, &found, get_at<objects::GrindPoint*>(this, kRailNode), alignment, side) &&
                found && found != get_at<objects::GrindPoint*>(this, kRailNode) && found->succ_;
    if (onto && !in_park_editor()) {
        // (Not this rail's next or last node.)
        auto* current = get_at<objects::GrindPoint*>(this, kRailNode);
        onto = current->succ_ != found && current->last_value_ != found;
    }
    if (onto) {
        objects::GrindPoint* next = found->succ_;
        const maths::Vec4& p = vector_at(object_of(this), kPosition);
        maths::Vec4 found_pos = grind_point_in_world(this, found);
        maths::Vec4 to_found = found_pos - p;
        maths::Vec4 next_pos = grind_point_in_world(this, next);
        maths::Vec4 to_next = next_pos - vector_at(object_of(this), kPosition);
        double toward = (static_cast<double>(to_next.x) * to_found.x + static_cast<double>(to_next.z) * to_found.z) +
                        static_cast<double>(to_next.y) * to_found.y;
        float toward_rounded = static_cast<float>(toward);
        if (!(toward < 0.0)) {
            // Not between its two nodes: never in the park editor; elsewhere only when the move reaches as far as
            // the segment is long - then onto its middle.
            if (!in_park_editor()) {
                maths::Vec4 a = grind_point_in_world(this, found);
                maths::Vec4 b = grind_point_in_world(this, next);
                maths::Vec4 segment = b - a;
                maths::Vec4 move = stop - start;
                float segment_length = static_cast<float>(length_of(segment));
                onto = !(length_of(move) < segment_length);
                if (onto && toward_rounded >= 0.0f) {
                    maths::Vec4 c = grind_point_in_world(this, found);
                    maths::Vec4 d = grind_point_in_world(this, next);
                    point = (d + c) / 2.0f;
                }
            } else {
                onto = false;
            }
        }
    }
    if (!onto) {
        trigger_rail_script(kGrindOffEvents);
        change_state(this, kAir);
        maths::Vec4& p = vector_at(object_of(this), kPosition);
        p.y = p.y + 1.0f;
        static_cast<objects::Entity*>(object_of(this))->post_own_event(kOffRail, nullptr);
        return;
    }
    // Onto it: the velocity along it (the same way), the skater at the point.
    objects::GrindPoint* next = found->succ_;
    maths::Vec4 a = grind_point_in_world(this, found);
    maths::Vec4 b = grind_point_in_world(this, next);
    maths::Vec4 direction = b - a;
    direction.make_unit();
    maths::Vec4& v = vector_at(object_of(this), kVelocity);
    if (v.x == 0.0f && v.z == 0.0f) {
        v.x = vector_at(object_of(this), kFacing).x;
        vector_at(object_of(this), kVelocity).z = vector_at(object_of(this), kFacing).z;
    }
    maths::Vec4& velocity = vector_at(object_of(this), kVelocity);
    float sign = maths::get_sign(static_cast<float>(dot3(direction, velocity)));
    velocity.change_heading(direction);
    vector_at(object_of(this), kVelocity) *= sign;
    get_at<objects::GrindPoint*>(this, kRailNode) = found;
    vector_at(object_of(this), kPosition) = point;
    trigger_rail_script(kGrindEvents);
    set_surface_kind(get_at<objects::GrindPoint*>(this, kRailNode)->surface_type_);
}

// 0x0050DDF0
void SkaterPhysicsModule::step_grind_move()
{
    set_flag(this, 0x78, 0);
    set_flag(this, 0xA8, 0);
    set_flag(this, 0x80, 0);
    set_flag(this, 0xB0, 0);
    get_at<uint8_t>(this, 0x2E4) = 0;
    ride_moving_rail();

    void* object = object_of(this);
    get_at<int>(this, 0x23C) = static_cast<int>(script::fetch_float(kRailRegrabDelay));
    set_flag(this, 0x50, 0);
    // Off the rail (0x004FFB10): the TriggerScripts, the jump's rerail time, up and out.
    if (jump_released()) {
        trigger_rail_script(kJumpTriggerEvents);
        trigger_rail_script(kGrindOffEvents);
        get_at<int>(this, 0x23C) = static_cast<int>(script::fetch_float(kJumpRerailTime));
        set_flag(this, 0xD0, 1);
        set_flag(this, 0xD8, 1);
        maths::Vec4& p = vector_at(object_of(this), kPosition);
        p.y = p.y + 1.0f;
        finish_rail();
        if (thaw_bmx::live_now(this))
            thaw_bmx::rail_stall_slip(this); // (not original: THAW 0x005E7074 - the bike's stall)
        return;
    }
    update_grind_balance();

    auto* node = get_at<objects::GrindPoint*>(this, kRailNode);
    objects::GrindPoint* const start_node = node;
    objects::GrindPoint* next = node->succ_;
    objects::GrindPoint* go_on_to = nullptr;
    if (!next) {
        // The rail's last node.
        natas_spin();
        return;
    }

    // The segment's direction (x scaled by the unrounded inverse length, the rest by it rounded - as the original).
    maths::Vec4 from = grind_point_in_world(this, node);
    maths::Vec4 to = grind_point_in_world(this, next);
    double dx = static_cast<double>(to.x) - from.x, dy = static_cast<double>(to.y) - from.y;
    double dz = static_cast<double>(to.z) - from.z, dw = static_cast<double>(to.w) - from.w;
    float segment_length = static_cast<float>(std::sqrt((dx * dx + dy * dy) + dz * dz));
    double inverse = 1.0 / segment_length;
    float inverse_rounded = static_cast<float>(inverse);
    maths::Vec4 along = {static_cast<float>(inverse * dx), static_cast<float>(inverse_rounded * dy),
                         static_cast<float>(inverse_rounded * dz), static_cast<float>(inverse_rounded * dw)};

    // The rail's gravity along it; turned back by it: the other way round (+0x34C).
    float before = sign_of(dot3(along, vector_at(object_of(this), kVelocity)));
    maths::Vec4 gravity = {0.0f, tuning_float_lookup(kRailGravity, object), 0.0f, 1.0f};
    gravity.project_onto(along);
    {
        float t = get_at<float>(this, kFrameLength);
        maths::Vec4& v = vector_at(object_of(this), kVelocity);
        float gy = t * gravity.y, gz = t * gravity.z, gw = t * gravity.w;
        v.x = static_cast<float>(static_cast<double>(gravity.x) * t + v.x);
        v.y = gy + v.y;
        v.z = gz + v.z;
        v.w = gw + v.w;
    }
    float after = sign_of(dot3(along, vector_at(object_of(this), kVelocity)));
    if (before != after)
        get_at<uint8_t>(this, 0x34C) = !get_at<uint8_t>(this, 0x34C);

    // The segment to go on to that way (its node flagged 4, the corner gentle enough) - or none.
    bool no_next_segment = false;
    if (after < 0.0f) {
        objects::GrindPoint* prev = node->last_value_;
        if (prev && continues(prev)) {
            maths::Vec4 a = grind_point_in_world(this, node);
            maths::Vec4 b = grind_point_in_world(this, next);
            maths::Vec4 ahead = b - a;
            maths::Vec4 c = grind_point_in_world(this, prev);
            maths::Vec4 d = grind_point_in_world(this, node);
            maths::Vec4 behind = d - c;
            if (gentle_corner(ahead, behind))
                go_on_to = node->last_value_;
            else
                no_next_segment = true;
        } else {
            no_next_segment = true;
        }
    } else {
        objects::GrindPoint* beyond = next->succ_;
        if (beyond && continues(beyond)) {
            maths::Vec4 a = grind_point_in_world(this, next);
            maths::Vec4 b = grind_point_in_world(this, node);
            maths::Vec4 back = b - a;
            maths::Vec4 c = grind_point_in_world(this, next->succ_);
            maths::Vec4 d = grind_point_in_world(this, next);
            maths::Vec4 back_further = d - c;
            if (gentle_corner(back, back_further))
                go_on_to = next;
            else
                no_next_segment = true;
        } else {
            no_next_segment = true;
        }
    }

    // Past the segment's end (measured from the node it started at): onto the next segment, else on from the rail.
    float overshoot = 0.0f;
    {
        objects::GrindPoint* from_node = after < 0.0f ? next : node;
        const maths::Vec4& p = vector_at(object_of(this), kPosition);
        maths::Vec4 q = grind_point_in_world(this, from_node);
        double ex = static_cast<double>(p.x) - q.x, ey = static_cast<double>(p.y) - q.y, ez = static_cast<double>(p.z) - q.z;
        double distance = std::sqrt((ez * ez + ey * ey) + ex * ex);
        if (distance > static_cast<double>(segment_length) + 0.1f) {
            overshoot = static_cast<float>(distance - segment_length);
            objects::GrindPoint* corner = after < 0.0f ? node : next;               // the node passed
            objects::GrindPoint* onward = after < 0.0f ? node->last_value_ : next->succ_; // the next segment's far node
            objects::GrindPoint* flagged = after < 0.0f ? node->last_value_ : next;      // the node whose flag says go on
            bool clear = false;
            if (onward && continues(flagged)) {
                maths::Vec4 far = grind_point_in_world(this, onward);
                maths::Vec4 near = grind_point_in_world(this, corner);
                clear = objects::rail_clear(near, far);
            }
            if (clear && !no_next_segment) {
                vector_at(object_of(this), kPosition) = grind_point_in_world(this, corner);
                get_at<objects::GrindPoint*>(this, kRailNode) = after < 0.0f ? node->last_value_ : next;
                set_surface_kind(get_at<objects::GrindPoint*>(this, kRailNode)->surface_type_);
                trigger_rail_script(kGrindEvents);
            } else {
                maths::Vec4 end = grind_point_in_world(this, corner);
                transfer_or_leave_rail(&end);
            }
        }
    }
    if (state_of(this) != kRail) {
        finish_rail();
        if (thaw_bmx::live_now(this))
            thaw_bmx::rail_stall_slip(this); // (not original, as above)
        return;
    }

    grind_along(start_node, go_on_to, no_next_segment, overshoot);
    finish_rail();
    if (thaw_bmx::live_now(this))
        thaw_bmx::rail_stall_slip(this); // (not original, as above)
}

// (Part of 0x0050DDF0) A moving rail: its matrix asked for (0x0042BA10 - not used), then carried along with it.
void SkaterPhysicsModule::ride_moving_rail()
{
    if (objects::RideTracker* tracker = get_tracker(this)) {
        if (!tracker->entity_.owner_object_) {
            stop_riding(this);
        } else {
            void* rails = static_cast<objects::GameEntity*>(get_tracker(this)->entity_.owner_object_)->fetch_module(kRailManagerType);
            maths::Mat4 matrix;
            static_cast<objects::GrindPathLinkModule*>(rails)->update_rail_matrix(&matrix);
            tracker = get_tracker(this);
            if (tracker) {
                if (!tracker->entity_.owner_object_ || !tracker->tick_frame(&vector_at(object_of(this), kPosition))) {
                    stop_riding(this);
                } else {
                    vector_at(object_of(this), kPosition) += get_tracker(this)->carried_;
                    if (get_tracker(this)->turned_) {
                        auto multiply = d3dx::matrix_multiply;
                        maths::Mat4& m = matrix_at(object_of(this), kMatrix);
                        multiply(&m, &m, &get_tracker(this)->turn_);
                        multiply(&matrix_at(this, kDisplayMatrix), &matrix_at(this, kDisplayMatrix),
                                 &get_tracker(this)->turn_);
                    }
                }
            }
        }
    }
}

// (Part of 0x0050DDF0) A grind or a slide: its balance (+0x7C of the balance trick component), the Perfect Rail cheat.
void SkaterPhysicsModule::update_grind_balance()
{
    uint32_t kind = get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0x18);
    if (kind == kGrind || kind == kSlide) {
        reinterpret_cast<objects::Balance*>(get_at<uint8_t*>(this, kBalanceTrick) + 0x7C)->tick_frame();
        auto career = [] { return get_at<objects::RiderStoryProgress*>(reinterpret_cast<uint8_t*>(modules::g_skate_module), 0x20); };
        if (career()->is_code_on(kPerfectRail)) {
            career()->set_global_flag(script::fetch_int_value(kPerfectRail));
            get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0xAC) = 0;
            get_at<uint32_t>(get_at<void*>(this, kBalanceTrick), 0xB0) = 0;
            reinterpret_cast<uint8_t&>(online::g_cheat_flag) = 1;
        }
    }
}

// (Part of 0x0050DDF0) Still on the rail: the velocity along the segment, the facing turned to it, the move (and
// what went past the last segment's end), then a feeler ahead along the move.
void SkaterPhysicsModule::grind_along(objects::GrindPoint* start_node, objects::GrindPoint* go_on_to, bool no_next_segment,
                                      float overshoot)
{
    auto* node = get_at<objects::GrindPoint*>(this, kRailNode);
    objects::GrindPoint* next = node->succ_;
    maths::Vec4 from = grind_point_in_world(this, node);
    maths::Vec4 to = grind_point_in_world(this, next);
    maths::Vec4 direction = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
    direction.make_unit();
    float sign = sign_of(dot3(direction, vector_at(object_of(this), kVelocity)));
    get_at<int>(this, 0x234) = get_now();
    vector_at(object_of(this), kVelocity).change_heading(direction);
    maths::Vec4& v = vector_at(object_of(this), kVelocity);
    v.x = sign * v.x;
    v.y = sign * v.y;
    v.z = sign * v.z;
    v.w = sign * v.w;

    // The facing: along the rail (the other way round with +0x34C); the display facing (+0x38) turned 0.3 of the way
    // to it, the display and the object's matrices built around them (level rights).
    float way = get_at<uint8_t>(this, 0x34C) ? -sign : sign;
    maths::Vec4 facing = {direction.x * way, direction.y * way, direction.z * way, way * direction.w};
    maths::Vec4& display = vector_at(this, 0x38);
    maths::Vec4 turned;
    walk::get_lerp(&turned, &display, &facing, kTurnFacing);
    display = turned;
    display.make_unit();
    maths::Vec4& right = vector_at(this, 0x18);
    right.x = get_at<float>(this, 0x40);
    right.z = -display.x;
    right.y = 0.0f;
    right.w = 0.0f;
    right.make_unit();
    vector_at(this, 0x28) = maths::cross_vec(display, right);
    vector_at(this, 0x28).make_unit();
    void* object = object_of(this);
    vector_at(object, kFacing) = facing;
    vector_at(object_of(this), kFacing).make_unit();
    object = object_of(this);
    maths::Vec4& object_right = vector_at(object, kRight);
    float fz = vector_at(object, kFacing).z;
    object_right.z = -vector_at(object, kFacing).x;
    object_right.x = fz;
    object_right.y = 0.0f;
    object_right.w = 0.0f;
    vector_at(object_of(this), kRight).make_unit();
    object = object_of(this);
    vector_at(object, kUp) = maths::cross_vec(vector_at(object, kFacing), vector_at(object, kRight));
    vector_at(object_of(this), kUp).make_unit();

    // What went past the last segment's end is only carried on when this segment is longer.
    maths::Vec4 a = grind_point_in_world(this, node);
    maths::Vec4 b = grind_point_in_world(this, next);
    double sx = static_cast<double>(b.x) - a.x, sy = static_cast<double>(b.y) - a.y;
    double sz = static_cast<double>(b.z) - a.z;
    double length = std::sqrt((sx * sx + sz * sz) + sy * sy);
    if (length <= overshoot)
        overshoot = 0.0f;

    // The move: the velocity over the frame, plus the overshoot along the facing (x, y of both unrounded).
    maths::Vec4& p = vector_at(object_of(this), kPosition);
    maths::Vec4 old = p;
    {
        float t = get_at<float>(this, kFrameLength);
        const maths::Vec4& u = vector_at(object_of(this), kVelocity);
        float my = t * u.y, mz = t * u.z, mw = t * u.w;
        p.x = static_cast<float>(static_cast<double>(t) * u.x + p.x);
        p.y = my + p.y;
        p.z = mz + p.z;
        p.w = mw + p.w;
        maths::Vec4& q = vector_at(object_of(this), kPosition);
        float oz = facing.z * overshoot, ow = facing.w * overshoot;
        q.x = static_cast<float>(static_cast<double>(facing.x) * overshoot + q.x);
        q.y = static_cast<float>(static_cast<double>(facing.y) * overshoot + q.y);
        q.z = oz + q.z;
        q.w = ow + q.w;
        q.w = q.w + 1.0f;
    }
    const maths::Vec4& now = vector_at(object_of(this), kPosition);
    maths::Vec4 move = {now.x - old.x, now.y - old.y, now.z - old.z, now.w - old.w};
    maths::Vec4 move_direction = move;
    move_direction.make_unit();

    // Going on along the rail: only (not in the park editor) when the next segment climbs as this one does.
    if (!no_next_segment) {
        if (in_park_editor())
            return;
        maths::Vec4 c = grind_point_in_world(this, start_node);
        maths::Vec4 d = grind_point_in_world(this, start_node->succ_);
        maths::Vec4 this_way = d - c;
        maths::Vec4 e = grind_point_in_world(this, go_on_to);
        maths::Vec4 f = grind_point_in_world(this, go_on_to->succ_);
        maths::Vec4 that_way = f - e;
        this_way.make_unit();
        that_way.make_unit();
        if (!(std::fabs(static_cast<double>(this_way.y) - that_way.y) < 0.01f))
            return;
    }

    // The feeler: from the last position to 6 past the new one along the move, raised by the up.
    maths::Vec4& start = vector_at(this, kProbeOrigin);
    maths::Vec4& end = vector_at(this, kProbeTip);
    double ahead_x = static_cast<double>(move_direction.x) * kFeelerAhead;
    double ahead_y = static_cast<double>(move_direction.y) * kFeelerAhead;
    float ahead_z = move_direction.z * kFeelerAhead;
    float ahead_w = move_direction.w * kFeelerAhead;
    start = old;
    end = vector_at(object_of(this), kPosition);
    float extend_x = static_cast<float>(ahead_x + move.x);
    double extend_y = ahead_y + move.y;
    double extend_z = static_cast<double>(ahead_z) + move.z;
    float extend_w = ahead_w + move.w;
    end.x = extend_x + end.x;
    end.y = static_cast<float>(extend_y + end.y);
    end.z = static_cast<float>(extend_z + end.z);
    end.w = extend_w + end.w;
    const maths::Vec4& up = vector_at(object_of(this), kUp);
    start.x = up.x + start.x, start.y = up.y + start.y, start.z = up.z + start.z, start.w = up.w + start.w;
    const maths::Vec4& up2 = vector_at(object_of(this), kUp);
    end.x = end.x + up2.x, end.y = end.y + up2.y, end.z = end.z + up2.z, end.w = end.w + up2.w;
    if (!get_feel(this, 0x10, 0))
        return;
    // Something ahead: off the rail (not for faces flagged 0x10 at +0xF9, outside the park editor).
    if (!in_park_editor() && (get_at<uint8_t>(this, 0xF9) & 0x10))
        return;
    trigger_rail_script(kGrindOffEvents);
    void* o = object_of(this);
    vector_at(o, kPosition) = vector_at(o, kOldPosition);
    maths::Vec4& position = vector_at(object_of(this), kPosition);
    position.y = position.y + 1.0f;
    vector_at(object_of(this), kVelocity).flatten_onto_plane(vector_at(this, kHitNormal));
    change_state(this, kAir);
    static_cast<objects::Entity*>(object_of(this))->post_own_event(kOffRail, nullptr);
}

// (Part of 0x0050DDF0) Every way out of the update (but the rail's last node): the ground normal, the score's rail
// time (0x00564270, +0x3A8 scaled), the node's Name.
void SkaterPhysicsModule::finish_rail()
{
    vector_at(this, kFloorUpVector) = vector_at(object_of(this), kUp);
    void* score = get_at<void*>(get_at<void*>(this, 0x1520), 0x18);
    int time = static_cast<int>(static_cast<double>(get_at<int>(this, 0x3A8)) * get_at<float>(score, 0x154C));
    static_cast<modules::PointTally*>(score)->tweak_trick(time);
    if (get_at<objects::GrindRailHub*>(this, kRailManager)->node_array_) {
        script::Struct* script = level_nodes::fetch_vertex(get_at<objects::GrindPoint*>(this, kRailNode)->label_, true);
        // (0x00476EC0: read_hash_value with a bool for its last argument - the wrapper passes it on as 1)
        script->read_hash_value(kName, &get_at<uint32_t>(this, kRailScriptNode), 1);
        static_cast<objects::MoveExecutor*>(get_at<void*>(this, kSkaterComponent))
            ->pass_to_trick_objects(get_at<uint32_t>(this, kRailScriptNode));
    }
}

// Not original: THAW's stall state (THAW 0x005E2E30; game/rider/thaw_bmx.h - its bike's, R1 held as a grind
// starts): the rail state's update around a bike held still on the rail, facing along it. Update runs it in that state
// (ThawBmx::grind_stall / rail_stall_slip set it) while the feature is on.
void SkaterPhysicsModule::do_bike_stall_physics()
{
    // As the rail state's: the flags (THAW clears a fifth of its own), the moving rail, the rerail time.
    set_flag(this, 0x78, 0);
    set_flag(this, 0xA8, 0);
    set_flag(this, 0x80, 0);
    set_flag(this, 0xB0, 0);
    get_at<uint8_t>(this, 0x2E4) = 0;
    ride_moving_rail();
    get_at<int>(this, 0x23C) = static_cast<int>(script::fetch_float(kRailRegrabDelay));
    set_flag(this, 0x50, 0);
    if (jump_released()) {
        // Jumped (0x005E3053): the rail's TriggerScripts (THAW's with its stall event, which THUG2's haven't), the
        // jump's rerail time, up.
        trigger_rail_script(kJumpTriggerEvents);
        trigger_rail_script(kGrindOffEvents);
        get_at<int>(this, 0x23C) = static_cast<int>(script::fetch_float(kJumpRerailTime));
        set_flag(this, 0xD0, 1);
        set_flag(this, 0xD8, 1);
        maths::Vec4& p = vector_at(object_of(this), kPosition);
        p.y = p.y + 1.0f;
    } else {
        update_grind_balance();
        // (THAW draws the segment here with rail_highlights on - a debug view of its own)
        auto* node = get_at<objects::GrindPoint*>(this, kRailNode);
        objects::GrindPoint* next = node->succ_;
        if (!next)
            return;
        vector_at(object_of(this), kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        if (state_of(this) == kBikeStall) {
            // 0x005E34AE: the facing along the segment the way it points, the display facing turned 0.3 of the way
            // to it and the matrices built round them as a grind's (grind_along); no move.
            maths::Vec4 from = grind_point_in_world(this, node);
            maths::Vec4 to = grind_point_in_world(this, next);
            maths::Vec4 direction = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
            direction.make_unit();
            get_at<int>(this, 0x234) = get_now();
            maths::Vec4 facing = thaw_bmx::bike::stall_facing(vector_at(object_of(this), kFacing), direction);
            maths::Vec4& display = vector_at(this, 0x38);
            maths::Vec4 turned;
            walk::get_lerp(&turned, &display, &facing, kTurnFacing);
            display = turned;
            display.make_unit();
            maths::Vec4& right = vector_at(this, 0x18);
            right.x = get_at<float>(this, 0x40);
            right.z = -display.x;
            right.y = 0.0f;
            right.w = 0.0f;
            right.make_unit();
            vector_at(this, 0x28) = maths::cross_vec(display, right);
            vector_at(this, 0x28).make_unit();
            void* object = object_of(this);
            vector_at(object, kFacing) = facing;
            vector_at(object, kFacing).make_unit();
            maths::Vec4& object_right = vector_at(object, kRight);
            float fz = vector_at(object, kFacing).z;
            object_right.z = -vector_at(object, kFacing).x;
            object_right.x = fz;
            object_right.y = 0.0f;
            object_right.w = 0.0f;
            object_right.make_unit();
            vector_at(object, kUp) = maths::cross_vec(vector_at(object, kFacing), object_right);
            vector_at(object, kUp).make_unit();
            vector_at(object, kVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        }
    }
    // 0x005E37FE: on a rail too steep, grinding again; the ground normal the up, the score's rail time, the node's name.
    thaw_bmx::stall_steep_rail(this);
    finish_rail();
}

} // namespace Skater
