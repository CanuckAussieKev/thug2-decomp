// The walk component's hanging, ladder and rail-climbing code (see walk_hang.h). Floating point follows the
// original's x87 code: values it keeps on the x87 stack are doubles here, values it stores are floats.

#include "engine/objects/walk_hang.h"

#include <bit>
#include <cmath>
#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
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
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/walk_camera.h"
#include "game/rider/walk_physics.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "game/skate/skate_module.h"
#include "engine/core/code_address.h"
#include "engine/objects/trigger_events.h"
#include "engine/objects/wait_and_die.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"

namespace {

template <typename T>
T& get_at(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
template <typename T>
const T& get_at(const void* object, uint32_t offset)
{
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(object) + offset);
}

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

objects::GameEntity* object_of(objects::FootMovement* walk) { return walk->entity_; }
objects::GrindRailHub*& get_rails_of(objects::FootMovement* walk) { return walk->rail_manager_; }
skater::SkaterPhysicsModule* physics_of(objects::FootMovement* walk) { return walk->core_physics_; }
maths::Vec4& vector_at(void* walk, uint32_t offset) { return get_at<maths::Vec4>(walk, offset); }

// What find_rail finds (its 0x34-byte record, filled by check_rail): the hang point, the direction to face (out from
// the wall), how far along, out and up from the rail, the animation, and the rail's node.
struct RailHit {
    maths::Vec4 point;         // +0x00
    maths::Vec4 facing;        // +0x10
    float along;               // +0x20
    float out;                 // +0x24
    float up;                  // +0x28
    uint32_t anim;             // +0x2C the hang's animation (SWING, or WALL)
    objects::GrindPoint* node; // +0x30
};
static_assert(sizeof(RailHit) == sizeof(objects::Record34), "the rail manager's record");

// A feeler copied (its assignment 0x0043A930).
void assign_feeler(render::ProbeRay* to, const render::ProbeRay* from) { to->get_assign(*from); }
// The movable contact: no ride any more (0x00427C70).
void stop_riding(objects::FootMovement* walk) { walk->ride_->stop_riding(); }
// A rail manager component's object's matrix (0x0042BA10) and its rail manager (vtable +0x34).
void object_matrix_of(objects::ObjectModule* component, maths::Mat4* matrix)
{
    static_cast<objects::GrindPathLinkModule*>(component)->update_rail_matrix(matrix);
}
objects::GrindRailHub* rail_manager_of(objects::ObjectModule* component)
{
    using Pull = objects::GrindRailHub*(__thiscall*)(objects::ObjectModule*);
    return (*reinterpret_cast<Pull**>(component))[0x34 / 4](component);
}
// The skate module's rail manager (the level's).
objects::GrindRailHub* get_level_rails() { return modules::g_skate_module->rail_manager_; }

// Onto a ladder from the level's rails: the ride let go, stopped.
void grabbed_ladder(objects::FootMovement* walk)
{
    get_rails_of(walk) = get_level_rails();
    stop_riding(walk);
    walk->up_speed_ = 0.0f;
    walk->vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
}

// find_rail for a walk component between `start` and `end`: the level's rails, else each moving rail manager's (its
// object's space: riding that object if one is found). `direction` / the margin's global float are read again for
// each. The rail manager used is left at +0x176C.
bool find_walk_rail(objects::FootMovement* walk, const maths::Vec4* start, const maths::Vec4* end,
                    const maths::Vec4* (*direction)(objects::FootMovement*, maths::Vec4*), uint32_t margin,
                    RailHit* found)
{
    maths::Vec4 buffer;
    get_rails_of(walk) = get_level_rails();
    const maths::Vec4* towards = direction(walk, &buffer);
    float snap = script::fetch_float(margin);
    auto* record = reinterpret_cast<objects::Record34*>(found);
    if (get_rails_of(walk)->find_rail(start, end, towards, walk, record, snap)) {
        stop_riding(walk);
        return true;
    }
    for (objects::ObjectModule* component = objects::object_hub_slot()->find_first_module_of_kind(kRailHubModuleName);
         component; component = component->next_of_type_) {
        maths::Mat4 to_local;
        object_matrix_of(component, &to_local);
        to_local.upend();
        maths::Vec4 local_start, local_end;
        to_local.xform(local_start, *start);
        to_local.xform(local_end, *end);
        get_rails_of(walk) = rail_manager_of(component);
        towards = direction(walk, &buffer);
        snap = script::fetch_float(margin);
        if (get_rails_of(walk)->find_rail(&local_start, &local_end, towards, walk, record, snap)) {
            reinterpret_cast<walk::MovableContact*>(walk->ride_)->start_riding(component->entity_);
            return true;
        }
    }
    return false;
}
const maths::Vec4* facing_away(objects::FootMovement* walk, maths::Vec4* buffer)
{
    const maths::Vec4& facing = walk->facing_;
    *buffer = {-facing.x, -facing.y, -facing.z, -facing.w};
    return buffer;
}
const maths::Vec4* current_facing(objects::FootMovement* walk, maths::Vec4*) { return &walk->facing_; }

// The animation-end callback (+0x1948): a member function pointer as the compiler lays it out - the code address
// and three words of adjustment.
template <typename Method>
uint32_t member_code_address(Method method)
{
    static_assert(sizeof(Method) == sizeof(uint32_t), "a plain member function pointer: just the code address");
    return std::bit_cast<uint32_t>(method);
}
void set_anim_done(objects::FootMovement* walk, uint32_t code)
{
    objects::AnimDoneCallback& callback = walk->anim_done_;
    callback.code_ = reinterpret_cast<void*>(code);
    callback.this_adjust_ = 0;
    callback.unknown8_ = 0;
    callback.unknownc_ = 0;
}

// The skater core's rail node (its +0x22C), if there is one.
void tell_physics_node(objects::FootMovement* walk)
{
    if (objects::GrindPoint* node = walk->hang_node_)
        physics_of(walk)->rail_node_ = node;
}

// The skater core's rail node and rail manager (its +0x22C / +0x230), for its rail scripts.
void tell_physics_rail(objects::FootMovement* walk)
{
    tell_physics_node(walk);
    if (objects::GrindRailHub* rails = get_rails_of(walk))
        physics_of(walk)->rail_manager_ = rails;
}
void trigger_rail_script(objects::FootMovement* walk, const int* events)
{
    physics_of(walk)->trigger_rail_script(events);
}
// The hang offset (+0x71C) for the facing: hang_critical_point_horiz_offset along it, the critical point's height up.
void set_hang_offset(objects::FootMovement* walk)
{
    float horiz = objects::FootMovement::get_param(kHangCriticalPointHorizOffset);
    const maths::Vec4& facing = walk->facing_;
    maths::Vec4& offset = walk->critical_point_offset_;
    offset.z = horiz * facing.z;
    offset.w = horiz * facing.w;
    offset.x = horiz * facing.x;
    offset.y = horiz * facing.y;
    offset.y = static_cast<float>(walk->critical_point_up());
}

// The trigger component told a feeler hit something (for `events`), and the movable contact the feeler's hit.
void trigger_feeler(objects::FootMovement* walk, const int* events, render::ProbeRay* feeler)
{
    walk->trigger_->probe_touches_zone(events, feeler);
}
void contact_feeler(objects::FootMovement* walk, render::ProbeRay* feeler) { walk->ride_->start_riding(feeler); }

// The game's clock with its offset.
int get_now() { return frame_clock::game_clock_elapsed() + objects::g_clock_offset; }

// The movable contact's ride let go (its object kept at +0x6C, the time at +0x70).
void let_go_of_ride(objects::MovingSurfaceModule* contact)
{
    auto* tracker = contact->ride_;
    if (!tracker)
        return;
    contact->last_object_ = tracker->entity_.get_pull(); // (the object last ridden)
    tracker->entity_.get_release();
    memory::dealloc(tracker);
    contact->ride_ = nullptr;
    contact->stop_time_ = get_now();
}

// The skater core's timed flag +0xA0 of +0x1528 set (with the time at +0xA4), unless it is.
void set_physics_flag_a0(objects::FootMovement* walk)
{
    objects::RiderPoseState* flags = physics_of(walk)->state_;
    if (flags->flag(objects::PoseFlag::kWallplanted).on != 1) {
        flags->flag(objects::PoseFlag::kWallplanted).on = 1;
        flags->flag(objects::PoseFlag::kWallplanted).changed_at = get_now();
    }
}

// The x87's cosine (the original's FCOS), rounded to a double.
double x87_cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
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

// What find_ladder / find_ladder_on_line find (their 0x24-byte record, filled by the ladder checkers): the point to climb
// from, the direction to face (the ladder's orientation's third row), how far along.
struct FoundLadder {
    maths::Vec4 point;  // +0x00
    maths::Vec4 facing; // +0x10
    float along;        // +0x20
};
static_assert(sizeof(FoundLadder) == sizeof(objects::Record24), "the rail manager's record");

// A ladder's other end: the node's next, else its previous.
objects::GrindPoint* ladder_other_end(objects::GrindPoint* node)
{
    return node->succ_ ? node->succ_ : node->last_value_;
}

// 0x006468F8: 60 degrees (a double)
constexpr double kSixtyDegrees = 1.0471975511965976;

} // namespace

