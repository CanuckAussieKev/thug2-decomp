// The Velocity component: see velocity_component.h. From the disassembly of 0x0043D2B0-0x0043D487.

#include "engine/objects/velocity_component.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/collide_and_die.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kVel = 0x0C4C809E;
const float kHalf = 0.5f; // 0x006453BC

// The Suspend component's flag at +0x30 (0x00433E90; not rewritten yet).
bool SuspendFlag(CObjectModule* suspend) { return static_cast<CDormancyModule*>(suspend)->IsFar(); }

} // namespace

// 0x0043D430
CObjectModule* __cdecl CVelocityTrackerModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CVelocityTrackerModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CVelocityTrackerModule*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    return component;
}

// 0x0043D2B0
void CVelocityTrackerModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0043D2C0
CVelocityTrackerModule* CVelocityTrackerModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0043D2F0
void CVelocityTrackerModule::ConfigureFromData(Scripts::CStruct* params)
{
    Scripts::VectorValue vel;
    params->FetchVec(kVel, &vel, 1);
    m_entity->m_vel.x = vel.x;
    m_entity->m_vel.y = vel.y;
    m_entity->m_vel.z = vel.z;
    m_entity->m_vel.w = vel.w;
}

// 0x0043D340
void CVelocityTrackerModule::TickFrame()
{
    if (!g_onlineHub->InNetGame() && SuspendFlag(m_suspend))
        return;
    const Maths::Vec4& vel = m_entity->m_vel;
    // Half the velocity (each rounded), the position as it was.
    float halfX = vel.x * kHalf, halfY = vel.y * kHalf, halfZ = vel.z * kHalf, halfW = vel.w * kHalf;
    Maths::Vec4 pos = m_entity->m_atPoint;
    double frame = FrameClock::GetSlomoFrameSeconds();
    // Each step rounded, then added - but w's step isn't rounded.
    float stepX = static_cast<float>(halfX * frame);
    float stepY = static_cast<float>(halfY * frame);
    float stepZ = static_cast<float>(halfZ * frame);
    Maths::Vec4& out = m_entity->m_atPoint;
    out.z = static_cast<float>(static_cast<double>(stepZ) + pos.z);
    out.w = static_cast<float>(frame * halfW + pos.w);
    out.x = static_cast<float>(static_cast<double>(stepX) + pos.x);
    out.y = static_cast<float>(static_cast<double>(stepY) + pos.y);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CVelocityTrackerModule::Teardown,
         &GameObjects::CVelocityTrackerModule::TickFrame,
         &GameObjects::CVelocityTrackerModule::ConfigureFromData,
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
