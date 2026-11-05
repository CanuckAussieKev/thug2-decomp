// The walk component's hanging, ladder and rail-climbing code (see walk_hang.h). Floating point follows the
// original's x87 code: values it keeps on the x87 stack are doubles here, values it stores are floats.

#include "game/rider/walk_hang.h"
#include "engine/objects/walk_lerp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "base/maths/shape_tests.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/trigger_component.h"
#include "engine/script/member_code.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/walk_camera.h"
#include "game/rider/walk_physics.h"
#include "engine/sys/game_clock.h"
#include "engine/objects/control_pad.h"
#include "game/skate/skate_module.h"
#include "engine/objects/trigger_events.h"
#include "engine/objects/wait_and_die.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "engine/gfx/model.h"
#include "engine/objects/model_component.h"
#include "game/rider/tagging.h"
#include <gsl/pointers>
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/maths/trig.h"

namespace {
constexpr double kQuarterPi = 0.78539818525314331;
constexpr float kQuarterPiF = 0.78539818525314331F;
constexpr double kHalfPi = 1.5707963705062866;
constexpr double kRadiansPerDegree = 0.017453292384743690;
constexpr int kModelScaledFlag = 8;
constexpr int kRailJoinKind = 0x28;
constexpr double kCoincidentDot = 0.86599999666213989;
constexpr int kSourceLineBadAngle = 0x4F2;         // the source lines the original reports its messages from
constexpr int kSourceLineCoincidentRail = 0x4FC;
constexpr float kRailStepAcross = 12.0F; // inches
constexpr float kRailStepBack = 24.0F;
constexpr float kHalfF = 0.5F;
constexpr int kProbeSides = 7;
constexpr float kProbeBehind = 6.0F;
constexpr float kMaxUpSpeed = 50.0F;
constexpr float kAlongStep = 2.0F;
constexpr double kMaxTurnCos = 0.87000000476837158;
constexpr float kMaxRailHeightGap = 12.0F;
constexpr int kFlagRight = 0x80;
constexpr int kFlagLeft = 0x40;
constexpr float kBoxReach = 24.0F;
constexpr float kUprightY = 0.995F;
constexpr float kLadderStep = 6.0F;
constexpr uint32_t kSettleMs = 25;

// --- the walk component's fields (offsets) ---

// The events the rail scripts and triggers get (const int[3]).

constexpr uint32_t kWalkParameters = 0x6775E538;               // "walk_parameters"
constexpr uint32_t kComboRunSpeed = 0x7213EF07;                // "combo_run_speed"
constexpr uint32_t kRunSpeed = 0xCC461B87;                     // "run_speed"
constexpr uint32_t kHangVertOriginOffset = 0xA8C13E74;         // "hang_vert_origin_offset"
constexpr uint32_t kHangCriticalPointVertOffset = 0xDE86132E;  // "hang_critical_point_vert_offset"
constexpr uint32_t kMaxCasScaling = 0x7201AD48;                // "max_cas_scaling"
constexpr uint32_t kHangCriticalPointHorizOffset = 0xC3263E6E; // "hang_critical_point_horiz_offset"
constexpr uint32_t kHangObstructionFeelerSide = 0xAFBF7687;    // "hang_obstruction_feeler_side"
constexpr uint32_t kHangObstructionFeelerUp = 0x8F35F29F;      // "hang_obstruction_feeler_up"
constexpr uint32_t kSkaterExitHang = 0xBEEDB40D;               // "SkaterExitHang"
constexpr uint32_t kPushFeelerLength = 0xA20C43B7;             // "push_feeler_length"
constexpr uint32_t kProbeElevation = 0x6DA7F696;               // "feeler_height"
constexpr uint32_t kPullUpObstructionHeight = 0x9B2818CF;      // "pull_up_obstruction_height"
constexpr uint32_t kPullUpOffsetForward = 0x21DFBE77;          // "pull_up_offset_forward"
constexpr uint32_t kDropToHangSpeedFactor = 0x01D1E6AF;        // "drop_to_hang_speed_factor"
constexpr uint32_t kDropToClimbMaxSnap = 0xA8276303;           // "Drop_To_Climb_Max_Snap" (a global float)
constexpr uint32_t kClimbMaxSnap = 0x30CE7F2C;                 // "Climb_Max_Snap" (a global float)
constexpr uint32_t kDropToHangRotateFactor = 0xFDC8AEC0;       // "drop_to_hang_rotate_factor"
constexpr uint32_t kHangMoveCollisionSideLength = 0x3344A6D0;  // "hang_move_collision_side_length"
constexpr uint32_t kHangMoveCollisionUp = 0x84FF931C;          // "hang_move_collision_up"
constexpr uint32_t kHangMoveCollisionBack = 0xCBEB3D89;        // "hang_move_collision_back"
constexpr uint32_t kHangMoveCollisionSideHeight = 0xC774DD6D;  // "hang_move_collision_side_height"
constexpr uint32_t kRailHubModuleName = 0x1CDFB2A3;            // "railmanager" (the component type)
constexpr uint32_t kAnimPullUpFromHang = 0x9EF9F8F1;           // "PullUpFromHang"
constexpr uint32_t kAnimDropToHang = 0xEB71D98E;               // "DropToHang"
constexpr uint32_t kAnimHang = 0x4194ECCA;                     // "Hang"

// More fields: the facing aimed at in an animated move, the height there, the hang point's offset along the rail.

constexpr uint32_t kTaggingControl = 0x380D909A; // "TaggingControl"

// The hang's own update.
constexpr uint32_t kHangHorizOriginOffset = 0x262A9467;    // "hang_horiz_origin_offset"
constexpr uint32_t kHopObstructionFeelerUp = 0xA85EC070;   // "hop_obstruction_feeler_up"
constexpr uint32_t kHangMaxRailAscent = 0x98724EBC;        // "hang_max_rail_ascent"
constexpr uint32_t kHangInitAnimFeelerHeight = 0xDBB3D53E; // "hang_init_anim_feeler_height"
constexpr uint32_t kHangInitAnimFeelerLength = 0x2F83AE83; // "hang_init_anim_feeler_length"
constexpr uint32_t kPullUpOffsetUp = 0x52FA7CA6;           // "pull_up_offset_up"
constexpr uint32_t kHangVertControlTolerance = 0x1B877928; // "hang_vert_control_tolerance"
constexpr uint32_t kHangMoveSpeedParam = 0xD77EE881;       // "hang_move_speed"
constexpr uint32_t kHangMoveLerpRate = 0xC506A97C;         // "hang_move_lerp_rate"
constexpr uint32_t kHangMoveCutoff = 0x228B5376;           // "hang_move_cutoff"
constexpr uint32_t kAnimAir = 0x439F4704;                  // "Air"
constexpr uint32_t kAnimHangMoveLeft = 0x2D9815C3;         // "HangMoveLeft"
constexpr uint32_t kAnimHangMoveRight = 0x279B1F0B;        // "HangMoveRight"
constexpr uint32_t kAnimSwing = 0x1EE948A5;                // "SWING"
constexpr uint32_t kAnimWall = 0xEC0A1009;                 // "WALL"

// Ladders.
constexpr uint32_t kLadderBottomOffsetForward = 0xCAF2831B; // "ladder_bottom_offset_forward"
constexpr uint32_t kLadderTopOffsetForward = 0xB620B580;    // "ladder_top_offset_forward"
constexpr uint32_t kMaxOntoLadderAngle = 0x456F216D;        // "max_onto_ladder_angle"
constexpr uint32_t kLadderBottomOffsetUp = 0x5296E3FD;      // "ladder_bottom_offset_up"
constexpr uint32_t kLadderTopOffsetUp = 0x76BF49E0;         // "ladder_top_offset_up"
constexpr uint32_t kLadderClimbOffset = 0x35B3BBDA;         // "ladder_climb_offset"
constexpr uint32_t kMaxVertSnapDistance = 0xC4C164E8;       // "max_vert_snap_distance"
constexpr uint32_t kButtonHorizSnapDistance = 0x0EECD255;   // "button_horiz_snap_distance"
constexpr uint32_t kMaxHorizSnapDistance = 0xA2735DD9;      // "max_horiz_snap_distance"
constexpr uint32_t kLadderMoveSpeed = 0x0AB2DB54;           // "ladder_move_speed"
constexpr uint32_t kLadderControlTolerance = 0x7EE1B95B;    // "ladder_control_tolerance"
constexpr uint32_t kAnimLadder = 0xC84243DA;                // "Ladder"
constexpr uint32_t kAnimLadderMoveUp = 0xAF5ABC82;          // "LadderMoveUp"
constexpr uint32_t kAnimLadderMoveDown = 0xFEC9DDED;        // "LadderMoveDown"
constexpr uint32_t kAnimLadderOntoBottom = 0xE2524194;      // "LadderOntoBottom"
constexpr uint32_t kAnimLadderOntoTop = 0xD63ADCAD;         // "LadderOntoTop"
constexpr uint32_t kAnimLadderOffBottom = 0x4DB1BBB1;       // "LadderOffBottom"
constexpr uint32_t kAnimLadderOffTop = 0x12521BFB;          // "LadderOffTop"

objects::GameEntity* object_of(objects::FootMovement* walk) { return walk->entity(); }
objects::GrindRailHub*& get_rails_of(objects::FootMovement* walk) { return walk->rail_manager(); }
skater::SkaterPhysicsModule* physics_of(objects::FootMovement* walk) { return walk->core_physics(); }

// What find_rail finds (its 0x34-byte record, filled by check_rail): the hang point, the direction to face (out from
// the wall), how far along, out and up from the rail, the animation, and the rail's node.
using objects::RailHit;

// A feeler copied (its assignment 0x0043A930).
void assign_feeler(render::ProbeRay* to, const render::ProbeRay* from) { to->get_assign(*from); }
// The movable contact: no ride any more (0x00427C70).
void stop_riding(objects::FootMovement* walk) { walk->ride()->stop_riding(); }
// A rail manager component's object's matrix (0x0042BA10) and its rail manager (the original's vtable +0x34, the
// shared getter of +0x18).
void object_matrix_of(objects::ObjectModule* component, maths::Mat4* matrix)
{
    core::down_cast<objects::GrindPathLinkModule>(component)->update_rail_matrix(matrix);
}
objects::GrindRailHub* rail_manager_of(objects::ObjectModule* component)
{
    return core::down_cast<objects::GrindPathLinkModule>(component)->get_rail_groups();
}
// The skate module's rail manager (the level's).
objects::GrindRailHub* get_level_rails() { return modules::Skate::sole()->rail_manager().get(); }

// Onto a ladder from the level's rails: the ride let go, stopped.
void grabbed_ladder(objects::FootMovement* walk)
{
    get_rails_of(walk) = get_level_rails();
    stop_riding(walk);
    walk->set_up_speed(0.0F);
    walk->vel() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}

// find_rail for a walk component between `start` and `end`: the level's rails, else each moving rail manager's (its
// object's space: riding that object if one is found). `direction` / the margin's global float are read again for
// each. The rail manager used is left at +0x176C.
bool find_walk_rail(objects::FootMovement* walk, const maths::Vec4* start, const maths::Vec4* end,
                    const maths::Vec4* (*direction)(objects::FootMovement*, maths::Vec4*), uint32_t margin,
                    RailHit* found)
{
    maths::Vec4 buffer{};
    get_rails_of(walk) = get_level_rails();
    const maths::Vec4* towards = direction(walk, &buffer);
    float snap = script::fetch_float(margin);
    if (get_rails_of(walk)->find_rail(start, end, towards, walk, found, snap)) {
        stop_riding(walk);
        return true;
    }
    for (objects::ObjectModule* component = objects::object_hub_slot()->find_first_module_of_kind(kRailHubModuleName);
         component; component = component->next_of_type()) {
        maths::Mat4 to_local{};
        object_matrix_of(component, &to_local);
        to_local.upend();
        maths::Vec4 local_start{};
        maths::Vec4 local_end{};
        to_local.xform(local_start, *start);
        to_local.xform(local_end, *end);
        get_rails_of(walk) = rail_manager_of(component);
        towards = direction(walk, &buffer);
        snap = script::fetch_float(margin);
        if (get_rails_of(walk)->find_rail(&local_start, &local_end, towards, walk, found, snap)) {
            walk->ride()->ride_on(component->entity());
            return true;
        }
    }
    return false;
}
const maths::Vec4* facing_away(objects::FootMovement* walk, maths::Vec4* buffer)
{
    const maths::Vec4& facing = walk->facing();
    *buffer = {.x = -facing.x, .y = -facing.y, .z = -facing.z, .w = -facing.w};
    return buffer;
}
const maths::Vec4* current_facing(objects::FootMovement* walk, maths::Vec4* /*unused*/) { return &walk->facing(); }

// The animation-end callback (+0x1948): the code, no adjustment.
void set_anim_done(objects::FootMovement* walk, script::ObjectCallback code)
{
    objects::AnimDoneCallback& callback = walk->anim_done();
    callback.code_ = code;
    callback.this_adjust_ = 0;
    callback.unknown8_ = 0;
    callback.unknownc_ = 0;
}

// The skater core's rail node (its +0x22C), if there is one.
void tell_physics_node(objects::FootMovement* walk)
{
    if (objects::GrindPoint* node = walk->hang_node()) {
        physics_of(walk)->set_rail_node(node);
    }
}

// The skater core's rail node and rail manager (its +0x22C / +0x230), for its rail scripts.
void tell_physics_rail(objects::FootMovement* walk)
{
    tell_physics_node(walk);
    if (objects::GrindRailHub* rails = get_rails_of(walk)) {
        physics_of(walk)->set_rail_manager(rails);
    }
}
void trigger_rail_script(objects::FootMovement* walk, const std::array<int, 3>& events)
{
    physics_of(walk)->trigger_rail_script(events);
}
// The hang offset (+0x71C) for the facing: hang_critical_point_horiz_offset along it, the critical point's height up.
void set_hang_offset(objects::FootMovement* walk)
{
    const float horiz = objects::FootMovement::get_param(kHangCriticalPointHorizOffset);
    const maths::Vec4& facing = walk->facing();
    maths::Vec4& offset = walk->critical_point_offset();
    offset.z = horiz * facing.z;
    offset.w = horiz * facing.w;
    offset.x = horiz * facing.x;
    offset.y = horiz * facing.y;
    offset.y = static_cast<float>(objects::FootMovement::critical_point_up());
}

// The trigger component told a feeler hit something (for `events`), and the movable contact the feeler's hit.
void trigger_feeler(objects::FootMovement* walk, const std::array<int, 3>& events, render::ProbeRay* feeler)
{
    walk->trigger()->probe_touches_zone(events, feeler);
}
void contact_feeler(objects::FootMovement* walk, render::ProbeRay* feeler) { walk->ride()->start_riding(feeler); }

// The game's clock with its offset.
int get_now() { return frame_clock::game_clock_elapsed() + objects::clock_offset(); }

// The movable contact's ride let go (its object kept at +0x6C, the time at +0x70).
void let_go_of_ride(objects::MovingSurfaceModule* contact)
{
    auto* tracker = contact->ride().get();
    if (!tracker) {
        return;
    }
    contact->set_last_object(tracker->entity().get_pull()); // (the object last ridden)
    tracker->~RideTracker();                             // (its smart pointer lets go)
    memory::dealloc(tracker);
    contact->ride() = nullptr;
    contact->set_stop_time(get_now());
}

// The skater core's timed flag +0xA0 of +0x1528 set (with the time at +0xA4), unless it is.
void set_physics_flag_a0(objects::FootMovement* walk)
{
    objects::RiderPoseState* flags = physics_of(walk)->state();
    if (flags->flag(objects::PoseFlag::kWallplanted).on != 1) {
        flags->flag(objects::PoseFlag::kWallplanted).on = 1;
        flags->flag(objects::PoseFlag::kWallplanted).changed_at = get_now();
    }
}

// What find_ladder / find_ladder_on_line find (their 0x24-byte record, filled by the ladder checkers): the point to climb
// from, the direction to face (the ladder's orientation's third row), how far along.

// A ladder's other end: the node's next, else its previous.
objects::GrindPoint* ladder_other_end(objects::GrindPoint* node)
{
    return node->succ() ? node->succ() : node->last_value();
}

// 0x006468F8: 60 degrees (a double)
constexpr double kSixtyDegrees = 1.0471975511965976;

} // namespace