// --- small helpers ---

// 0x0043D990
maths::Vec4* __cdecl walk::get_lerp(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float t)
{
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    float dz = static_cast<float>(static_cast<double>(b->z) - a->z);
    float dw = static_cast<float>(static_cast<double>(b->w) - a->w);
    float sx = static_cast<float>(dx * t);
    float sz = static_cast<float>(static_cast<double>(dz) * t);
    float sw = static_cast<float>(static_cast<double>(dw) * t);
    out->w = static_cast<float>(static_cast<double>(sw) + a->w);
    out->x = static_cast<float>(static_cast<double>(sx) + a->x);
    out->y = static_cast<float>(dy * t + a->y);
    out->z = static_cast<float>(static_cast<double>(sz) + a->z);
    return out;
}

// 0x0043DA20
int walk::SkaterTurn::turned_round() const
{
    const void* skater = this;
    if (get_at<int>(skater, 0x234) == 0 && get_at<float>(skater, 0x22C) != 0.0f &&
        3.1415927410125732 - std::fabs(static_cast<double>(get_at<float>(skater, 0x228))) < 0.78539818525314331)
        return 1;
    return 0;
}

// 0x0043DA70
float __cdecl objects::FootMovement::get_param(uint32_t name)
{
    float value;
    script::read_sub_struct(kWalkParameters)->fetch_float(name, &value, 1);
    return value;
}

// 0x0043DAA0
double objects::FootMovement::run_speed()
{
    float combo = get_param(kComboRunSpeed);
    float run = get_param(kRunSpeed);
    return (static_cast<double>(combo) - run) * run_adjustment_ + run;
}

// 0x0043DC00
double objects::FootMovement::hang_height_offset()
{
    const uint8_t* node = get_at<uint8_t*>(model_, 0x208);
    if (!(node[0x44] & 8))
        return 0.0;
    float fraction = get_at<float>(node, 0x20);
    float offset = get_param(kHangVertOriginOffset);
    return (1.0 - fraction) * offset;
}

// 0x0043DC60
double objects::FootMovement::critical_point_up()
{
    float vert = get_param(kHangCriticalPointVertOffset);
    float scaling = get_param(kMaxCasScaling);
    float origin = get_param(kHangVertOriginOffset);
    return vert - static_cast<double>(origin) * scaling;
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
    critical_point_offset_ = {0.0f, 0.0f, 0.0f, 1.0f};
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
    get_at<bool>(object_of(this)->fetch_module(kTaggingControl), 0x1D) = false;
}

// 0x0043DDD0
bool objects::FootMovement::side_clear(render::ProbeRay* feeler, const maths::Vec4* hang, const maths::Vec4* side)
{
    float side_length = get_param(kHangObstructionFeelerSide);
    float sx = side_length * side->x;
    float sy = side_length * side->y;
    float sz = side_length * side->z;
    float sw = side_length * side->w;
    float horiz = get_param(kHangCriticalPointHorizOffset);
    const maths::Vec4& point = hang[0];
    const maths::Vec4& out = hang[1];
    // the critical point (the y left unrounded, as the original keeps it on the x87 stack), moved along the side
    float cx = static_cast<float>(static_cast<double>(horiz) * out.x + point.x);
    double cy = static_cast<double>(horiz) * out.y + point.y;
    float hz = horiz * out.z;
    float hw = horiz * out.w;
    float cz = static_cast<float>(static_cast<double>(hz) + point.z);
    float cw = static_cast<float>(static_cast<double>(hw) + point.w);
    maths::Seg& line = feeler->line_;
    line.start.w = cw + sw;
    line.start.x = cx + sx;
    line.start.y = static_cast<float>(cy + sy);
    line.start.z = static_cast<float>(static_cast<double>(cz) + sz);
    line.end = line.start;
    float origin = get_param(kHangVertOriginOffset);
    line.start.y = static_cast<float>(static_cast<double>(line.start.y) - origin + critical_point_up());
    float up = get_param(kHangObstructionFeelerUp);
    line.end.y = up + line.end.y;
    if (feeler->get_hit_shape(true, false))
        return false;
    maths::Vec4 start = line.start;
    line.start = line.end;
    line.end = start;
    return !feeler->get_hit_shape(true, false);
}

// 0x0043DFC0
void objects::FootMovement::face_hang_rail(float distance, bool from_end, const maths::Vec4* direction)
{
    objects::GrindRailHub* rails = get_rails_of(this);
    maths::Vec4 a, b;
    rails->get_absolute_pos(&a, hang_next_node_);
    rails->get_absolute_pos(&b, hang_node_);
    maths::Vec4 d = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    float xx = d.x * d.x;
    double length = std::sqrt(static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y + xx);
    if (distance > length)
        distance = static_cast<float>(length);
    double fraction = distance / length;
    along_ = static_cast<float>(fraction);
    if (from_end)
        along_ = static_cast<float>(1.0 - fraction);
    // across the rail, flat
    maths::Vec4 across = {-d.z, 0.0f, d.x, 0.0f};
    double across_length = std::sqrt(static_cast<double>(across.x) * across.x + xx);
    if (across_length > 0.0) {
        double scale = 1.0 / across_length;
        across.x = static_cast<float>(across.x * scale);
        across.y = static_cast<float>(0.0 * scale);
        across.z = static_cast<float>(scale * d.x);
    }
    maths::Vec4 reversed = {-d.x, -d.y, -d.z, -d.w};
    const maths::Vec4& along = from_end ? reversed : d;
    const maths::Vec4& facing = facing_;
    double turn = static_cast<double>(direction->z) * facing.x - static_cast<double>(facing.z) * direction->x;
    double side = static_cast<double>(across.x) * along.z - static_cast<double>(across.z) * along.x;
    if (side * turn < 0.0) {
        across.x = -across.x;
        across.y = -across.y;
        across.z = -across.z;
    }
    double dot = static_cast<double>(across.z) * facing.z + static_cast<double>(across.y) * facing.y +
                 static_cast<double>(across.x) * facing.x;
    if (x87_cos(kSixtyDegrees) > dot)
        walk_camera_->apply_queued_change();
    facing_ = {across.x, across.y, across.z, 1.0f};
    set_hang_offset(this);
}

