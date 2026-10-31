// The WaitAndDie component: see wait_and_die.h. From the disassembly of 0x0043D840-0x0043D928.

#include "game/obj/wait_and_die.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/wait_and_die.h"

namespace Obj {

namespace {

constexpr uint32_t kWaitFor = 0x1869F559;
constexpr uint32_t kLifeTime = 0xC218CF77;
const float kMilliseconds = 1000.0f; // 0x006453C0

// The original's _ftol (0x006236FC): truncated towards zero (the low half of the 64-bit result).
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The object's vtable slot 6 (MarkAsDead), as the original's tail jump.
void Die(CObject* object) { reinterpret_cast<void(__thiscall*)(CObject*)>(object->m_vtable[6])(object); }

} // namespace

// 0x0043D840
void CWaitAndDieComponent::DestroyBody()
{
    m_vtable = g_waitAndDieVtable;
    CBaseComponent::DestroyBody();
}

// 0x0043D850
CWaitAndDieComponent* CWaitAndDieComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0043D880
void CWaitAndDieComponent::InitFromStructure(Script::CStruct* params)
{
    float seconds = 0.0f;
    if (!params->GetChecksum(kWaitFor, &m_waitFor, 0))
        params->GetFloat(kLifeTime, &seconds, 1);
    m_lifetime = static_cast<uint32_t>(Ftol(static_cast<double>(seconds) * kMilliseconds));
    int clock = Tmr::GameClockElapsed();
    m_start = clock + g_clockOffset;
}

// 0x0043D8E0
void CWaitAndDieComponent::Update()
{
    int start = m_start;
    int clock = Tmr::GameClockElapsed();
    m_elapsed = static_cast<uint32_t>(clock) + (static_cast<uint32_t>(g_clockOffset) - static_cast<uint32_t>(start));
    if (m_waitFor) {
        if (!FindObject(m_waitFor))
            Die(m_object);
    } else if (m_elapsed > m_lifetime) {
        Die(m_object);
    }
}

} // namespace Obj

namespace Obj {
namespace {

} // namespace
} // namespace Obj

namespace Obj {

static VtableSlot g_waitAndDieVtable_slots[] = {&Obj::CWaitAndDieComponent::Destroy,
         &Obj::CWaitAndDieComponent::Update,
         &Obj::CWaitAndDieComponent::InitFromStructure,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const g_waitAndDieVtable = reinterpret_cast<void**>(g_waitAndDieVtable_slots);

} // namespace Obj