// --- small helpers ---

// 0x0043DA20
int walk::turned_round(const objects::PadSnapshot* pad)
{
    if (core::at(pad->lock(), 1) == 0 && core::at(pad->polar(), 1).length != 0.0F &&
        maths::kPi - std::fabs(static_cast<double>(core::at(pad->polar(), 1).angle)) < kQuarterPi) {
        return 1;
    }
    return 0;
}

// 0x0043DA70
float objects::FootMovement::get_param(uint32_t name)
{
    float value = 0.0F;
    script::read_sub_struct(kWalkParameters)->fetch_float(name, &value, 1);
    return value;
}

// 0x0043DAA0
double objects::FootMovement::run_speed() const
{
    const float combo = get_param(kComboRunSpeed);
    const float run = get_param(kRunSpeed);
    return ((static_cast<double>(combo) - run) * run_adjustment_) + run;
}

// 0x0043DC00
double objects::FootMovement::hang_height_offset()
{
    const render::RenderModel* model = model_->mesh_instance().get();
    if ((model->flag_bits() & kModelScaledFlag) == 0) { // (kModelScaled)
        return 0.0;
    }
    const float fraction = model->size_factor().y;
    const float offset = get_param(kHangVertOriginOffset);
    return (1.0 - fraction) * offset;
}

// 0x0043DC60
double objects::FootMovement::critical_point_up()
{
    const float vert = get_param(kHangCriticalPointVertOffset);
    const float scaling = get_param(kMaxCasScaling);
    const float origin = get_param(kHangVertOriginOffset);
    return vert - (static_cast<double>(origin) * scaling);
}

// 0x0043DB00
void objects::FootMovement::end_pull_up()
{
    change_phase(0);
    object_of(this)->post_own_event(kSkaterExitHang, nullptr);
    trigger_rail_script(this, kExitHangEvents);
    if (feeler_hit_) {
        auto* feeler = &feeler_;
        trigger_feeler(this, kPullUpEvents, feeler);
        contact_feeler(this, feeler);
    }
    critical_point_offset_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    model_lift_ = transition_display_offset_;
}

// 0x0043DB90
void objects::FootMovement::end_drop_from_hang()
{
    change_phase(1);
    object_of(this)->post_own_event(kSkaterExitHang, nullptr);
    trigger_rail_script(this, kExitHangEvents);
    leap_heading_ = facing_;
    leave_ride(&vel_, &up_speed_);
    model_lift_ = transition_display_offset_;
}

// 0x0043DCF0
void objects::FootMovement::end_drop_to_hang()
{
    set_hang_offset(this);
    change_phase(2);
    tell_physics_rail(this);
    trigger_rail_script(this, kHangEvents);
    core::down_cast<objects::TaggingControlComponent>(object_of(this)->fetch_module(kTaggingControl))->set_blocked(false);
}