// 0x0043E230
bool objects::FootMovement::next_hang_rail(float* t, objects::GrindPoint* start, objects::GrindPoint* end,
                                           objects::GrindPoint** new_start, objects::GrindPoint** new_end)
{
    objects::GrindRailHub* rails = get_rails_of(this);
    auto grabbable = [rails](objects::GrindPoint* from, objects::GrindPoint* to) {
        maths::Vec4 a, b;
        rails->get_absolute_pos(&b, to);
        rails->get_absolute_pos(&a, from);
        return objects::rail_clear(a, b);
    };
    // a segment's flags are its first node's
    auto usable = [](const objects::GrindPoint* first) {
        return (first->flag_bits_ & objects::kRailFlagActive) && !(first->flag_bits_ & objects::kRailFlagNoClimbing);
    };
    objects::GrindPoint* joining = nullptr;
    if (*t < 0.0f) {
        // back past the start: the segment before it
        objects::GrindPoint* previous = start->last_value_;
        if (previous && usable(previous) && grabbable(previous, start)) {
            *new_end = start;
            *new_start = start->last_value_;
            return true;
        }
        if (!rails->find_joining_rail(start, 0x28, &joining))
            return false;
    } else {
        objects::GrindPoint* next = end->succ_;
        if (next && usable(end) && grabbable(end, next)) {
            *new_start = end;
            *new_end = end->succ_;
            return true;
        }
        if (!rails->find_joining_rail(end, 0x28, &joining))
            return false;
    }
    // a joining rail: does it run on the same way?
    bool same_way = objects::nodes_together(joining, end) || objects::nodes_together(joining->succ_, start);
    maths::Vec4 a, b;
    rails->get_absolute_pos(&a, start);
    rails->get_absolute_pos(&b, end);
    maths::Vec4 here = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    here.make_unit();
    objects::GrindPoint* joined_next = joining->succ_;
    rails->get_absolute_pos(&a, joining);
    rails->get_absolute_pos(&b, joined_next);
    maths::Vec4 there = {b.x - a.x, b.y - a.y, b.z - a.z, b.w - a.w};
    there.make_unit();
    double tx = there.x, ty = there.y, tz = there.z;
    if (!same_way) {
        tx *= -1.0f;
        ty *= -1.0f;
        tz *= -1.0f;
    }
    double dot = here.z * tz + ty * here.y + tx * here.x;
    if (dot < -0.86599999666213989) {
        debug::printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", 0x4F2, "BAD ANGLE ON COINCIDENT RAIL");
        return false;
    }
    if (!same_way)
        *t = 1.0f - *t;
    *new_start = joining;
    *new_end = joining->succ_;
    debug::printf("M:%s:%i: %s\n", "omponents\\WalkHangUtil.cpp", 0x4FC, "FOUND COINCIDENT RAIL");
    return true;
}

// 0x0043E530
bool objects::FootMovement::pull_up(maths::Vec4* target)
{
    // the ground to stand on: near the target, else 12 to one side of it, else 12 to the other
    render::ProbeRay ledge;
    ledge.make_new();
    maths::Vec4 ground;
    bool found = find_stand_position(target, &ground, &ledge);
    if (!found) {
        float across = -facing_.z;
        maths::Vec4 first = ground;
        float facing_x = facing_.x;
        target->x = static_cast<float>(static_cast<double>(across) * 12.0f + target->x);
        target->z = static_cast<float>(static_cast<double>(facing_x) * 12.0f + target->z);
        target->w = target->w + 12.0f;
        found = find_stand_position(target, &ground, &ledge);
        if (!found) {
            target->x = static_cast<float>(target->x - static_cast<double>(across) * 24.0f);
            target->z = static_cast<float>(target->z - static_cast<double>(facing_x) * 24.0f);
            target->w = target->w - 24.0f;
            found = find_stand_position(target, &ground, &ledge);
            if (!found)
                ground = first;
        }
    }
    // seven feelers round the ground point, half the push feeler's length out (an x87 sine and cosine each)
    render::ProbeRay probe;
    probe.make_new();
    float push = get_param(kPushFeelerLength);
    float half_push = static_cast<float>(static_cast<double>(push) * 0.5f);
    float height = get_param(kProbeElevation);
    float no_rise = static_cast<float>(static_cast<double>(half_push) * 0.0f);
    const maths::Vec4& facing = facing_;
    for (int i = 0; i < 7; ++i) {
        double angle = static_cast<double>(i) * 0.89759790897369385;
        double c = x87_cos(angle);
        double s = x87_sin(angle);
        probe.line_.start = {ground.x, static_cast<float>(static_cast<double>(ground.y) + height), ground.z, ground.w};
        float dx = static_cast<float>(c * facing.x - s * facing.z);
        double dz = s * facing.x + c * facing.z;
        float out_x = static_cast<float>(static_cast<double>(dx) * half_push);
        float out_z = static_cast<float>(dz * half_push);
        float rise = no_rise + ground.y;
        probe.line_.end = {out_x + ground.x, static_cast<float>(static_cast<double>(rise) + height), out_z + ground.z,
                           ground.w + half_push};
        if (probe.get_hit_shape(true, false))
            return false;
    }
    // and from behind the skater, up past the obstruction height, to just above the ground point
    const maths::Vec4& pos = pos_;
    probe.line_.start = {static_cast<float>(static_cast<double>(facing.x) * -6.0f + pos.x),
                         static_cast<float>(pos.y + static_cast<double>(facing.y) * -6.0f), pos.z + facing.z * -6.0f,
                         pos.w + facing.w * -6.0f};
    float top = get_param(kHangVertOriginOffset);
    float obstruction = get_param(kPullUpObstructionHeight);
    probe.line_.start.y = static_cast<float>(static_cast<double>(probe.line_.start.y) + obstruction + top);
    probe.line_.end = {ground.x, ground.y + 1.0f, ground.z, ground.w};
    if (probe.get_hit_shape(true, false))
        return false;

    // the animated move: walking at the end (the ledge's feeler kept), or in the air with no ground
    if (found) {
        set_anim_done(this, member_code_address(&objects::FootMovement::end_pull_up));
        assign_feeler(&feeler_, &ledge);
        feeler_hit_ = true;
        floor_normal_ = ledge.surface_dir_;
    } else {
        set_anim_done(this, member_code_address(&objects::FootMovement::end_drop_from_hang));
    }
    transition_start_ = pos;
    transition_pos_ = ground;
    set_transition_facing(&facing_, 1.0f);
    anim_waiting_ = 1;
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0f;
    double back = -(static_cast<double>(get_param(kPullUpOffsetForward)) + 6.0f);
    float back_f = static_cast<float>(back);
    maths::Vec4& offset = critical_point_offset_;
    offset = {back_f * facing.x, back_f * facing.y, back_f * facing.z, static_cast<float>(back * facing.w)};
    offset.y = 1.0f;
    push_normal_ = facing;
    push_height_ = pos.y - rail_up_;
    change_phase(4);
    anim_state_ = kAnimPullUpFromHang;
    return true;
}

// 0x0043EC40
bool objects::FootMovement::drop_to_hang()
{
    if (physics_of(this)->state_->flag(objects::PoseFlag::kBailing).on)
        return false;
    float factor = get_param(kDropToHangSpeedFactor);
    double speed = run_speed() * factor;
    const maths::Vec4& velocity = vel_;
    double moving = static_cast<double>(velocity.x) * velocity.x + static_cast<double>(velocity.y) * velocity.y +
                    static_cast<double>(velocity.z) * velocity.z;
    if (speed * speed < moving)
        return false;
    objects::ObjectModule* tagging = object_of(this)->fetch_module(kTaggingControl);
    if (get_at<bool>(tagging, 0x1C))
        return false;
    old_pos_.w = 1.0f;
    pos_.w = 1.0f;
    RailHit rail;
    if (!find_walk_rail(this, &old_pos_, &pos_, facing_away, kDropToClimbMaxSnap, &rail))
        return false;
    hang_node_ = rail.node;
    hang_next_node_ = rail.node->succ_;
    up_speed_ = 0.0f;
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    along_ = rail.along;
    rail_out_ = rail.out;
    rail_up_ = rail.up;
    get_at<bool>(tagging, 0x1D) = true;
    transition_start_ = pos_;
    transition_pos_ = rail.point;
    set_anim_done(this, member_code_address(&objects::FootMovement::end_drop_to_hang));
    set_transition_facing(&rail.facing, get_param(kDropToHangRotateFactor));
    anim_waiting_ = 1;
    float horiz = get_param(kHangCriticalPointHorizOffset);
    critical_point_offset_ = {rail.facing.x * horiz, rail.facing.y * horiz, rail.facing.z * horiz,
                              rail.facing.w * horiz};
    push_normal_ = rail.facing;
    push_height_ = pos_.y;
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = static_cast<float>(hang_height_offset());
    change_phase(4);
    anim_state_ = kAnimDropToHang;
    return true;
}

