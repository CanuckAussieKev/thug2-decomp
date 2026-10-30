// A component whose object is shown as it is turned (game/obj/display_matrix_component.h).

#include "game/obj/display_matrix_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace Obj {

namespace {

extern void** const kVtable;

} // namespace

// 0x004312A0
CBaseComponent* __cdecl CDisplayMatrixComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CDisplayMatrixComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CDisplayMatrixComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x00431240
void CDisplayMatrixComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00431250
CDisplayMatrixComponent* CDisplayMatrixComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00431280
void CDisplayMatrixComponent::Update() { m_object->m_displayMatrix = m_object->m_matrix; }

} // namespace Obj

REPLACE(0x004312A0, Obj::CDisplayMatrixComponent::Create);
REPLACE(0x00431240, &Obj::CDisplayMatrixComponent::DestroyBody);
REPLACE(0x00431250, &Obj::CDisplayMatrixComponent::Destroy);
REPLACE(0x00431280, &Obj::CDisplayMatrixComponent::Update);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00646464,
    &Obj::CDisplayMatrixComponent::Destroy);

} // namespace
} // namespace Obj
