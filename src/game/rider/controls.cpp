// The skater's control code, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/rider/controls.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
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
#include "engine/core/checked_index.h"

namespace skater {

bool ps2_controls() { return sio::the_input_settings().ps2_controls_ != 0; }

namespace {

int& os_layer() { return game_config::g_platform; }

constexpr uint32_t kAcidDropState = 0x2EDA83EA;

// The physics' acid drop search: fills `velocity` if there's somewhere to drop in.
bool find_acid_drop(objects::FootMovement* walk, maths::Vec4* velocity)
{
    return walk->core_physics_->find_acid_drop(false, &walk->pos_, &walk->old_pos_, velocity, &walk->acid_drop_);
}

// The two buttons; PartyMod's PS2 controls skip the first (its NOP at 0x00527546 / 0x00527636).
bool buttons_held(objects::FootMovement* walk)
{
    if (walk->off_vert_ || walk->physics_control_->board_missing_)
        return false;
    const objects::PadSnapshot& pad = walk->input_->pad_primary_;
    if (!ps2_controls() && !pad.shoulder_left_b_.held_down_)
        return false;
    return pad.shoulder_right_b_.held_down_ != 0;
}

// The velocity (x, y, z, w) and the up speed after it: the flat velocity and its y as the up speed.
void begin_drop_in(objects::FootMovement* walk, const maths::Vec4& velocity)
{
    maths::Vec4& v = walk->vel_;
    v.x = velocity.x;
    v.w = velocity.w;
    v.y = velocity.y;
    v.z = velocity.z;
    v.y = 0.0f;
    walk->up_speed_ = velocity.y;
    walk->leave_for_ = 2;
    walk->anim_state_ = kAcidDropState;
}

} // namespace

// 0x0051F4C0
script::Struct* platform_structure(script::Struct* params)
{
    script::Struct* found = nullptr;
    const int xbox = ps2_controls() ? 5 : 7; // (PartyMod's byte at 0x0051F4C8)
    if (os_layer() == xbox) {
        if (!params->read_sub_struct(0xCE42EE1A, &found, 0))
            params->read_sub_struct(0xE594F0A2, &found, 1);
        return found;
    }
    if (os_layer() == 5) {
        if (!params->read_sub_struct(0xCB0E600C, &found, 0))
            params->read_sub_struct(0xE594F0A2, &found, 1);
        return found;
    }
    params->read_sub_struct(0xE594F0A2, &found, 1);
    return found;
}

// 0x00527510
bool try_walk_acid_drop(objects::FootMovement* walk)
{
    if (!buttons_held(walk))
        return false;
    maths::Vec4 velocity = {walk->vel_.x, walk->up_speed_, walk->vel_.z, 1.0f};
    if (!find_acid_drop(walk, &velocity))
        return false;
    walk->ride_->start_riding(&walk->feeler_);
    begin_drop_in(walk, velocity);
    return true;
}

// 0x00527600
bool try_acid_drop(objects::FootMovement* walk)
{
    if (!buttons_held(walk))
        return false;
    maths::Vec4 velocity = walk->vel_;
    // (a physics value, 0xC0491D2E, as the second component)
    velocity.y = objects::FootMovement::get_param(0xC0491D2E);
    if (!find_acid_drop(walk, &velocity))
        return false;
    if (walk->feeler_hit_) {
        walk->trigger_->probe_touches_zone(kWalkEvents0064c63c.data(), &walk->feeler_);
        if (walk->physics_control_->flag26_)
            return true;
    }
    walk->change_phase(1);
    begin_drop_in(walk, velocity);
    return true;
}

} // namespace skater
