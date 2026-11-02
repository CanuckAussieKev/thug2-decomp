// The skater core physics component: taken down, set up and finalized, the physics state changed, and the small
// helpers next to them (0x004FC370-0x004FC778, 0x004FDD50-0x004FE1E2).

#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "game/rider/trick_component.h"
#include "engine/objects/input_component.h"
#include "engine/objects/trigger_component.h"
#include "game/rider/balance_trick.h"
#include "game/rider/rotate_component.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_sound.h"
#include "game/rider/tagging.h"
#include "game/rider/walk_physics.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kCoastDrag = 0x78F80EC4;    // Physics_Rolling_Friction
constexpr uint32_t kGetOffEvent = 0x5E27200A;  // (broadcast when the skater gets off its board; name not recovered)
constexpr uint32_t kRailWaitName = 0xE4412EB7; // (a script value: the rail wait, ms; name not recovered)
constexpr uint32_t kTrackingName = 0x19FB78FA; // (a script value: the tracking printout on; name not recovered)
const char* const kProjectileControl = "projectile_control"; // "projectile_control"

// One of the components it finds in Finalize: the object's component of `type`, kept in `slot`.
template <typename Module>
void find_module(objects::GameEntity* object, Module*& slot, uint32_t type)
{
    slot = static_cast<Module*>(object->fetch_module(type));
}

// The component's fields used here.

// The state component's (+0x1528) flags used here.

constexpr int kGround = 0;
constexpr int kAir = 1;
constexpr int kWallRide = 2;
constexpr int kLip = 3;
constexpr int kRail = 4;


} // namespace

// 0x004FC4A0 (cdecl): with the script value 0x19FB78FA set, "Tracking%d" and two vectors printed (compiled out).
void tracking_print(int index, const maths::Vec4* a, const maths::Vec4* b)
{
    if (!script::fetch_int_value(kTrackingName))
        return;
    debug::printf("Tracking%d %.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", index, static_cast<double>(a->x),
                  static_cast<double>(a->y), static_cast<double>(a->z), static_cast<double>(b->x),
                  static_cast<double>(b->y), static_cast<double>(b->z));
}

// 0x004FC3C0
bool SkaterPhysicsModule::projectile_button_pressed()
{
    const uint32_t button = script::get_symbol_value(kProjectileControl); // (the game's string)
    if (!trick_->check_release_press(button, false, true))
        return false;
    projectile_input_ = -1;
    return true;
}

// 0x004FC440
int SkaterPhysicsModule::is_sides_swapped()
{
    const uint8_t flipped = state_->flag(PoseFlag::kFlipped).on;
    if (core::down_cast<objects::Rider>(entity())->goofy())
        return flipped;
    return flipped ? 0 : 1;
}

// 0x004FC470
void SkaterPhysicsModule::set_surface_kind(uint32_t terrain)
{
    state_->set_surface_kind(static_cast<uint8_t>(terrain));
    sound_->set_surface_type(static_cast<uint8_t>(terrain));
}

// 0x004FC500 (0x00500DB0 the deleting one)
// (the members' destructors, as the original's order: the cache unregistered, the skitch target, the floor
// feeler's and the feeler's objects let go)
SkaterPhysicsModule::~SkaterPhysicsModule() = default;

// 0x004FC5C0
void SkaterPhysicsModule::configure_from_data(script::Struct*)
{
    rolling_friction_ = tuning_float_lookup(kCoastDrag, object_of(this));
    objects::GameEntity* object = object_of(this);
    object->at_point() = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    maths::Mat4& matrix = entity()->basis_frame();
    core::at(matrix.row, 0) = maths::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    core::at(matrix.row, 1) = maths::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    core::at(matrix.row, 2) = maths::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    core::at(matrix.row, 3) = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    display_matrix_ = entity()->basis_frame();
    drawn_normal_ = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    floor_normal_ = drawn_normal_;
    old_normal_ = drawn_normal_;
    point_rail_spin_ = 0;
    ammo_ = 0;
}

// 0x004FC690
void SkaterPhysicsModule::seal()
{
    find_module(entity(), input_, 0x27D7CD28);             // Input
    find_module(entity(), sound_, 0x187574FA);             // SkaterSound
    find_module(entity(), trigger_, 0xE594F0A2);           // Trigger
    find_module(entity(), trick_, 0x270F56E1);             // Trick
    find_module(entity(), rotate_, 0x52D61A0D);            // SkaterRotate
    find_module(entity(), score_, 0xD262802F);             // SkaterScore
    find_module(entity(), balance_, 0x589E5FB3);           // SkaterBalanceTrick
    find_module(entity(), state_, 0x43686585);             // SkaterState
    find_module(entity(), ride_, 0x408D2AA9);              // MovableContact
    find_module(entity(), physics_control_, 0xD9151D4D);   // SkaterPhysicsControl
    find_module(entity(), walk_, 0x726E85AA);              // Walk
    find_module(entity(), tagging_component_, 0x380D909A); // (the projectiles')
}

