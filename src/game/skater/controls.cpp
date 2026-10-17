// The skater's control code, with PartyMod's PS2-style controls built in.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/skater/controls.h"

#include <cstring>

#include "game/script/struct.h"
#include "game/sio/sdl_input.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

bool Ps2Controls() { return Sio::TheInputSettings().ps2Controls != 0; }

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

int& Platform() { return GameVariable<int>(0x007CEFA8); }

// The fields the acid drops use (offsets into the skater component).
constexpr uint32_t kVelocity = 0x34;    // four floats, then one more at +0x44
constexpr uint32_t kStateChecksum = 0x78;
constexpr uint32_t kDisabled = 0x9B;   // (a bool that stops it)
constexpr uint32_t kMode = 0x748;
constexpr uint32_t kInput = 0x1770;    // the pad: +0xC0 and +0x120 are held buttons
constexpr uint32_t kFlags = 0x1780;    // +0x24 / +0x26: bools
constexpr uint32_t kSound = 0x177C;
constexpr uint32_t kTrickComponent = 0x1784;
constexpr uint32_t kPhysics = 0x178C;
constexpr uint32_t kTrickName = 0x17B4;
constexpr uint32_t kWalking = 0x1864;
constexpr uint32_t kAcidDropState = 0x2EDA83EA;

// The physics' acid drop search (0x00502240): fills `velocity` if there's somewhere to drop in.
bool FindAcidDrop(void* skater, float* velocity)
{
    return GameFunction<bool(__thiscall*)(void*, int, void*, void*, float*, void*)>(0x00502240)(
        At<void*>(skater, kPhysics), 0, static_cast<uint8_t*>(skater) + 0x24, static_cast<uint8_t*>(skater) + 0x68,
        velocity, static_cast<uint8_t*>(skater) + 0x768);
}

// The two buttons; PartyMod's PS2 controls skip the first (its NOP at 0x00527546 / 0x00527636).
bool ButtonsHeld(void* skater)
{
    if (At<bool>(skater, kDisabled) || At<bool>(At<uint8_t*>(skater, kFlags), 0x24))
        return false;
    uint8_t* input = At<uint8_t*>(skater, kInput);
    if (!Ps2Controls() && !input[0xC0])
        return false;
    return input[0x120] != 0;
}

void EnterAcidDrop(void* skater, const float* velocity)
{
    float* v = &At<float>(skater, kVelocity);
    v[0] = velocity[0];
    v[3] = velocity[3];
    v[1] = velocity[1];
    v[2] = velocity[2];
    v[1] = 0.0f;
    std::memcpy(&At<float>(skater, kVelocity + 0x10), &velocity[1], 4);
    At<int>(skater, kMode) = 2;
    At<uint32_t>(skater, kStateChecksum) = kAcidDropState;
}

} // namespace

// 0x0051F4C0
Script::CStruct* Component::PlatformStructure(Script::CStruct* params)
{
    Script::CStruct* found = nullptr;
    int xbox = Ps2Controls() ? 5 : 7; // (PartyMod's byte at 0x0051F4C8)
    if (Platform() == xbox) {
        if (!params->GetStructure(0xCE42EE1A, &found, 0))
            params->GetStructure(0xE594F0A2, &found, 1);
        return found;
    }
    if (Platform() == 5) {
        if (!params->GetStructure(0xCB0E600C, &found, 0))
            params->GetStructure(0xE594F0A2, &found, 1);
        return found;
    }
    params->GetStructure(0xE594F0A2, &found, 1);
    return found;
}

// 0x00527510
bool Component::TryWalkAcidDrop()
{
    if (!ButtonsHeld(this))
        return false;
    float velocity[4] = {At<float>(this, kVelocity), At<float>(this, kVelocity + 0x10), At<float>(this, kVelocity + 8),
                         1.0f};
    if (!FindAcidDrop(this, velocity))
        return false;
    GameFunction<void(__thiscall*)(void*, void*)>(0x00427CD0)(At<void*>(this, kTrickComponent),
                                                              static_cast<uint8_t*>(static_cast<void*>(this)) + kTrickName);
    EnterAcidDrop(this, velocity);
    return true;
}

// 0x00527600
bool Component::TryAcidDrop()
{
    if (!ButtonsHeld(this))
        return false;
    float velocity[4] = {At<float>(this, kVelocity), At<float>(this, kVelocity + 4), At<float>(this, kVelocity + 8),
                         At<float>(this, kVelocity + 12)};
    // (a physics value, 0xC0491D2E, as the second component)
    velocity[1] = GameFunction<float(__cdecl*)(uint32_t)>(0x0043DA70)(0xC0491D2E);
    if (!FindAcidDrop(this, velocity))
        return false;
    if (At<bool>(this, kWalking)) {
        GameFunction<void(__thiscall*)(void*, const void*, void*)>(0x00434BF0)(
            At<void*>(this, kSound), reinterpret_cast<const void*>(0x0064C63C),
            static_cast<uint8_t*>(static_cast<void*>(this)) + kTrickName);
        if (At<bool>(At<uint8_t*>(this, kFlags), 0x26))
            return true;
    }
    GameFunction<void(__thiscall*)(void*, int)>(0x005260E0)(this, 1);
    EnterAcidDrop(this, velocity);
    return true;
}

} // namespace Skater

REPLACE(0x0051F4C0, &Skater::Component::PlatformStructure);
REPLACE(0x00527510, &Skater::Component::TryWalkAcidDrop);
REPLACE(0x00527600, &Skater::Component::TryAcidDrop);