// 0x0043F030
bool objects::FootMovement::grab_rail(maths::Vec4 start, maths::Vec4 end)
{
    if (up_speed_ > 50.0f)
        return false;
    start.y = start.y + get_param(kHangVertOriginOffset);
    end.y = end.y + get_param(kHangVertOriginOffset);
    start.w = 1.0f;
    end.w = 1.0f;
    RailHit rail;
    if (!find_walk_rail(this, &start, &end, current_facing, kClimbMaxSnap, &rail))
        return false;
    hang_node_ = rail.node;
    hang_next_node_ = rail.node->succ_;
    pos_ = rail.point;
    facing_ = rail.facing;
    along_ = rail.along;
    rail_up_ = rail.up;
    rail_out_ = rail.out;
    hang_init_anim_type_ = rail.anim;
    set_hang_offset(this);
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    up_speed_ = 0.0f;
    model_lift_ = static_cast<float>(hang_height_offset());
    change_phase(2);
    tell_physics_rail(this);
    trigger_rail_script(this, kHangEvents);
    anim_state_ = kAnimHang;
    set_physics_flag_a0(this);
    return true;
}

// 0x0043F3A0
bool objects::FootMovement::shimmy_blocked(const maths::Vec4* out, float along, const maths::Vec4* point, bool left,
                                           const maths::Vec4* move, maths::Vec4 segment, objects::GrindPoint* start,
                                           objects::GrindPoint* end)
{
    double dot = static_cast<double>(segment.y) * move->y + static_cast<double>(segment.x) * move->x +
                 static_cast<double>(segment.z) * move->z;
    bool towards_end = dot < 0.0 ? left : !left;
    maths::Vec4 sideways = left ? *move : maths::Vec4{-move->x, -move->y, -move->z, -move->w};
    maths::Vec4 outwards = *out;
    double t = towards_end ? 1.0 - along : along;
    double segment_length =
        std::sqrt(static_cast<double>(segment.z) * segment.z + static_cast<double>(segment.y) * segment.y +
                  static_cast<double>(segment.x) * segment.x);
    float distance = static_cast<float>(segment_length * t);
    float side_length = get_param(kHangMoveCollisionSideLength);
    if (static_cast<double>(side_length) + 1.0f > distance) {
        // near the segment's end: look along the next segment instead, if it doesn't turn too far
        along = (towards_end ? 2.0f : -2.0f) + along;
        objects::GrindPoint* next_start;
        objects::GrindPoint* next_end;
        if (next_hang_rail(&along, start, end, &next_start, &next_end)) {
            float sign = along > 0.5f ? -1.0f : 1.0f;
            maths::Vec4 a, b;
            get_rails_of(this)->get_absolute_pos(&a, next_end);
            get_rails_of(this)->get_absolute_pos(&b, next_start);
            double dw = static_cast<double>(b.w) - a.w;
            float dz = b.z - a.z;
            float dx = b.x - a.x;
            maths::Vec4 next = {dx * sign, 0.0f, dz * sign, static_cast<float>(dw * sign)};
            next.make_unit();
            double turn = static_cast<double>(next.y) * out->y + static_cast<double>(next.z) * out->z +
                          static_cast<double>(next.x) * out->x;
            if (turn < 0.0 && turn > -0.87000000476837158) {
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
    feeler.make_new();
    maths::Seg& line = feeler.line_;
    line.start = *point;
    line.start.y = line.start.y + get_param(kHangMoveCollisionUp);
    float back = get_param(kHangMoveCollisionBack);
    line.start.x = static_cast<float>(line.start.x - static_cast<double>(back) * out->x);
    line.start.y = static_cast<float>(line.start.y - static_cast<double>(back) * out->y);
    line.start.z = static_cast<float>(line.start.z - static_cast<double>(back) * out->z);
    line.start.w = static_cast<float>(line.start.w - static_cast<double>(back) * out->w);
    // `v` times `scale` added to the end (each product unrounded, as the x87 code adds it)
    auto add_scaled = [&line](float scale, const maths::Vec4& v) {
        line.end.x = static_cast<float>(static_cast<double>(scale) * v.x + line.end.x);
        line.end.y = static_cast<float>(static_cast<double>(scale) * v.y + line.end.y);
        line.end.z = static_cast<float>(static_cast<double>(scale) * v.z + line.end.z);
        line.end.w = static_cast<float>(static_cast<double>(scale) * v.w + line.end.w);
    };
    // sideways at the hang point's height, at the side height and at half of it
    line.end = line.start;
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    if (feeler.get_hit_shape(true, false))
        return true;
    line.end = line.start;
    line.end.y = line.end.y + get_param(kHangMoveCollisionSideHeight);
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    if (feeler.get_hit_shape(true, false))
        return true;
    line.end = line.start;
    line.end.y = static_cast<float>(static_cast<double>(get_param(kHangMoveCollisionSideHeight)) * 0.5f + line.end.y);
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    if (feeler.get_hit_shape(true, false))
        return true;
    // down at the critical point, out from the wall, sideways and one more along the segment
    line.end = line.start;
    float up = get_param(kHangMoveCollisionUp);
    line.end.y = static_cast<float>(critical_point_up() - up - 1.0f + line.end.y);
    add_scaled(get_param(kHangCriticalPointHorizOffset), outwards);
    add_scaled(get_param(kHangMoveCollisionSideLength), sideways);
    segment.make_unit();
    const maths::Vec4 onward = towards_end ? maths::Vec4{-segment.x, -segment.y, -segment.z, -segment.w} : segment;
    line.end.x = line.end.x + onward.x;
    line.end.y = line.end.y + onward.y;
    line.end.z = line.end.z + onward.z;
    line.end.w = line.end.w + onward.w;
    if (feeler.get_hit_shape(true, false))
        return true;
    // half the side height down, out and sideways (each step rounded: the original's vector operators)
    line.end = line.start;
    line.end.y = static_cast<float>(line.end.y - static_cast<double>(get_param(kHangMoveCollisionSideHeight)) * 0.5f);
    line.end += get_param(kHangCriticalPointHorizOffset) * outwards;
    line.end += get_param(kHangMoveCollisionSideLength) * sideways;
    return feeler.get_hit_shape(true, false) != 0;
}

// 0x0043FC70
void objects::FootMovement::place_on_hang()
{
    GrindRailHub* rails = get_rails_of(this);
    maths::Vec4 a, b, point;
    maths::Vec4* end_pos = rails->get_absolute_pos(&a, hang_next_node_);
    maths::Vec4* start_pos = rails->get_absolute_pos(&b, hang_node_);
    maths::Vec4& pos = pos_;
    pos = *walk::get_lerp(&point, start_pos, end_pos, along_);
    pos.y = rail_up_ + pos.y;
    // out from the rail along the facing (x's product left unrounded, as the x87 code adds it)
    float out = rail_out_;
    maths::Vec4& facing = facing_;
    double out_x = static_cast<double>(out) * facing.x;
    float out_y = out * facing.y;
    float out_z = out * facing.z;
    float out_w = out * facing.w;
    pos.x = static_cast<float>(out_x + pos.x);
    pos.y = out_y + pos.y;
    pos.z = out_z + pos.z;
    pos.w = out_w + pos.w;

    // riding something: let go of it once it's gone; when it turned, the facing across the rail again
    objects::MovingSurfaceModule* contact = ride_;
    auto* tracker = contact->ride_;
    if (!tracker)
        return;
    if (!tracker->entity_.get_pull()) {
        stop_riding(this);
        return;
    }
    if (!tracker->turned_)
        return;
    rails->get_absolute_pos(&a, hang_next_node_);
    rails->get_absolute_pos(&b, hang_node_);
    maths::Vec4 along = {b.x - a.x, 0.0f, b.z - a.z, b.w - a.w};
    along.make_unit();
    double side = static_cast<double>(along.x) * facing.z + static_cast<double>(-along.z) * facing.x;
    float facing_y = facing.y;
    facing.y = 0.0f;
    side = side + static_cast<double>(facing_y) * 0.0f;
    if (side > 0.0) {
        facing.x = -along.z;
        facing.z = along.x;
        facing.w = 1.0f;
    } else {
        facing.w = -1.0f; // (the original's)
        facing.x = along.z;
        facing.z = -along.x;
    }
}

// 0x0043FE30
bool objects::FootMovement::hang_clear(render::ProbeRay* feeler, const maths::Vec4* point, Record34* record)
{
    auto* rail = reinterpret_cast<RailHit*>(record);
    rail->point = *point;
    GrindPoint* node = rail->node;
    maths::Vec4 side = {-rail->facing.z, 0.0f, rail->facing.x, 1.0f};
    maths::Vec4 a, b;
    maths::Vec4* next_pos = get_rails_of(this)->get_absolute_pos(&a, node->succ_);
    maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&b, rail->node);
    maths::Vec4 segment = {node_pos->x - next_pos->x, node_pos->y - next_pos->y, node_pos->z - next_pos->z,
                           node_pos->w - next_pos->w};
    // room at both sides
    if (!side_clear(feeler, &rail->point, &side))
        return false;
    maths::Vec4 other_side = {-side.x, 0.0f, -side.z, -1.0f};
    if (!side_clear(feeler, &rail->point, &other_side))
        return false;
    // the hang point: below the rail and out from it; room to shimmy either way from there
    rail->point.y = rail->point.y - get_param(kHangVertOriginOffset);
    float horiz = get_param(kHangHorizOriginOffset);
    rail->point.x = static_cast<float>(static_cast<double>(horiz) * rail->facing.x + rail->point.x);
    rail->point.y = static_cast<float>(static_cast<double>(horiz) * rail->facing.y + rail->point.y);
    rail->point.z = static_cast<float>(static_cast<double>(horiz) * rail->facing.z + rail->point.z);
    rail->point.w = static_cast<float>(static_cast<double>(horiz) * rail->facing.w + rail->point.w);
    if (shimmy_blocked(&rail->facing, rail->along, &rail->point, true, &side, segment, rail->node, rail->node->succ_))
        return false;
    if (shimmy_blocked(&rail->facing, rail->along, &rail->point, false, &side, segment, rail->node, rail->node->succ_))
        return false;
    // nothing between the skater and the critical point - or, on the ground, over whatever is (up first)
    maths::Vec4 critical = rail->point + get_param(kHangCriticalPointHorizOffset) * rail->facing;
    critical.y = static_cast<float>(critical_point_up() + critical.y);
    maths::Seg& line = feeler->line_;
    line.start = pos_;
    line.end = critical;
    if (!feeler->get_hit_shape(true, false))
        return true;
    if (state_ != 0)
        return false;
    line.end = pos_;
    line.end.y = get_param(kHopObstructionFeelerUp) + line.end.y;
    if (feeler->get_hit_shape(true, false))
        return false;
    line.start = line.end;
    line.end = critical;
    return !feeler->get_hit_shape(true, false);
}

// 0x00440130
void objects::FootMovement::shimmy(float move)
{
    maths::Vec4& pos = pos_;
    maths::Vec4& offset = critical_point_offset_;
    maths::Vec4& facing = facing_;
    float& along = along_;
    GrindPoint*& start = hang_node_;
    GrindPoint*& end = hang_next_node_;
    // all of it kept, to be put back if the move is blocked
    const maths::Vec4 old_pos = pos;
    const maths::Vec4 old_critical = {pos.x + offset.x, pos.y + offset.y, pos.z + offset.z, pos.w + offset.w};
    const float old_along = along;
    GrindPoint* const old_start = start;
    GrindPoint* const old_end = end;
    const maths::Vec4 old_offset = offset;
    const maths::Vec4 old_facing = facing;

    maths::Vec4 side = {-facing.z, 0.0f, facing.x, 1.0f};
    maths::Vec4 a, b;
    maths::Vec4* end_pos = get_rails_of(this)->get_absolute_pos(&a, end);
    maths::Vec4* start_pos = get_rails_of(this)->get_absolute_pos(&b, start);
    maths::Vec4 segment = {start_pos->x - end_pos->x, start_pos->y - end_pos->y, start_pos->z - end_pos->z,
                           start_pos->w - end_pos->w};
    bool left = move > 0.0f;
    // (the differences unrounded here, as the x87 code keeps them)
    double dot = static_cast<double>(side.x) * (static_cast<double>(start_pos->x) - end_pos->x) +
                 (static_cast<double>(start_pos->z) - end_pos->z) * old_facing.x +
                 (static_cast<double>(start_pos->y) - end_pos->y) * 0.0f;
    bool towards_end = dot < 0.0 ? left : !left;
    if (shimmy_blocked(&facing, along, &pos, left, &side, segment, start, end)) {
        climb_scale_ = 0.0f;
        anim_state_ = kAnimHang;
        return;
    }

    // along the rail by the move
    double length_squared = static_cast<double>(segment.x) * segment.x + static_cast<double>(segment.y) * segment.y +
                            static_cast<double>(segment.z) * segment.z;
    float length_squared_f = static_cast<float>(length_squared);
    double step = std::fabs(move) / std::sqrt(length_squared);
    if (!towards_end)
        step = -step;
    double moved = step + along;
    along = static_cast<float>(moved);
    if (moved < 0.0 || moved > 1.0) {
        // past an end: onto the next rail (the rest of the way along it), or stopped at the end
        if (!next_hang_rail(&along, start, end, &start, &end)) {
            float t = along;
            climb_scale_ = 0.0f;
            if (t > 1.0f)
                along = 1.0f;
            else
                along = t < 0.0f ? 0.0f : t;
            anim_state_ = kAnimHang;
            goto check;
        }
        float t = along;
        double clamped = t > 1.0f ? 1.0 : t < 0.0f ? 0.0 : t;
        double over = static_cast<double>(along) - clamped;
        float rest = static_cast<float>(std::fabs(over) * std::sqrt(static_cast<double>(length_squared_f)));
        maths::Vec4 reversed;
        const maths::Vec4* direction = &segment;
        if (!towards_end) {
            reversed = {-segment.x, -segment.y, -segment.z, -segment.w};
            direction = &reversed;
        }
        face_hang_rail(rest, along < 0.0f, direction);
    }
    anim_state_ = left ? kAnimHangMoveLeft : kAnimHangMoveRight;

check:
    // nothing in the way from the old critical point to the new one, or it's all put back
    {
        maths::Vec4 critical = {offset.x + pos.x, offset.y + pos.y, offset.z + pos.z, offset.w + pos.w};
        render::ProbeRay feeler;
        feeler.make_new();
        feeler.line_.start = old_critical;
        feeler.line_.end = critical;
        if (feeler.get_hit_shape(true, false)) {
            pos = old_pos;
            along = old_along;
            start = old_start;
            end = old_end;
            offset = old_offset;
            facing = old_facing;
            climb_scale_ = 0.0f;
            anim_state_ = kAnimHang;
        }
    }
}

// 0x00440690
bool objects::FootMovement::check_rail(const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* on_rail,
                                       const maths::Vec4* on_line, float along, Record34* record, bool away)
{
    auto* rail = reinterpret_cast<RailHit*>(record);
    float origin = get_param(kHangVertOriginOffset);
    // in the air: only rails at or below the line, and close to it
    if (state_ == 1) {
        if (on_rail->y > on_line->y)
            return false;
        if (std::fabs(static_cast<double>(on_rail->y) - on_line->y) > 12.0f)
            return false;
    }
    // not too steep
    float dz = end->z - start->z;
    float dx = end->x - start->x;
    maths::Vec4 direction = {dx, end->y - start->y, dz, end->w - start->w};
    direction.make_unit();
    if (std::fabs(direction.y) > x87_sin(static_cast<double>(get_param(kHangMaxRailAscent)) * 0.017453292384743690))
        return false;

    // the facing: across the rail, flat - towards the skater's facing (or away)
    maths::Vec4& out = rail->facing;
    out = {-dz, 0.0f, dx, 1.0f};
    out.make_unit();
    const maths::Vec4& facing = facing_;
    auto flip = [&out] {
        out.x = -out.x;
        out.y = -out.y;
        out.z = -out.z;
    };
    double towards = static_cast<double>(facing.z) * out.z + static_cast<double>(facing.y) * out.y +
                     static_cast<double>(facing.x) * out.x;
    if (away ? towards > 0.0 : towards < 0.0)
        flip();
    // a one-sided rail (flag 0x80: hung from one side, 0x40: the other) turns it round
    bool on_right = static_cast<double>(direction.x) * out.z - static_cast<double>(direction.z) * out.x > 0.0;
    uint32_t flags = rail->node->flag_bits_;
    if (((flags & 0x80) && on_right) || ((flags & 0x40) && !on_right)) {
        flip();
        on_right = !on_right;
    }

    // room to hang there (the collision near the rail cached), facing that way - or the other, if the rail allows
    render::ProbeRay feeler;
    feeler.make_new();
    render::HitMeshCache cache;
    cache.make_new();
    float height = origin + 12.0f;
    maths::BBox box;
    box.max = {on_rail->x + 24.0f, height + on_rail->y, on_rail->z + 24.0f, on_rail->w + 1.0f};
    box.min = {on_rail->x - 24.0f, on_rail->y - 24.0f, on_rail->z - 24.0f, on_rail->w - 1.0f};
    cache.tick_frame(box);
    feeler.hit_cache_ = &cache;
    rail->along = along;
    if (!hang_clear(&feeler, on_rail, record)) {
        if ((state_ != 1 && away) || ((flags & 0x80) && !on_right) || ((flags & 0x40) && on_right) ||
            (flip(), !hang_clear(&feeler, on_rail, record))) {
            cache.destruct();
            return false;
        }
    }

    // up and out from the rail; the animation: WALL when a feeler out from the hang point, raised, hits
    rail->up = rail->point.y - on_rail->y;
    rail->out = static_cast<float>(((static_cast<double>(rail->point.y) - on_rail->y) * out.y +
                                    (static_cast<double>(rail->point.z) - on_rail->z) * out.z) +
                                   (static_cast<double>(rail->point.x) - on_rail->x) * out.x);
    float horiz = get_param(kHangCriticalPointHorizOffset);
    float hx = horiz * out.x, hy = horiz * out.y, hz = horiz * out.z;
    double hw = static_cast<double>(horiz) * out.w;
    maths::Seg& line = feeler.line_;
    line.start = {hx + rail->point.x, hy + rail->point.y, hz + rail->point.z, static_cast<float>(hw + rail->point.w)};
    line.start.y = line.start.y + get_param(kHangInitAnimFeelerHeight);
    line.end = line.start;
    float length = get_param(kHangInitAnimFeelerLength);
    line.end.x = static_cast<float>(static_cast<double>(length) * out.x + line.end.x);
    line.end.y = static_cast<float>(static_cast<double>(length) * out.y + line.end.y);
    line.end.z = static_cast<float>(static_cast<double>(length) * out.z + line.end.z);
    line.end.w = static_cast<float>(static_cast<double>(length) * out.w + line.end.w);
    rail->anim = feeler.get_hit_shape(true, false) ? kAnimWall : kAnimSwing;
    cache.destruct();
    return true;
}

// 0x00440BF0
void objects::FootMovement::update_hang()
{
    // carried by what it rides on
    objects::MovingSurfaceModule* contact = ride_;
    if (auto* tracker = contact->ride_) {
        if (!tracker->entity_.get_pull() || !tracker->tick_frame(&pos_)) {
            stop_riding(this);
        } else {
            ObjectModule* component = ride_->ride_->entity_.get_pull()->fetch_module(kRailHubModuleName);
            maths::Mat4 matrix;
            object_matrix_of(component, &matrix); // (unused)
            place_on_hang();
        }
    }
    jump_unsteered_ = false;

    maths::Vec4& pos = pos_;
    const maths::Vec4& facing = facing_;
    objects::PadInputModule* input = input_;
    if (control_full_) {
        if (input->pad_primary_.lock_[0] == 0 && input->pad_primary_.polar_[1].length != 0.0f &&
            std::fabs(input->pad_primary_.polar_[1].angle) < 0.78539818525314331f) {
            // pushed forward: a pull-up, pull_up_offset_forward ahead - then half that
            float forward = get_param(kPullUpOffsetForward);
            maths::Vec4 target = {static_cast<float>(static_cast<double>(forward) * facing.x + pos.x),
                                  static_cast<float>(static_cast<double>(forward) * facing.y + pos.y),
                                  forward * facing.z + pos.z, forward * facing.w + pos.w};
            target.y = get_param(kPullUpOffsetUp) + target.y;
            if (pull_up(&target))
                return;
            double half = static_cast<double>(get_param(kPullUpOffsetForward)) * 0.5f;
            target = {static_cast<float>(half * facing.x) + pos.x, static_cast<float>(half * facing.y) + pos.y,
                      static_cast<float>(half * facing.z) + pos.z, static_cast<float>(half * facing.w + pos.w)};
            target.y = get_param(kPullUpOffsetUp) + target.y;
            if (pull_up(&target))
                return;
        } else if (reinterpret_cast<const walk::SkaterTurn*>(&input->pad_primary_.up_dir_)->turned_round()) {
            // turned round: let go
            change_phase(1);
            object_of(this)->post_own_event(kSkaterExitHang, nullptr);
            trigger_rail_script(this, kExitHangEvents);
            leap_heading_ = facing;
            leave_ride(&vel_, &up_speed_);
            anim_state_ = kAnimAir;
            return;
        }
    }

    // the shimmy's speed: the control's sideways part (within hang_vert_control_tolerance of straight sideways)
    float sideways = static_cast<float>(1.5707963705062866 - std::fabs(input->pad_primary_.polar_[1].angle));
    float tolerance = get_param(kHangVertControlTolerance);
    float wanted = 0.0f;
    if (x87_cos(static_cast<double>(tolerance) * 0.017453292384743690) > std::fabs(sideways)) {
        float direction = input->pad_primary_.polar_[1].angle > 0.0f ? 1.0f : -1.0f;
        float speed = get_param(kHangMoveSpeedParam);
        wanted = static_cast<float>(static_cast<double>(direction) * stick_deflection_ * speed);
    }
    float rate = get_param(kHangMoveLerpRate);
    float& move_speed = climb_scale_;
    move_speed = static_cast<float>(move_speed + (static_cast<double>(wanted) - move_speed) *
                                                     (static_cast<double>(rate) * frame_length_));
    float speed_now = move_speed;
    float cutoff = get_param(kHangMoveCutoff);
    if (std::fabs(speed_now) > cutoff) {
        speed_ = std::fabs(move_speed);
        shimmy(move_speed * frame_length_);
        place_on_hang();
        return;
    }
    anim_state_ = kAnimHang;
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
    GrindPoint* node = hang_node_;
    GrindPoint* other = ladder_other_end(node);
    maths::Vec4 a, b;
    maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&a, node);
    maths::Vec4* other_pos = get_rails_of(this)->get_absolute_pos(&b, other);
    float dx = other_pos->x - node_pos->x;
    float dy = other_pos->y - node_pos->y;
    float dz = other_pos->z - node_pos->z;
    float up = get_param(kLadderBottomOffsetUp);
    double length =
        std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx);
    along_ = static_cast<float>(up / length);
    change_phase(3);
    tell_physics_node(this);
}

