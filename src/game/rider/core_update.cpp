// The skater's core physics component: the frame's update (the state's own update and what runs around it).

#include <cstdint>
#include <unordered_map>

#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include "game/rider/collision_events.h"
#include "game/rider/core_fields.h"
#include "game/rider/core_physics.h"
#include "game/rider/thaw_bmx.h"
#include "engine/sys/stopwatch.h"
#include "engine/sys/tick_rate.h"
#include "engine/render/probe_ray.h"
#include "platform/run_log.h"

namespace skater {

using namespace core_fields;

namespace {

constexpr uint32_t kEnteredAir = 0xF225FE69; // the events sent when +0x4F0 changes (names unknown)
constexpr uint32_t kLeftAir = 0x5E27200A;

// Not original (the 60 Hz grid - see core_physics.h): what the skater keeps for a tick rate other than 60, beside the
// component (its layout is the original's).
struct GridState {
    maths::Vec4 frame_start; // the last position as the current 1/60 s frame began (do_air_physics)
    double ahead = 0.0;      // seconds of the coming ticks already covered by an update that took a whole frame
    bool placed = false;     // put somewhere new since the last update (note_placed)
    bool whole = false;      // this update takes a whole frame (take_whole_frame)
    maths::Vec4 move_vel;    // the velocity the frame's first update moved the skater with (frame_move_velocity)
    bool move_valid = false; // move_vel is this frame's and the last update ended on the ground
    maths::Vec4 ground_from; // where the frame's ground move began (grid_ground_move)
    maths::Vec4 ground_step; // the frame's ground move so far
    uint64_t ground_frame = 0;
    bool ground_frame_valid = false;
};
GridState& grid_state(const SkaterPhysicsModule* skater)
{
    static std::unordered_map<const SkaterPhysicsModule*, GridState> states;
    return states[skater];
}

// Not original: does this update begin with what ends the 60 Hz run's update of a frame early - a jump let go
// (jump_released's test, after tick_frame's crouch) on the ground or a rail? The 60 Hz run's update then moves the whole
// frame first (on the ground) or not at all (on a rail), and the next frame begins after it.
bool jump_lets_go(const SkaterPhysicsModule* skater, const objects::PadInputModule* input)
{
    const int state = state_of(skater);
    return (state == kGround || state == kRail) && bit_mark(skater, PoseFlag::kCrouched) != 0U &&
           input->pad_primary().pos_x().held_down() == 0U;
}

} // namespace

maths::Vec4& SkaterPhysicsModule::grid_frame_start() { return grid_state(this).frame_start; }

void SkaterPhysicsModule::note_placed()
{
    if (!tick_rate::original()) {
        grid_state(this).placed = true;
        grid_state(this).ahead = 0.0;
    }
}

// Not original: a tick the skater's last update already covered (take_whole_frame) - none at 60. A tick that reaches past
// that time takes what is left of it.
bool SkaterPhysicsModule::held_by_grid()
{
    if (tick_rate::original()) {
        return false;
    }
    GridState& grid = grid_state(this);
    if (grid.ahead <= 0.0) {
        return false;
    }
    const double tick = frame_.length;
    if (grid.ahead >= tick * (1.0 - 1e-6)) {
        grid.ahead = std::max(0.0, grid.ahead - tick);
        return true;
    }
    frame_.length = static_cast<float>(tick - grid.ahead);
    grid.ahead = 0.0;
    return false;
}

void SkaterPhysicsModule::hold_rest_of_frame()
{
    if (tick_rate::original() || frame_clock::grid_frames() == 0) {
        return;
    }
    GridState& grid = grid_state(this);
    const double rest = frame_clock::get_slomo_grid_seconds() - frame_.length;
    grid.ahead = std::max(grid.ahead, rest);
}

// Not original: the 60 Hz run's update of a frame where it ends early, at another tick rate (docs/design/tick-rate.md
// "Events inside a frame"): an update after a placement, or one beginning a frame with a jump let go, takes the whole
// 1/60 s, as the 60 Hz run's does, and the ticks it covered are held (held_by_grid). Nothing at 60.
void SkaterPhysicsModule::take_whole_frame()
{
    if (tick_rate::original()) {
        return;
    }
    GridState& grid = grid_state(this);
    if (grid.ahead > 0.0) {
        return;
    }
    const bool placed = grid.placed;
    grid.placed = false;
    if (placed || (frame_clock::grid_frames() != 0 && jump_lets_go(this, input_))) {
        const double tick = frame_.length;
        const double frame = frame_clock::get_slomo_grid_seconds();
        frame_.length = static_cast<float>(frame);
        grid.ahead = std::max(0.0, frame - tick);
        grid.whole = true;
    }
}

bool SkaterPhysicsModule::frame_move_velocity(maths::Vec4* velocity)
{
    if (tick_rate::original()) {
        return false;
    }
    GridState& grid = grid_state(this);
    if (forces_this_tick()) {
        grid.move_vel = *velocity; // (the frame's first update: its own velocity, noted)
        grid.move_valid = true;
        return false;
    }
    if (!grid.move_valid || !(entity()->vel() == grid.move_vel)) {
        grid.move_valid = false; // (something else changed the velocity since: the tick's own)
        return false;
    }
    *velocity = grid.move_vel;
    return true;
}

bool SkaterPhysicsModule::grid_ground_move(maths::Vec4* step)
{
    if (tick_rate::original()) {
        return true;
    }
    GridState& grid = grid_state(this);
    const uint64_t frame = frame_clock::grid_frame_of_tick();
    if (get_tracker(this) || bit_mark(this, PoseFlag::kSkitching) != 0U) {
        grid.ground_frame_valid = false; // (riding something: tick by tick)
        return true;
    }
    if (forces_this_tick()) {
        grid.ground_frame = frame;
        grid.ground_frame_valid = true;
        grid.ground_from = entity()->at_point();
        grid.ground_step = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    }
    if (!grid.ground_frame_valid || grid.ground_frame != frame) {
        return true;
    }
    grid.ground_step.x += step->x;
    grid.ground_step.y += step->y;
    grid.ground_step.z += step->z;
    grid.ground_step.w += step->w;
    if (!frame_ends_this_tick()) {
        maths::Vec4& position = entity()->at_point();
        position.x += step->x;
        position.y += step->y;
        position.z += step->z;
        position.w += step->w;
        return false;
    }
    grid.ground_frame_valid = false;
    entity()->at_point() = grid.ground_from;
    entity()->old_pos() = grid.ground_from;
    *step = grid.ground_step;
    return true;
}

bool SkaterPhysicsModule::frame_ends_this_tick() const
{
    return tick_rate::original() || frame_clock::grid_frame_ends() || grid_state(this).whole;
}

bool SkaterPhysicsModule::forces_this_tick() const
{
    return tick_rate::original() || frame_clock::grid_frames() != 0 || grid_state(this).whole;
}

float SkaterPhysicsModule::force_step() const
{
    return tick_rate::original() ? frame_.length : static_cast<float>(frame_clock::get_slomo_grid_seconds());
}

// 0x00510830
void SkaterPhysicsModule::tick_frame()
{
    frame_.length = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    {
        static const bool kTrace = config_int("debug", "tick_trace", 0) != 0;
        if (kTrace) {
            const maths::Vec4& p = entity()->at_point();
            const maths::Vec4& v = entity()->vel();
            get_log("TRACE %llu frame %llu gf %d t %u p %.4f %.4f %.4f v %.4f %.4f %.4f s %d",
                    static_cast<unsigned long long>(frame_clock::vblanks_since_reset()),
                    static_cast<unsigned long long>(frame_clock::grid_frame_of_tick()), frame_clock::grid_frames(),
                    frame_clock::get_time(), static_cast<double>(p.x), static_cast<double>(p.y),
                    static_cast<double>(p.z), static_cast<double>(v.x), static_cast<double>(v.y),
                    static_cast<double>(v.z), static_cast<int>(state_of(this)));
        }
    }
    if (!tick_rate::original() && frame_clock::grid_frames() != 0) {
        grid_frame_start() = entity()->old_pos(); // (not original: the 60 Hz grid - do_air_physics)
    }
    if (!tick_rate::original()) {
        grid_state(this).whole = false;
    }
    if (held_by_grid()) {
        return; // (not original: a tick the last update covered)
    }
    collision_events::checkpoint(this, "update-start"); // (not original)
    const bool bike = thaw_bmx::live_now(this);         // (not original: THAW's bike, thaw_bmx_physics.cpp)
    if (bike) {
        thaw_bmx::begin_frame(this);
    }
    frame_flags_.landed = 0;
    frame_flags_.began_in_lip = static_cast<uint8_t>(state_of(this) == kLip);
    frame_flags_.began_in_drop = bit_mark(this, PoseFlag::kAcidDrop);
    if ((bit_mark(this, PoseFlag::kCrouched) == 0U) && (input_->pad_primary().pos_x().held_down() != 0U)) {
        set_flag(this, PoseFlag::kCrouched, 1);
    }
    take_whole_frame(); // (not original: the 60 Hz grid)
    if (forces_this_tick()) {
        // (not original: at another tick rate the speed limits, their friction and timers once a 1/60 s frame over the
        // whole of it - forces_this_tick; at 60 every tick)
        const float tick_length = frame_.length;
        frame_.length = force_step();
        clamp_skater_speed();
        frame_.length = tick_length;
    }
    set_flag(this, PoseFlag::kGroundSnapped, 0);
    set_flag(this, PoseFlag::kWallplanted, 0);
    refresh_hit_cache();
    update_model_rotation();

    switch (state_of(this)) {
    case kGround:
        last_ground_.pos = entity()->at_point();
        do_ground_physics();
        try_start_skitch();
        break;
    case kAir:
        do_air_physics();
        if (state_of(this) == kGround) {
            frame_flags_.landed = 1;
        }
        break;
    case kWallRide:
        step_wall_ride();
        break;
    case kLip:
        step_coping_move();
        break;
    case kRail:
        step_grind_move();
        break;
    case kWallplant:
        step_wallplant();
        break;
    case kBikeStall: // (not original: THAW's stall state - only THAW's bike gets into it, thaw_bmx.h)
        if (thaw_bmx::is_switched_on()) {
            do_bike_stall_physics();
        }
        break;
    default:
        break;
    }

    collision_events::checkpoint(this, "state-physics"); // (not original)
    update_speed_override();
    collision_events::checkpoint(this, "after-4FF200"); // (not original)
    if (frame_ends_this_tick()) {
        try_grind(false); // (inlined in the original; not original: at another tick rate after the frame's last move)
    }
    collision_events::checkpoint(this, "try-grind");    // (not original)
    friction_.update_free_revert();
    collision_events::checkpoint(this, "free-revert"); // (not original)
    update_focus_button();
    update_projectiles();
    update_special_time();
    collision_events::checkpoint(this, "update-rest"); // (not original)

    const auto in_air = static_cast<uint8_t>(state_of(this) == kAir && (bit_mark(this, PoseFlag::kVertAir) != 0U) &&
                                             (bit_mark(this, PoseFlag::kAcidDrop) == 0U));
    uint8_t& air_from_jump = vert_air_announced_;
    if (air_from_jump != in_air) {
        air_from_jump = static_cast<uint8_t>(air_from_jump == 0U);
        static_cast<objects::Entity*>(object_of(this))
            ->announce((air_from_jump != 0U) ? kEnteredAir : kLeftAir, nullptr, 0.0F);
    }
    if (state_of(this) != kAir) {
        late_wallplant_.frames = 0;
    }
    if (bike && thaw_bmx::live_now(this)) {
        thaw_bmx::end_frame(this); // (not original: THAW 0x005C9210, the x-up let go)
    }
    if (!tick_rate::original()) {
        GridState& grid = grid_state(this); // (not original: frame_move_velocity)
        grid.move_valid = grid.move_valid && state_of(this) == kGround;
    }
    render::set_current_cache(nullptr);
    collision_events::end_frame(this); // (not original: [debug] collision_events)
}

} // namespace skater