// 0x0043DDD0
bool objects::FootMovement::side_clear(render::ProbeRay* feeler, const RailHit& hang, const maths::Vec4* side)
{
    const float side_length = get_param(kHangObstructionFeelerSide);
    const float sx = side_length * side->x;
    const float sy = side_length * side->y;
    const float sz = side_length * side->z;
    const float sw = side_length * side->w;
    const float horiz = get_param(kHangCriticalPointHorizOffset);
    const maths::Vec4& point = hang.point;
    const maths::Vec4& out = hang.facing;
    // the critical point (the y left unrounded, as the original keeps it on the x87 stack), moved along the side
    auto cx = static_cast<float>((static_cast<double>(horiz) * out.x) + point.x);
    const double cy = (static_cast<double>(horiz) * out.y) + point.y;
    const float hz = horiz * out.z;
    const float hw = horiz * out.w;
    auto cz = static_cast<float>(static_cast<double>(hz) + point.z);
    auto cw = static_cast<float>(static_cast<double>(hw) + point.w);
    maths::Seg& line = feeler->line();
    line.start.w = cw + sw;
    line.start.x = cx + sx;
    line.start.y = static_cast<float>(cy + sy);
    line.start.z = static_cast<float>(static_cast<double>(cz) + sz);
    line.end = line.start;
    const float origin = get_param(kHangVertOriginOffset);
    line.start.y = static_cast<float>(static_cast<double>(line.start.y) - origin + critical_point_up());
    const float up = get_param(kHangObstructionFeelerUp);
    line.end.y = up + line.end.y;
    if (feeler->get_hit_shape(true, false) != 0) {
        return false;
    }
    const maths::Vec4 start = line.start;
    line.start = line.end;
    line.end = start;
    return feeler->get_hit_shape(true, false) == 0;
}

// 0x0043DFC0
void objects::FootMovement::face_hang_rail(float distance, bool from_end, const maths::Vec4* direction)
{
    objects::GrindRailHub* rails = get_rails_of(this);
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    rails->get_absolute_pos(&point_a, hang_rail_.next_node);
    rails->get_absolute_pos(&point_b, hang_rail_.node);
    const maths::Vec4 delta = {
        .x = point_b.x - point_a.x, .y = point_b.y - point_a.y, .z = point_b.z - point_a.z, .w = point_b.w - point_a.w};
    const float xx = delta.x * delta.x;
    const double length =
        std::sqrt((static_cast<double>(delta.z) * delta.z) + (static_cast<double>(delta.y) * delta.y) + xx);
    if (distance > length) {
        distance = static_cast<float>(length);
    }
    const double fraction = distance / length;
    hang_.along = static_cast<float>(fraction);
    if (from_end) {
        hang_.along = static_cast<float>(1.0 - fraction);
    }
    // across the rail, flat
    maths::Vec4 across = {.x = -delta.z, .y = 0.0F, .z = delta.x, .w = 0.0F};
    const double across_length = std::sqrt((static_cast<double>(across.x) * across.x) + xx);
    if (across_length > 0.0) {
        const double scale = 1.0 / across_length;
        across.x = static_cast<float>(across.x * scale);
        across.y = static_cast<float>(0.0 * scale);
        across.z = static_cast<float>(scale * delta.x);
    }
    const maths::Vec4 reversed = {.x = -delta.x, .y = -delta.y, .z = -delta.z, .w = -delta.w};
    const maths::Vec4& along = from_end ? reversed : delta;
    const maths::Vec4& facing = facing_;
    const double turn = (static_cast<double>(direction->z) * facing.x) - (static_cast<double>(facing.z) * direction->x);
    const double side = (static_cast<double>(across.x) * along.z) - (static_cast<double>(across.z) * along.x);
    if (side * turn < 0.0) {
        across.x = -across.x;
        across.y = -across.y;
        across.z = -across.z;
    }
    const double dot = (static_cast<double>(across.z) * facing.z) + (static_cast<double>(across.y) * facing.y) +
                       (static_cast<double>(across.x) * facing.x);
    if (std::cos(kSixtyDegrees) > dot) {
        walk_camera_->apply_queued_change();
    }
    facing_ = {.x = across.x, .y = across.y, .z = across.z, .w = 1.0F};
    set_hang_offset(this);
}

// 0x0043E230
bool objects::FootMovement::next_hang_rail(float* fraction, objects::GrindPoint* start, objects::GrindPoint* end,
                                           HangSegment* onto)
{
    objects::GrindRailHub* rails = get_rails_of(this);
    auto grabbable = [rails](objects::GrindPoint* from, objects::GrindPoint* to) {
        maths::Vec4 point_a{};
        maths::Vec4 point_b{};
        rails->get_absolute_pos(&point_b, to);
        rails->get_absolute_pos(&point_a, from);
        return objects::rail_clear(point_a, point_b);
    };
    // a segment's flags are its first node's
    auto usable = [](const objects::GrindPoint* first) {
        return (first->flag_bits() & objects::kRailFlagActive) && !(first->flag_bits() & objects::kRailFlagNoClimbing);
    };
    objects::GrindPoint* joining = nullptr;
    if (*fraction < 0.0F) {
        // back past the start: the segment before it
        objects::GrindPoint* previous = start->last_value();
        if (previous && usable(previous) && grabbable(previous, start)) {
            onto->end = start;
            onto->start = start->last_value();
            return true;
        }
        if (!rails->find_joining_rail(start, kRailJoinKind, &joining)) {
            return false;
        }
    } else {
        objects::GrindPoint* next = end->succ();
        if (next && usable(end) && grabbable(end, next)) {
            onto->start = end;
            onto->end = end->succ();
            return true;
        }
        if (!rails->find_joining_rail(end, kRailJoinKind, &joining)) {
            return false;
        }
    }
    // a joining rail: does it run on the same way?
    const bool same_way = objects::nodes_together(joining, end) || objects::nodes_together(joining->succ(), start);
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    rails->get_absolute_pos(&point_a, start);
    rails->get_absolute_pos(&point_b, end);
    maths::Vec4 here = {
        .x = point_b.x - point_a.x, .y = point_b.y - point_a.y, .z = point_b.z - point_a.z, .w = point_b.w - point_a.w};
    here.make_unit();
    const objects::GrindPoint* joined_next = joining->succ();
    rails->get_absolute_pos(&point_a, joining);
    rails->get_absolute_pos(&point_b, joined_next);
    maths::Vec4 there = {
        .x = point_b.x - point_a.x, .y = point_b.y - point_a.y, .z = point_b.z - point_a.z, .w = point_b.w - point_a.w};
    there.make_unit();
    double tx = there.x;
    double ty = there.y;
    double tz = there.z;
    if (!same_way) {
        tx *= -1.0F;
        ty *= -1.0F;
        tz *= -1.0F;
    }
    const double dot = (here.z * tz) + (ty * here.y) + (tx * here.x);
    if (dot < -kCoincidentDot) {
        debug::printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", kSourceLineBadAngle, "BAD ANGLE ON COINCIDENT RAIL");
        return false;
    }
    if (!same_way) {
        *fraction = 1.0F - *fraction;
    }
    onto->start = joining;
    onto->end = joining->succ();
    debug::printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", kSourceLineCoincidentRail, "FOUND COINCIDENT RAIL");
    return true;
}

// 0x0043E530
bool objects::FootMovement::pull_up(maths::Vec4* target)
{
    // the ground to stand on: near the target, else 12 to one side of it, else 12 to the other
    render::ProbeRay ledge;
    maths::Vec4 ground{};
    bool found = find_stand_position(target, &ground, &ledge);
    if (!found) {
        const float across = -facing_.z;
        const maths::Vec4 first = ground;
        const float facing_x = facing_.x;
        target->x = static_cast<float>((static_cast<double>(across) * kRailStepAcross) + target->x);
        target->z = static_cast<float>((static_cast<double>(facing_x) * kRailStepAcross) + target->z);
        target->w = target->w + kRailStepAcross;
        found = find_stand_position(target, &ground, &ledge);
        if (!found) {
            target->x = static_cast<float>(target->x - (static_cast<double>(across) * kRailStepBack));
            target->z = static_cast<float>(target->z - (static_cast<double>(facing_x) * kRailStepBack));
            target->w = target->w - kRailStepBack;
            found = find_stand_position(target, &ground, &ledge);
            if (!found) {
                ground = first;
            }
        }
    }
    // seven feelers round the ground point, half the push feeler's length out (an x87 sine and cosine each)
    render::ProbeRay probe;
    const float push = get_param(kPushFeelerLength);
    auto half_push = static_cast<float>(static_cast<double>(push) * kHalfF);
    const float height = get_param(kProbeElevation);
    auto no_rise = static_cast<float>(static_cast<double>(half_push) * 0.0F);
    const maths::Vec4& facing = facing_;
    for (int i = 0; i < kProbeSides; ++i) {
        const double angle = static_cast<double>(i) * 0.89759790897369385;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        probe.line().start = {.x = ground.x, .y = static_cast<float>(static_cast<double>(ground.y) + height), .z = ground.z, .w = ground.w};
        auto dx = static_cast<float>((cosine * facing.x) - (sine * facing.z));
        const double dz = (sine * facing.x) + (cosine * facing.z);
        auto out_x = static_cast<float>(static_cast<double>(dx) * half_push);
        auto out_z = static_cast<float>(dz * half_push);
        const float rise = no_rise + ground.y;
        probe.line().end = {.x = out_x + ground.x, .y = static_cast<float>(static_cast<double>(rise) + height), .z = out_z + ground.z,
                           .w = ground.w + half_push};
        if (probe.get_hit_shape(true, false) != 0) {
            return false;
        }
    }
    // and from behind the skater, up past the obstruction height, to just above the ground point
    const maths::Vec4& pos = pos_;
    probe.line().start = {.x = static_cast<float>((static_cast<double>(facing.x) * -kProbeBehind) + pos.x),
                          .y = static_cast<float>(pos.y + (static_cast<double>(facing.y) * -kProbeBehind)),
                          .z = pos.z + (facing.z * -kProbeBehind),
                          .w = pos.w + (facing.w * -kProbeBehind)};
    const float top = get_param(kHangVertOriginOffset);
    const float obstruction = get_param(kPullUpObstructionHeight);
    probe.line().start.y = static_cast<float>(static_cast<double>(probe.line().start.y) + obstruction + top);
    probe.line().end = {.x = ground.x, .y = ground.y + 1.0F, .z = ground.z, .w = ground.w};
    if (probe.get_hit_shape(true, false) != 0) {
        return false;
    }

    // the animated move: walking at the end (the ledge's feeler kept), or in the air with no ground
    if (found) {
        set_anim_done(this, script::object_callback<&objects::FootMovement::end_pull_up>());
        assign_feeler(&feeler_, &ledge);
        feeler_hit_ = true;
        floor_normal_ = ledge.surface_dir();
    } else {
        set_anim_done(this, script::object_callback<&objects::FootMovement::end_drop_from_hang>());
    }
    transition_start_ = pos;
    transition_pos_ = ground;
    set_transition_facing(&facing_, 1.0F);
    anim_waiting_ = 1;
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0F;
    const double back = -(static_cast<double>(get_param(kPullUpOffsetForward)) + 6.0F);
    auto back_f = static_cast<float>(back);
    maths::Vec4& offset = critical_point_offset_;
    offset = {.x = back_f * facing.x, .y = back_f * facing.y, .z = back_f * facing.z, .w = static_cast<float>(back * facing.w)};
    offset.y = 1.0F;
    push_normal_ = facing;
    push_height_ = pos.y - hang_.up;
    change_phase(4);
    frame_.anim = kAnimPullUpFromHang;
    return true;
}