// 0x00441190
void objects::FootMovement::end_onto_ladder_top()
{
    GrindPoint* node = hang_node_;
    GrindPoint* other = ladder_other_end(node);
    maths::Vec4 a, b;
    maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&a, node);
    maths::Vec4* other_pos = get_rails_of(this)->get_absolute_pos(&b, other);
    double dx = static_cast<double>(other_pos->x) - node_pos->x;
    double dy = static_cast<double>(other_pos->y) - node_pos->y;
    double dz = static_cast<double>(other_pos->z) - node_pos->z;
    float length = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
    float up = get_param(kLadderTopOffsetUp);
    along_ = static_cast<float>((static_cast<double>(length) - up) / length);
    change_phase(3);
    tell_physics_node(this);
}

// 0x00441260
bool objects::FootMovement::check_ladder_top(const maths::Vec4* bottom, const maths::Vec4* top,
                                             const maths::Mat4* orientation, Record24* record)
{
    if (bottom->y > top->y)
        return false;
    maths::Vec4 up = {top->x - bottom->x, top->y - bottom->y, top->z - bottom->z, top->w - bottom->w};
    up.make_unit();
    if (up.y < 0.995f)
        return false;
    // ladder_top_offset_up below the top, ladder_climb_offset out of the ladder (x and y's products unrounded, as the
    // x87 code subtracts them)
    auto* ladder = reinterpret_cast<FoundLadder*>(record);
    float offset = get_param(kLadderTopOffsetUp);
    float oy = offset * up.y, oz = offset * up.z, ow = offset * up.w;
    ladder->point = {static_cast<float>(top->x - static_cast<double>(offset) * up.x), top->y - oy, top->z - oz,
                     top->w - ow};
    float climb = get_param(kLadderClimbOffset);
    const maths::Vec4& at = orientation->row[2];
    float cz = climb * at.z, cw = climb * at.w;
    ladder->point.x = static_cast<float>(ladder->point.x - static_cast<double>(climb) * at.x);
    ladder->point.y = static_cast<float>(ladder->point.y - static_cast<double>(climb) * at.y);
    ladder->point.z = ladder->point.z - cz;
    ladder->point.w = ladder->point.w - cw;
    ladder->facing = at;
    return true;
}

