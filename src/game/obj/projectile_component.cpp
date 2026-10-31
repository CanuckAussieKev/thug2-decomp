// The projectile component (GameObjects::CProjectileComponent): see projectile_component.h.

#include "game/obj/projectile_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vec4.h"
#include "game/nx/probe_ray.h"
#include "game/obj/trigger_component.h"
#include "game/skate/skate_module_calls.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/trigger_events.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

// The local skater's projectile slots (CRider +0x1C0, five of 0x18 bytes: +0 the name, +4 taken, +8 the position).
constexpr uint32_t kSlots = 0x1C0;
constexpr uint32_t kSlotSize = 0x18;
constexpr int kSlotCount = 5;

// The trigger component's events for a projectile's touch (0x0064B89C: 14, none, none).
const int* TouchEvents() { return g_projectileTouchEvents; }

constexpr float kReach = 12.0f;          // 0x0064546C (also added to the feeler's end w)
constexpr float kStep = 0.8975979f;      // 0x00646968 (2pi / 7)
constexpr int kFeelers = 8;
constexpr int kRestFrames = 10;

uint8_t& SlotTaken(uint8_t* skater, int slot) { return skater[kSlots + 4 + slot * static_cast<int>(kSlotSize)]; }

// One feeler's end x / z, as the original's x87 code: the cosine of the angle as computed (53 bits), the sine of it
// rounded to float; z adds (sin + float(cos)) * 12 rounded to float first.
void FeelerEnd(int index, float x, float z, float* lastColumn, float* endZ)
{
    float angleF;
    float slot;
    float step = kStep;
    float reach = kReach;
    __asm {
        fild index
        fmul step
        fst angleF
        fcos
        fld angleF
        fsin
        fxch st(1)
        fst slot
        fsub st(0), st(1)
        fxch st(1)
        fadd slot
        fmul reach
        fstp slot
        fmul reach
        fld z
        fadd slot
        fxch st(1)
        fadd x
        mov eax, lastColumn
        fstp dword ptr [eax]
        mov eax, endZ
        fstp dword ptr [eax]
    }
}

} // namespace

// 0x004F3890
CObjectModule* __cdecl CProjectileComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CProjectileComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CProjectileComponent*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    component->m_rest = 0;
    return component;
}

// 0x004F37B0
void CProjectileComponent::DestroyBody()
{
    m_vtable = kVtable;
    SlotTaken(m_riderRef, m_seat) = 0;
    CObjectModule::DestroyBody();
}

// 0x004F37D0
CProjectileComponent* CProjectileComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004F3800
void CProjectileComponent::ConfigureFromData(Scripts::CStruct* /*params*/)
{
    auto* skater = reinterpret_cast<uint8_t*>(SkateCalls::GetHomeRider());
    m_riderRef = skater;
    uint32_t name = m_entity->m_ident;
    m_seat = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if (name == *reinterpret_cast<uint32_t*>(skater + kSlots + i * kSlotSize)) {
            m_seat = i;
            break;
        }
    }
    SlotTaken(skater, m_seat) = 1;
}

// 0x004F3860
void CProjectileComponent::Seal()
{
    auto* skater = SkateCalls::GetHomeRider();
    if (skater)
        mp_trigger = static_cast<CActivationZoneModule*>(skater->FetchModule(CActivationZoneModule::kType));
}

// 0x004F3A90
void CProjectileComponent::TickFrame()
{
    *reinterpret_cast<Maths::Vec4*>(m_riderRef + kSlots + 8 + m_seat * static_cast<int>(kSlotSize)) = m_entity->m_atPoint;
    CheckTriggers();
}

// 0x004F3910
void CProjectileComponent::CheckTriggers()
{
    if (m_rest > 0) {
        --m_rest;
        return;
    }
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    const Maths::Vec4 pos = m_entity->m_atPoint;
    float endW = static_cast<float>(static_cast<double>(pos.w) + kReach);
    for (int i = 0; i < kFeelers; ++i) {
        feeler.m_line.start = pos;
        feeler.m_line.end.y = pos.y;
        feeler.m_line.end.w = endW;
        FeelerEnd(i, pos.x, pos.z, &feeler.m_line.end.x, &feeler.m_line.end.z);
        if (feeler.GetHitShape(true, false) && mp_trigger->CheckFeeler(TouchEvents(), &feeler))
            m_rest = kRestFrames;
    }
    feeler.m_entity.Release();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CProjectileComponent::Teardown,
         &GameObjects::CProjectileComponent::TickFrame,
         &GameObjects::CProjectileComponent::ConfigureFromData,
         &GameObjects::CProjectileComponent::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
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