// 0x0043EC40
bool objects::FootMovement::drop_to_hang()
{
    if (physics_of(this)->state()->flag(objects::PoseFlag::kBailing).on != 0U) {
        return false;
    }
    const float factor = get_param(kDropToHangSpeedFactor);
    const double speed = run_speed() * factor;
    const maths::Vec4& velocity = vel_;
    const double moving = (static_cast<double>(velocity.x) * velocity.x) +
                          (static_cast<double>(velocity.y) * velocity.y) +
                          (static_cast<double>(velocity.z) * velocity.z);
    if (speed * speed < moving) {
        return false;
    }
    auto* tagging = core::down_cast<objects::TaggingControlComponent>(object_of(this)->fetch_module(kTaggingControl));
    if (tagging->pending()) {
        return false;
    }
    old_pos_.w = 1.0F;
    pos_.w = 1.0F;
    RailHit rail{};
    if (!find_walk_rail(this, &old_pos_, &pos_, facing_away, kDropToClimbMaxSnap, &rail)) {
        return false;
    }
    hang_rail_.node = rail.node;
    hang_rail_.next_node = rail.node->succ();
    up_speed_ = 0.0F;
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    hang_.along = rail.along;
    hang_.out = rail.out;
    hang_.up = rail.up;
    tagging->set_blocked(true);
    transition_start_ = pos_;
    transition_pos_ = rail.point;
    set_anim_done(this, script::object_callback<&objects::FootMovement::end_drop_to_hang>());
    set_transition_facing(&rail.facing, get_param(kDropToHangRotateFactor));
    anim_waiting_ = 1;
    const float horiz = get_param(kHangCriticalPointHorizOffset);
    critical_point_offset_ = {.x = rail.facing.x * horiz, .y = rail.facing.y * horiz, .z = rail.facing.z * horiz,
                              .w = rail.facing.w * horiz};
    push_normal_ = rail.facing;
    push_height_ = pos_.y;
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = static_cast<float>(hang_height_offset());
    change_phase(4);
    frame_.anim = kAnimDropToHang;
    return true;
}

// 0x0043F030
bool objects::FootMovement::grab_rail(maths::Vec4 start, maths::Vec4 end)
{
    if (up_speed_ > kMaxUpSpeed) {
        return false;
    }
    start.y = start.y + get_param(kHangVertOriginOffset);
    end.y = end.y + get_param(kHangVertOriginOffset);
    start.w = 1.0F;
    end.w = 1.0F;
    RailHit rail{};
    if (!find_walk_rail(this, &start, &end, current_facing, kClimbMaxSnap, &rail)) {
        return false;
    }
    hang_rail_.node = rail.node;
    hang_rail_.next_node = rail.node->succ();
    pos_ = rail.point;
    facing_ = rail.facing;
    hang_.along = rail.along;
    hang_.up = rail.up;
    hang_.out = rail.out;
    hang_.anim_type = rail.anim;
    set_hang_offset(this);
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    up_speed_ = 0.0F;
    model_lift_ = static_cast<float>(hang_height_offset());
    change_phase(2);
    tell_physics_rail(this);
    trigger_rail_script(this, kHangEvents);
    frame_.anim = kAnimHang;
    set_physics_flag_a0(this);
    return true;
}

// 0x0043F3A0
bool objects::FootMovement::shimmy_blocked(const maths::Vec4* out, float along, const maths::Vec4* point, bool left,
                                           const maths::Vec4* move, maths::Vec4 segment, objects::GrindPoint* start,
                                           objects::GrindPoint* end)
{
    const double dot = (static_cast<double>(segment.y) * move->y) + (static_cast<double>(segment.x) * move->x) +
                       (static_cast<double>(segment.z) * move->z);
    const bool towards_end = dot < 0.0 ? left : !left;
    maths::Vec4 sideways = left ? *move : maths::Vec4{.x = -move->x, .y = -move->y, .z = -move->z, .w = -move->w};
    maths::Vec4 outwards = *out;
    const double fraction = towards_end ? 1.0 - along : along;
    const double segment_length =
        std::sqrt((static_cast<double>(segment.z) * segment.z) + (static_cast<double>(segment.y) * segment.y) +
                  (static_cast<double>(segment.x) * segment.x));
    auto distance = static_cast<float>(segment_length * fraction);
    const float side_length = get_param(kHangMoveCollisionSideLength);
    if (static_cast<double>(side_length) + 1.0F > distance) {
        // near the segment's end: look along the next segment instead, if it doesn't turn too far
        along = (towards_end ? kAlongStep : -kAlongStep) + along;
        HangSegment onto_rail{.start = nullptr, .end = nullptr};
        const bool onto_next = next_hang_rail(&along, start, end, &onto_rail);
        const objects::GrindPoint* next_start = onto_rail.start;
        const objects::GrindPoint* next_end = onto_rail.end;
        if (onto_next) {
            const float sign = along > 0.5F ? -1.0F : 1.0F;
            maths::Vec4 point_a{};
            maths::Vec4 point_b{};
            get_rails_of(this)->get_absolute_pos(&point_a, next_end);
            get_rails_of(this)->get_absolute_pos(&point_b, next_start);
            const double dw = static_cast<double>(point_b.w) - point_a.w;
            const float dz = point_b.z - point_a.z;
            const float dx = point_b.x - point_a.x;
            maths::Vec4 next = {.x = dx * sign, .y = 0.0F, .z = dz * sign, .w = static_cast<float>(dw * sign)};
            next.make_unit();
            const double turn = (static_cast<double>(next.y) * out->y) + (static_cast<double>(next.z) * out->z) +
                                (static_cast<double>(next.x) * out->x);
            if (turn < 0.0 && turn > -kMaxTurnCos) {
                sideways = next;
                if (left) {
                    outwards.x = next.z;
                    outwards.z = -next.x;
                } else {
                    outwards.z = next.x;
                    outwards.x = -next.z;
                }
            }
        }
    }
    // the feeler: from the hang point, raised and pulled back out of the wall
    render::ProbeRay feeler;
    maths::Seg& line = feeler.line();
    line.start = *point;
    line.start.y = line.start.y + get_param(kHangMoveCollisionUp);
    const float back = get_param(kHangMoveCollisionBack);
    line.start.x = static_cast<float>(line.start.x - (static_cast<double>(back) * out->x));
    line.start.y = static_cast<float>(line.start.y - (static_cast<double>(back) * out->y));
    line.start.z = static_cast<float>(line.start.z - (static_cast<double>(back) * out->z));
    line.start.w = static_cast<float>(line.start.w - (static_cast<double>(back) * out->w));
    // `v` times `scale` added to the end (each product unrounded, as the x87 code adds it)
    auto add_scaled = [&line](float scale, const maths::Vec4& v) {
        line.end.x = static_cast<float>((static_cast<double>(scale) * v.x) + line.end.x);
        line.end.y = static_cast<float>((static_cast<double>(scale) * v.y) + line.end.y);
        line.end.z = static_cast<float>((static_cast<double>(scale) * v.z) + line.end.z);
        line.end.w = static_cast<float>((static_cast<double>(scale) * v.w) + line.end.w);
    };
    // sideways at the hang point's height, at the side height and at half of it
    line.end = line.start;
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    if (feeler.get_hit_shape(true, false) != 0) {
        return true;
    }
    line.end = line.start;
    line.end.y = line.end.y + get_param(kHangMoveCollisionSideHeight);
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    if (feeler.get_hit_shape(true, false) != 0) {
        return true;
    }
    line.end = line.start;
    line.end.y =
        static_cast<float>((static_cast<double>(get_param(kHangMoveCollisionSideHeight)) * kHalfF) + line.end.y);
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    if (feeler.get_hit_shape(true, false) != 0) {
        return true;
    }
    // down at the critical point, out from the wall, sideways and one more along the segment
    line.end = line.start;
    const float up = get_param(kHangMoveCollisionUp);
    line.end.y = static_cast<float>(critical_point_up() - up - 1.0F + line.end.y);
    add_scaled(get_param(kHangCriticalPointHorizOffset), outwards);
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    segment.make_unit();
    const maths::Vec4 onward = towards_end ? maths::Vec4{.x = -segment.x, .y = -segment.y, .z = -segment.z, .w = -segment.w} : segment;
    line.end.x = line.end.x + onward.x;
    line.end.y = line.end.y + onward.y;
    line.end.z = line.end.z + onward.z;
    line.end.w = line.end.w + onward.w;
    if (feeler.get_hit_shape(true, false) != 0) {
        return true;
    }
    // half the side height down, out and sideways (each step rounded: the original's vector operators)
    line.end = line.start;
    line.end.y =
        static_cast<float>(line.end.y - (static_cast<double>(get_param(kHangMoveCollisionSideHeight)) * kHalfF));
    line.end += get_param(kHangCriticalPointHorizOffset) * outwards;
    line.end += get_param(kHangMoveCollisionSideLength) * sideways;
    return feeler.get_hit_shape(true, false) != 0;
}

