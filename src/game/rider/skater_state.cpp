// The skater's state component (GameObjects::RiderPoseState) and three functions of its state history: see
// skater_state.h.

#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
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
#include "game/rider/state_history.h"
#include "engine/objects/control_pad.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {
std::array<uint32_t, 1> g_powerup_names{{0xD039432C}};
} // namespace
std::array<script::MemberFunctionEntry, 19> g_skater_state_member_functions{{
    {0x5EA287F2, 0x43686585}, {0x3527FC07, 0x43686585}, {0xA32C1A15, 0x43686585}, {0x5CB1FBD8, 0x43686585},
    {0xE9851E62, 0x43686585}, {0xA64DCF8B, 0x43686585}, {0x1AB2389B, 0x43686585}, {0x57BFBAE8, 0x43686585},
    {0xF2813EE5, 0x43686585}, {0xEE584CBC, 0x43686585}, {0xB32E9EAE, 0x43686585}, {0x953A7B6D, 0x43686585},
    {0x58AD903F, 0x43686585}, {0xB07AC662, 0x43686585}, {0x0E11B7CA, 0x43686585}, {0x44BA5FCE, 0x43686585},
    {0xE0645004, 0x43686585}, {0xEF7A8687, 0x43686585}, {0, 0x43686585}}};

namespace {

constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kOnSkaterStateChange = 0xA7662059, kOnSkaterPhysicsStateChange = 0x6BAC488C,
                   kOnSkaterTerrainChange = 0xD3FDE02A;
constexpr uint32_t kTerrain = 0x3789AC4E, kSparksPos = 0xFF5318DA;

// The powerups' names (0x0067F09C): one.
constexpr int kPowerups = 1;
const uint32_t* powerup_names() { return g_powerup_names.data(); }

// The powerup the unnamed checksum names (asserted), or -1.
int find_powerup(script::Struct* params)
{
    // (without one the original's variable keeps the params pointer: the stack slot it reuses)
    uint32_t name = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(uint32_t{0}, &name, 1);
    for (int i = 0; i < kPowerups; ++i) {
        if (powerup_names()[i] == name)
            return i;
    }
    return -1;
}

} // namespace

// 0x00519740
gsl::owner<ObjectModule*> RiderPoseState::fabricate() { return new RiderPoseState; }

// (the constructor: inlined in fabricate in the original)
RiderPoseState::RiderPoseState()
{
    kind_tag_ = kSkaterState;
    core::at(boost_items_, 0) = 0;
}

// 0x00519430
void RiderPoseState::publish_method_table()
{
    script::register_member_function_table(g_skater_state_member_functions);
}

// 0x00519450 (0x00519620 the deleting one)
RiderPoseState::~RiderPoseState() = default;

// (the core physics' timed flag setter, inlined everywhere in the original)
void RiderPoseState::set_flag(PoseFlag f, uint8_t value)
{
    TimedFlag& timed = flag(f);
    if (timed.on == value)
        return;
    timed.on = value;
    timed.changed_at = frame_clock::game_clock_elapsed() + g_clock_offset;
}

// 0x00519460
void RiderPoseState::configure_from_data(script::Struct* /*params*/) { cur_phase_ = kAir; }

// 0x00519660
void RiderPoseState::change_phase(int state)
{
    if (state == cur_phase_)
        return;
    cur_phase_ = state;
    entity_->post_own_event(kOnSkaterStateChange, nullptr);
}

// 0x00519680
void RiderPoseState::set_physics_mode(int mode)
{
    if (mode == physics_mode_)
        return;
    physics_mode_ = mode;
    entity_->post_own_event(kOnSkaterPhysicsStateChange, nullptr);
}

// 0x005196A0
void RiderPoseState::set_surface_kind(uint8_t terrain)
{
    if (terrain == surface_type_)
        return;
    surface_type_ = terrain;
    entity_->post_own_event(kOnSkaterTerrainChange, nullptr);
}

// 0x00519470
void RiderPoseState::clear_powerups() { core::at(boost_items_, 0) = 0; }

// 0x00519650
void RiderPoseState::clear_doing_trick() { in_trick_move_ = 0; }

// 0x00519480
bool RiderPoseState::in_air(script::Struct*, script::Script*) { return cur_phase_ == kAir; }
// 0x00519490
bool RiderPoseState::get_on_wall(script::Struct*, script::Script*) { return cur_phase_ == kWall; }
// 0x005194A0
bool RiderPoseState::on_lip(script::Struct*, script::Script*) { return cur_phase_ == kLip; }
// 0x005194B0
bool RiderPoseState::get_on_rail(script::Struct*, script::Script*) { return cur_phase_ == kRail; }
// 0x005194C0
bool RiderPoseState::in_wallplant(script::Struct*, script::Script*) { return cur_phase_ == kWallplant; }
// 0x005196D0
bool RiderPoseState::in_bail_state(script::Struct*, script::Script*) { return is_on(PoseFlag::kBailing) != 0; }
// 0x005194D0
bool RiderPoseState::on_foot(script::Struct*, script::Script*) { return !in_vehicle_ && physics_mode_ == kWalking; }
// 0x005194F0
bool RiderPoseState::skating(script::Struct*, script::Script*) { return !in_vehicle_ && physics_mode_ == kSkating; }
// 0x00519510
bool RiderPoseState::get_driving(script::Struct*, script::Script*) { return in_vehicle_ != 0; }
// 0x00519520
bool RiderPoseState::set_driving(script::Struct*, script::Script*)
{
    in_vehicle_ = 1;
    return true;
}
// 0x00519530
bool RiderPoseState::unset_driving(script::Struct*, script::Script*)
{
    in_vehicle_ = 0;
    return true;
}
// 0x005196E0
bool RiderPoseState::in_air_move(script::Struct*, script::Script*) { return in_trick_move_ != 0; }

// 0x00519540
bool RiderPoseState::has_powerup(script::Struct* params, script::Script*)
{
    const int index = find_powerup(params);
    return index != -1 && core::at(boost_items_, index) != 0;
}

// 0x005195B0
bool RiderPoseState::picked_up_powerup(script::Struct* params, script::Script*)
{
    const int index = find_powerup(params);
    if (index != -1)
        core::at(boost_items_, index) = 1;
    return true;
}

// 0x005196F0
bool RiderPoseState::get_surface_kind(script::Struct*, script::Script* script)
{
    script->args_->put_int(kTerrain, surface_type_);
    return true;
}

// 0x00519720
bool RiderPoseState::get_sparks_pos(script::Struct*, script::Script* script)
{
    script->args_->put_name_key(kSparksPos, sparks_pos_);
    return true;
}

// 0x00519600
bool RiderPoseState::set_sparks_pos(script::Struct* params, script::Script*)
{
    params->read_hash_value(uint32_t{0}, &sparks_pos_, 0);
    return true;
}

// 0x005197D0
uint32_t SkaterStateHistoryFrame::get_time() { return static_cast<uint32_t>(wraps) << 16 | time; }

// 0x005197E0
void SkaterStateHistoryFrame::change_moment(uint32_t stamp)
{
    time = static_cast<uint16_t>(stamp);
    wraps = static_cast<uint16_t>(stamp >> 16);
}

// 0x005197C0 (0x00519840 the deleting one)
RiderFrameLog::~RiderFrameLog() = default;

} // namespace objects

namespace objects {} // namespace objects
