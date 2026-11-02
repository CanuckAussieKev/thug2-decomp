// NOT ORIGINAL - THAW's BMX ridden in THUG2, phase 4 (game/rider/thaw_bmx.h):
// with THUG2's vehicle mode gone ("on the bike" is the bike flag alone), THAW's own gates of what the bike can do -
// wallrides on square too, skitching on R2, no vert wallplants, no lip tricks, the bike's wallplant -, the tire tap,
// the manual's pose on the rider and the bike, the bike's sounds and the THAW-only members its bike scripts ask. Our
// addition at the user's request - not THUG2's code and not PartyMod's. Read from THAW's reference build
// (game/rider/thaw_bmx.h - no tracing); our THUG2 functions call these only while ThawBmx::live_now(core).

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/core/checksums.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/core_physics.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/manual_balance.h"
#include "game/rider/rider.h"
#include "game/rider/special_item.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "game/rider/balance_trick.h"
#include "engine/gfx/model.h"
#include "engine/objects/model_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace skater::thaw_bmx {

namespace {

// The timed flags (THUG2's; THAW's in the comment).
// THUG2's state flags against THAW's offsets: kVertAir 0x30, kVertWall 0x38, kBailing 0x78, kAcidDrop 0x80.
using objects::PoseFlag;
// The skater object's.
// The pad's buttons (offsets in THUG2's pad = THAW's).
constexpr uint32_t kR1 = 0xE0, kR2 = 0x100, kSquare = 0x160;
// THAW's stall state (core_fields::kBikeStall).
constexpr int kStateBikeStall = 6;
// Components.
constexpr uint32_t kSpecialItem = 0xF27A294D;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kModel = 0x286A8D26; // model

// THAW's constants.
constexpr float kRadiansPerDegree = 0.0174532924f;  // 0x006DB474
constexpr float kTapAbove = -30.0f;                 // 0x006EDDA0 (in vert: nothing this near below - the tap's room)
constexpr float kTapSearchStart = 100.0f;           // 0x006DCBC0 (in vert: the depths looked at, from this ...)
constexpr float kTapSearchStep = 3.0f;              // 0x006DC7F0 (... down by this ...)
constexpr float kTapSearchEnd = 0.01f;              // 0x006DB448 (... while above this)
constexpr float kTapDrop = 1000.0f;                 // 0x006DB3D0 (each feeler's length down)
constexpr float kTapAhead = 100.0f;                 // 0x006DCBC0 (out of vert the ledge looked for this far ahead)
constexpr float kTapMargin = 90.0f;                 // 0x42B40000 (the ledge search's margin)
constexpr float kTapPitch = 0.785398185f;           // 0x3F490FDB (45 degrees: up held the nose down, down the tail)
constexpr float kTapPitchTime = 0.5f;               // 0x006DB3CC (the bike turned over to it in this long)
constexpr float kTapReach = 5.0f;                   // 0x006DD10C (the wheel moved this far a frame towards the point)
constexpr float kTapSlowing = 50.0f;                // 0x006DD6C8 (the horizontal speed's loss a frame)
constexpr float kTapPull = 3.0f;                    // 0x006DC7F0 (let go: pulled towards where the point lies)
constexpr float kTapRiseFast = 100.0f;              // 0x006DCBC0
constexpr float kTapRiseFraction = 0.1f;            // 0x006DBD8C
constexpr float kTapRiseSlow = 15.0f;               // 0x006DD430
constexpr double kManualWobble = 0.01;              // 0x006ED758 (the manual angle to the wobble's fraction)
constexpr float kBalanceMiddle = 4096.0f;           // THAW 0x006E1BC4 (a balance's lean to the wobble's fraction:
constexpr float kBalanceScale = 0.0001220703125f;   // THAW 0x006EF068  (4096 +- lean) / 8192)
constexpr float kLedgeHeight = 12.0f;               // (THUG2's ledge search: a rail at most this far below in the air)
constexpr double kTapLedgeWeight = 0.001;           // THAW 0x006DBD98 (a ledge's distance against a surface's, in vert)
constexpr double kTapNearer = 0.001;                // THAW 0x006E1B90 (a double: "no further than the best so far")
constexpr float kTapFar = 10000.0f;                 // THAW 0x006E0834 (the nearest surface so far, to begin with)
constexpr uint32_t kHangMaxRailAscent = 0x98724EBC; // hang_max_rail_ascent (walk_parameters)
constexpr uint32_t kWalkParameters = 0x6775E538;    // walk_parameters

// The scripts' names (tools/pipeline/thaw_bmx/bike_scripts.ts, ride.ts).
constexpr const char* kWallplantTrick = "bike_Wallplant_Trick";
constexpr uint32_t kThugWallplantTrick = 0x5D1F84A7; // Wallplant_Trick
constexpr const char* kBikeObject = "ThawBmx";
constexpr auto kSoundTables = std::to_array<const char*>({
    "thaw_bmx_terrain_land", "thaw_bmx_terrain_grindland", "thaw_bmx_terrain_ollie", "thaw_bmx_terrain_grindjump",
    "thaw_bmx_terrain_bonk", "thaw_bmx_terrain_roll",      "thaw_bmx_terrain_grind",
});

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }
objects::GameEntity* object_of(const SkaterPhysicsModule* core) { return core->entity_; }
float get_frame_length(const SkaterPhysicsModule* core) { return core->frame_length(); }
int get_now() { return frame_clock::game_clock_elapsed() + objects::clock_offset(); }

