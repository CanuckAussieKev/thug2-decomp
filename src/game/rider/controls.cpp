// The skater's control code, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/rider/controls.h"

#include <cstdint>
#include "engine/maths/vec4.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/trigger_component.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/sdl_input.h"
#include "game/rider/core_physics.h"
#include "game/rider/walk_physics.h"
#include "engine/sys/game_config.h"
#include "engine/objects/trigger_events.h"
#include "game/rider/physics_control.h"
#include "engine/objects/input_component.h"
namespace skater {

bool ps2_controls() { return sio::the_input_settings().ps2_controls_ != 0; }

namespace {
constexpr int kOsLayerPs2Style = 5;
constexpr uint32_t kXboxTrigger = 0xCE42EE1A; // Xbox_Trigger
constexpr uint32_t kTrigger = 0xE594F0A2; // Trigger
constexpr uint32_t kNGCTrigger = 0xCB0E600C; // NGC_Trigger
constexpr uint32_t kAcidDropJumpVelocity = 0xC0491D2E; // acid_drop_jump_velocity

int& os_layer() { return game_config::hardware(); }

constexpr uint32_t kAcidDropState = 0x2EDA83EA;

// The physics' acid drop search: fills `velocity` if there's somewhere to drop in.
bool find_acid_drop(objects::FootMovement* walk, maths::Vec4* velocity)
{
    return walk->core_physics()->find_acid_drop(false, &walk->pos(), &walk->old_pos(), velocity, &walk->acid_drop());
}

// The two buttons; PartyMod's PS2 controls skip the first (its NOP at 0x00527546 / 0x00527636).
bool buttons_held(objects::FootMovement* walk)
{
    if (walk->off_vert() || walk->physics_control()->board_missing())
        return false;
    const objects::PadSnapshot& pad = walk->input()->pad_primary();
    if (!ps2_controls() && !pad.shoulder_left_b().held_down())
        return false;
    return pad.shoulder_right_b().held_down() != 0;
}

// The velocity (x, y, z, w) and the up speed after it: the flat velocity and its y as the up speed.
void begin_drop_in(objects::FootMovement* walk, const maths::Vec4& velocity)
{
    maths::Vec4& v = walk->vel();
    v.x = velocity.x;
    v.w = velocity.w;
    v.y = velocity.y;
    v.z = velocity.z;
    v.y = 0.0F;
    walk->set_up_speed(velocity.y);
    walk->set_leave_for(2);
    walk->set_anim_state(kAcidDropState);
}

} // namespace

// 0x0051F4C0
script::Struct* platform_structure(script::Struct* params)
{
    script::Struct* found = nullptr;
    const int xbox = ps2_controls() ? 5 : 7; // (PartyMod's byte at 0x0051F4C8)
    if (os_layer() == xbox) {
        if (!params->read_sub_struct(kXboxTrigger, &found, 0))
            params->read_sub_struct(kTrigger, &found, 1);
        return found;
    }
    if (os_layer() == kOsLayerPs2Style) {
        if (!params->read_sub_struct(kNGCTrigger, &found, 0))
            params->read_sub_struct(kTrigger, &found, 1);
        return found;
    }
    params->read_sub_struct(kTrigger, &found, 1);
    return found;
}

// 0x00527510
bool try_walk_acid_drop(objects::FootMovement* walk)
{
    if (!buttons_held(walk))
        return false;
    maths::Vec4 velocity = {.x = walk->vel().x, .y = walk->up_speed(), .z = walk->vel().z, .w = 1.0F};
    if (!find_acid_drop(walk, &velocity))
        return false;
    walk->ride()->start_riding(&walk->get_feeler());
    begin_drop_in(walk, velocity);
    return true;
}

// 0x00527600
bool try_acid_drop(objects::FootMovement* walk)
{
    if (!buttons_held(walk))
        return false;
    maths::Vec4 velocity = walk->vel();
    // (a physics value, 0xC0491D2E, as the second component)
    velocity.y = objects::FootMovement::get_param(kAcidDropJumpVelocity);
    if (!find_acid_drop(walk, &velocity))
        return false;
    if (walk->feeler_hit()) {
        walk->trigger()->probe_touches_zone(kWalkEvents0064c63c, &walk->get_feeler());
        if (walk->physics_control()->flag26())
            return true;
    }
    walk->change_phase(1);
    begin_drop_in(walk, velocity);
    return true;
}

} // namespace skater