// 0x0043FC70
void objects::FootMovement::place_on_hang()
{
    GrindRailHub* rails = get_rails_of(this);
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    maths::Vec4 point{};
    const maths::Vec4* end_pos = rails->get_absolute_pos(&point_a, hang_rail_.next_node);
    const maths::Vec4* start_pos = rails->get_absolute_pos(&point_b, hang_rail_.node);
    maths::Vec4& pos = pos_;
    pos = *walk::get_lerp(&point, start_pos, end_pos, hang_.along);
    pos.y = hang_.up + pos.y;
    // out from the rail along the facing (x's product left unrounded, as the x87 code adds it)
    const float out = hang_.out;
    maths::Vec4& facing = facing_;
    const double out_x = static_cast<double>(out) * facing.x;
    const float out_y = out * facing.y;
    const float out_z = out * facing.z;
    const float out_w = out * facing.w;
    pos.x = static_cast<float>(out_x + pos.x);
    pos.y = out_y + pos.y;
    pos.z = out_z + pos.z;
    pos.w = out_w + pos.w;

    // riding something: let go of it once it's gone; when it turned, the facing across the rail again
    const objects::MovingSurfaceModule* contact = ride_;
    auto* tracker = contact->ride().get();
    if (!tracker) {
        return;
    }
    if (!tracker->entity().get_pull()) {
        stop_riding(this);
        return;
    }
    if (!tracker->turned()) {
        return;
    }
    rails->get_absolute_pos(&point_a, hang_rail_.next_node);
    rails->get_absolute_pos(&point_b, hang_rail_.node);
    maths::Vec4 along = {.x = point_b.x - point_a.x, .y = 0.0F, .z = point_b.z - point_a.z, .w = point_b.w - point_a.w};
    along.make_unit();
    double side = (static_cast<double>(along.x) * facing.z) + (static_cast<double>(-along.z) * facing.x);
    const float facing_y = facing.y;
    facing.y = 0.0F;
    side = side + (static_cast<double>(facing_y) * 0.0F);
    if (side > 0.0) {
        facing.x = -along.z;
        facing.z = along.x;
        facing.w = 1.0F;
    } else {
        facing.w = -1.0F; // (the original's)
        facing.x = along.z;
        facing.z = -along.x;
    }
}

// 0x0043FE30
bool objects::FootMovement::hang_clear(render::ProbeRay* feeler, const maths::Vec4* point, RailHit* record)
{
    RailHit* rail = record;
    rail->point = *point;
    const GrindPoint* node = rail->node;
    const maths::Vec4 side = {.x = -rail->facing.z, .y = 0.0F, .z = rail->facing.x, .w = 1.0F};
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    const maths::Vec4* next_pos = get_rails_of(this)->get_absolute_pos(&point_a, node->succ());
    const maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&point_b, rail->node);
    const maths::Vec4 segment = {.x = node_pos->x - next_pos->x, .y = node_pos->y - next_pos->y, .z = node_pos->z - next_pos->z,
                                 .w = node_pos->w - next_pos->w};
    // room at both sides
    if (!side_clear(feeler, *rail, &side)) {
        return false;
    }
    const maths::Vec4 other_side = {.x = -side.x, .y = 0.0F, .z = -side.z, .w = -1.0F};
    if (!side_clear(feeler, *rail, &other_side)) {
        return false;
    }
    // the hang point: below the rail and out from it; room to shimmy either way from there
    rail->point.y = rail->point.y - get_param(kHangVertOriginOffset);
    const float horiz = get_param(kHangHorizOriginOffset);
    rail->point.x = static_cast<float>((static_cast<double>(horiz) * rail->facing.x) + rail->point.x);
    rail->point.y = static_cast<float>((static_cast<double>(horiz) * rail->facing.y) + rail->point.y);
    rail->point.z = static_cast<float>((static_cast<double>(horiz) * rail->facing.z) + rail->point.z);
    rail->point.w = static_cast<float>((static_cast<double>(horiz) * rail->facing.w) + rail->point.w);
    if (shimmy_blocked(&rail->facing, rail->along, &rail->point, true, &side, segment, rail->node,
                       rail->node->succ())) {
        return false;
    }
    if (shimmy_blocked(&rail->facing, rail->along, &rail->point, false, &side, segment, rail->node,
                       rail->node->succ())) {
        return false;
    }
    // nothing between the skater and the critical point - or, on the ground, over whatever is (up first)
    maths::Vec4 critical = rail->point + get_param(kHangCriticalPointHorizOffset) * rail->facing;
    critical.y = static_cast<float>(critical_point_up() + critical.y);
    maths::Seg& line = feeler->line();
    line.start = pos_;
    line.end = critical;
    if (feeler->get_hit_shape(true, false) == 0) {
        return true;
    }
    if (phase_.state != 0) {
        return false;
    }
    line.end = pos_;
    line.end.y = get_param(kHopObstructionFeelerUp) + line.end.y;
    if (feeler->get_hit_shape(true, false) != 0) {
        return false;
    }
    line.start = line.end;
    line.end = critical;
    return feeler->get_hit_shape(true, false) == 0;
}

// 0x00440130
void objects::FootMovement::shimmy(float move)
{
    maths::Vec4& pos = pos_;
    maths::Vec4& offset = critical_point_offset_;
    maths::Vec4& facing = facing_;
    float& along = hang_.along;
    GrindPoint*& start = hang_rail_.node;
    GrindPoint*& end = hang_rail_.next_node;
    // all of it kept, to be put back if the move is blocked
    const maths::Vec4 old_pos = pos;
    const maths::Vec4 old_critical = {.x = pos.x + offset.x, .y = pos.y + offset.y, .z = pos.z + offset.z, .w = pos.w + offset.w};
    const float old_along = along;
    GrindPoint* const old_start = start;
    GrindPoint* const old_end = end;
    const maths::Vec4 old_offset = offset;
    const maths::Vec4 old_facing = facing;

    const maths::Vec4 side = {.x = -facing.z, .y = 0.0F, .z = facing.x, .w = 1.0F};
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    const maths::Vec4* end_pos = get_rails_of(this)->get_absolute_pos(&point_a, end);
    const maths::Vec4* start_pos = get_rails_of(this)->get_absolute_pos(&point_b, start);
    const maths::Vec4 segment = {.x = start_pos->x - end_pos->x, .y = start_pos->y - end_pos->y, .z = start_pos->z - end_pos->z,
                                 .w = start_pos->w - end_pos->w};
    const bool left = move > 0.0F;
    // (the differences unrounded here, as the x87 code keeps them)
    const double dot = (static_cast<double>(side.x) * (static_cast<double>(start_pos->x) - end_pos->x)) +
                       ((static_cast<double>(start_pos->z) - end_pos->z) * old_facing.x) +
                       ((static_cast<double>(start_pos->y) - end_pos->y) * 0.0F);
    const bool towards_end = dot < 0.0 ? left : !left;
    if (shimmy_blocked(&facing, along, &pos, left, &side, segment, start, end)) {
        hang_.climb_scale = 0.0F;
        frame_.anim = kAnimHang;
        return;
    }

    // along the rail by the move
    const double length_squared = (static_cast<double>(segment.x) * segment.x) +
                                  (static_cast<double>(segment.y) * segment.y) +
                                  (static_cast<double>(segment.z) * segment.z);
    auto length_squared_f = static_cast<float>(length_squared);
    double step = std::fabs(move) / std::sqrt(length_squared);
    if (!towards_end) {
        step = -step;
    }
    const double moved = step + along;
    along = static_cast<float>(moved);
    // past an end: onto the next rail (the rest of the way along it), or stopped at the end
    const bool past_end = moved < 0.0 || moved > 1.0;
    HangSegment next{.start = start, .end = end};
    const bool onto_next = past_end && next_hang_rail(&along, start, end, &next);
    start = next.start;
    end = next.end;
    if (past_end && !onto_next) {
        const float fraction = along;
        hang_.climb_scale = 0.0F;
        if (fraction > 1.0F) {
            along = 1.0F;
        } else {
            along = fraction < 0.0F ? 0.0F : fraction;
        }
        frame_.anim = kAnimHang;
    } else {
        if (past_end) {
            const float fraction = along;
            const double non_negative = fraction < 0.0F ? 0.0 : fraction;
            const double clamped = fraction > 1.0F ? 1.0 : non_negative;
            const double over = static_cast<double>(along) - clamped;
            auto rest = static_cast<float>(std::fabs(over) * std::sqrt(static_cast<double>(length_squared_f)));
            maths::Vec4 reversed{};
            const maths::Vec4* direction = &segment;
            if (!towards_end) {
                reversed = {.x = -segment.x, .y = -segment.y, .z = -segment.z, .w = -segment.w};
                direction = &reversed;
            }
            face_hang_rail(rest, along < 0.0F, direction);
        }
        frame_.anim = left ? kAnimHangMoveLeft : kAnimHangMoveRight;
    }

    // nothing in the way from the old critical point to the new one, or it's all put back
    {
        const maths::Vec4 critical = {.x = offset.x + pos.x, .y = offset.y + pos.y, .z = offset.z + pos.z, .w = offset.w + pos.w};
        render::ProbeRay feeler;
        feeler.line().start = old_critical;
        feeler.line().end = critical;
        if (feeler.get_hit_shape(true, false) != 0) {
            pos = old_pos;
            along = old_along;
            start = old_start;
            end = old_end;
            offset = old_offset;
            facing = old_facing;
            hang_.climb_scale = 0.0F;
            frame_.anim = kAnimHang;
        }
    }
}

