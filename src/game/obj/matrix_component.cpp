// A component whose matrix becomes its object's display matrix (GameObjects::CMatrixComponent).

#include "game/obj/matrix_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

} // namespace

// 0x0041CB20
CObjectModule* __cdecl CMatrixComponent::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CMatrixComponent));
    return memory ? static_cast<CMatrixComponent*>(memory)->Construct() : nullptr;
}

// 0x0041CAD0
CMatrixComponent* CMatrixComponent::Construct()
{
    CObjectModule::Construct();
    m_vtable = kVtable;
    m_type = kType;
    for (int r = 0; r < 4; ++r)
        m_basisFrame.row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
    return this;
}

// 0x0041CA60
void CMatrixComponent::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041CA70
CMatrixComponent* CMatrixComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0041CAA0
void CMatrixComponent::Update() { m_object->m_displayMatrix = m_basisFrame; }

// 0x0041CAC0
void CMatrixComponent::SetMatrix(const Maths::Matrix* matrix) { m_basisFrame = *matrix; }

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CMatrixComponent::Destroy,
         &GameObjects::CMatrixComponent::Update,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
