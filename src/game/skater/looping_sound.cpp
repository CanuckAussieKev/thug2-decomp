// The skater's looping sound component (Obj::CSkaterLoopingSoundComponent): see looping_sound.h.

#include "game/skater/looping_sound.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/terrain.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BF38);
constexpr uint32_t kSkaterLoopingSound = 0xB5F1E9D2;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTurnOn = 0xEDE3935F, kTurnOff = 0x9731E193, kPlayAnim = 0x0B1E7291;
constexpr uint32_t kAnimTags = 0x5DB4115F, kWalkToSkateTransition = 0x910D77C1, kVolumeMult = 0xF1A99B27;
// The terrain sound tables
constexpr uint32_t kTableWheelroll = 0xDAFD1B8C, kTableGrind = 0x29A21517, kTableSlide = 0x81ECDCE5;

constexpr int kStateGround = 0, kStateAir = 1, kStateRail = 4;

constexpr float kAirPitchFloor = 45.0f;           // 0x42340000
constexpr float kAirPitchFall = 18.3333340f;      // 0x4192AAAB (a second)
constexpr float kPercent = 100.0f;                // 0x0064BF2C
constexpr float kChangeThreshold = 0.5f;          // 0x0064BF28 (volume or pitch: an update when it moved more)
constexpr float kCapsizedFraction = 0.5f;         // the core physics' +0x2EC

template <typename T>
T& Field(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

Sfx::CSfxManager* SfxManager() { return GameVariable<Sfx::CSfxManager*>(0x006F85A8); }

// The terrain sounds: a table by name, the terrain's entry in it copied into `entry` (false if it has none), the
// entry's volume for a percentage.
uint8_t SoundTable(uint32_t name) { return Terrain::GetTableIndex(name); }
bool TerrainSound(uint8_t* entry, int terrain, uint8_t table)
{
    return Terrain::GetTerrainSound(reinterpret_cast<Terrain::SoundInfo*>(entry), static_cast<uint8_t>(terrain), table,
                                    0.0f);
}
double SoundVolume(uint8_t* entry, float percent)
{
    return Terrain::GetScaledVolume(reinterpret_cast<const Terrain::SoundInfo*>(entry), percent, false);
}

uint32_t SoundChecksum(uint8_t* entry) { return Field<uint32_t>(entry, 0x68); }
float FullSpeedPitch(uint8_t* entry) { return Field<float>(entry, 0x6C); }
float RestPitch(uint8_t* entry) { return Field<float>(entry, 0x70); }

// The air sound's entry (0x0067F018).
const uint8_t* AirSound() { return reinterpret_cast<const uint8_t*>(0x0067F018); }

} // namespace

// 0x00513990
LoopingSoundVolume* LoopingSoundVolume::Assign(const Sfx::sVolume* other)
{
    std::memcpy(static_cast<Sfx::sVolume*>(this), other, sizeof(Sfx::sVolume));
    return this;
}

// 0x00513AD0
CBaseComponent* __cdecl CSkaterLoopingSoundComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterLoopingSoundComponent));
    return memory ? static_cast<CSkaterLoopingSoundComponent*>(memory)->Construct() : nullptr;
}

// 0x00513860
void __cdecl CSkaterLoopingSoundComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterLoopingSound),
                                        &GameVariable<uint32_t>(0x00684A68), 0x18);
}

// 0x005138E0
CSkaterLoopingSoundComponent* CSkaterLoopingSoundComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_lastVolume.Init();
    m_soundChanged = 1;
    m_state = kStateAir;
    m_on = 1;
    m_sliding = 0;
    m_noRailSound = 0;
    m_haveSound = 0;
    m_forceUpdate = 0;
    m_type = kSkaterLoopingSound;
    m_volumeMult = 1.0f;
    return this;
}

// 0x00513AB0
void CSkaterLoopingSoundComponent::StopSound()
{
    if (m_soundId) {
        SfxManager()->StopSound(m_soundId);
        m_soundId = 0;
    }
}

// 0x00513B30
void CSkaterLoopingSoundComponent::DestroyBody()
{
    m_vtable = kVtable;
    StopSound();
    // (m_lastVolume's destructor: nothing)
    CBaseComponent::DestroyBody();
}