// 0x00440690
bool objects::FootMovement::check_rail(const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* on_rail,
                                       const maths::Vec4* on_line, float along, RailHit* record, bool away)
{
    RailHit* rail = record;
    const float origin = get_param(kHangVertOriginOffset);
    // in the air: only rails at or below the line, and close to it
    if (phase_.state == 1) {
        if (on_rail->y > on_line->y) {
            return false;
        }
        if (std::fabs(static_cast<double>(on_rail->y) - on_line->y) > kMaxRailHeightGap) {
            return false;
        }
    }
    // not too steep
    const float dz = end->z - start->z;
    const float dx = end->x - start->x;
    maths::Vec4 direction = {.x = dx, .y = end->y - start->y, .z = dz, .w = end->w - start->w};
    direction.make_unit();
    if (std::fabs(direction.y) > std::sin(static_cast<double>(get_param(kHangMaxRailAscent)) * kRadiansPerDegree)) {
        return false;
    }

    // the facing: across the rail, flat - towards the skater's facing (or away)
    maths::Vec4& out = rail->facing;
    out = {.x = -dz, .y = 0.0F, .z = dx, .w = 1.0F};
    out.make_unit();
    const maths::Vec4& facing = facing_;
    auto flip = [&out] {
        out.x = -out.x;
        out.y = -out.y;
        out.z = -out.z;
    };
    const double towards = (static_cast<double>(facing.z) * out.z) + (static_cast<double>(facing.y) * out.y) +
                           (static_cast<double>(facing.x) * out.x);
    if (away ? towards > 0.0 : towards < 0.0) {
        flip();
    }
    // a one-sided rail (flag 0x80: hung from one side, 0x40: the other) turns it round
    bool on_right = (static_cast<double>(direction.x) * out.z) - (static_cast<double>(direction.z) * out.x) > 0.0;
    const uint32_t flags = rail->node->flag_bits();
    if ((((flags & kFlagRight) != 0U) && on_right) || (((flags & kFlagLeft) != 0U) && !on_right)) {
        flip();
        on_right = !on_right;
    }

    // room to hang there (the collision near the rail cached), facing that way - or the other, if the rail allows
    render::ProbeRay feeler;
    render::HitMeshCache cache;
    const float height = origin + 12.0F;
    maths::BBox box;
    box.max = {
        .x = on_rail->x + kBoxReach, .y = height + on_rail->y, .z = on_rail->z + kBoxReach, .w = on_rail->w + 1.0F};
    box.min = {
        .x = on_rail->x - kBoxReach, .y = on_rail->y - kBoxReach, .z = on_rail->z - kBoxReach, .w = on_rail->w - 1.0F};
    cache.tick_frame(box);
    feeler.set_hit_cache(&cache);
    rail->along = along;
    if (!hang_clear(&feeler, on_rail, record)) {
        if ((phase_.state != 1 && away) || (((flags & kFlagRight) != 0U) && !on_right) ||
            (((flags & kFlagLeft) != 0U) && on_right) || (flip(), !hang_clear(&feeler, on_rail, record))) {
            return false;
        }
    }

    // up and out from the rail; the animation: WALL when a feeler out from the hang point, raised, hits
    rail->up = rail->point.y - on_rail->y;
    rail->out = static_cast<float>((((static_cast<double>(rail->point.y) - on_rail->y) * out.y) +
                                    ((static_cast<double>(rail->point.z) - on_rail->z) * out.z)) +
                                   ((static_cast<double>(rail->point.x) - on_rail->x) * out.x));
    const float horiz = get_param(kHangCriticalPointHorizOffset);
    const float hx = horiz * out.x;
    const float hy = horiz * out.y;
    const float hz = horiz * out.z;
    const double hw = static_cast<double>(horiz) * out.w;
    maths::Seg& line = feeler.line();
    line.start = {.x = hx + rail->point.x, .y = hy + rail->point.y, .z = hz + rail->point.z, .w = static_cast<float>(hw + rail->point.w)};
    line.start.y = line.start.y + get_param(kHangInitAnimFeelerHeight);
    line.end = line.start;
    const float length = get_param(kHangInitAnimFeelerLength);
    line.end.x = static_cast<float>((static_cast<double>(length) * out.x) + line.end.x);
    line.end.y = static_cast<float>((static_cast<double>(length) * out.y) + line.end.y);
    line.end.z = static_cast<float>((static_cast<double>(length) * out.z) + line.end.z);
    line.end.w = static_cast<float>((static_cast<double>(length) * out.w) + line.end.w);
    rail->anim = (feeler.get_hit_shape(true, false) != 0) ? kAnimWall : kAnimSwing;
    return true;
}

// 0x00440BF0
void objects::FootMovement::update_hang()
{
    // carried by what it rides on
    const objects::MovingSurfaceModule* contact = ride_;
    if (auto* tracker = contact->ride().get()) {
        if (!tracker->entity().get_pull() || !tracker->tick_frame(&pos_)) {
            stop_riding(this);
        } else {
            ObjectModule* component = ride_->ride()->entity().get_pull()->fetch_module(kRailHubModuleName);
            maths::Mat4 matrix{};
            object_matrix_of(component, &matrix); // (unused)
            place_on_hang();
        }
    }
    jump_unsteered_ = false;

    const maths::Vec4& pos = pos_;
    const maths::Vec4& facing = facing_;
    const objects::PadInputModule* input = input_;
    if (control_full_) {
        if (core::at(input->pad_primary().lock(), 0) == 0 && core::at(input->pad_primary().polar(), 1).length != 0.0F &&
            std::fabs(core::at(input->pad_primary().polar(), 1).angle) < kQuarterPiF) {
            // pushed forward: a pull-up, pull_up_offset_forward ahead - then half that
            const float forward = get_param(kPullUpOffsetForward);
            maths::Vec4 target = {.x = static_cast<float>((static_cast<double>(forward) * facing.x) + pos.x),
                                  .y = static_cast<float>((static_cast<double>(forward) * facing.y) + pos.y),
                                  .z = (forward * facing.z) + pos.z,
                                  .w = (forward * facing.w) + pos.w};
            target.y = get_param(kPullUpOffsetUp) + target.y;
            if (pull_up(&target)) {
                return;
            }
            const double half = static_cast<double>(get_param(kPullUpOffsetForward)) * 0.5F;
            target = {.x = static_cast<float>(half * facing.x) + pos.x,
                      .y = static_cast<float>(half * facing.y) + pos.y,
                      .z = static_cast<float>(half * facing.z) + pos.z,
                      .w = static_cast<float>((half * facing.w) + pos.w)};
            target.y = get_param(kPullUpOffsetUp) + target.y;
            if (pull_up(&target)) {
                return;
            }
        } else if (walk::turned_round(&input->pad_primary()) != 0) {
            // turned round: let go
            change_phase(1);
            object_of(this)->post_own_event(kSkaterExitHang, nullptr);
            trigger_rail_script(this, kExitHangEvents);
            leap_heading_ = facing;
            leave_ride(&vel_, &up_speed_);
            frame_.anim = kAnimAir;
            return;
        }
    }

    // the shimmy's speed: the control's sideways part (within hang_vert_control_tolerance of straight sideways)
    auto sideways = static_cast<float>(kHalfPi - std::fabs(core::at(input->pad_primary().polar(), 1).angle));
    const float tolerance = get_param(kHangVertControlTolerance);
    float wanted = 0.0F;
    if (std::cos(static_cast<double>(tolerance) * kRadiansPerDegree) > std::fabs(sideways)) {
        const float direction = core::at(input->pad_primary().polar(), 1).angle > 0.0F ? 1.0F : -1.0F;
        const float speed = get_param(kHangMoveSpeedParam);
        wanted = static_cast<float>(static_cast<double>(direction) * control_.deflection * speed);
    }
    const float rate = get_param(kHangMoveLerpRate);
    float& move_speed = hang_.climb_scale;
    move_speed = static_cast<float>(
        move_speed + ((static_cast<double>(wanted) - move_speed) * (static_cast<double>(rate) * frame_.length)));
    const float speed_now = move_speed;
    const float cutoff = get_param(kHangMoveCutoff);
    if (std::fabs(speed_now) > cutoff) {
        speed_ = std::fabs(move_speed);
        shimmy(move_speed * frame_.length);
        place_on_hang();
        return;
    }
    frame_.anim = kAnimHang;
}

