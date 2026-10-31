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

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kCoastDrag = 0x78F80EC4;    // Physics_Rolling_Friction
constexpr uint32_t kGetOffEvent = 0x5E27200A;  // (broadcast when the skater gets off its board; name not recovered)
constexpr uint32_t kRailWaitName = 0xE4412EB7; // (a script value: the rail wait, ms; name not recovered)
constexpr uint32_t kTrackingName = 0x19FB78FA; // (a script value: the tracking printout on; name not recovered)
const char* const kProjectileControl = "projectile_control"; // "projectile_control"

// The components it finds in Finalize (each one's type, then where it's kept).
constexpr struct {
    uint32_t type;
    uint32_t offset;
} kComponents[] = {
    {0x27D7CD28, 0x150C}, // Input
    {0x187574FA, 0x1514}, // SkaterSound
    {0xE594F0A2, 0x1510}, // Trigger
    {0x270F56E1, 0x1518}, // Trick
    {0x52D61A0D, 0x151C}, // SkaterRotate
    {0xD262802F, 0x1520}, // SkaterScore
    {0x589E5FB3, 0x1524}, // SkaterBalanceTrick
    {0x43686585, 0x1528}, // SkaterState
    {0x408D2AA9, 0x152C}, // MovableContact
    {0xD9151D4D, 0x1530}, // SkaterPhysicsControl
    {0x726E85AA, 0x1534}, // Walk
    {0x380D909A, 0x1538}, // (the projectiles')
};

// The component's fields used here.
constexpr uint32_t kGroundNormalA = 0x58;
constexpr uint32_t kGroundNormalB = 0x68;
constexpr uint32_t kGroundNormalC = 0x78;
constexpr uint32_t kStateTime = 0x90;
constexpr uint32_t kOldState = 0x94;
constexpr uint32_t kFeelerObject = 0x140;       // (the feeler +0xA8's smart pointer)
constexpr uint32_t kGroundFeelerObject = 0x1F0; // (the feeler +0x158's)
constexpr uint32_t kRailLeft = 0x234;
constexpr uint32_t kRailLeftAt = 0x238;
constexpr uint32_t kRailWait = 0x23C;
constexpr uint32_t kLipPosition = 0x258;
constexpr uint32_t kUnknown2eC = 0x2EC;
constexpr uint32_t kProjectileCount = 0x2F8;
constexpr uint32_t kProjectileInput = 0x308;
constexpr uint32_t kSpinQueued = 0x30C;
constexpr uint32_t kRollingFrictionValue = 0x314;
constexpr uint32_t kFacingBackwards = 0x34C;
constexpr uint32_t kSkitched = 0x364;
constexpr uint32_t kAirStart = 0x380;
constexpr uint32_t kAirEnd = 0x384;
constexpr uint32_t kGetOffPending = 0x4F0;
constexpr uint32_t kContactMeshStore = 0x4F4;
constexpr uint32_t kSfxModuleSlot = 0x1514; // (+0x18 the state, +0x1C the terrain, +0x1E sliding)

// The state component's (+0x1528) flags used here.
constexpr uint32_t kFlagFlipped = 0x28;
constexpr uint32_t kFlagLipToAir = 0x50;
constexpr uint32_t kFlagSlide = 0x58;
constexpr uint32_t kFlagC0 = 0xC0;
constexpr uint32_t kFlagD0 = 0xD0;
constexpr uint32_t kFlagD8 = 0xD8;

enum : int { kGround = 0, kAir = 1, kWallRide = 2, kLip = 3, kRail = 4 };

objects::RiderPoseState* state_of(void* component) { return get_at<objects::RiderPoseState*>(component, kTimedFlags); }
uint8_t* sound_of(void* component) { return get_at<uint8_t*>(component, kSfxModuleSlot); }
objects::MoveExecutor* trick_of(void* component) { return get_at<objects::MoveExecutor*>(component, kSkaterComponent); }
void release_pointer(void* component, uint32_t offset)
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
    get_at<int>(this, kProjectileInput) = -1;
    return true;
}

// 0x004FC440
int SkaterPhysicsModule::is_sides_swapped()
{
    uint8_t flipped = get_at<uint8_t>(state_of(this), kFlagFlipped);
    if (get_at<uint8_t>(object_of(this), 0x110))
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
    get_at<render::HitMeshCache>(this, kContactMeshStore).destruct();
    release_pointer(this, kSkitched);
    release_pointer(this, kGroundFeelerObject);
    release_pointer(this, kFeelerObject);
}

// 0x004FC5C0
void SkaterPhysicsModule::configure_from_data(script::Struct*)
{
    get_at<float>(this, kRollingFrictionValue) = tuning_float_lookup(kCoastDrag, object_of(this));
    void* object = object_of(this);
    vector_at(object, kPosition) = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    maths::Mat4& matrix = get_at<maths::Mat4>(object_of(this), kMatrix);
    matrix.row[0] = maths::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = maths::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = maths::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    vector_at(this, kGroundNormalA) = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    vector_at(this, kGroundNormalB) = vector_at(this, kGroundNormalA);
    vector_at(this, kGroundNormalC) = vector_at(this, kGroundNormalA);
    get_at<uint8_t>(this, kUnknown2eC) = 0;
    get_at<int>(this, kProjectileCount) = 0;
}