// 0x004413E0
bool objects::FootMovement::check_ladder_line(const maths::Vec4* bottom, const maths::Vec4* top,
                                              const maths::Mat4* orientation, const maths::Vec4* on_ladder, float along,
                                              Record24* record)
{
    // upright enough (its height as part of its length; no length: the height itself)
    float dy = top->y - bottom->y;
    double dz = static_cast<double>(top->z) - bottom->z;
    double dx = static_cast<double>(top->x) - bottom->x;
    double length = std::sqrt(dx * dx + static_cast<double>(dy) * dy + dz * dz);
    float up_y = dy;
    if (length > 0.0)
        up_y = static_cast<float>(dy * (1.0 / length));
    if (up_y < 0.995f)
        return false;
    // the line meets it between ladder_bottom_offset_up above the bottom and ladder_top_offset_up below the top
    if (static_cast<double>(up_y) * get_param(kLadderBottomOffsetUp) + bottom->y > on_ladder->y)
        return false;
    if (top->y - static_cast<double>(up_y) * get_param(kLadderTopOffsetUp) < on_ladder->y)
        return false;
    // the point ladder_climb_offset out of it there, if nothing is in the way
    auto* ladder = reinterpret_cast<FoundLadder*>(record);
    float climb = get_param(kLadderClimbOffset);
    const maths::Vec4& at = orientation->row[2];
    float cx = climb * at.x, cy = climb * at.y, cz = climb * at.z;
    ladder->point = {on_ladder->x - cx, on_ladder->y - cy, on_ladder->z - cz,
                     static_cast<float>(on_ladder->w - static_cast<double>(climb) * at.w)};
    render::ProbeRay feeler;
    feeler.make_new();
    feeler.line_.start = pos_;
    feeler.line_.end = ladder->point;
    if (feeler.get_hit_shape(true, false))
        return false;
    ladder->facing = at;
    ladder->along = along;
    set_physics_flag_a0(this);
    return true;
}

