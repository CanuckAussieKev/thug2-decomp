// The projectile component (Obj::CProjectileComponent): see projectile_component.h.

#include "game/obj/projectile_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/trigger_component.h"
#include "game/skate/skate_module_calls.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

namespace {

extern void** const kVtable;

// The local skater's projectile slots (CSkater +0x1C0, five of 0x18 bytes: +0 the name, +4 taken, +8 the position).
constexpr uint32_t kSlots = 0x1C0;
constexpr uint32_t kSlotSize = 0x18;
constexpr int kSlotCount = 5;

// The trigger component's events for a projectile's touch (0x0064B89C: 14, none, none).
const int* TouchEvents() { return reinterpret_cast<const int*>(0x0064B89C); }

constexpr float kReach = 12.0f;          // 0x0064546C (also added to the feeler's end w)
constexpr float kStep = 0.8975979f;      // 0x00646968 (2pi / 7)
constexpr int kFeelers = 8;
constexpr int kRestFrames = 10;

uint8_t& SlotTaken(uint8_t* skater, int slot) { return skater[kSlots + 4 + slot * static_cast<int>(kSlotSize)]; }

// One feeler's end x / z, as the original's x87 code: the cosine of the angle as computed (53 bits), the sine of it
// rounded to float; z adds (sin + float(cos)) * 12 rounded to float first.
void FeelerEnd(int index, float x, float z, float* endX, float* endZ)
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
        mov eax, endX
        fstp dword ptr [eax]
        mov eax, endZ
        fstp dword ptr [eax]
    }
}

} // namespace

// 0x004F3890
CBaseComponent* __cdecl CProjectileComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CProjectileComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CProjectileComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->m_rest = 0;
    return component;
}

// 0x004F37B0
void CProjectileComponent::DestroyBody()
{
    m_vtable = kVtable;
    SlotTaken(mp_skater, m_slot) = 0;
    CBaseComponent::DestroyBody();
}

// 0x004F37D0
CProjectileComponent* CProjectileComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004F3800
void CProjectileComponent::InitFromStructure(Script::CStruct* /*params*/)
{
    auto* skater = reinterpret_cast<uint8_t*>(SkateCalls::GetLocalSkater());
    mp_skater = skater;
    uint32_t name = m_object->m_id;
    m_slot = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if (name == *reinterpret_cast<uint32_t*>(skater + kSlots + i * kSlotSize)) {
            m_slot = i;
            break;
        }
    }
    SlotTaken(skater, m_slot) = 1;
}

// 0x004F3860
void CProjectileComponent::Finalize()
{
    auto* skater = SkateCalls::GetLocalSkater();
    if (skater)
        mp_trigger = static_cast<CTriggerComponent*>(skater->GetComponent(CTriggerComponent::kType));
}

// 0x004F3A90
void CProjectileComponent::Update()
{
    *reinterpret_cast<Mth::Vector*>(mp_skater + kSlots + 8 + m_slot * static_cast<int>(kSlotSize)) = m_object->m_pos;
    CheckTriggers();
}

// 0x004F3910
void CProjectileComponent::CheckTriggers()
{
    if (m_rest > 0) {
        --m_rest;
        return;
    }
    Nx::CFeeler feeler;
    feeler.Construct();
    const Mth::Vector pos = m_object->m_pos;
    float endW = static_cast<float>(static_cast<double>(pos.w) + kReach);
    for (int i = 0; i < kFeelers; ++i) {
        feeler.m_line.start = pos;
        feeler.m_line.end.y = pos.y;
        feeler.m_line.end.w = endW;
        FeelerEnd(i, pos.x, pos.z, &feeler.m_line.end.x, &feeler.m_line.end.z);
        if (feeler.GetCollision(true, false) && mp_trigger->CheckFeeler(TouchEvents(), &feeler))
            m_rest = kRestFrames;
    }
    feeler.m_object.Release();
}

} // namespace Obj

REPLACE(0x004F3890, Obj::CProjectileComponent::Create);
REPLACE(0x004F37B0, &Obj::CProjectileComponent::DestroyBody);
REPLACE(0x004F37D0, &Obj::CProjectileComponent::Destroy);
REPLACE(0x004F3800, &Obj::CProjectileComponent::InitFromStructure);
REPLACE(0x004F3860, &Obj::CProjectileComponent::Finalize);
REPLACE(0x004F3A90, &Obj::CProjectileComponent::Update);
REPLACE(0x004F3910, &Obj::CProjectileComponent::CheckTriggers);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x0064B8A8,
    &Obj::CProjectileComponent::Destroy,
    &Obj::CProjectileComponent::Update,
    &Obj::CProjectileComponent::InitFromStructure,
    &Obj::CProjectileComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