// --- ladders ---

// 0x00441020
void objects::FootMovement::end_ladder_off_bottom()
{
    change_phase(0);
    trigger_rail_script(this, kExitLadderEvents);
    if (feeler_hit_) {
        auto* feeler = &feeler_;
        trigger_feeler(this, kLadderOffBottomEvents, feeler);
        contact_feeler(this, feeler);
    }
}

// 0x00441070
void objects::FootMovement::end_ladder_off_top()
{
    change_phase(0);
    trigger_rail_script(this, kExitLadderEvents);
    if (feeler_hit_) {
        auto* feeler = &feeler_;
        trigger_feeler(this, kLadderOffTopEvents, feeler);
        contact_feeler(this, feeler);
    }
}

// 0x00441860
void objects::FootMovement::end_ladder_off_top_to_air()
{
    change_phase(1);
    trigger_rail_script(this, kExitLadderEvents);
    leap_heading_ = facing_;
    leave_ride(&vel_, &up_speed_);
}

// 0x004410C0
void objects::FootMovement::end_onto_ladder_bottom()
{
    GrindPoint* node = hang_rail_.node;
    const GrindPoint* other = ladder_other_end(node);
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    const maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&point_a, node);
    const maths::Vec4* other_pos = get_rails_of(this)->get_absolute_pos(&point_b, other);
    const float dx = other_pos->x - node_pos->x;
    const float dy = other_pos->y - node_pos->y;
    const float dz = other_pos->z - node_pos->z;
    const float up = get_param(kLadderBottomOffsetUp);
    const double length =
        std::sqrt((static_cast<double>(dz) * dz) + (static_cast<double>(dy) * dy) + (static_cast<double>(dx) * dx));
    hang_.along = static_cast<float>(up / length);
    change_phase(3);
    tell_physics_node(this);
}

// 0x00441190
void objects::FootMovement::end_onto_ladder_top()
{
    GrindPoint* node = hang_rail_.node;
    const GrindPoint* other = ladder_other_end(node);
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    const maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&point_a, node);
    const maths::Vec4* other_pos = get_rails_of(this)->get_absolute_pos(&point_b, other);
    const double dx = static_cast<double>(other_pos->x) - node_pos->x;
    const double dy = static_cast<double>(other_pos->y) - node_pos->y;
    const double dz = static_cast<double>(other_pos->z) - node_pos->z;
    auto length = static_cast<float>(std::sqrt((dz * dz) + (dy * dy) + (dx * dx)));
    const float up = get_param(kLadderTopOffsetUp);
    hang_.along = static_cast<float>((static_cast<double>(length) - up) / length);
    change_phase(3);
    tell_physics_node(this);
}

// 0x00441260
bool objects::FootMovement::check_ladder_top(const maths::Vec4* bottom, const maths::Vec4* top,
                                             const maths::Mat4* orientation, LadderHit* record)
{
    if (bottom->y > top->y) {
        return false;
    }
    maths::Vec4 up = {.x = top->x - bottom->x, .y = top->y - bottom->y, .z = top->z - bottom->z, .w = top->w - bottom->w};
    up.make_unit();
    if (up.y < kUprightY) {
        return false;
    }
    // ladder_top_offset_up below the top, ladder_climb_offset out of the ladder (x and y's products unrounded, as the
    // x87 code subtracts them)
    objects::LadderHit* ladder = record;
    const float offset = get_param(kLadderTopOffsetUp);
    const float oy = offset * up.y;
    const float oz = offset * up.z;
    const float ow = offset * up.w;
    ladder->point = {.x = static_cast<float>(top->x - (static_cast<double>(offset) * up.x)),
                     .y = top->y - oy,
                     .z = top->z - oz,
                     .w = top->w - ow};
    const float climb = get_param(kLadderClimbOffset);
    const maths::Vec4& at = core::at(orientation->row, 2);
    const float cz = climb * at.z;
    const float cw = climb * at.w;
    ladder->point.x = static_cast<float>(ladder->point.x - (static_cast<double>(climb) * at.x));
    ladder->point.y = static_cast<float>(ladder->point.y - (static_cast<double>(climb) * at.y));
    ladder->point.z = ladder->point.z - cz;
    ladder->point.w = ladder->point.w - cw;
    ladder->facing = at;
    return true;
}

// 0x004413E0
bool objects::FootMovement::check_ladder_line(const maths::Vec4* bottom, const maths::Vec4* top,
                                              const maths::Mat4* orientation, const maths::Vec4* on_ladder, float along,
                                              LadderHit* record)
{
    // upright enough (its height as part of its length; no length: the height itself)
    const float dy = top->y - bottom->y;
    const double dz = static_cast<double>(top->z) - bottom->z;
    const double dx = static_cast<double>(top->x) - bottom->x;
    const double length = std::sqrt((dx * dx) + (static_cast<double>(dy) * dy) + (dz * dz));
    float up_y = dy;
    if (length > 0.0) {
        up_y = static_cast<float>(dy * (1.0 / length));
    }
    if (up_y < kUprightY) {
        return false;
    }
    // the line meets it between ladder_bottom_offset_up above the bottom and ladder_top_offset_up below the top
    if ((static_cast<double>(up_y) * get_param(kLadderBottomOffsetUp)) + bottom->y > on_ladder->y) {
        return false;
    }
    if (top->y - (static_cast<double>(up_y) * get_param(kLadderTopOffsetUp)) < on_ladder->y) {
        return false;
    }
    // the point ladder_climb_offset out of it there, if nothing is in the way
    objects::LadderHit* ladder = record;
    const float climb = get_param(kLadderClimbOffset);
    const maths::Vec4& at = core::at(orientation->row, 2);
    const float cx = climb * at.x;
    const float cy = climb * at.y;
    const float cz = climb * at.z;
    ladder->point = {.x = on_ladder->x - cx,
                     .y = on_ladder->y - cy,
                     .z = on_ladder->z - cz,
                     .w = static_cast<float>(on_ladder->w - (static_cast<double>(climb) * at.w))};
    render::ProbeRay feeler;
    feeler.line().start = pos_;
    feeler.line().end = ladder->point;
    if (feeler.get_hit_shape(true, false) != 0) {
        return false;
    }
    ladder->facing = at;
    ladder->along = along;
    set_physics_flag_a0(this);
    return true;
}

// 0x00441680
void objects::FootMovement::climb_off_bottom()
{
    // ladder_bottom_offset_forward back and ladder_bottom_offset_up down: the ground there, if any
    const float forward = get_param(kLadderBottomOffsetForward);
    const maths::Vec4& pos = pos_;
    const maths::Vec4& facing = facing_;
    const float fz = forward * facing.z;
    const float fw = forward * facing.w;
    maths::Vec4 target = {.x = static_cast<float>(pos.x - (static_cast<double>(forward) * facing.x)),
                          .y = static_cast<float>(pos.y - (static_cast<double>(forward) * facing.y)),
                          .z = pos.z - fz,
                          .w = pos.w - fw};
    target.y = target.y - get_param(kLadderBottomOffsetUp);
    render::ProbeRay* ground = &feeler_;
    const bool found = find_stand_position(&target, &target, ground);
    feeler_hit_ = found;
    if (found) {
        floor_normal_ = ground->surface_dir();
    }
    // the animated move there
    transition_start_ = pos;
    transition_pos_ = target;
    set_anim_done(this, script::object_callback<&objects::FootMovement::end_ladder_off_bottom>());
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0F;
    set_transition_facing(&facing, 1.0F);
    anim_waiting_ = 1;
    push_normal_ = facing;
    push_height_ = pos.y;
    change_phase(4);
    frame_.anim = kAnimLadderOffBottom;
}

// 0x004418B0
bool objects::FootMovement::check_ladder_bottom(const maths::Vec4* bottom, const maths::Vec4* top,
                                                const maths::Mat4* orientation, LadderHit* record)
{
    if (bottom->y > top->y) {
        return false;
    }
    maths::Vec4 up = {.x = top->x - bottom->x, .y = top->y - bottom->y, .z = top->z - bottom->z, .w = top->w - bottom->w};
    up.make_unit();
    if (up.y < kUprightY) {
        return false;
    }
    // faced (within max_onto_ladder_angle)
    const float max_angle = get_param(kMaxOntoLadderAngle);
    const maths::Vec4& at = core::at(orientation->row, 2);
    const maths::Vec4& facing = facing_;
    const double dot = (static_cast<double>(at.z) * facing.z) + (static_cast<double>(at.y) * facing.y) +
                       (static_cast<double>(at.x) * facing.x);
    if (dot < std::cos(static_cast<double>(max_angle) * kRadiansPerDegree)) {
        return false;
    }
    // ladder_bottom_offset_up above the bottom, ladder_climb_offset out of the ladder
    objects::LadderHit* ladder = record;
    const float offset = get_param(kLadderBottomOffsetUp);
    const float ox = offset * up.x;
    const float oy = offset * up.y;
    const float oz = offset * up.z;
    ladder->point = {.x = ox + bottom->x,
                     .y = oy + bottom->y,
                     .z = oz + bottom->z,
                     .w = static_cast<float>((static_cast<double>(offset) * up.w) + bottom->w)};
    const float climb = get_param(kLadderClimbOffset);
    ladder->point.x = static_cast<float>(ladder->point.x - (static_cast<double>(climb) * at.x));
    ladder->point.y = static_cast<float>(ladder->point.y - (static_cast<double>(climb) * at.y));
    ladder->point.z = static_cast<float>(ladder->point.z - (static_cast<double>(climb) * at.z));
    ladder->point.w = static_cast<float>(ladder->point.w - (static_cast<double>(climb) * at.w));
    // nothing between the skater and 6 behind that point
    render::ProbeRay feeler;
    feeler.line().start = pos_;
    const float bx = at.x * 6.0F;
    const float by = at.y * 6.0F;
    const float bz = at.z * 6.0F;
    feeler.line().end = {.x = ladder->point.x - bx,
                         .y = ladder->point.y - by,
                         .z = ladder->point.z - bz,
                         .w = static_cast<float>(ladder->point.w - (static_cast<double>(at.w) * kLadderStep))};
    if (feeler.get_hit_shape(true, false) != 0) {
        return false;
    }
    ladder->facing = at;
    return true;
}