// 0x00441680
void objects::FootMovement::climb_off_bottom()
{
    // ladder_bottom_offset_forward back and ladder_bottom_offset_up down: the ground there, if any
    float forward = get_param(kLadderBottomOffsetForward);
    maths::Vec4& pos = pos_;
    maths::Vec4& facing = facing_;
    float fz = forward * facing.z, fw = forward * facing.w;
    maths::Vec4 target = {static_cast<float>(pos.x - static_cast<double>(forward) * facing.x),
                          static_cast<float>(pos.y - static_cast<double>(forward) * facing.y), pos.z - fz, pos.w - fw};
    target.y = target.y - get_param(kLadderBottomOffsetUp);
    render::ProbeRay* ground = &feeler_;
    bool found = find_stand_position(&target, &target, ground);
    feeler_hit_ = found;
    if (found)
        floor_normal_ = ground->surface_dir_;
    // the animated move there
    transition_start_ = pos;
    transition_pos_ = target;
    set_anim_done(this, member_code_address(&objects::FootMovement::end_ladder_off_bottom));
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0f;
    set_transition_facing(&facing, 1.0f);
    anim_waiting_ = 1;
    push_normal_ = facing;
    push_height_ = pos.y;
    change_phase(4);
    anim_state_ = kAnimLadderOffBottom;
}

// 0x004418B0
bool objects::FootMovement::check_ladder_bottom(const maths::Vec4* bottom, const maths::Vec4* top,
                                                const maths::Mat4* orientation, Record24* record)
{
    if (bottom->y > top->y)
        return false;
    maths::Vec4 up = {top->x - bottom->x, top->y - bottom->y, top->z - bottom->z, top->w - bottom->w};
    up.make_unit();
    if (up.y < 0.995f)
        return false;
    // faced (within max_onto_ladder_angle)
    float max_angle = get_param(kMaxOntoLadderAngle);
    const maths::Vec4& at = orientation->row[2];
    const maths::Vec4& facing = facing_;
    double dot = static_cast<double>(at.z) * facing.z + static_cast<double>(at.y) * facing.y +
                 static_cast<double>(at.x) * facing.x;
    if (dot < x87_cos(static_cast<double>(max_angle) * 0.017453292384743690))
        return false;
    // ladder_bottom_offset_up above the bottom, ladder_climb_offset out of the ladder
    auto* ladder = reinterpret_cast<FoundLadder*>(record);
    float offset = get_param(kLadderBottomOffsetUp);
    float ox = offset * up.x, oy = offset * up.y, oz = offset * up.z;
    ladder->point = {ox + bottom->x, oy + bottom->y, oz + bottom->z,
                     static_cast<float>(static_cast<double>(offset) * up.w + bottom->w)};
    float climb = get_param(kLadderClimbOffset);
    ladder->point.x = static_cast<float>(ladder->point.x - static_cast<double>(climb) * at.x);
    ladder->point.y = static_cast<float>(ladder->point.y - static_cast<double>(climb) * at.y);
    ladder->point.z = static_cast<float>(ladder->point.z - static_cast<double>(climb) * at.z);
    ladder->point.w = static_cast<float>(ladder->point.w - static_cast<double>(climb) * at.w);
    // nothing between the skater and 6 behind that point
    render::ProbeRay feeler;
    feeler.make_new();
    feeler.line_.start = pos_;
    float bx = at.x * 6.0f, by = at.y * 6.0f, bz = at.z * 6.0f;
    feeler.line_.end = {ladder->point.x - bx, ladder->point.y - by, ladder->point.z - bz,
                        static_cast<float>(ladder->point.w - static_cast<double>(at.w) * 6.0f)};
    if (feeler.get_hit_shape(true, false))
        return false;
    ladder->facing = at;
    return true;
}