// 0x00513FA0
CSkaterLoopingSoundComponent* CSkaterLoopingSoundComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00513880
void CSkaterLoopingSoundComponent::InitFromStructure(Script::CStruct* params)
{
    m_airPitchFall = kAirPitchFall;
    m_airPitchFloor = kAirPitchFloor;
    params->GetFloat(kVolumeMult, &m_volumeMult, 0);
}

// 0x005138B0
void CSkaterLoopingSoundComponent::SetSpeedFraction(float fraction)
{
    m_speedFraction = fraction;
    if (fraction > 1.0f)
        m_speedFraction = 1.0f;
}

// 0x00513970
void CSkaterLoopingSoundComponent::Finalize()
{
    m_physicsControl = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterPhysicsControl));
}

// 0x00513F60
void CSkaterLoopingSoundComponent::Suspend(bool suspend)
{
    CBaseComponent::Suspend(suspend);
    if (suspend)
        StopSound();
    else
        m_soundChanged = 1;
}

// 0x005139E0
int CSkaterLoopingSoundComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/,
                                                     Script::CScript* /*script*/)
{
    switch (name) {
    case kTurnOn:
        if (m_on != 1) {
            VirtualSuspend(false);
            m_on = 1;
        }
        return 1;
    case kTurnOff:
        if (m_on) {
            VirtualSuspend(true);
            m_on = 0;
        }
        return 1;
    case kPlayAnim: {
        // Turned on again by the walk-to-skate animation (its animTags in the skater's tags).
        Script::CStruct* tags = m_object->mp_tags;
        uint8_t* state = Field<uint8_t*>(m_physicsControl, 0x2C);
        Script::CStruct* animTags;
        if (tags && !Field<uint8_t>(state, 0x118) && !Field<int>(state, 0x1C) &&
            tags->GetStructure(kAnimTags, &animTags, 0) && animTags->ContainsFlag(kWalkToSkateTransition)) {
            VirtualSuspend(false);
            m_on = 1;
        }
        return 2;
    }
    default:
        return 2;
    }
}

