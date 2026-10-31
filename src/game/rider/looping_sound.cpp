// The skater's looping sound component (GameObjects::CRiderRollingAudio): see looping_sound.h.

#include "game/rider/looping_sound.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/rider/thaw_bmx.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

uint32_t g_loopingSoundMemberFunctions[6]{0, 0xB5F1E9D2, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
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
T& Prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

SoundFx::CAudioMixer* AudioMixerHandle() { return SoundFx::g_audioMixer; }

// The terrain sounds: a table by name, the terrain's entry in it copied into `entry` (false if it has none), the
// entry's volume for a percentage.
uint8_t SoundTable(uint32_t name) { return SurfaceKind::GetTableIndex(name); }
bool TerrainSound(uint8_t* entry, int terrain, uint8_t table)
{
    return SurfaceKind::GetTerrainSound(reinterpret_cast<SurfaceKind::SoundInfo*>(entry), static_cast<uint8_t>(terrain), table,
                                    0.0f);
}
double SoundVolume(uint8_t* entry, float percent)
{
    return SurfaceKind::GetScaledVolume(reinterpret_cast<const SurfaceKind::SoundInfo*>(entry), percent, false);
}

uint32_t SoundNameKey(uint8_t* entry) { return Prop<uint32_t>(entry, 0x68); }
float FullSpeedPitch(uint8_t* entry) { return Prop<float>(entry, 0x6C); }
float RestPitch(uint8_t* entry) { return Prop<float>(entry, 0x70); }

// The air sound's entry (a terrain sound entry of its own: 0x84 bytes, its name, the sound's checksum, the pitches at
// full speed and at rest).
struct AirSoundEntry {
    char name[0x68];
    uint32_t checksum;
    float fullSpeedPitch;
    float restPitch;
    uint8_t unused[0x10];
};
static_assert(sizeof(AirSoundEntry) == 0x84, "a terrain sound entry");
AirSoundEntry g_airSoundEntry{"wheels01", 0x24257432, 100.0f, 110.0f};
const uint8_t* AirSound() { return reinterpret_cast<const uint8_t*>(&g_airSoundEntry); }

} // namespace

// 0x00513990
LoopingSoundVolume* LoopingSoundVolume::Assign(const SoundFx::volumeRef* other)
{
    std::memcpy(static_cast<SoundFx::volumeRef*>(this), other, sizeof(SoundFx::volumeRef));
    return this;
}

// 0x00513AD0
CObjectModule* __cdecl CRiderRollingAudio::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderRollingAudio));
    return memory ? static_cast<CRiderRollingAudio*>(memory)->MakeNew() : nullptr;
}

// 0x00513860
void __cdecl CRiderRollingAudio::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterLoopingSound),
                                        g_loopingSoundMemberFunctions, 0x18);
}

// 0x005138E0
CRiderRollingAudio* CRiderRollingAudio::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_prevLevels.PrepareAll();
    m_soundChanged = 1;
    m_curPhase = kStateAir;
    m_on = 1;
    m_sliding = 0;
    m_noRailSound = 0;
    m_haveSound = 0;
    m_forceUpdate = 0;
    m_kindTag = kSkaterLoopingSound;
    m_volumeMult = 1.0f;
    return this;
}

// 0x00513AB0
void CRiderRollingAudio::StopSound()
{
    if (m_soundId) {
        AudioMixerHandle()->StopSound(m_soundId);
        m_soundId = 0;
    }
}

// 0x00513B30
void CRiderRollingAudio::DestroyBody()
{
    m_vtable = kVtable;
    StopSound();
    // (m_prevLevels's destructor: nothing)
    CObjectModule::DestroyBody();
}

// 0x00513FA0
CRiderRollingAudio* CRiderRollingAudio::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00513880
void CRiderRollingAudio::ConfigureFromData(Scripts::CStruct* params)
{
    m_airPitchFall = kAirPitchFall;
    m_airPitchFloor = kAirPitchFloor;
    params->FetchFloat(kVolumeMult, &m_volumeMult, 0);
}

// 0x005138B0
void CRiderRollingAudio::SetPaceRatio(float fraction)
{
    m_speedRatio = fraction;
    if (fraction > 1.0f)
        m_speedRatio = 1.0f;
}

// 0x00513970
void CRiderRollingAudio::Seal()
{
    m_physicsControl = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterPhysicsControl));
}

// 0x00513F60
void CRiderRollingAudio::HoldOff(bool suspend)
{
    CObjectModule::HoldOff(suspend);
    if (suspend)
        StopSound();
    else
        m_soundChanged = 1;
}