void set_timed_flag(const SkaterPhysicsModule* core, PoseFlag flag, bool on)
{
    if ((core->state()->is_on(flag) != 0) == on)
        return;
    core->state()->set_flag(flag, on ? 1 : 0);
}

double dot3(const maths::Vec4& a, const maths::Vec4& b)
{
    return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}

// The level's rails (the skate module's rail manager: 0x007CE478's +0x24 - THAW's 0x008A3B5C's +0x24).
objects::GrindRailHub* get_level_rails()
{
    const modules::Skate* module = modules::Skate::sole();
    return module ? module->rail_manager().get() : nullptr;
}

// The tire tap's ledge: THAW 0x0058C850 on the level's rails (the rails its 0x0058C856.. walks, flags read as THAW's:
// a ladder never, switched on, with a next node; a rail that can't be climbed on is fine on the bike). Phase 7, read
// whole: it is THUG2's GrindRailHub::find_rail (0x0057DDA0) with that one bike exception,
// and its checker is the walk component's hang check (THAW 0x0060AC40 - THUG2's FootMovement::check_rail): in the
// air a rail not above the line and within 12 below it, not steeper than hang_max_rail_ascent, room to hang. Ours
// takes the first two (the hang's room is for a hanging skater, not a wheel) and the rail's own point - THAW's is the
// hang point check_rail makes, which its ledge state (7, not ported) then works from.
float rail_ascent_sine()
{
    float degrees = 0.0f;
    if (script::Struct* walk = script::read_sub_struct(kWalkParameters))
        walk->fetch_float(kHangMaxRailAscent, &degrees, 0);
    return static_cast<float>(std::sin(static_cast<double>(degrees) * kRadiansPerDegree));
}

bool find_ledge(const maths::Vec4& start, const maths::Vec4& end, const maths::Vec4& direction, maths::Vec4* point)
{
    objects::GrindRailHub* rails = get_level_rails();
    if (!rails)
        return false;
    bool found = false;
    float best = 1e14f;
    const float steepest = rail_ascent_sine();
    for (int index = 0; index < rails->num_; ++index) {
        const objects::GrindPoint* node = &core::at(rails->node_run(), index);
        if ((node->flag_bits_ & objects::kRailFlagLadder) || !(node->flag_bits_ & objects::kRailFlagActive) ||
            !node->succ_)
            continue;
        maths::Vec4 a, b, at;
        rails->get_absolute_pos(&a, node);
        rails->get_absolute_pos(&b, node->succ_);
        float score = 0.0f;
        if (!bike::ledge_point(start, end, direction, a, b, kTapMargin, &at, &score) || score > best)
            continue;
        maths::Vec4 along = b - a;
        along.make_unit();
        if (std::fabs(along.y) > steepest)
            continue;
        if (!objects::rail_clear(a, b))
            continue;
        best = score;
        *point = at;
        found = true;
    }
    return found;
}

