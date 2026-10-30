// The collide-and-die component (Obj::CCollideAndDieComponent): see collide_and_die.h. From the disassembly of
// 0x0041C1F0-0x0041C707. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "game/obj/collide_and_die.h"
#include "game/obj/projectile_components.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/feeler.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Obj {

GAME_GLOBAL(GameNet::Manager*, g_gameNetManager, 0x006FA7D0);

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00645EB4);

constexpr uint32_t kRadius = 0xC48391A5;      // radius
constexpr uint32_t kScale = 0x13B9DA7B;       // scale
constexpr uint32_t kDeathScript = 0x6647ADC3; // death_script
constexpr uint32_t kVel = 0x0C4C809E;         // vel
constexpr uint32_t kPos = 0x7F261953;         // pos
constexpr uint32_t kSuspend = 0xCE0CA665;
constexpr uint32_t kProjectileCollision = 0x7767D6D7;

constexpr uint32_t kLifetime = 10000;  // milliseconds
constexpr uint32_t kDyingTime = 1000;  // milliseconds after the hit
constexpr float kAbove = 24.0f;        // 0x00645EE8

// The object dies (its vtable slot 6, MarkAsDead).
void Kill(CCompositeObject* object) { reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[6])(object); }

// The projectile collision component's 0x004F2F40 (no source yet): told of the hit.
void ProjectileHit(CBaseComponent* projectile)
{
    static_cast<CProjectileCollisionComponent*>(projectile)->MarkHit();
}

} // namespace

// 0x0041C370
CBaseComponent* __cdecl CCollideAndDieComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CCollideAndDieComponent));
    return memory ? static_cast<CCollideAndDieComponent*>(memory)->Construct() : nullptr;
}

// 0x0041C210
CCollideAndDieComponent* CCollideAndDieComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    m_radius = 0.0f;
    m_scale = 0.0f;
    m_madeTime = static_cast<uint32_t>(Tmr::GameClockElapsed() + g_clockOffset);
    m_deathScript = 0;
    m_dying = false;
    m_firstUpdate = true;
    return this;
}

// 0x0041C1F0
void CCollideAndDieComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041C280
CCollideAndDieComponent* CCollideAndDieComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041C2B0
void CCollideAndDieComponent::InitFromStructure(Script::CStruct* params)
{
    params->GetFloat(kRadius, &m_radius, 1);
    params->GetFloat(kScale, &m_scale, 1);
    params->GetChecksum(kDeathScript, &m_deathScript, 1);
    m_vel = {0.0f, 0.0f, 1.0f, 1.0f};
    params->GetVector(kVel, reinterpret_cast<Script::VectorValue*>(&m_vel), 1);
    m_vel.Normalize();
}

// 0x0041C330
void CCollideAndDieComponent::Finalize() { mp_suspend = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSuspend)); }

// 0x0041C3D0
void CCollideAndDieComponent::Update()
{
    if (!g_gameNetManager->InNetGame() && mp_suspend[0x30])
        return;
    int& offset = g_clockOffset;
    if (static_cast<uint32_t>(Tmr::GameClockElapsed()) + (static_cast<uint32_t>(offset) - m_madeTime) > kLifetime) {
        Kill(m_object);
    } else if (m_dying) {
        if (static_cast<uint32_t>(Tmr::GameClockElapsed() + offset) > m_deathTime)
            Kill(m_object);
    } else {
        Nx::CFeeler feeler;
        feeler.Construct();
        CCompositeObject* object = m_object;
        Mth::Vector direction = object->m_vel;
        direction.Normalize();
        // From where it was (24 up)...
        Mth::Vector start = m_firstUpdate ? object->m_pos : m_lastPos;
        start.y = start.y + kAbove;
        // ...to where it is (24 up), `radius` on along its velocity.
        double up = static_cast<double>(object->m_pos.y) + kAbove;
        float alongW = m_radius * direction.w;
        float alongZ = m_radius * direction.z;
        float alongY = m_radius * direction.y;
        Mth::Vector end;
        end.x = static_cast<float>(static_cast<double>(m_radius) * direction.x + object->m_pos.x);
        end.y = static_cast<float>(up + alongY);
        end.z = object->m_pos.z + alongZ;
        end.w = object->m_pos.w + alongW;
        feeler.SetStart(&start);
        feeler.SetEnd(&end);
        if (feeler.GetCollision(true, false) && !m_dying) {
            if (CBaseComponent* projectile = m_object->GetComponent(kProjectileCollision))
                ProjectileHit(projectile);
            m_dying = true;
            m_deathTime = static_cast<uint32_t>(Tmr::GameClockElapsed() + offset + static_cast<int>(kDyingTime));
            if (m_deathScript) {
                Mth::Vector point = feeler.m_point;
                Script::CStruct* params = Script::NewStruct();
                if (params)
                    params->Init();
                params->AddVector(kPos, *reinterpret_cast<Script::VectorValue*>(&point));
                params->AddVector(kVel, *reinterpret_cast<Script::VectorValue*>(&m_vel));
                params->AddFloat(kScale, m_scale);
                Script::RunScript(m_deathScript, params, nullptr, false, nullptr);
                if (params) {
                    params->Clear(); // (the destructor)
                    Script::DeleteStruct(params);
                }
            }
        }
    } // (the feeler's destructor lets go of the object it hit)
    m_lastPos = m_object->m_pos;
    m_firstUpdate = false;
}

} // namespace Obj

REPLACE(0x0041C370, Obj::CCollideAndDieComponent::Create);
REPLACE(0x0041C210, &Obj::CCollideAndDieComponent::Construct);
REPLACE(0x0041C1F0, &Obj::CCollideAndDieComponent::DestroyBody);
REPLACE(0x0041C280, &Obj::CCollideAndDieComponent::Destroy);
REPLACE(0x0041C2B0, &Obj::CCollideAndDieComponent::InitFromStructure);
REPLACE(0x0041C330, &Obj::CCollideAndDieComponent::Finalize);
REPLACE(0x0041C3D0, &Obj::CCollideAndDieComponent::Update);
