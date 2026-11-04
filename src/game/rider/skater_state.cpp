// The skater's state component (GameObjects::RiderPoseState) and three functions of its state history: see
// skater_state.h.

#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/control_pad.h"
#include "engine/core/checked_index.h"

namespace objects {
namespace {
constexpr int kWrapShift = 16;                      // a frame's clock wraps are the high half
constexpr uint32_t kOnGround = 0x5EA287F2; // OnGround
constexpr uint32_t kSkaterStateName = 0x43686585; // SkaterState
constexpr uint32_t kInAir = 0x3527FC07; // InAir
constexpr uint32_t kOnWall = 0xA32C1A15; // OnWall
constexpr uint32_t kOnLip = 0x5CB1FBD8; // OnLip
constexpr uint32_t kOnRail = 0xE9851E62; // OnRail
constexpr uint32_t kInWallplant = 0xA64DCF8B; // InWallplant
constexpr uint32_t kInBailState = 0x1AB2389B; // InBailState (letter case unknown)
constexpr uint32_t kWalkingName = 0x57BFBAE8; // Walking
constexpr uint32_t kSkatingName = 0xF2813EE5; // Skating
constexpr uint32_t kDriving = 0xEE584CBC; // Driving
constexpr uint32_t kSetDriving = 0xB32E9EAE; // SetDriving
constexpr uint32_t kUnsetDriving = 0x953A7B6D; // UnsetDriving
constexpr uint32_t kDoingTrick = 0x58AD903F; // DoingTrick
constexpr uint32_t kHasPowerup = 0xB07AC662; // HasPowerup
constexpr uint32_t kPickeduppowerup = 0x0E11B7CA; // pickeduppowerup
constexpr uint32_t kGetTerrain = 0x44BA5FCE; // GetTerrain
constexpr uint32_t kGetSparksPos = 0xE0645004; // GetSparksPos
constexpr uint32_t kSetSparksPos = 0xEF7A8687; // SetSparksPos
} // namespace

std::array<script::MemberFunctionEntry, kSkaterStateMemberFunctionCount>& skater_state_member_functions()
{
    static std::array<script::MemberFunctionEntry, kSkaterStateMemberFunctionCount> skater_state_member_functions{{
        {.callsign_ = kOnGround, .owner_kind_ = kSkaterStateName}, {.callsign_ = kInAir, .owner_kind_ = kSkaterStateName}, {.callsign_ = kOnWall, .owner_kind_ = kSkaterStateName}, {.callsign_ = kOnLip, .owner_kind_ = kSkaterStateName},
        {.callsign_ = kOnRail, .owner_kind_ = kSkaterStateName}, {.callsign_ = kInWallplant, .owner_kind_ = kSkaterStateName}, {.callsign_ = kInBailState, .owner_kind_ = kSkaterStateName}, {.callsign_ = kWalkingName, .owner_kind_ = kSkaterStateName},
        {.callsign_ = kSkatingName, .owner_kind_ = kSkaterStateName}, {.callsign_ = kDriving, .owner_kind_ = kSkaterStateName}, {.callsign_ = kSetDriving, .owner_kind_ = kSkaterStateName}, {.callsign_ = kUnsetDriving, .owner_kind_ = kSkaterStateName},
        {.callsign_ = kDoingTrick, .owner_kind_ = kSkaterStateName}, {.callsign_ = kHasPowerup, .owner_kind_ = kSkaterStateName}, {.callsign_ = kPickeduppowerup, .owner_kind_ = kSkaterStateName}, {.callsign_ = kGetTerrain, .owner_kind_ = kSkaterStateName},
        {.callsign_ = kGetSparksPos, .owner_kind_ = kSkaterStateName}, {.callsign_ = kSetSparksPos, .owner_kind_ = kSkaterStateName}, {.callsign_ = 0, .owner_kind_ = kSkaterStateName}}};
    return skater_state_member_functions;
}

namespace {

constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kOnSkaterStateChange = 0xA7662059, kOnSkaterPhysicsStateChange = 0x6BAC488C,
                   kOnSkaterTerrainChange = 0xD3FDE02A;
constexpr uint32_t kTerrain = 0x3789AC4E, kSparksPos = 0xFF5318DA;

// The powerups' names (0x0067F09C): one.
constexpr int kPowerups = 1;
constexpr std::array<uint32_t, 1> kPowerupNames{{0xD039432C}};

// The powerup the unnamed checksum names (asserted), or -1.
int find_powerup(script::Struct* params)
{
    // (without one the original's variable keeps the params pointer: the stack slot it reuses)
    uint32_t name = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(uint32_t{0}, &name, 1);
    for (int i = 0; i < kPowerups; ++i) {
        if (core::at(kPowerupNames, i) == name) {
            return i;
        }
    }
    return -1;
}

} // namespace

// 0x00519740
gsl::owner<ObjectModule*> RiderPoseState::fabricate() { return new RiderPoseState; }

// (the constructor: inlined in fabricate in the original)
RiderPoseState::RiderPoseState()
{
    set_kind_tag(kSkaterState);
    core::at(boost_items_, 0) = 0;
}

// 0x00519430
void RiderPoseState::publish_method_table()
{
    script::register_member_function_table(skater_state_member_functions());
}

// 0x00519450 (0x00519620 the deleting one)
RiderPoseState::~RiderPoseState() = default;

// (the core physics' timed flag setter, inlined everywhere in the original)
void RiderPoseState::set_flag(PoseFlag f, uint8_t value)
{
    TimedFlag& timed = flag(f);
    if (timed.on == value) {
        return;
    }
    timed.on = value;
    timed.changed_at = frame_clock::game_clock_elapsed() + clock_offset();
}

// 0x00519460
void RiderPoseState::configure_from_data(script::Struct* /*params*/) { cur_phase_ = kAir; }

// 0x00519660
void RiderPoseState::change_phase(int state)
{
    if (state == cur_phase_) {
        return;
    }
    cur_phase_ = state;
    entity()->post_own_event(kOnSkaterStateChange, nullptr);
}

// 0x00519680
void RiderPoseState::set_physics_mode(PhysicsMode mode)
{
    if (mode == physics_mode_) {
        return;
    }
    physics_mode_ = mode;
    entity()->post_own_event(kOnSkaterPhysicsStateChange, nullptr);
}

// 0x005196A0
void RiderPoseState::set_surface_kind(uint8_t terrain)
{
    if (terrain == surface_type_) {
        return;
    }
    surface_type_ = terrain;
    entity()->post_own_event(kOnSkaterTerrainChange, nullptr);
}

// 0x00519470
void RiderPoseState::clear_powerups() { core::at(boost_items_, 0) = 0; }

// 0x00519650
void RiderPoseState::clear_doing_trick() { in_trick_move_ = 0; }

// 0x00519480
bool RiderPoseState::in_air(script::Struct* /*params*/, script::Script* /*script*/) const { return cur_phase_ == kAir; }
// 0x00519490
bool RiderPoseState::get_on_wall(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return cur_phase_ == kWall;
}
// 0x005194A0
bool RiderPoseState::on_lip(script::Struct* /*params*/, script::Script* /*script*/) const { return cur_phase_ == kLip; }
// 0x005194B0
bool RiderPoseState::get_on_rail(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return cur_phase_ == kRail;
}
// 0x005194C0
bool RiderPoseState::in_wallplant(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return cur_phase_ == kWallplant;
}
// 0x005196D0
bool RiderPoseState::in_bail_state(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return is_on(PoseFlag::kBailing) != 0;
}
// 0x005194D0
bool RiderPoseState::on_foot(script::Struct* /*params*/, script::Script* /*script*/)
{
    return (in_vehicle_ == 0U) && physics_mode_ == PhysicsMode::kWalking;
}
// 0x005194F0
bool RiderPoseState::skating(script::Struct* /*params*/, script::Script* /*script*/)
{
    return (in_vehicle_ == 0U) && physics_mode_ == PhysicsMode::kSkating;
}
// 0x00519510
bool RiderPoseState::get_driving(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return in_vehicle_ != 0;
}
// 0x00519520
bool RiderPoseState::set_driving(script::Struct* /*params*/, script::Script* /*script*/)
{
    in_vehicle_ = 1;
    return true;
}
// 0x00519530
bool RiderPoseState::unset_driving(script::Struct* /*params*/, script::Script* /*script*/)
{
    in_vehicle_ = 0;
    return true;
}
// 0x005196E0
bool RiderPoseState::in_air_move(script::Struct* /*params*/, script::Script* /*script*/) const
{
    return in_trick_move_ != 0;
}

// 0x00519540
bool RiderPoseState::has_powerup(script::Struct* params, script::Script* /*script*/)
{
    const int index = find_powerup(params);
    return index != -1 && core::at(boost_items_, index) != 0;
}

// 0x005195B0
bool RiderPoseState::picked_up_powerup(script::Struct* params, script::Script* /*script*/)
{
    const int index = find_powerup(params);
    if (index != -1) {
        core::at(boost_items_, index) = 1;
    }
    return true;
}

// 0x005196F0
bool RiderPoseState::get_surface_kind(script::Struct* /*params*/, script::Script* script) const
{
    script->args()->put_int(checksums::Name{kTerrain}, surface_type_);
    return true;
}

// 0x00519720
bool RiderPoseState::get_sparks_pos(script::Struct* /*params*/, script::Script* script) const
{
    script->args()->put_name_key(checksums::Name{kSparksPos}, sparks_pos_);
    return true;
}

// 0x00519600
bool RiderPoseState::set_sparks_pos(script::Struct* params, script::Script* /*script*/)
{
    params->read_hash_value(uint32_t{0}, &sparks_pos_, 0);
    return true;
}

// 0x005197D0
uint32_t get_time(SkaterStateHistoryFrame& frame)
{
    return static_cast<uint32_t>(frame.wraps) << kWrapShift | frame.time;
}

// 0x005197E0
void change_moment(SkaterStateHistoryFrame& frame, uint32_t stamp)
{
    frame.time = static_cast<uint16_t>(stamp);
    frame.wraps = static_cast<uint16_t>(stamp >> kWrapShift);
}

// 0x005197C0 (0x00519840 the deleting one)
RiderFrameLog::~RiderFrameLog() = default;

} // namespace objects

namespace objects {} // namespace objects