// 0x005139E0
int CRiderRollingAudio::InvokeMethodByName(uint32_t name, Scripts::CStruct* /*params*/,
                                                     Scripts::CScript* /*script*/)
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
        Scripts::CStruct* tags = m_entity->m_tagTable;
        uint8_t* state = Prop<uint8_t*>(m_physicsControl, 0x2C);
        Scripts::CStruct* animTags;
        if (tags && !Prop<uint8_t>(state, 0x118) && !Prop<int>(state, 0x1C) &&
            tags->ReadSubStruct(kAnimTags, &animTags, 0) && animTags->HasBareFlag(kWalkToSkateTransition)) {
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
void CRiderRollingAudio::TickFrame()
{
    if (!m_on) {
        VirtualSuspend(true);
        return;
    }
    LoopingSoundVolume volume;
    volume.PrepareAll();
    // Not original: THAW's BMX (game/rider/thaw_bmx.h; THAW 0x0046A097 / 0x0046A0D3) - on the bike its roll and grind
    // (BIKE_ROLL, BIKE_GRIND) on every terrain, nothing in the air or sliding; chosen every frame, and again from the
    // terrain once off the bike.
    int bike = -1;
    if (Skater::ThawBmx::IsSwitchedOn()) {
        using Skater::ThawBmx::BikeSound;
        BikeSound kind = m_curPhase == kStateGround ? BikeSound::Roll
                         : m_curPhase == kStateRail && !m_sliding && !m_noRailSound ? BikeSound::Grind
                                                                                  : BikeSound::Air;
        bike = Skater::ThawBmx::BikeSoundOf(m_entity, kind, m_sound);
        if (bike >= 0) {
            m_haveSound = static_cast<uint8_t>(bike);
            m_soundChanged = 1;
        }
    }
    // The sound for the state, chosen again when something changed.
    switch (bike >= 0 ? -1 : m_curPhase) {
    case kStateGround:
        if (m_soundChanged) {
            m_haveSound = TerrainSound(m_sound, m_surfaceType, SoundTable(kTableWheelroll)) ? 1 : 0;
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
                m_haveSound = TerrainSound(m_sound, m_surfaceType, SoundTable(table)) ? 1 : 0;
                m_soundChanged = 0;
            }
        }
        break;
    default:
        break;
    }
    if (m_soundId && (!m_haveSound || m_playingChecksum != SoundNameKey(m_sound))) {
        AudioMixerHandle()->StopSound(m_soundId);
        m_soundId = 0;
    }
    if (!m_haveSound)
        return;

    // Its volume from where the skater is - stopped while the skater hasn't moved.
    SoundFx::CAudioMixer* sfx = AudioMixerHandle();
    if (m_entity->m_atPoint != m_lastPos) {
        float dropoff = sfx->GetDropoffDistance(SoundNameKey(m_sound));
        sfx->FetchLoudness(&volume, &m_entity->m_atPoint, dropoff, 0, nullptr, nullptr);
    } else {
        sfx->StopSound(m_soundId);
        m_soundId = 0;
    }
    m_lastPos = m_entity->m_atPoint;

    float pitch;
    if (m_curPhase == kStateAir && m_soundId) {
        // In the air the playing sound's pitch falls (to the floor, then it's silent).
        if (m_leanPitchAngle > m_airPitchFloor) {
            m_leanPitchAngle = static_cast<float>(m_leanPitchAngle - static_cast<double>(FrameClock::GetSlomoFrameSeconds()) * m_airPitchFall);
            pitch = m_leanPitchAngle;
        } else {
            volume.ChangeMuted();
            pitch = m_leanPitchAngle;
        }
    } else {
        // By the speed (half when the core physics' +0x2EC is set).
        auto* core = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterCorePhysics));
        if (core && Prop<uint8_t>(core, 0x2EC))
            m_speedRatio = kCapsizedFraction;
        if (m_speedRatio > 0.0f) {
            float percent = static_cast<float>(static_cast<double>(m_volumeMult) * m_speedRatio * kPercent);
            volume.PercentTweak(static_cast<float>(SoundVolume(m_sound, percent)));
            pitch = static_cast<float>((static_cast<double>(FullSpeedPitch(m_sound)) - RestPitch(m_sound)) *
                                           m_speedRatio +
                                       RestPitch(m_sound));
            m_leanPitchAngle = pitch;
        } else {
            volume.ChangeMuted();
            pitch = 0.0f;
            m_leanPitchAngle = 0.0f;
        }
    }

    if (volume.IsAudioOff()) {
        StopSound();
        return;
    }
    if (!m_soundId) {
        static_cast<LoopingSoundVolume*>(&m_prevLevels)->Assign(&volume);
        m_prevPitchValue = pitch;
        m_soundId = sfx->EmitSound(SoundNameKey(m_sound), &volume, pitch, 0, nullptr,
                                   static_cast<int>(reinterpret_cast<uintptr_t>(m_sound)));
        if (m_soundId)
            m_playingChecksum = SoundNameKey(m_sound);
        return;
    }
    // Playing: updated when the volume (its first channel) or the pitch moved by more than a half.
    if (!m_forceUpdate &&
        !(std::fabs(static_cast<double>(volume.m_lanes[0]) - m_prevLevels.m_lanes[0]) > kChangeThreshold) &&
        !(std::fabs(static_cast<double>(pitch) - m_prevPitchValue) > kChangeThreshold))
        return;
    static_cast<LoopingSoundVolume*>(&m_prevLevels)->Assign(&volume);
    m_prevPitchValue = pitch;
    m_forceUpdate = 0;
    sfx->SetSoundParamsScaled(m_soundId, &volume, pitch);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderRollingAudio::Teardown,
         &GameObjects::CRiderRollingAudio::TickFrame,
         &GameObjects::CRiderRollingAudio::ConfigureFromData,
         &GameObjects::CRiderRollingAudio::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CRiderRollingAudio::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &GameObjects::CRiderRollingAudio::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
