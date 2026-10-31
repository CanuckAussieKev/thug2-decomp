// The skater's control code, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/rider/controls.h"

#include <cstdint>
#include <cstring>

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

namespace skater {

bool ps2_controls() { return sio::the_input_settings().ps2_controls_ != 0; }

namespace {

template <typename T>
T& get_at(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

int& os_layer() { return game_config::g_platform; }

// The fields the acid drops use (offsets into the skater component).
constexpr uint32_t kVelocity = 0x34;    // four floats, then one more at +0x44
constexpr uint32_t kStateKeyField = 0x78;
constexpr uint32_t kDisabled = 0x9B;   // (a bool that stops it)
constexpr uint32_t kMode = 0x748;
constexpr uint32_t kInput = 0x1770;    // the pad: +0xC0 and +0x120 are held buttons
constexpr uint32_t kFlags = 0x1780;    // +0x24 / +0x26: bools
constexpr uint32_t kTrigger = 0x177C;        // the trigger component
constexpr uint32_t kMovableContact = 0x1784; // the movable contact component
constexpr uint32_t kPhysics = 0x178C;        // the core physics component
constexpr uint32_t kTriggerFeeler = 0x17B4;  // (an render::ProbeRay)
constexpr uint32_t kWalking = 0x1864;
constexpr uint32_t kAcidDropState = 0x2EDA83EA;

// The physics' acid drop search: fills `velocity` if there's somewhere to drop in.
bool find_acid_drop(void* skater, float* velocity)
{
    return get_at<SkaterPhysicsModule*>(skater, kPhysics)->find_acid_drop(
        false, &get_at<maths::Vec4>(skater, 0x24), &get_at<maths::Vec4>(skater, 0x68), reinterpret_cast<maths::Vec4*>(velocity),
        &get_at<AcidDrop>(skater, 0x768));
}

// The two buttons; PartyMod's PS2 controls skip the first (its NOP at 0x00527546 / 0x00527636).
bool buttons_held(void* skater)
{
    if (get_at<bool>(skater, kDisabled) || get_at<bool>(get_at<uint8_t*>(skater, kFlags), 0x24))
        return false;
    uint8_t* input = get_at<uint8_t*>(skater, kInput);
    if (!ps2_controls() && !input[0xC0])
        return false;
    return input[0x120] != 0;
}

void begin_drop_in(void* skater, const float* velocity)
{
    float* v = &get_at<float>(skater, kVelocity);
    v[0] = velocity[0];
    v[3] = velocity[3];
    v[1] = velocity[1];
    v[2] = velocity[2];
    v[1] = 0.0f;
    std::memcpy(&get_at<float>(skater, kVelocity + 0x10), &velocity[1], 4);
    get_at<int>(skater, kMode) = 2;
    get_at<uint32_t>(skater, kStateKeyField) = kAcidDropState;
}

} // namespace

// 0x0051F4C0
script::Struct* Component::platform_structure(script::Struct* params)
{
    script::Struct* found = nullptr;
    int xbox = ps2_controls() ? 5 : 7; // (PartyMod's byte at 0x0051F4C8)
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
bool Component::try_walk_acid_drop()
{
    if (!buttons_held(this))
        return false;
    float velocity[4] = {get_at<float>(this, kVelocity), get_at<float>(this, kVelocity + 0x10), get_at<float>(this, kVelocity + 8),
                         1.0f};
    if (!find_acid_drop(this, velocity))
        return false;
    get_at<objects::MovingSurfaceModule*>(this, kMovableContact)->start_riding(&get_at<render::ProbeRay>(this, kTriggerFeeler));
    begin_drop_in(this, velocity);
    return true;
}

// 0x00527600
bool Component::try_acid_drop()
{
    if (!buttons_held(this))
        return false;
    float velocity[4] = {get_at<float>(this, kVelocity), get_at<float>(this, kVelocity + 4), get_at<float>(this, kVelocity + 8),
                         get_at<float>(this, kVelocity + 12)};
    // (a physics value, 0xC0491D2E, as the second component)
    velocity[1] = objects::FootMovement::get_param(0xC0491D2E);
    if (!find_acid_drop(this, velocity))
        return false;
    if (get_at<bool>(this, kWalking)) {
        get_at<objects::ActivationZoneModule*>(this, kTrigger)->probe_touches_zone(kWalkEvents0064c63c,
                                                                 &get_at<render::ProbeRay>(this, kTriggerFeeler));
        if (get_at<bool>(get_at<uint8_t*>(this, kFlags), 0x26))
            return true;
    }
    reinterpret_cast<objects::FootMovement*>(this)->change_phase(1);
    begin_drop_in(this, velocity);
    return true;
}

} // namespace Skater