// The bike (the skater's special item named ThawBmx), or null.
objects::GameEntity* bike_of(void* skater)
{
    auto* items =
        core::down_cast<objects::PickupItemHandler>(static_cast<objects::GameEntity*>(skater)->fetch_module(kSpecialItem));
    if (!items)
        return nullptr;
    const uint32_t id = name_hash(kBikeObject);
    for (objects::GameEntity* item : items->items_) {
        if (item && (item->ident_ == id || item->ident_ == id + static_cast<objects::Entity*>(skater)->ident_))
            return item;
    }
    return nullptr;
}

void set_wobble(objects::GameEntity* object, float fraction)
{
    if (auto* blender = core::down_cast<objects::AnimBlenderComponent>(object->fetch_module(kAnimBlender)))
        blender->first_channel_47e_280(fraction, 1);
}

} // namespace

objects::GameEntity* bike_object_of(void* skater) { return bike_of(skater); }

// --- the maths ---------------------------------------------------------------------------------------------------

namespace bike {

maths::Vec4 tire_tap_velocity(const maths::Vec4& velocity)
{
    maths::Vec4 v = velocity;
    const double flat = std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.z) * v.z);
    if (flat > 0.0) {
        const double keep = flat > kTapSlowing ? (flat - kTapSlowing) / flat : 0.0;
        v.x = static_cast<float>(v.x * keep);
        v.z = static_cast<float>(v.z * keep);
    }
    if (v.y > 0.0f)
        v.y = v.y > kTapRiseFast ? v.y - v.y * kTapRiseFraction : v.y - kTapRiseSlow;
    return v;
}

TapStep tire_tap_step(const maths::Vec4& position, const maths::Vec4& tap)
{
    TapStep step = {.position = position, .ends = false, .lands = false};
    const maths::Vec4 toward = {.x = tap.x - position.x, .y = 0.0f, .z = tap.z - position.z, .w = 0.0f};
    const double distance = std::sqrt(dot3(toward, toward));
    if (distance < kTapReach) {
        if (tap.y > position.y) {
            step.position = {.x = tap.x, .y = tap.y, .z = tap.z, .w = position.w};
            step.lands = true;
        } else {
            step.position.x = tap.x;
            step.position.z = tap.z;
        }
        return step;
    }
    if (tap.y > position.y) {
        step.ends = true;
        return step;
    }
    auto scale = static_cast<float>(kTapReach / distance);
    step.position.x += toward.x * scale;
    step.position.z += toward.z * scale;
    return step;
}

float manual_wobble(float manual_angle) { return static_cast<float>(manual_angle * kManualWobble); }

float balance_wobble(float lean, bool flip)
{
    return (flip ? kBalanceMiddle - lean : lean + kBalanceMiddle) * kBalanceScale;
}

bool display_pitch_beyond(const maths::Vec4& up, const maths::Vec4& normal, float degrees)
{
    return std::cos(static_cast<double>(degrees) * kRadiansPerDegree) > dot3(up, normal);
}

bool ledge_point(const maths::Vec4& start, const maths::Vec4& end, const maths::Vec4& direction, const maths::Vec4& a,
                 const maths::Vec4& b, float margin, maths::Vec4* point, float* score)
{
    const maths::Seg line(start, end);
    const maths::Seg rail(a, b);
    maths::Vec4 on_line, on_rail;
    float s = 0.0f, t = 0.0f;
    if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, s, t, true)) {
        // (along the line, or a rail of no length: the rail's point nearest the line's start)
        const maths::Vec4 along = b - a;
        const double length = dot3(along, along);
        double k = length > 0.0 ? dot3(start - a, along) / length : 0.0;
        k = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
        on_rail = a + static_cast<float>(k) * along;
        on_line = start;
    }
    if (on_rail.y > on_line.y || on_line.y - on_rail.y > kLedgeHeight)
        return false;
    maths::Vec4 apart = on_rail - on_line;
    if (dot3(apart, apart) > static_cast<double>(margin) * margin)
        return false;
    const double forward = dot3(apart, direction);
    if (forward > 0.0)
        apart -= static_cast<float>(forward * 0.6666667f) * direction;
    *score = static_cast<float>(dot3(apart, apart));
    *point = on_rail;
    return true;
}

} // namespace bike

// --- the gates ---------------------------------------------------------------------------------------------------

bool wallride_button(SkaterPhysicsModule* core) { return core->input()->pad_primary_.button_box().held_down_ != 0; }

bool wallplant_by_square(SkaterPhysicsModule* core) { return core->input()->pad_primary_.button_box().held_down_ != 0; }

