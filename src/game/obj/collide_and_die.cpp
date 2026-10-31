// The collide-and-die component (GameObjects::CImpactDestructModule): see collide_and_die.h. From the disassembly of
// 0x0041C1F0-0x0041C707. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "game/obj/collide_and_die.h"
#include "game/obj/projectile_components.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/probe_ray.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"

namespace GameObjects {

OnlinePlay::Hub* g_gameNetManager;

namespace {

extern void** const kVtable;

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

// The object dies (its vtable slot 6, FlagAsDead).
void Kill(CGameEntity* object) { reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[6])(object); }

// The projectile collision component's 0x004F2F40 (no source yet): told of the hit.
void ProjectileHit(CObjectModule* projectile)
{
    static_cast<CShotImpactModule*>(projectile)->MarkHit();
}

} // namespace

// 0x0041C370
CObjectModule* __cdecl CImpactDestructModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CImpactDestructModule));
    return memory ? static_cast<CImpactDestructModule*>(memory)->MakeNew() : nullptr;
}

// 0x0041C210
CImpactDestructModule* CImpactDestructModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kType;
    m_radius = 0.0f;
    m_sizeFactor = 0.0f;
    m_madeTime = static_cast<uint32_t>(FrameClock::GameClockElapsed() + g_clockOffset);
    m_deathScript = 0;
    m_fadingOut = false;
    m_firstUpdate = true;
    return this;
}

// 0x0041C1F0
void CImpactDestructModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041C280
CImpactDestructModule* CImpactDestructModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041C2B0
void CImpactDestructModule::ConfigureFromData(Scripts::CStruct* params)
{
    params->FetchFloat(kRadius, &m_radius, 1);
    params->FetchFloat(kScale, &m_sizeFactor, 1);
    params->ReadHashValue(kDeathScript, &m_deathScript, 1);
    m_vel = {0.0f, 0.0f, 1.0f, 1.0f};
    params->FetchVec(kVel, reinterpret_cast<Scripts::VectorValue*>(&m_vel), 1);
    m_vel.MakeUnit();
}

// 0x0041C330
void CImpactDestructModule::Seal() { mp_suspend = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSuspend)); }

// 0x0041C3D0
void CImpactDestructModule::TickFrame()
{
    if (!g_gameNetManager->InNetGame() && mp_suspend[0x30])
        return;
    int& offset = g_clockOffset;
    if (static_cast<uint32_t>(FrameClock::GameClockElapsed()) + (static_cast<uint32_t>(offset) - m_madeTime) > kLifetime) {
        Kill(m_entity);
    } else if (m_fadingOut) {
        if (static_cast<uint32_t>(FrameClock::GameClockElapsed() + offset) > m_deathTime)
            Kill(m_entity);
    } else {
        Engine3D::CProbeRay feeler;
        feeler.MakeNew();
        CGameEntity* object = m_entity;
        Maths::Vec4 direction = object->m_vel;
        direction.MakeUnit();
        // From where it was (24 up)...
        Maths::Vec4 start = m_firstUpdate ? object->m_atPoint : m_lastPos;
        start.y = start.y + kAbove;
        // ...to where it is (24 up), `radius` on along its velocity.
        double up = static_cast<double>(object->m_atPoint.y) + kAbove;
        float alongW = m_radius * direction.w;
        float alongZ = m_radius * direction.z;
        float alongY = m_radius * direction.y;
        Maths::Vec4 end;
        end.x = static_cast<float>(static_cast<double>(m_radius) * direction.x + object->m_atPoint.x);
        end.y = static_cast<float>(up + alongY);
        end.z = object->m_atPoint.z + alongZ;
        end.w = object->m_atPoint.w + alongW;
        feeler.ChangeOrigin(&start);
        feeler.ChangeFinish(&end);
        if (feeler.GetHitShape(true, false) && !m_fadingOut) {
            if (CObjectModule* projectile = m_entity->FetchModule(kProjectileCollision))
                ProjectileHit(projectile);
            m_fadingOut = true;
            m_deathTime = static_cast<uint32_t>(FrameClock::GameClockElapsed() + offset + static_cast<int>(kDyingTime));
            if (m_deathScript) {
                Maths::Vec4 point = feeler.m_contactPos;
                Scripts::CStruct* params = Scripts::NewStruct();
                if (params)
                    params->PrepareAll();
                params->PutVec3(kPos, *reinterpret_cast<Scripts::VectorValue*>(&point));
                params->PutVec3(kVel, *reinterpret_cast<Scripts::VectorValue*>(&m_vel));
                params->PushFloat(kScale, m_sizeFactor);
                Scripts::LaunchScript(m_deathScript, params, nullptr, false, nullptr);
                if (params) {
                    params->Clear(); // (the destructor)
                    Scripts::DeleteStruct(params);
                }
            }
        }
    } // (the feeler's destructor lets go of the object it hit)
    m_lastPos = m_entity->m_atPoint;
    m_firstUpdate = false;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CImpactDestructModule::Teardown,
         &GameObjects::CImpactDestructModule::TickFrame,
         &GameObjects::CImpactDestructModule::ConfigureFromData,
         &GameObjects::CImpactDestructModule::Seal,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