// 0x00513BB0
void CSkaterLoopingSoundComponent::Update()
{
    if (!m_on) {
        VirtualSuspend(true);
        return;
    }
    LoopingSoundVolume volume;
    volume.Init();
    // Not original: THAW's BMX (game/skater/thaw_bmx.h; THAW 0x0046A1B7 / 0x0046A1F3) - on the bike its roll and grind
    // (BIKE_ROLL, BIKE_GRIND) on every terrain, nothing in the air or sliding; chosen every frame, and again from the
    // terrain once off the bike.
    int bike = -1;
    if (Skater::ThawBmx::Enabled()) {
        using Skater::ThawBmx::BikeSound;
        BikeSound kind = m_state == kStateGround ? BikeSound::Roll
                         : m_state == kStateRail && !m_sliding && !m_noRailSound ? BikeSound::Grind
                                                                                  : BikeSound::Air;
        bike = Skater::ThawBmx::BikeSoundOf(m_object, kind, m_sound);
        if (bike >= 0) {
            m_haveSound = static_cast<uint8_t>(bike);
            m_soundChanged = 1;
        }
    }
    // The sound for the state, chosen again when something changed.
    switch (bike >= 0 ? -1 : m_state) {
    case kStateGround:
        if (m_soundChanged) {
            m_haveSound = TerrainSound(m_sound, m_terrain, SoundTable(kTableWheelroll)) ? 1 : 0;
            m_soundChanged = 0;
        }
        break;
    case kStateAir:
        if (m_soundChanged) {
            m_haveSound = 1;
            std::memcpy(m_sound, AirSound(), sizeof(m_sound));
            m_soundChanged = 0;
        }
        break;
    case kStateRail:
        if (m_noRailSound) {
            m_haveSound = 0;
        } else {
            uint32_t table = m_sliding ? kTableSlide : kTableGrind;
            if (m_soundChanged) {
                m_haveSound = TerrainSound(m_sound, m_terrain, SoundTable(table)) ? 1 : 0;
                m_soundChanged = 0;
            }
        }
        break;
    default:
        break;
    }
    if (m_soundId && (!m_haveSound || m_playingChecksum != SoundChecksum(m_sound))) {
        SfxManager()->StopSound(m_soundId);
        m_soundId = 0;
    }
    if (!m_haveSound)
        return;

    // Its volume from where the skater is - stopped while the skater hasn't moved.
    Sfx::CSfxManager* sfx = SfxManager();
    if (m_object->m_pos != m_lastPos) {
        float dropoff = sfx->GetDropoffDistance(SoundChecksum(m_sound));
        sfx->GetVolume(&volume, &m_object->m_pos, dropoff, 0, nullptr, nullptr);
    } else {
        sfx->StopSound(m_soundId);
        m_soundId = 0;
    }
    m_lastPos = m_object->m_pos;

    float pitch;
    if (m_state == kStateAir && m_soundId) {
        // In the air the playing sound's pitch falls (to the floor, then it's silent).
        if (m_pitch > m_airPitchFloor) {
            m_pitch = static_cast<float>(m_pitch - static_cast<double>(Tmr::GetSlomoFrameSeconds()) * m_airPitchFall);
            pitch = m_pitch;
        } else {
            volume.SetSilent();
            pitch = m_pitch;
        }
    } else {
        // By the speed (half when the core physics' +0x2EC is set).
        auto* core = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
        if (core && Field<uint8_t>(core, 0x2EC))
            m_speedFraction = kCapsizedFraction;
        if (m_speedFraction > 0.0f) {
            float percent = static_cast<float>(static_cast<double>(m_volumeMult) * m_speedFraction * kPercent);
            volume.PercentageAdjustment(static_cast<float>(SoundVolume(m_sound, percent)));
            pitch = static_cast<float>((static_cast<double>(FullSpeedPitch(m_sound)) - RestPitch(m_sound)) *
                                           m_speedFraction +
                                       RestPitch(m_sound));
            m_pitch = pitch;
        } else {
            volume.SetSilent();
            pitch = 0.0f;
            m_pitch = 0.0f;
        }
    }

    if (volume.IsSilent()) {
        StopSound();
        return;
    }
    if (!m_soundId) {
        static_cast<LoopingSoundVolume*>(&m_lastVolume)->Assign(&volume);
        m_lastPitch = pitch;
        m_soundId = sfx->PlaySound(SoundChecksum(m_sound), &volume, pitch, 0, nullptr,
                                   static_cast<int>(reinterpret_cast<uintptr_t>(m_sound)));
        if (m_soundId)
            m_playingChecksum = SoundChecksum(m_sound);
        return;
    }
    // Playing: updated when the volume (its first channel) or the pitch moved by more than a half.
    if (!m_forceUpdate &&
        !(std::fabs(static_cast<double>(volume.m_channels[0]) - m_lastVolume.m_channels[0]) > kChangeThreshold) &&
        !(std::fabs(static_cast<double>(pitch) - m_lastPitch) > kChangeThreshold))
        return;
    static_cast<LoopingSoundVolume*>(&m_lastVolume)->Assign(&volume);
    m_lastPitch = pitch;
    m_forceUpdate = 0;
    sfx->SetSoundParamsScaled(m_soundId, &volume, pitch);
}

} // namespace Obj

REPLACE(0x00513AD0, Obj::CSkaterLoopingSoundComponent::Create);
REPLACE(0x00513860, Obj::CSkaterLoopingSoundComponent::RegisterMemberFunctions);
REPLACE(0x005138E0, &Obj::CSkaterLoopingSoundComponent::Construct);
REPLACE(0x00513B30, &Obj::CSkaterLoopingSoundComponent::DestroyBody);
REPLACE(0x00513FA0, &Obj::CSkaterLoopingSoundComponent::Destroy);
REPLACE(0x00513880, &Obj::CSkaterLoopingSoundComponent::InitFromStructure);
REPLACE(0x005138B0, &Obj::CSkaterLoopingSoundComponent::SetSpeedFraction);
REPLACE(0x00513970, &Obj::CSkaterLoopingSoundComponent::Finalize);
REPLACE(0x00513F60, &Obj::CSkaterLoopingSoundComponent::Suspend);
REPLACE(0x005139E0, &Obj::CSkaterLoopingSoundComponent::CallMemberFunction);
REPLACE(0x00513BB0, &Obj::CSkaterLoopingSoundComponent::Update);
REPLACE(0x00513AB0, &Obj::CSkaterLoopingSoundComponent::StopSound);
REPLACE(0x00513990, &Obj::LoopingSoundVolume::Assign);
// 0x00513BED is Update's body after its first test (jumped to from inside it; no callers) that Ghidra made a function
// of: its source is Update's.
REPLACE_SOURCE_ONLY(0x00513BED, &Obj::CSkaterLoopingSoundComponent::Update);