uint32_t wallplant_trick_list()
{
    const uint32_t bike = name_hash(kWallplantTrick);
    return script::fetch_list(bike) ? bike : kThugWallplantTrick;
}

objects::ControlButton* tow_button(SkaterPhysicsModule* core) { return &core->input()->pad_primary_.shoulder_right_b(); }

// --- the tire tap ------------------------------------------------------------------------------------------------

bool tire_tapping(const SkaterPhysicsModule* core)
{
    const BikeState* state = state_of(core, false);
    return state && state->tire_tap_;
}

namespace {

// THAW 0x005DC290: in the air, R1 and up or down held: a point to tap the wheel on - in vert the surface below (not
// within 30 under the bike already; feelers 1000 down from points along the bike's down, from 100 by 3), else a ledge
// (a rail) on the way ahead (100 along the flattened velocity; THAW's ledge search through the walk component's hang
// check, here find_ledge). Found: the tire tap on, the bike turned 45 degrees over (up held: onto the front wheel),
// THAW's vert flags off. In vert, once asked, it goes on looking every frame without the buttons (THAW's +0x54C) -
// for the coping still to come; out of vert only while they're held.
void start_tire_tap(SkaterPhysicsModule* core, BikeState& state)
{
    objects::RiderPoseState* flags = core->state();
    if (flags->flag(PoseFlag::kAcidDrop).on) {
        state.tap_armed_ = false;
        return;
    }
    const bool not_vert = !flags->flag(PoseFlag::kVertAir).on;
    if (not_vert)
        state.tap_armed_ = false;
    const objects::PadSnapshot* pad = &core->input()->pad_primary_;
    const bool held = pad->shoulder_right_a().held_down_ && (state.stick_up_.down || state.stick_down_.down);
    if (!state.tap_armed_ && !held)
        return;
    const objects::GameEntity* object = object_of(core);
    auto* physics = static_cast<SkaterPhysicsModule*>(core);
    const maths::Vec4 position = object->at_point_;
    const maths::Vec4 up = core::at(object->basis_frame_.row, 1);
    bool found = false;
    float best = kTapFar;
    if (!not_vert) {
        core->probe_start() = position;
        core->probe_end() = position + kTapAbove * up;
        if (physics->probe_touches_zone(0x10, 0)) {
            state.tap_armed_ = true;
            return;
        }
        // (phase 7: THAW's search direction is the bike's down flattened - THAW 0x005DC290 zeroes its y - out
        // over the deck; phase 4 took the down itself)
        maths::Vec4 down = {.x = -up.x, .y = 0.0f, .z = -up.z, .w = -up.w};
        down.make_unit();
        // NOLINTNEXTLINE(bugprone-float-loop-counter): a float step, as intended
        for (float depth = kTapSearchStart; depth > kTapSearchEnd; depth -= kTapSearchStep) {
            const maths::Vec4 from = position + depth * down;
            const maths::Vec4 to = {.x = from.x, .y = from.y - kTapDrop, .z = from.z, .w = from.w};
            core->probe_start() = from;
            core->probe_end() = to;
            if (!physics->probe_touches_zone(0x10, 0) || (core->feeler().hit_.face_flags_ & 8))
                continue;
            const float distance = from.y - core->feeler().contact_pos_.y;
            if (distance > best + kTapNearer)
                continue;
            best = distance;
            state.tap_point_ = core->feeler().contact_pos_;
            state.tap_start_ = position;
            state.tap_ledge_ = false;
            found = true;
        }
    }
    maths::Vec4 direction = object->vel_;
    direction.y = 0.0f;
    direction.w = 0.0f;
    if (dot3(direction, direction) > 0.0)
        direction.make_unit();
    maths::Vec4 end = {.x = position.x, .y = position.y - kTapDrop, .z = position.z, .w = 1.0f};
    if (not_vert)
        end = position + kTapAhead * direction;
    // A ledge (phase 7, THAW 0x005DC290: looked for in vert too, and taken over a surface found below unless it's
    // a thousand times further away).
    maths::Vec4 ledge;
    if (find_ledge(position, end, direction, &ledge)) {
        const maths::Vec4 apart = ledge - position;
        const double distance = std::sqrt(dot3(apart, apart)) * kTapLedgeWeight;
        if (!found || distance <= static_cast<double>(best) + kTapNearer) {
            state.tap_point_ = ledge;
            state.tap_start_ = position;
            state.tap_ledge_ = true;
            found = true;
        }
    }
    if (!found) {
        state.tap_armed_ = true;
        return;
    }
    // On.
    state.tire_tap_ = true;
    state.tire_tap_nose_ = state.stick_up_.down;
    state.tap_pitch_ = 0.0f;
    set_timed_flag(core, PoseFlag::kVertAir, false);
    set_timed_flag(core, PoseFlag::kVertWall, false);
    set_timed_flag(core, PoseFlag::kLeftVert, false);
}

// THAW 0x005CE920: the tire tap going on - over when an acid drop starts; while R1 and up / down stay held (or the point
// is a ledge's) the bike slows and goes towards the point (reaching it above: put on it - a ledge's: landed from the
// tire tap), the point above it while still far off: over; let go, the bike is pulled the way the point lies. Rising, it
// slows (THAW's fixed amounts a frame).
void carry_tire_tap(SkaterPhysicsModule* core, BikeState& state)
{
    objects::RiderPoseState* flags = core->state();
    if (flags->flag(PoseFlag::kBailing).on) {
        state.tire_tap_ = false;
        state.tap_ledge_ = false;
        return;
    }
    state.tap_armed_ = false;
    objects::GameEntity* object = object_of(core);
    maths::Vec4& velocity = object->vel_;
    maths::Vec4& position = object->at_point_;
    const objects::PadSnapshot* pad = &core->input()->pad_primary_;
    const bool held = pad->shoulder_right_a().held_down_ && (state.stick_up_.down || state.stick_down_.down);
    if (held || state.tap_ledge_) {
        velocity = bike::tire_tap_velocity(velocity);
        const bike::TapStep step = bike::tire_tap_step(position, state.tap_point_);
        if (step.ends) {
            state.tire_tap_ = false;
            state.tap_ledge_ = false;
            return;
        }
        position = step.position;
        if (step.lands) {
            // (THAW: its ledge state 7 and Landed; ours: on the point, falling no more - THUG2's ground check lands
            // the bike on it next.)
            if (velocity.y > 0.0f)
                velocity.y = 0.0f;
            state.landed_from_tire_tap_ = state.tap_ledge_;
        }
    } else {
        maths::Vec4 pull = {.x = state.tap_point_.x - state.tap_start_.x, .y = 0.0f, .z = state.tap_point_.z - state.tap_start_.z,
                            .w = 0.0f};
        if (dot3(pull, pull) > 0.0)
            pull.make_unit();
        velocity.x += pull.x * kTapPull;
        velocity.z += pull.z * kTapPull;
        if (velocity.y > 0.0f)
            velocity.y =
                velocity.y > kTapRiseFast ? velocity.y - velocity.y * kTapRiseFraction : velocity.y - kTapRiseSlow;
    }
    // THAW 0x005DC9E9..: the bike turned over to 45 degrees in half a second (THAW slerps its display there; here the
    // skater's matrix and the display's, a step a frame).
    const float target = state.tire_tap_nose_ ? kTapPitch : -kTapPitch;
    const float remaining = target - state.tap_pitch_;
    if (remaining != 0.0f) {
        const float most = kTapPitch / kTapPitchTime * get_frame_length(core);
        const float step = std::fabs(remaining) < most ? remaining : (remaining > 0.0f ? most : -most);
        state.tap_pitch_ += step;
        object->basis_frame_.pitch_locally(step);
        core->display_matrix().pitch_locally(step);
    }
}

} // namespace

