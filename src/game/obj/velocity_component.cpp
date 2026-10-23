// The Velocity component: see velocity_component.h. From the disassembly of 0x0043D2B0-0x0043D487.

#include "game/obj/velocity_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/suspend_component.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646830);
constexpr uint32_t kVel = 0x0C4C809E;
const float kHalf = 0.5f; // 0x006453BC

// The Suspend component's flag at +0x30 (0x00433E90; not rewritten yet).
bool SuspendFlag(CBaseComponent* suspend) { return static_cast<CSuspendComponent*>(suspend)->IsFar(); }

} // namespace

// 0x0043D430
CBaseComponent* __cdecl CVelocityComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CVelocityComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CVelocityComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x0043D2B0
void CVelocityComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0043D2C0
CVelocityComponent* CVelocityComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0043D2F0
void CVelocityComponent::InitFromStructure(Script::CStruct* params)
{
    Script::VectorValue vel;
    params->GetVector(kVel, &vel, 1);
    m_object->m_vel.x = vel.x;
    m_object->m_vel.y = vel.y;
    m_object->m_vel.z = vel.z;
    m_object->m_vel.w = vel.w;
}

// 0x0043D340
void CVelocityComponent::Update()
{
    if (!GameVariable<GameNet::Manager*>(0x006FA7D0)->InNetGame() && SuspendFlag(mp_suspend))
        return;
    const Mth::Vector& vel = m_object->m_vel;
    // Half the velocity (each rounded), the position as it was.
    float halfX = vel.x * kHalf, halfY = vel.y * kHalf, halfZ = vel.z * kHalf, halfW = vel.w * kHalf;
    Mth::Vector pos = m_object->m_pos;
    double frame = Tmr::GetSlomoFrameSeconds();
    // Each step rounded, then added - but w's step isn't rounded.
    float stepX = static_cast<float>(halfX * frame);
    float stepY = static_cast<float>(halfY * frame);
    float stepZ = static_cast<float>(halfZ * frame);
    Mth::Vector& out = m_object->m_pos;
    out.z = static_cast<float>(static_cast<double>(stepZ) + pos.z);
    out.w = static_cast<float>(frame * halfW + pos.w);
    out.x = static_cast<float>(static_cast<double>(stepX) + pos.x);
    out.y = static_cast<float>(static_cast<double>(stepY) + pos.y);
}

} // namespace Obj

REPLACE(0x0043D430, Obj::CVelocityComponent::Create);
REPLACE(0x0043D2B0, &Obj::CVelocityComponent::DestroyBody);
REPLACE(0x0043D2C0, &Obj::CVelocityComponent::Destroy);
REPLACE(0x0043D2F0, &Obj::CVelocityComponent::InitFromStructure);
REPLACE(0x0043D340, &Obj::CVelocityComponent::Update);
