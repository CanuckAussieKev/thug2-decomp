// The skater's sound component (GameObjects::CRiderSfxPlayer): see skater_sound.h.

#include "game/rider/skater_sound.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/rider/thaw_bmx.h"
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterSound = 0x187574FA;
constexpr uint32_t kVolumeMult = 0xF1A99B27;
constexpr uint32_t kPlayJumpSound = 0x0E338369, kPlayBonkSound = 0x0069E457, kPlayLandSound = 0x2C779F22,
                   kPlayCessSound = 0x6F5E9124;
// The terrain sound tables
constexpr uint32_t kTableLand = 0x1D296AEC, kTableGrindLand = 0x80E4427E, kTableSlideLand = 0x840278CC,
                   kTableJump = 0x129AB922, kTableGrindJump = 0x8F5791B0, kTableSlideJump = 0x8BB1AB02,
                   kTableBonk = 0x67D72D44, kTableCess = 0x3EFC3BBD;

constexpr int kStateGround = 0, kStateAir = 1, kStateRail = 4;
constexpr float kPercent = 100.0f; // 0x006463D4

// The terrain sounds: a table by name; `terrain`'s sound in it played at `pos` (and sent to the other players).
uint8_t SoundTable(uint32_t name) { return SurfaceKind::GetTableIndex(name); }
void PlayTerrainSound(uint32_t table, int terrain, const Maths::Vec4* pos, float volume, float percent)
{
    SurfaceKind::PlayTerrainSound(SoundTable(table), static_cast<uint8_t>(terrain), pos, volume, percent, percent, true);
}

bool OnGroundOrInAir(int state) { return state == kStateGround || state == kStateAir; }

// Not original: THAW's BMX (game/rider/thaw_bmx.h) - on the bike its own sound for `kind` instead of the terrain's
// (THAW 0x005F197B / 0x005F1A10 / 0x005F1AB0: BIKE_LAND, BIKE_GRINDLAND, BIKE_OLLIE, BIKE_GRINDJUMP, BIKE_BONK).
// Whether it was the bike's (played, or none).
bool PlayBikeSound(CGameEntity* skater, Skater::ThawBmx::BikeSound kind, float volume, float percent)
{
    if (!Skater::ThawBmx::IsSwitchedOn())
        return false;
    SurfaceKind::SoundInfo info;
    int bike = Skater::ThawBmx::BikeSoundOf(skater, kind, &info);
    if (bike < 0)
        return false;
    if (bike > 0)
        SurfaceKind::PlaySoundInfo(info, &skater->m_atPoint, volume, percent);
    return true;
}

// The speed (x, y, z summed in that order) over full speed.
float FullLength(const Maths::Vec4& v, float fullSpeed)
{
    double x = v.x, y = v.y, z = v.z;
    return static_cast<float>(std::sqrt((x * x + y * y) + z * z) / fullSpeed);
}

} // namespace

// 0x00518E20
CObjectModule* __cdecl CRiderSfxPlayer::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderSfxPlayer));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderSfxPlayer*>(memory);
    component->CObjectModule::MakeNew();
    component->m_fullSpeed = 1.0f;
    component->m_volumeMult = 1.0f;
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterSound;
    component->m_sliding = 0;
    return component;
}

// 0x00518C80
void CRiderSfxPlayer::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00518C90
CRiderSfxPlayer* CRiderSfxPlayer::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00518CC0
void CRiderSfxPlayer::ConfigureFromData(Scripts::CStruct* params)
{
    float mult;
    if (params->FetchFloat(kVolumeMult, &mult, 0))
        m_volumeMult = mult;
}

// 0x00518CF0
void CRiderSfxPlayer::PlayLand(float speed, int terrain)
{
    uint32_t table = m_curPhase == kStateRail ? (m_sliding ? kTableSlideLand : kTableGrindLand) : kTableLand;
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    using Skater::ThawBmx::BikeSound;
    if (PlayBikeSound(m_entity, m_curPhase == kStateRail ? BikeSound::GrindLand : BikeSound::Land, volume, percent))
        return;
    PlayTerrainSound(table, terrain, &m_entity->m_atPoint, volume, percent);
}

// 0x00518D60
void CRiderSfxPlayer::PlayJump(float speed, int terrain, int state)
{
    uint32_t table = state == kStateRail ? (m_sliding ? kTableSlideJump : kTableGrindJump) : kTableJump;
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    using Skater::ThawBmx::BikeSound;
    if (PlayBikeSound(m_entity, state == kStateRail ? BikeSound::GrindJump : BikeSound::Ollie, volume, percent))
        return;
    PlayTerrainSound(table, terrain, &m_entity->m_atPoint, volume, percent);
}

// 0x00518DD0
void CRiderSfxPlayer::PlayBonk(float speed, int terrain)
{
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    if (PlayBikeSound(m_entity, Skater::ThawBmx::BikeSound::Bonk, volume, percent))
        return;
    PlayTerrainSound(kTableBonk, terrain, &m_entity->m_atPoint, volume, percent);
}

// 0x00518EA0
void CRiderSfxPlayer::PlayLandHere(float speed)
{
    PlayLand(speed, OnGroundOrInAir(m_curPhase) ? m_lastGround : m_surfaceType);
}

// 0x00518FA0
void CRiderSfxPlayer::PlayJumpHere(float speed)
{
    PlayJump(speed, OnGroundOrInAir(m_curPhase) ? m_lastGround : m_surfaceType, m_curPhase);
}

// 0x00518EE0
void CRiderSfxPlayer::PlayJumpInState(float speed, int terrain) { PlayJump(speed, terrain, m_curPhase); }

// 0x00518F00
void CRiderSfxPlayer::PlayBonkHere(float speed)
{
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    if (PlayBikeSound(m_entity, Skater::ThawBmx::BikeSound::Bonk, volume, percent))
        return;
    PlayTerrainSound(kTableBonk, m_surfaceType, &m_entity->m_atPoint, volume, percent);
}

// 0x00518F50
void CRiderSfxPlayer::PlayCessHere(float speed)
{
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    PlayTerrainSound(kTableCess, m_surfaceType, &m_entity->m_atPoint, volume, percent);
}

// 0x00518FD0
int CRiderSfxPlayer::InvokeMethodByName(uint32_t name, Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    const Maths::Vec4& vel = m_entity->m_vel;
    switch (name) {
    case kPlayJumpSound:
        if (m_curPhase >= 0 && (m_curPhase <= kStateAir || m_curPhase == kStateRail))
            PlayJumpHere(static_cast<float>(vel.LenOf() / m_fullSpeed));
        return 1;
    case kPlayBonkSound:
        PlayBonkHere(FullLength(vel, m_fullSpeed));
        return 1;
    case kPlayLandSound:
        if (m_curPhase >= 0 && m_curPhase <= kStateAir)
            PlayLandHere(static_cast<float>(std::fabs(vel.y) / m_fullSpeed));
        else if (m_curPhase == kStateRail)
            PlayLandHere(static_cast<float>(vel.LenOf() / m_fullSpeed));
        return 1;
    case kPlayCessSound:
        PlayCessHere(FullLength(vel, m_fullSpeed));
        return 1;
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderSfxPlayer::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CRiderSfxPlayer::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderSfxPlayer::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
