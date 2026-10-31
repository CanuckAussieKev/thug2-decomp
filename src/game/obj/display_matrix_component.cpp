// A component whose object is shown as it is turned (game/obj/display_matrix_component.h).

#include "game/obj/display_matrix_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/camera_lookaround.h"
#include "game/skater/vehicle_sound.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

} // namespace

// 0x004312A0
CObjectModule* __cdecl CFaceHeadingModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CFaceHeadingModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CFaceHeadingModule*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    return component;
}

// 0x00431240
void CFaceHeadingModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00431250
CFaceHeadingModule* CFaceHeadingModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00431280
void CFaceHeadingModule::TickFrame() { m_entity->m_displayMatrix = m_entity->m_basisFrame; }

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CFaceHeadingModule::Teardown,
         &GameObjects::CFaceHeadingModule::TickFrame,
         Stubs::IgnoreArgument,
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
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