// 0x00441B80
void objects::FootMovement::climb_off_top()
{
    // ladder_top_offset_forward ahead and ladder_top_offset_up up: the ground there - or into the air
    float forward = get_param(kLadderTopOffsetForward);
    maths::Vec4& pos = pos_;
    maths::Vec4& facing = facing_;
    float fx = forward * facing.x, fy = forward * facing.y, fz = forward * facing.z;
    maths::Vec4 target = {fx + pos.x, fy + pos.y, fz + pos.z,
                          static_cast<float>(static_cast<double>(forward) * facing.w + pos.w)};
    target.y = target.y + get_param(kLadderTopOffsetUp);
    render::ProbeRay ledge;
    ledge.make_new();
    bool found = find_stand_position(&target, &target, &ledge);
    transition_start_ = pos;
    transition_pos_ = target;
    if (found) {
        set_anim_done(this, member_code_address(&objects::FootMovement::end_ladder_off_top));
        assign_feeler(&feeler_, &ledge);
        feeler_hit_ = true;
        floor_normal_ = feeler_.surface_dir_;
    } else {
        set_anim_done(this, member_code_address(&objects::FootMovement::end_ladder_off_top_to_air));
    }
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0f;
    set_transition_facing(&facing, 1.0f);
    anim_waiting_ = 1;
    push_normal_ = facing;
    push_height_ = target.y;
    change_phase(4);
    anim_state_ = kAnimLadderOffTop;
}

// 0x00441DE0
bool objects::FootMovement::climb_onto_bottom(bool button)
{
    if (physics_of(this)->state_->flag(objects::PoseFlag::kBailing).on)
        return false;
    float radius = button ? get_param(kButtonHorizSnapDistance) : get_param(kMaxHorizSnapDistance);
    float height = get_param(kMaxVertSnapDistance);
    FoundLadder ladder;
    if (!get_level_rails()->find_ladder(&pos_, radius, height, true, this, reinterpret_cast<uint32_t>(&ladder),
                                        &hang_node_))
        return false;
    grabbed_ladder(this);
    transition_start_ = pos_;
    transition_pos_ = ladder.point;
    set_anim_done(this, member_code_address(&objects::FootMovement::end_onto_ladder_bottom));
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0f;
    set_transition_facing(&ladder.facing, 1.0f);
    anim_waiting_ = 1;
    push_normal_ = ladder.facing;
    push_height_ = ladder.point.y;
    change_phase(4);
    anim_state_ = kAnimLadderOntoBottom;
    return true;
}

// 0x00441FC0
bool objects::FootMovement::climb_onto_top()
{
    if (physics_of(this)->state_->flag(objects::PoseFlag::kBailing).on)
        return false;
    float height = get_param(kMaxVertSnapDistance);
    float radius = get_param(kMaxHorizSnapDistance);
    FoundLadder ladder;
    if (!get_level_rails()->find_ladder(&pos_, radius, height, false, this, reinterpret_cast<uint32_t>(&ladder),
                                        &hang_node_))
        return false;
    grabbed_ladder(this);
    transition_start_ = pos_;
    transition_pos_ = ladder.point;
    set_anim_done(this, member_code_address(&objects::FootMovement::end_onto_ladder_top));
    transition_start_offset_ = model_lift_;
    transition_display_offset_ = 0.0f;
    set_transition_facing(&ladder.facing, 1.0f);
    anim_waiting_ = 1;
    push_normal_ = ladder.facing;
    push_height_ = pos_.y;
    change_phase(4);
    anim_state_ = kAnimLadderOntoTop;
    return true;
}

// 0x00442170
bool objects::FootMovement::grab_ladder(const maths::Vec4* start, const maths::Vec4* end)
{
    FoundLadder ladder;
    if (!get_level_rails()->find_ladder_on_line(start, end, this, reinterpret_cast<Record24*>(&ladder), &hang_node_))
        return false;
    get_rails_of(this) = get_level_rails();
    let_go_of_ride(ride_);
    pos_ = ladder.point;
    facing_ = ladder.facing;
    along_ = ladder.along;
    up_speed_ = 0.0f;
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    change_phase(3);
    tell_physics_node(this);
    anim_state_ = kAnimLadder;
    return true;
}

// 0x00442280
void objects::FootMovement::climb_ladder()
{
    // the control's up / down part (its angle's cosine) at ladder_move_speed
    float speed = get_param(kLadderMoveSpeed);
    objects::PadInputModule* input = input_;
    double move_speed = x87_cos(input->pad_primary_.polar_[1].angle) * stick_deflection_ * static_cast<double>(speed);
    speed_ = static_cast<float>(move_speed);
    float move = static_cast<float>(move_speed * frame_length_);
    GrindPoint* node = hang_node_;
    GrindPoint* other = ladder_other_end(node);
    maths::Vec4 a, b;
    maths::Vec4* node_pos = get_rails_of(this)->get_absolute_pos(&a, node);
    maths::Vec4* other_pos = get_rails_of(this)->get_absolute_pos(&b, other);
    maths::Vec4 d = {other_pos->x - node_pos->x, other_pos->y - node_pos->y, other_pos->z - node_pos->z,
                     other_pos->w - node_pos->w};
    double length =
        std::sqrt(static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y + static_cast<double>(d.x) * d.x);
    double step = move / length;
    bool up = step > 0.0;
    float& along = along_;
    along = static_cast<float>(step + along);
    float length_f = static_cast<float>(length);
    // off the bottom or the top
    float bottom_up = get_param(kLadderBottomOffsetUp);
    double inverse = 1.0 / length_f;
    float inverse_f = static_cast<float>(inverse);
    if (inverse * bottom_up > along) {
        climb_off_bottom();
        return;
    }
    float top_up = get_param(kLadderTopOffsetUp);
    if ((static_cast<double>(length_f) - top_up) * inverse_f < along) {
        climb_off_top();
        return;
    }
    // else on it: the animation, the place along it (ladder_climb_offset out of it)
    float t = along;
    along = t > 1.0f ? 1.0f : t < 0.0f ? 0.0f : t;
    if (along == 0.0f || along == 1.0f) {
        anim_state_ = kAnimLadder;
        speed_ = 0.0f;
    } else {
        anim_state_ = up ? kAnimLadderMoveUp : kAnimLadderMoveDown;
    }
    float climb = get_param(kLadderClimbOffset);
    const maths::Vec4& facing = facing_;
    maths::Vec4 out = {climb * facing.x, climb * facing.y, climb * facing.z, climb * facing.w};
    maths::Vec4 part = {d.x * along, d.y * along, d.z * along, d.w * along};
    node_pos = get_rails_of(this)->get_absolute_pos(&a, hang_node_);
    double x = static_cast<double>(part.x) + node_pos->x;
    float y = part.y + node_pos->y;
    float z = part.z + node_pos->z;
    float w = part.w + node_pos->w;
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
    if (static_cast<uint32_t>(get_now()) - static_cast<uint32_t>(state_time_) < 25)
        trigger_rail_script(this, kOntoLadderEvents);
    jump_unsteered_ = false;
    // the control within ladder_control_tolerance of up or down: climbing
    objects::PadInputModule* input = input_;
    float upright = static_cast<float>(1.5707963705062866 - std::fabs(input->pad_primary_.polar_[1].angle));
    float tolerance = get_param(kLadderControlTolerance);
    if (x87_cos(static_cast<double>(tolerance) * 0.017453292384743690) < std::fabs(upright)) {
        climb_ladder();
        return;
    }
    anim_state_ = kAnimLadder;
    speed_ = 0.0f;
}

// 0x0043EB80
void walk::MovableContact::start_riding(objects::GameEntity* object)
{
    let_go_of_ride(reinterpret_cast<objects::MovingSurfaceModule*>(this));
    void* memory = memory::alloc(sizeof(objects::RideTracker));
    reinterpret_cast<objects::MovingSurfaceModule*>(this)->ride_ =
        memory ? static_cast<objects::RideTracker*>(memory)->make_new(object) : nullptr;
}

namespace objects {

// 0x0043D930
ObjectModule* __cdecl create_wait_and_die_component() { return new WaitAndDieComponent; }

} // namespace objects
