// The WaitAndDie component: see wait_and_die.h. From the disassembly of 0x0043D840-0x0043D928.

#include "engine/objects/wait_and_die.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/wait_and_die.h"

namespace GameObjects {

namespace {

constexpr uint32_t kWaitFor = 0x1869F559;
constexpr uint32_t kLifeTime = 0xC218CF77;
const float kMilliseconds = 1000.0f; // 0x006453C0

// The original's _ftol (0x006236FC): truncated towards zero (the low half of the 64-bit result).
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The object's vtable slot 6 (FlagAsDead), as the original's tail jump.
void Die(CEntity* object) { reinterpret_cast<void(__thiscall*)(CEntity*)>(object->m_vtable[6])(object); }

} // namespace

// 0x0043D840
void CWaitAndDieComponent::DestroyBody()
{
    m_vtable = g_waitAndDieVtable;
    CObjectModule::DestroyBody();
}

// 0x0043D850
CWaitAndDieComponent* CWaitAndDieComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0043D880
void CWaitAndDieComponent::ConfigureFromData(Scripts::CStruct* params)
{
    float seconds = 0.0f;
    if (!params->ReadHashValue(kWaitFor, &m_waitFor, 0))
        params->FetchFloat(kLifeTime, &seconds, 1);
    m_lifespan = static_cast<uint32_t>(Ftol(static_cast<double>(seconds) * kMilliseconds));
    int clock = FrameClock::GameClockElapsed();
    m_outset = clock + g_clockOffset;
}

// 0x0043D8E0
void CWaitAndDieComponent::TickFrame()
{
    int start = m_outset;
    int clock = FrameClock::GameClockElapsed();
    m_elapsed = static_cast<uint32_t>(clock) + (static_cast<uint32_t>(g_clockOffset) - static_cast<uint32_t>(start));
    if (m_waitFor) {
        if (!FindObject(m_waitFor))
            Die(m_entity);
    } else if (m_elapsed > m_lifespan) {
        Die(m_entity);
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

} // namespace
} // namespace Obj

namespace GameObjects {

static VtableSlot g_waitAndDieVtable_slots[] = {&GameObjects::CWaitAndDieComponent::Teardown,
         &GameObjects::CWaitAndDieComponent::TickFrame,
         &GameObjects::CWaitAndDieComponent::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const g_waitAndDieVtable = reinterpret_cast<void**>(g_waitAndDieVtable_slots);

} // namespace Obj
