// The Vibration component: see vibration_component.h. From the disassembly of 0x0043D490-0x0043D83F (0x0043D490 and
// the member function 0x0043D6F0 aren't Ghidra functions; Ghidra split 0x0043D610 at 0x0043D61F).

#include "game/obj/vibration_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/sio/device.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/obj/movie_manager.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_vibrationComponentMemberFunctions[12]{
            0xDF8A5F0A, 0x404EB936, 0, 0, 0, 0,
            0, 0x404EB936, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kOff = 0xD443A2BC;
constexpr uint32_t kActuator = 0x114FC131;
constexpr uint32_t kPercent = 0x9E497FC6;
constexpr uint32_t kDuration = 0x79A07F3F;
const float kMilliseconds = 1000.0f; // 0x006453C0

// The original's _ftol (0x006236FC): truncated towards zero (the low half of the 64-bit result).
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

bool IsGameHalted() { return g_gamePaused; }

} // namespace

// 0x0043D670
CObjectModule* __cdecl CPadRumbleModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CPadRumbleModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CPadRumbleModule*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    component->m_active = false;
    for (int i = kActuators - 1; i >= 0; --i)
        component->m_timers[i].running = false;
    return component;
}

// 0x0043D490
void __cdecl CPadRumbleModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_vibrationComponentMemberFunctions, 0x18);
}

// 0x0043D4B0
void CPadRumbleModule::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_device) {
        for (int i = kActuators - 1; i >= 0; --i)
            mp_device->FireActuator(i, 0);
    }
    CObjectModule::DestroyBody();
}

// 0x0043D5E0
CPadRumbleModule* CPadRumbleModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0043D610
void CPadRumbleModule::TickFrame()
{
    if (!m_active) {
        VirtualSuspend(true);
        return;
    }
    UpdateTimers();
}

// 0x0043D61F
void CPadRumbleModule::UpdateTimers()
{
    for (int i = kActuators - 1; i >= 0; --i) {
        Stopwatch& timer = m_timers[i];
        if (!timer.running)
            continue;
        int start = timer.start;
        int clock = FrameClock::GameClockElapsed();
        uint32_t elapsed = static_cast<uint32_t>(clock) + (static_cast<uint32_t>(g_clockOffset) - static_cast<uint32_t>(start));
        if (elapsed >= timer.duration) {
            mp_device->FireActuator(i, 0);
            timer.running = false;
        }
    }
}

// 0x0043D520
void CPadRumbleModule::StopAll()
{
    if (!m_active)
        return;
    for (int i = kActuators - 1; i >= 0; --i) {
        mp_device->FireActuator(i, 0);
        m_timers[i].running = false;
    }
}

// 0x0043D550
void CPadRumbleModule::ResetDevice() { mp_device->ClearRumble(); }

// 0x0043D560
void CPadRumbleModule::ChangeLive(bool active)
{
    if (active == m_active)
        return;
    if (!active) {
        m_active = false;
        VirtualSuspend(true);
        if (mp_device) {
            mp_device->StopActuators();
            mp_device->StopRumble();
        }
    } else {
        m_active = true;
        VirtualSuspend(false);
        if (mp_device)
            mp_device->EnableActuators();
    }
}

// 0x0043D5C0
void CPadRumbleModule::ChangeGadget(Sio::InputPort* device)
{
    mp_device = device;
    if (m_active)
        device->EnableActuators();
    else
        device->StopRumble();
}

// 0x0043D6F0
bool CPadRumbleModule::Vibrate(Scripts::CStruct* params, Scripts::CScript*)
{
    if (!params->HasBareFlag(kOff) && (IsGameHalted() || (m_entity->m_compositeFlags & COMPOSITE_FLAG_PAUSED)))
        return true;
    if (!m_active)
        return true;
    int actuator = 0;
    params->FetchIntValue(kActuator, &actuator, 0);
    float percent = 0.0f;
    params->FetchFloat(kPercent, &percent, 0);
    if (params->HasBareFlag(kOff)) {
        mp_device->StopActuators();
        for (int i = kActuators - 1; i >= 0; --i)
            m_timers[i].running = false;
    } else {
        mp_device->FireActuator(actuator, Ftol(percent));
    }
    float duration = 0.0f;
    if (params->FetchFloat(kDuration, &duration, 0)) {
        m_timers[actuator].running = true;
        m_timers[actuator].duration = static_cast<uint32_t>(Ftol(static_cast<double>(duration) * kMilliseconds));
        int clock = FrameClock::GameClockElapsed();
        m_timers[actuator].start = clock + g_clockOffset;
    } else {
        m_timers[actuator].running = false;
    }
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CPadRumbleModule::Teardown,
         &GameObjects::CPadRumbleModule::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
