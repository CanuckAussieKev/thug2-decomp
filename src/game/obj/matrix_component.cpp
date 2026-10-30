// A component whose matrix becomes its object's display matrix (Obj::CMatrixComponent).

#include "game/obj/matrix_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
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

} // namespace

// 0x0041CB20
CBaseComponent* __cdecl CMatrixComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CMatrixComponent));
    return memory ? static_cast<CMatrixComponent*>(memory)->Construct() : nullptr;
}

// 0x0041CAD0
CMatrixComponent* CMatrixComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    for (int r = 0; r < 4; ++r)
        m_matrix.row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
    return this;
}

// 0x0041CA60
void CMatrixComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041CA70
CMatrixComponent* CMatrixComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041CAA0
void CMatrixComponent::Update() { m_object->m_displayMatrix = m_matrix; }

// 0x0041CAC0
void CMatrixComponent::SetMatrix(const Mth::Matrix* matrix) { m_matrix = *matrix; }

} // namespace Obj

REPLACE(0x0041CB20, Obj::CMatrixComponent::Create);
REPLACE(0x0041CAD0, &Obj::CMatrixComponent::Construct);
REPLACE(0x0041CA60, &Obj::CMatrixComponent::DestroyBody);
REPLACE(0x0041CA70, &Obj::CMatrixComponent::Destroy);
REPLACE(0x0041CAA0, &Obj::CMatrixComponent::Update);
REPLACE(0x0041CAC0, &Obj::CMatrixComponent::SetMatrix);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00645F24,
    &Obj::CMatrixComponent::Destroy,
    &Obj::CMatrixComponent::Update,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    Stubs::IgnoreArgument,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