void tire_tap(SkaterPhysicsModule* core)
{
    BikeState& state = *state_of(core, true);
    state.landed_from_tire_tap_ = false;
    if (state.tire_tap_)
        carry_tire_tap(core, state);
    else
        start_tire_tap(core, state);
}

// --- the manual's pose -------------------------------------------------------------------------------------------

void manual_pose(SkaterPhysicsModule* core)
{
    auto* skater = static_cast<objects::GameEntity*>(object_of(core));
    objects::GameEntity* bike = bike_of(skater);
    const BikeState* state = state_of(core, false);
    if (!state)
        return;
    float fraction = 0.0f;
    if (state->manual) {
        fraction = bike::manual_wobble(state->manual_angle_);
    } else {
        // THAW 0x006207A8: else the running balance trick's balance (the grind's and the stall's range animations
        // among them) - THAW keeps one wobble fraction, which its animation trees play on the rider and the bike alike.
        // THUG2's balance (Balance::tick_frame) moves only the rider's animation: the bike stayed on its range
        // animation's first or last frame - in a stall (played from its end) beside the rail, twisted. The flip as
        // THUG2's balance takes it for the rider (THAW asks its own stance there), so the two agree.
        objects::RiderTeeterMoves* tricks = core->balance();
        const objects::Balance* balance = tricks ? tricks->at_hand() : nullptr; // (THAW 0x006207A8's)
        if (!balance)
            return;
        bool flip = balance->flip();
        if (balance->mirrored() && core->state()->flag(PoseFlag::kFlipped).on)
            flip = !flip;
        fraction = bike::balance_wobble(balance->lean(), flip);
    }
    set_wobble(skater, fraction);
    if (bike)
        set_wobble(bike, fraction);
}

