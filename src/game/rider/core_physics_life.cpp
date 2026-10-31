// The skater core physics component: taken down, set up and finalized, the physics state changed, and the small
// helpers next to them (0x004FC370-0x004FC778, 0x004FDD50-0x004FE1E2).

#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
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
constexpr uint32_t kFeelerObject = 0x140;       // (the feeler +0xA8's smart pointer)
constexpr uint32_t kGroundFeelerObject = 0x1F0; // (the feeler +0x158's)
constexpr uint32_t kSkitched = 0x364;

// The state component's (+0x1528) flags used here.

enum : int { kGround = 0, kAir = 1, kWallRide = 2, kLip = 3, kRail = 4 };

objects::RiderPoseState* state_of(SkaterPhysicsModule* component) { return component->state_; }
uint8_t* sound_of(SkaterPhysicsModule* component) { return reinterpret_cast<uint8_t*>(component->sound_); }
objects::MoveExecutor* trick_of(SkaterPhysicsModule* component) { return component->trick_; }
void release_pointer(SkaterPhysicsModule* component, uint32_t offset)
{
    get_at<objects::SharedRef<objects::SharedBase>>(component, offset).get_release();
}

} // namespace

// 0x004FC4A0 (cdecl): with the script value 0x19FB78FA set, "Tracking%d" and two vectors printed (compiled out).
void __cdecl tracking_print(int index, const maths::Vec4* a, const maths::Vec4* b)
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
    uint32_t button = script::get_symbol_value(kProjectileControl); // (the game's string)
    if (!trick_of(this)->check_release_press(button, false, true))
        return false;
    projectile_input_ = -1;
    return true;
}

// 0x004FC440
int SkaterPhysicsModule::is_sides_swapped()
{
    uint8_t flipped = state_of(this)->flag(PoseFlag::kFlipped).on;
    if (static_cast<objects::Rider*>(entity_)->goofy_)
        return flipped;
    return flipped ? 0 : 1;
}

// 0x004FC470
void SkaterPhysicsModule::set_surface_kind(uint32_t terrain)
{
    state_of(this)->set_surface_kind(static_cast<uint8_t>(terrain));
    sound_of(this)[0x1C] = static_cast<uint8_t>(terrain);
}

// 0x004FC500 (0x00500DB0 the deleting one)
SkaterPhysicsModule::~SkaterPhysicsModule()
{
    hit_cache_.destruct();
    release_pointer(this, kSkitched);
    release_pointer(this, kGroundFeelerObject);
    release_pointer(this, kFeelerObject);
}

// 0x004FC5C0
void SkaterPhysicsModule::configure_from_data(script::Struct*)
{
    rolling_friction_ = tuning_float_lookup(kCoastDrag, object_of(this));
    objects::GameEntity* object = object_of(this);
    object->at_point_ = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    maths::Mat4& matrix = entity_->basis_frame_;
    matrix.row[0] = maths::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = maths::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = maths::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    display_matrix_ = entity_->basis_frame_;
    drawn_normal_ = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    floor_normal_ = drawn_normal_;
    old_normal_ = drawn_normal_;
    point_rail_spin_ = 0;
    ammo_ = 0;
}

// 0x004FC690
void SkaterPhysicsModule::seal()
{
    find_module(entity_, input_, 0x27D7CD28);             // Input
    find_module(entity_, sound_, 0x187574FA);             // SkaterSound
    find_module(entity_, trigger_, 0xE594F0A2);           // Trigger
    find_module(entity_, trick_, 0x270F56E1);             // Trick
    find_module(entity_, rotate_, 0x52D61A0D);            // SkaterRotate
    find_module(entity_, score_, 0xD262802F);             // SkaterScore
    find_module(entity_, balance_, 0x589E5FB3);           // SkaterBalanceTrick
    find_module(entity_, state_, 0x43686585);             // SkaterState
    find_module(entity_, ride_, 0x408D2AA9);              // MovableContact
    find_module(entity_, physics_control_, 0xD9151D4D);   // SkaterPhysicsControl
    find_module(entity_, walk_, 0x726E85AA);              // Walk
    find_module(entity_, tagging_component_, 0x380D909A); // (the projectiles')
}

// 0x004FDD50
void SkaterPhysicsModule::leave_skating()
{
    if (vert_air_announced_) {
        vert_air_announced_ = 0;
        static_cast<objects::Entity*>(object_of(this))->announce(kGetOffEvent, nullptr, 0.0f);
    }
    rail_wait_ = ftol(script::fetch_float(kRailWaitName));
    if (static_cast<uint32_t>(lip_time_) > static_cast<uint32_t>(rail_time_))
        rail_time_ = static_cast<uint32_t>(lip_time_);
    objects::RiderPoseState* state = state_of(this);
    if (state->cur_phase_ == kAir && old_state_ == kLip && state->flag(PoseFlag::kLipToAir).on)
        set_timed_flag(this, PoseFlag::kLipToAir, 0);
    state_of(this)->change_phase(kGround);
    state_of(this)->drawn_normal_ = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    state_of(this)->floor_normal_ = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    state_of(this)->drop_velocity_ = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    state_of(this)->lip_straight_up_ = 0;
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
            sound_of(this)[0x1E] = 1;
        set_timed_flag(this, static_cast<PoseFlag>(flag), 0);
        if (flag == 7)
            sound_of(this)[0x1E] = 0;
    }
}

// 0x004FDED0
void SkaterPhysicsModule::set_physics_state(int state)
{
    if (state_of(this)->cur_phase_ != state) {
        state_changed_at_ = get_now();
        old_state_ = state_of(this)->cur_phase_;
    }
    int old = state_of(this)->cur_phase_;
    maths::Vec4& lip = lip_pos_;
    if (old == kLip && state != kLip && (lip.x != 0.0f || lip.y != 0.0f || lip.z != 0.0f)) {
        objects::GameEntity* object = object_of(this);
        object->at_point_ = lip;
        object->old_pos_ = object->at_point_;
    }
    if (state != kLip)
        lip = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (state != kGround)
        trick_of(this)->set_trick_objects(true);
    if (state_of(this)->cur_phase_ == kRail && state != kRail) {
        set_timed_flag(this, PoseFlag::kSlide, 0);
        sound_of(this)[0x1E] = 0;
    }
    old = state_of(this)->cur_phase_;
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
        trick_of(this)->spin_total_ = 0.0f;
        take_press(&input_->pad_primary_.shoulder_left_a_);
        take_press(&input_->pad_primary_.shoulder_right_a_);
        queued_spin_ = 0.0f;
    }
    state_of(this)->change_phase(state);
    get_at<int>(sound_of(this), 0x18) = state;
}

// 0x004FE120
void SkaterPhysicsModule::turn_around()
{
    objects::GameEntity* object = object_of(this);
    maths::Vec4& facing = object->basis_frame_.row[2];
    facing = maths::Vec4(-facing.x, -facing.y, -facing.z, -facing.w);
    maths::Vec4& right = entity_->basis_frame_.row[0];
    right = maths::Vec4(-right.x, -right.y, -right.z, -right.w);
    display_matrix_ = entity_->basis_frame_;
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
    double length_squared = (static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z;
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
    return !state->in_vehicle_ && state->physics_mode_ == 1;
}

} // namespace objects

using skater::SkaterPhysicsModule;