// 0x004FC690
void SkaterPhysicsModule::seal()
{
    for (const auto& c : kComponents)
        get_at<void*>(this, c.offset) = static_cast<objects::GameEntity*>(object_of(this))->fetch_module(c.type);
}

// 0x004FDD50
void SkaterPhysicsModule::leave_skating()
{
    if (get_at<uint8_t>(this, kGetOffPending)) {
        get_at<uint8_t>(this, kGetOffPending) = 0;
        static_cast<objects::Entity*>(object_of(this))->announce(kGetOffEvent, nullptr, 0.0f);
    }
    get_at<uint32_t>(this, kRailWait) = ftol(script::fetch_float(kRailWaitName));
    if (get_at<uint32_t>(this, kRailLeftAt) > get_at<uint32_t>(this, kRailLeft))
        get_at<uint32_t>(this, kRailLeft) = get_at<uint32_t>(this, kRailLeftAt);
    uint8_t* state = reinterpret_cast<uint8_t*>(state_of(this));
    if (get_at<int>(state, kState) == kAir && get_at<int>(this, kOldState) == kLip && state[kFlagLipToAir])
        set_timed_flag(this, kFlagLipToAir, 0);
    state_of(this)->change_phase(kGround);
    vector_at(state_of(this), 0xF8) = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    vector_at(state_of(this), 0x108) = maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    vector_at(state_of(this), 0xE4) = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    get_at<uint8_t>(state_of(this), 0xF4) = 0;
}

// 0x004FDE40
void SkaterPhysicsModule::clear_flags()
{
    // (Flags 23 down to 0 - the flags block's +0x20 + 8 each -, all but 1: flipped.)
    for (int flag = 23; flag >= 0; --flag) {
        if (flag == 1)
            continue;
        uint32_t offset = 0x20 + 8 * static_cast<uint32_t>(flag);
        set_timed_flag(this, offset, 1);
        if (flag == 7)
            sound_of(this)[0x1E] = 1;
        set_timed_flag(this, offset, 0);
        if (flag == 7)
            sound_of(this)[0x1E] = 0;
    }
}

// 0x004FDED0
void SkaterPhysicsModule::set_physics_state(int state)
{
    if (state_of(this)->cur_phase_ != state) {
        get_at<int>(this, kStateTime) = get_now();
        get_at<int>(this, kOldState) = state_of(this)->cur_phase_;
    }
    int old = state_of(this)->cur_phase_;
    maths::Vec4& lip = vector_at(this, kLipPosition);
    if (old == kLip && state != kLip && (lip.x != 0.0f || lip.y != 0.0f || lip.z != 0.0f)) {
        void* object = object_of(this);
        vector_at(object, kPosition) = lip;
        vector_at(object, kOldPosition) = vector_at(object, kPosition);
    }
    if (state != kLip)
        lip = maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (state != kGround)
        trick_of(this)->set_trick_objects(true);
    if (state_of(this)->cur_phase_ == kRail && state != kRail) {
        set_timed_flag(this, kFlagSlide, 0);
        sound_of(this)[0x1E] = 0;
    }
    old = state_of(this)->cur_phase_;
    if (old == kAir) {
        if (state != kAir) {
            get_at<int>(this, kAirEnd) = get_now();
            set_timed_flag(this, kFlagC0, 0);
            set_timed_flag(this, kFlagD0, 0);
            set_timed_flag(this, kFlagD8, 0);
        }
    } else if (state == kAir) {
        if (old != kWallRide)
            get_at<int>(this, kAirStart) = get_now();
        get_at<int>(trick_of(this), 0x13A4) = 0;
        take_press(input_of(this) + 0xA0);
        take_press(input_of(this) + 0x100);
        get_at<int>(this, kSpinQueued) = 0;
    }
    state_of(this)->change_phase(state);
    get_at<int>(sound_of(this), 0x18) = state;
}

// 0x004FE120
void SkaterPhysicsModule::turn_around()
{
    void* object = object_of(this);
    maths::Vec4& facing = vector_at(object, kForward);
    facing = maths::Vec4(-facing.x, -facing.y, -facing.z, -facing.w);
    maths::Vec4& right = vector_at(object_of(this), kMatrix);
    right = maths::Vec4(-right.x, -right.y, -right.z, -right.w);
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
    get_at<uint8_t>(this, kFacingBackwards) = get_at<uint8_t>(this, kFacingBackwards) ? 0 : 1;
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
    auto* state = reinterpret_cast<RiderPoseState*>(cur_phase_);
    return !state->in_vehicle_ && state->physics_mode_ == 1;
}

} // namespace objects

using skater::SkaterPhysicsModule;
