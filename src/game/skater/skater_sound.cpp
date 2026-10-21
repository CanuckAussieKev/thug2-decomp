// The skater's sound component (Obj::CSkaterSoundComponent): see skater_sound.h.

#include "game/skater/skater_sound.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064C0C4);
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

// The terrain sounds (not rewritten yet): a table by name (0x004443B0); `terrain`'s sound in it played at `pos`
// (0x00444BB0).
void* SoundTable(uint32_t name) { return GameFunction<void*(__cdecl*)(uint32_t)>(0x004443B0)(name); }
void PlayTerrainSound(uint32_t table, int terrain, const Mth::Vector* pos, float volume, float percent)
{
    GameFunction<void(__cdecl*)(void*, int, const Mth::Vector*, float, float, float, int)>(0x00444BB0)(
        SoundTable(table), terrain, pos, volume, percent, percent, 1);
}

bool OnGroundOrInAir(int state) { return state == kStateGround || state == kStateAir; }

// The speed (x, y, z summed in that order) over full speed.
float FullLength(const Mth::Vector& v, float fullSpeed)
{
    double x = v.x, y = v.y, z = v.z;
    return static_cast<float>(std::sqrt((x * x + y * y) + z * z) / fullSpeed);
}

} // namespace

// 0x00518E20
CBaseComponent* __cdecl CSkaterSoundComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterSoundComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterSoundComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_fullSpeed = 1.0f;
    component->m_volumeMult = 1.0f;
    component->m_vtable = kVtable;
    component->m_type = kSkaterSound;
    component->m_sliding = 0;
    return component;
}

// 0x00518C80
void CSkaterSoundComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00518C90
CSkaterSoundComponent* CSkaterSoundComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00518CC0
void CSkaterSoundComponent::InitFromStructure(Script::CStruct* params)
{
    float mult;
    if (params->GetFloat(kVolumeMult, &mult, 0))
        m_volumeMult = mult;
}

// 0x00518CF0
void CSkaterSoundComponent::PlayLand(float speed, int terrain)
{
    uint32_t table = m_state == kStateRail ? (m_sliding ? kTableSlideLand : kTableGrindLand) : kTableLand;
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    PlayTerrainSound(table, terrain, &m_object->m_pos, volume, percent);
}

// 0x00518D60
void CSkaterSoundComponent::PlayJump(float speed, int terrain, int state)
{
    uint32_t table = state == kStateRail ? (m_sliding ? kTableSlideJump : kTableGrindJump) : kTableJump;
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    PlayTerrainSound(table, terrain, &m_object->m_pos, volume, percent);
}

// 0x00518DD0
void CSkaterSoundComponent::PlayBonk(float speed, int terrain)
{
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    PlayTerrainSound(kTableBonk, terrain, &m_object->m_pos, volume, percent);
}

// 0x00518EA0
void CSkaterSoundComponent::PlayLandHere(float speed)
{
    PlayLand(speed, OnGroundOrInAir(m_state) ? m_lastGround : m_terrain);
}

// 0x00518FA0
void CSkaterSoundComponent::PlayJumpHere(float speed)
{
    PlayJump(speed, OnGroundOrInAir(m_state) ? m_lastGround : m_terrain, m_state);
}

// 0x00518EE0
void CSkaterSoundComponent::PlayJumpInState(float speed, int terrain) { PlayJump(speed, terrain, m_state); }

// 0x00518F00
void CSkaterSoundComponent::PlayBonkHere(float speed)
{
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    PlayTerrainSound(kTableBonk, m_terrain, &m_object->m_pos, volume, percent);
}

// 0x00518F50
void CSkaterSoundComponent::PlayCessHere(float speed)
{
    float percent = static_cast<float>(static_cast<double>(speed) * kPercent);
    float volume = static_cast<float>(static_cast<double>(percent) * m_volumeMult);
    PlayTerrainSound(kTableCess, m_terrain, &m_object->m_pos, volume, percent);
}

// 0x00518FD0
int CSkaterSoundComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    const Mth::Vector& vel = m_object->m_vel;
    switch (name) {
    case kPlayJumpSound:
        if (m_state >= 0 && (m_state <= kStateAir || m_state == kStateRail))
            PlayJumpHere(static_cast<float>(vel.Length() / m_fullSpeed));
        return 1;
    case kPlayBonkSound:
        PlayBonkHere(FullLength(vel, m_fullSpeed));
        return 1;
    case kPlayLandSound:
        if (m_state >= 0 && m_state <= kStateAir)
            PlayLandHere(static_cast<float>(std::fabs(vel.y) / m_fullSpeed));
        else if (m_state == kStateRail)
            PlayLandHere(static_cast<float>(vel.Length() / m_fullSpeed));
        return 1;
    case kPlayCessSound:
        PlayCessHere(FullLength(vel, m_fullSpeed));
        return 1;
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x00518E20, Obj::CSkaterSoundComponent::Create);
REPLACE(0x00518C80, &Obj::CSkaterSoundComponent::DestroyBody);
REPLACE(0x00518C90, &Obj::CSkaterSoundComponent::Destroy);
REPLACE(0x00518CC0, &Obj::CSkaterSoundComponent::InitFromStructure);
REPLACE(0x00518FD0, &Obj::CSkaterSoundComponent::CallMemberFunction);
REPLACE(0x00518CF0, &Obj::CSkaterSoundComponent::PlayLand);
REPLACE(0x00518D60, &Obj::CSkaterSoundComponent::PlayJump);
REPLACE(0x00518DD0, &Obj::CSkaterSoundComponent::PlayBonk);
REPLACE(0x00518EA0, &Obj::CSkaterSoundComponent::PlayLandHere);
REPLACE(0x00518FA0, &Obj::CSkaterSoundComponent::PlayJumpHere);
REPLACE(0x00518EE0, &Obj::CSkaterSoundComponent::PlayJumpInState);
REPLACE(0x00518F00, &Obj::CSkaterSoundComponent::PlayBonkHere);
REPLACE(0x00518F50, &Obj::CSkaterSoundComponent::PlayCessHere);