// 0x004FDD50
void SkaterPhysicsModule::leave_skating()
{
    if (vert_air_announced_) {
        vert_air_announced_ = 0;
        static_cast<objects::Entity*>(object_of(this))->announce(kGetOffEvent, nullptr, 0.0f);
    }
    rail_wait_ = static_cast<int>(ftol(script::fetch_float(kRailWaitName)));
    if (static_cast<uint32_t>(lip_time_) > static_cast<uint32_t>(rail_time_))
        rail_time_ = lip_time_;
    objects::RiderPoseState* state = state_;
    if (state->cur_phase() == kAir && old_state_ == kLip && state->flag(PoseFlag::kLipToAir).on)
        set_timed_flag(this, PoseFlag::kLipToAir, 0);
    state_->change_phase(kGround);
    state_->set_drawn_normal(maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    state_->floor_normal() = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    state_->drop_velocity() = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    state_->set_lip_straight_up(0);
}

// 0x004FDE40
void SkaterPhysicsModule::clear_flags()
{
    // (Flags 23 down to 0 - the flags block's +0x20 + 8 each -, all but 1: flipped.)
    for (int flag = 23; flag >= 0; --flag) {
        if (flag == 1)
            continue;
        set_timed_flag(this, static_cast<PoseFlag>(flag), 1);
        if (flag == 7)
            sound_->set_sliding(1);
        set_timed_flag(this, static_cast<PoseFlag>(flag), 0);
        if (flag == 7)
            sound_->set_sliding(0);
    }
}

// 0x004FDED0
void SkaterPhysicsModule::set_physics_state(int state)
{
    if (state_->cur_phase() != state) {
        state_changed_at_ = get_now();
        old_state_ = state_->cur_phase();
    }
    int old = state_->cur_phase();
    maths::Vec4& lip = lip_pos_;
    if (old == kLip && state != kLip && (lip.x != 0.0f || lip.y != 0.0f || lip.z != 0.0f)) {
        objects::GameEntity* object = object_of(this);
        object->at_point() = lip;
        object->old_pos() = object->at_point();
    }
    if (state != kLip)
        lip = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (state != kGround)
        trick_->set_trick_objects(true);
    if (state_->cur_phase() == kRail && state != kRail) {
        set_timed_flag(this, PoseFlag::kSlide, 0);
        sound_->set_sliding(0);
    }
    old = state_->cur_phase();
    if (old == kAir) {
        if (state != kAir) {
            air_end_ = get_now();
            set_timed_flag(this, PoseFlag::kNoSecondSpin, 0);
            set_timed_flag(this, PoseFlag::kJumpedOffRail, 0);
            set_timed_flag(this, PoseFlag::kJumped, 0);
        }
    } else if (state == kAir) {
        if (old != kWallRide)
            air_start_ = get_now();
        trick_->spin_total() = 0.0f;
        take_press(&input_->pad_primary().shoulder_left_a());
        take_press(&input_->pad_primary().shoulder_right_a());
        queued_spin_ = 0.0f;
    }
    state_->change_phase(state);
    sound_->set_cur_phase(state);
}

// 0x004FE120
void SkaterPhysicsModule::turn_around()
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& facing = core::at(object->basis_frame().row, 2);
    facing = maths::Vec4(-facing.x, -facing.y, -facing.z, -facing.w);
    maths::Vec4& right = core::at(entity()->basis_frame().row, 0);
    right = maths::Vec4(-right.x, -right.y, -right.z, -right.w);
    display_matrix_ = entity()->basis_frame();
    rail_backwards_ = rail_backwards_ ? 0 : 1;
}

// 0x004FE1D0
void SkaterPhysicsModule::reset_to_air(void*)
{
    clear_flags();
    set_physics_state(kAir);
}

} // namespace skater

namespace maths {

// 0x004FC370
Vec4& Vec4::clamp_tiny_to_nothing(float length)
{
    const double length_squared =
        (static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z;
    if (length_squared < static_cast<double>(length) * length)
        *this = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    return *this;
}

} // namespace maths

namespace objects {

// 0x004FC400
int MovingSurfaceModule::time_since_stop() { return static_cast<int>(skater::physics::get_since(stop_time_)); }

// 0x004FC420
bool RiderBoardPhysics::is_on_foot()
{
    auto* state = pose_;
    return !state->in_vehicle() && state->physics_mode() == 1;
}

} // namespace objects