// --- the shadow ----------------------------------------------------------------------------------------------------

namespace bike {

void* shadow_companion_of(const void* caster_model, const void* rider_model, void* bike_model, uint8_t bike_model_flags)
{
    if (!caster_model || caster_model != rider_model || !bike_model)
        return nullptr;
    return (bike_model_flags & 0x2) && !(bike_model_flags & 0x4) ? bike_model : nullptr;
}

} // namespace bike

void* shadow_companion(void* caster_model)
{
    // THAW's bike casts a volume shadow of its own (bike_details: Volume_BMX_Shadow.shd - THUG2 can't read it); here
    // the bike is drawn into the rider's detailed shadow, so the two are one shadow along the rider's light (phase 4's
    // detailed shadow of the bike's own was cast straight down, apart from his). [features] thaw_bmx_shadow=0: none.
    static const bool kShadow = config_int("features", "thaw_bmx_shadow", 1) != 0;
    if (!is_switched_on() || !kShadow || !modules::Skate::sole())
        return nullptr;
    auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
    if (!skater || !skater->core_physics() || !on_bike(skater->core_physics()))
        return nullptr;
    objects::ObjectModule* rider_model = skater->fetch_module(kModel);
    objects::GameEntity* bike = bike_of(skater);
    objects::ObjectModule* bike_model = bike ? bike->fetch_module(kModel) : nullptr;
    if (!rider_model || !bike_model)
        return nullptr;
    render::RenderModel* bike_renderer = core::down_cast<objects::MeshInstanceModule>(bike_model)->mesh_instance().get();
    return bike::shadow_companion_of(caster_model,
                                     core::down_cast<objects::MeshInstanceModule>(rider_model)->mesh_instance().get(),
                                     bike_renderer, bike_renderer ? bike_renderer->flag_bits() : 0);
}

// --- the flip ----------------------------------------------------------------------------------------------------

int play_flip(objects::GameEntity* object)
{
    if (!is_switched_on() || !object || !modules::Skate::sole())
        return -1;
    auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
    if (!skater || !skater->core_physics() || !on_bike(skater->core_physics()))
        return -1;
    if (object == skater) {
        // (THAW's Skater_Playanim: HandleFlipOrBoardRotateAfter before the branch - the component's own PlayAnim
        // answer does it too, after this or before: done once, the flags cleared)
        if (auto* flip_and_rotate = core::down_cast<objects::RiderSpinAndFlip>(skater->fetch_module(kSkaterFlipAndRotate)))
            flip_and_rotate->do_after_anim();
    } else if (object != bike_of(skater)) {
        return -1;
    }
    return skater->core_physics()->state()->is_on(PoseFlag::kFlipped) ? 1 : 0;
}

void flip_anims_to_flag(SkaterPhysicsModule* core)
{
    // (off the bike THUG2's way again: the rider's animations as his flag says - on the bike a Flip after the last
    // animation played left them as they were)
    auto* skater = static_cast<objects::GameEntity*>(object_of(core));
    if (auto* flip_and_rotate =
            skater ? core::down_cast<objects::RiderSpinAndFlip>(skater->fetch_module(kSkaterFlipAndRotate)) : nullptr)
        flip_and_rotate->apply_flip_to_anims();
}

// --- the sounds --------------------------------------------------------------------------------------------------

