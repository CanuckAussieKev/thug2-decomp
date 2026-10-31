// The skater's state component (GameObjects::RiderPoseState) and three functions of its state history: see
// skater_state.h.

#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/state_history.h"

namespace objects {

uint32_t g_powerup_names[1]{0xD039432C};
uint32_t g_skater_state_member_functions[114]{
    0x5EA287F2, 0x43686585, 0, 0, 0, 0, 0x3527FC07, 0x43686585, 0, 0, 0, 0, 0xA32C1A15, 0x43686585, 0, 0, 0, 0,
    0x5CB1FBD8, 0x43686585, 0, 0, 0, 0, 0xE9851E62, 0x43686585, 0, 0, 0, 0, 0xA64DCF8B, 0x43686585, 0, 0, 0, 0,
    0x1AB2389B, 0x43686585, 0, 0, 0, 0, 0x57BFBAE8, 0x43686585, 0, 0, 0, 0, 0xF2813EE5, 0x43686585, 0, 0, 0, 0,
    0xEE584CBC, 0x43686585, 0, 0, 0, 0, 0xB32E9EAE, 0x43686585, 0, 0, 0, 0, 0x953A7B6D, 0x43686585, 0, 0, 0, 0,
    0x58AD903F, 0x43686585, 0, 0, 0, 0, 0xB07AC662, 0x43686585, 0, 0, 0, 0, 0x0E11B7CA, 0x43686585, 0, 0, 0, 0,
    0x44BA5FCE, 0x43686585, 0, 0, 0, 0, 0xE0645004, 0x43686585, 0, 0, 0, 0, 0xEF7A8687, 0x43686585, 0, 0, 0, 0,
    0,          0x43686585, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kOnSkaterStateChange = 0xA7662059, kOnSkaterPhysicsStateChange = 0x6BAC488C,
                   kOnSkaterTerrainChange = 0xD3FDE02A;
constexpr uint32_t kTerrain = 0x3789AC4E, kSparksPos = 0xFF5318DA;

// The powerups' names (0x0067F09C): one.
constexpr int kPowerups = 1;
const uint32_t* powerup_names() { return g_powerup_names; }

// The powerup the unnamed checksum names (asserted), or -1.
int find_powerup(script::Struct* params)
{
    // (without one the original's variable keeps the params pointer: the stack slot it reuses)
    auto name = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
    params->read_hash_value(uint32_t{0}, &name, 1);
    for (int i = 0; i < kPowerups; ++i) {
        if (powerup_names()[i] == name)
            return i;
    }
    return -1;
}

} // namespace

// 0x00519740
ObjectModule* __cdecl RiderPoseState::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(RiderPoseState));
    if (!memory)
        return nullptr;
    auto* component = static_cast<RiderPoseState*>(memory);
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    component->kind_tag_ = kSkaterState;
    component->boost_items_[0] = 0;
    return component;
}

// 0x00519430
void __cdecl RiderPoseState::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kSkaterState), g_skater_state_member_functions,
                                           0x18);
}

// 0x00519450
void RiderPoseState::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x00519620
RiderPoseState* RiderPoseState::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
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
void RiderPoseState::clear_powerups() { boost_items_[0] = 0; }

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
bool RiderPoseState::in_bail_state(script::Struct*, script::Script*) { return bailing_ != 0; }
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
    int index = find_powerup(params);
    return index != -1 && boost_items_[index] != 0;
}

// 0x005195B0
bool RiderPoseState::picked_up_powerup(script::Struct* params, script::Script*)
{
    int index = find_powerup(params);
    if (index != -1)
        boost_items_[index] = 1;
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
uint32_t SkaterStateHistoryFrame::get_time()
{
    auto* self = reinterpret_cast<uint8_t*>(this);
    return static_cast<uint32_t>(*reinterpret_cast<uint16_t*>(self + 0x7A)) << 16 |
           *reinterpret_cast<uint16_t*>(self + 0x78);
}

// 0x005197E0
void SkaterStateHistoryFrame::change_moment(uint32_t time)
{
    auto* self = reinterpret_cast<uint8_t*>(this);
    *reinterpret_cast<uint16_t*>(self + 0x78) = static_cast<uint16_t>(time);
    *reinterpret_cast<uint16_t*>(self + 0x7A) = static_cast<uint16_t>(time >> 16);
}

// 0x005197C0
void RiderFrameLog::destroy_body()
{
    vtable_ = kStateHistoryVtable;
    ObjectModule::destroy_body();
}

} // namespace objects

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::RiderPoseState::teardown,
                                      &objects::ComponentWithoutUpdate::tick_frame,
                                      &objects::RiderPoseState::configure_from_data,
                                      stubs::do_nothing,
                                      stubs::do_nothing,
                                      objects::reapply_from_data,
                                      stubs::ignore_arguments2,
                                      &objects::ObjectModule::hold_off,
                                      stubs::ignore_argument,
                                      stubs::do_nothing,
                                      stubs::ignore_argument,
                                      &objects::ObjectModule::dispatch_script_method,
                                      &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace objects

namespace objects {

static VtableSlot g_state_history_vtable_slots[] = {
    &objects::RiderFrameLog::teardown,
    &objects::ComponentWithoutUpdate::tick_frame,
    stubs::ignore_argument,
    stubs::do_nothing,
    stubs::do_nothing,
    objects::reapply_from_data,
    stubs::ignore_arguments2,
    &objects::ObjectModule::hold_off,
    stubs::ignore_argument,
    stubs::do_nothing,
    stubs::ignore_argument,
    static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::ObjectModule::invoke_method_by_name),
    &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kStateHistoryVtable = reinterpret_cast<void**>(g_state_history_vtable_slots);

} // namespace objects