// 0x00441B80
void objects::FootMovement::climb_off_top()
{
    // ladder_top_offset_forward ahead and ladder_top_offset_up up: the ground there - or into the air
    const float forward = get_param(kLadderTopOffsetForward);
    const maths::Vec4& pos = pos_;
    const maths::Vec4& facing = facing_;
    const float fx = forward * facing.x;
    const float fy = forward * facing.y;
    const float fz = forward * facing.z;
    maths::Vec4 target = {.x = fx + pos.x,
                          .y = fy + pos.y,
                          .z = fz + pos.z,
                          .w = static_cast<float>((static_cast<double>(forward) * facing.w) + pos.w)};
    target.y = target.y + get_param(kLadderTopOffsetUp);
    render::ProbeRay ledge;
    const bool found = find_stand_position(&target, &target, &ledge);
    transition_start_ = pos;
    transition_pos_ = target;
    if (found) {
        set_anim_done(this, script::object_callback<&objects::FootMovement::end_ladder_off_top>());
        assign_feeler(&feeler_, &ledge);
        feeler_hit_ = true;
        floor_normal_ = feeler_.surface_dir();
    } else {
        set_anim_done(this, script::object_callback<&objects::FootMovement::end_ladder_off_top_to_air>());
    }
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0F;
    set_transition_facing(&facing, 1.0F);
    anim_waiting_ = 1;
    push_normal_ = facing;
    push_height_ = target.y;
    change_phase(4);
    frame_.anim = kAnimLadderOffTop;
}

// 0x00441DE0
bool objects::FootMovement::climb_onto_bottom(bool button)
{
    if (physics_of(this)->state()->flag(objects::PoseFlag::kBailing).on != 0U) {
        return false;
    }
    const float radius = button ? get_param(kButtonHorizSnapDistance) : get_param(kMaxHorizSnapDistance);
    const float height = get_param(kMaxVertSnapDistance);
    objects::LadderHit ladder{};
    if (!get_level_rails()->find_ladder(&pos_, radius, height, true, this, &ladder, &hang_rail_.node)) {
        return false;
    }
    grabbed_ladder(this);
    transition_start_ = pos_;
    transition_pos_ = ladder.point;
    set_anim_done(this, script::object_callback<&objects::FootMovement::end_onto_ladder_bottom>());
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0F;
    set_transition_facing(&ladder.facing, 1.0F);
    anim_waiting_ = 1;
    push_normal_ = ladder.facing;
    push_height_ = ladder.point.y;
    change_phase(4);
    frame_.anim = kAnimLadderOntoBottom;
    return true;
}

// 0x00441FC0
bool objects::FootMovement::climb_onto_top()
{
    if (physics_of(this)->state()->flag(objects::PoseFlag::kBailing).on != 0U) {
        return false;
    }
    const float height = get_param(kMaxVertSnapDistance);
    const float radius = get_param(kMaxHorizSnapDistance);
    objects::LadderHit ladder{};
    if (!get_level_rails()->find_ladder(&pos_, radius, height, false, this, &ladder, &hang_rail_.node)) {
        return false;
    }
    grabbed_ladder(this);
    transition_start_ = pos_;
    transition_pos_ = ladder.point;
    set_anim_done(this, script::object_callback<&objects::FootMovement::end_onto_ladder_top>());
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0F;
    set_transition_facing(&ladder.facing, 1.0F);
    anim_waiting_ = 1;
    push_normal_ = ladder.facing;
    push_height_ = pos_.y;
    change_phase(4);
    frame_.anim = kAnimLadderOntoTop;
    return true;
}

// 0x00442170
bool objects::FootMovement::grab_ladder(const maths::Vec4* start, const maths::Vec4* end)
{
    objects::LadderHit ladder{};
    if (!get_level_rails()->find_ladder_on_line(start, end, this, &ladder, &hang_rail_.node)) {
        return false;
    }
    get_rails_of(this) = get_level_rails();
    let_go_of_ride(ride_);
    pos_ = ladder.point;
    facing_ = ladder.facing;
    hang_.along = ladder.along;
    up_speed_ = 0.0F;
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    change_phase(3);
    tell_physics_node(this);
    frame_.anim = kAnimLadder;
    return true;
}

// 0x00442280
void objects::FootMovement::climb_ladder()
{
    // the control's up / down part (its angle's cosine) at ladder_move_speed
    const float speed = get_param(kLadderMoveSpeed);
    const objects::PadInputModule* input = input_;
    const double move_speed = std::cos(static_cast<double>(core::at(input->pad_primary().polar(), 1).angle)) *
                              control_.deflection * static_cast<double>(speed);
    speed_ = static_cast<float>(move_speed);
    auto move = static_cast<float>(move_speed * frame_.length);
    GrindPoint* node = hang_rail_.node;
    const GrindPoint* other = ladder_other_end(node);
    maths::Vec4 point_a{};
    maths::Vec4 point_b{};
    const maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&point_a, node);
    const maths::Vec4* other_pos = get_rails_of(this)->get_absolute_pos(&point_b, other);
    const maths::Vec4 delta = {.x = other_pos->x - node_pos->x,
                               .y = other_pos->y - node_pos->y,
                               .z = other_pos->z - node_pos->z,
                               .w = other_pos->w - node_pos->w};
    const double length =
        std::sqrt((static_cast<double>(delta.z) * delta.z) + (static_cast<double>(delta.y) * delta.y) +
                  (static_cast<double>(delta.x) * delta.x));
    const double step = move / length;
    const bool up = step > 0.0;
    float& along = hang_.along;
    along = static_cast<float>(step + along);
    auto length_f = static_cast<float>(length);
    // off the bottom or the top
    const float bottom_up = get_param(kLadderBottomOffsetUp);
    const double inverse = 1.0 / length_f;
    auto inverse_f = static_cast<float>(inverse);
    if (inverse * bottom_up > along) {
        climb_off_bottom();
        return;
    }
    const float top_up = get_param(kLadderTopOffsetUp);
    if ((static_cast<double>(length_f) - top_up) * inverse_f < along) {
        climb_off_top();
        return;
    }
    // else on it: the animation, the place along it (ladder_climb_offset out of it)
    const float fraction = along;
    along = fraction > 1.0F ? 1.0F : std::max(fraction, 0.0F);
    if (along == 0.0F || along == 1.0F) {
        frame_.anim = kAnimLadder;
        speed_ = 0.0F;
    } else {
        frame_.anim = up ? kAnimLadderMoveUp : kAnimLadderMoveDown;
    }
    const float climb = get_param(kLadderClimbOffset);
    const maths::Vec4& facing = facing_;
    const maths::Vec4 out = {.x = climb * facing.x, .y = climb * facing.y, .z = climb * facing.z, .w = climb * facing.w};
    const maths::Vec4 part = {.x = delta.x * along, .y = delta.y * along, .z = delta.z * along, .w = delta.w * along};
    node_pos = get_rails_of(this)->get_absolute_pos(&point_a, hang_rail_.node);
    const double x = static_cast<double>(part.x) + node_pos->x;
    const float y = part.y + node_pos->y;
    const float z = part.z + node_pos->z;
    const float w = part.w + node_pos->w;
    maths::Vec4& pos = pos_;
    pos.z = z - out.z;
    pos.w = w - out.w;
    pos.x = static_cast<float>(x - out.x);
    pos.y = y - out.y;
    speed_ = std::fabs(speed_);
}

// 0x004425B0
void objects::FootMovement::update_ladder()
{
    // the ladder's rail script for the first 25 ticks
    if (static_cast<uint32_t>(get_now()) - static_cast<uint32_t>(phase_.changed_at) < kSettleMs) {
        trigger_rail_script(this, kOntoLadderEvents);
    }
    jump_unsteered_ = false;
    // the control within ladder_control_tolerance of up or down: climbing
    const objects::PadInputModule* input = input_;
    auto upright = static_cast<float>(kHalfPi - std::fabs(core::at(input->pad_primary().polar(), 1).angle));
    const float tolerance = get_param(kLadderControlTolerance);
    if (std::cos(static_cast<double>(tolerance) * kRadiansPerDegree) < std::fabs(upright)) {
        climb_ladder();
        return;
    }
    frame_.anim = kAnimLadder;
    speed_ = 0.0F;
}

// 0x0043EB80
void objects::MovingSurfaceModule::ride_on(GameEntity* object)
{
    let_go_of_ride(this);
    // (the original wrote over the pointer without freeing what it held - let_go_of_ride has cleared it)
    memory::let_go(ride_);
    if (void* memory = memory::alloc(sizeof(RideTracker))) {
        ride_ = memory::take_unique<RideTracker, memory::HeapDestroy>(memory::construct_in<RideTracker>(memory, object));
    }
}