int bike_sound_of(const void* skater, BikeSound kind, void* info)
{
    if (!is_switched_on() || !skater)
        return -1;
    const SkaterPhysicsModule* core = static_cast<const objects::Rider*>(skater)->core_physics();
    if (!core || !on_bike(core))
        return -1;
    if (kind == BikeSound::kAir)
        return 0;
    script::Array* table = script::fetch_list(name_hash(core::at(kSoundTables, static_cast<int>(kind))));
    if (!table || table->extent_ == 0)
        return -1;
    auto* entry = table->struct_at(0);
    if (!surface_kind::get_sound_info(entry, static_cast<surface_kind::SoundInfo*>(info)))
        return 0;
    // (the first of each kind logged: the bike's sounds can be checked from the log)
    static std::array<bool, 8> s_logged = {};
    const int k = static_cast<int>(kind);
    if (!core::at(s_logged, k)) {
        core::at(s_logged, k) = true;
        get_log("thaw_bmx: bike sound %s -> %s", core::at(kSoundTables, k), static_cast<surface_kind::SoundInfo*>(info)->name.data());
    }
    return 1;
}

// --- the members -------------------------------------------------------------------------------------------------

int call_trick_member(SkaterPhysicsModule* core, uint32_t name, script::Struct* params, script::Script*)
{
    static const uint32_t kDisplayPitch = name_hash("DisplayPitchGreaterThan"),
                          kLandedFromTap = name_hash("LandedFromTireTap"), kInTireTap = name_hash("InTireTap"),
                          kPitchingForward = name_hash("PitchingForward"),
                          kPitchingBackward = name_hash("PitchingBackward"),
                          kShouldStall = name_hash("ShouldRunStallScript"),
                          kClearStall = name_hash("ClearStallScriptFlag"),
                          kExitStallBoost = name_hash("ExitStallBoost"), kOnStall = name_hash("OnStall");
    if (name == kDisplayPitch) {
        // 0x005CA490: the model's display up (the model component's matrix: THUG2's model draws
        // with the display matrix) against the GROUND normal (+0x68 - phase 4 took THAW's +0x68 for the display
        // normal; THAW's 0x005D0BD0 shows it's the ground's, as THUG2's).
        float degrees = 0.0f;
        if (params)
            params->fetch_float(static_cast<uint32_t>(0), &degrees, 0);
        return bike::display_pitch_beyond(core::at(core->display_matrix().row, 1), core->floor_normal(), degrees) ? 1 : 0;
    }
    if (name == kLandedFromTap) { // 0x005C7AF0
        const BikeState* state = state_of(core, false);
        return state && state->landed_from_tire_tap_ ? 1 : 0;
    }
    if (name == kInTireTap)
        return tire_tapping(core) ? 1 : 0;
    if (name == kPitchingForward || name == kPitchingBackward) {
        // THAW's matrix queries (THAW 0x005EBBC0 / 0x005EBBF0): the air pitch so far as the last frame in the air
        // began (+0x18C0) - above 0 forward (nose down), below 0 backward (nose up). bike_land2 starts the wheelie
        // from the air on it (with the stick held back / forward as it lands).
        const BikeState* state = state_of(core, false);
        const float pitch = state ? state->last_air_pitch_ : 0.0f;
        return (name == kPitchingForward ? pitch > 0.0f : pitch < 0.0f) ? 1 : 0;
    }
    if (name == kShouldStall) { // 0x005C8410: +0x49D
        const BikeState* state = state_of(core, false);
        return state && state->stall_script_ ? 1 : 0;
    }
    if (name == kClearStall) { // 0x005C8420: cleared only once in the stall state
        BikeState* state = state_of(core, false);
        if (state && core->state()->cur_phase_ == kStateBikeStall)
            state->stall_script_ = false;
        return 1;
    }
    if (name == kOnStall) // (its state component's: the physics state the stall's)
        return core->state()->cur_phase_ == kStateBikeStall ? 1 : 0;
    if (name == kExitStallBoost) {
        // 0x005C83D0: the velocity's x / z from THAW's +0x714 / +0x71C, which nothing of THAW's
        // writes that a search finds (the skater's stall boost) - left out; only BIKE_OLLIE's Boost asks, which no bike
        // script passes.
        return 1;
    }
    return 2;
}

} // namespace skater::thaw_bmx
