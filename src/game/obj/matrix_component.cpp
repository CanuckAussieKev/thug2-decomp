// A component whose matrix becomes its object's display matrix (Obj::CMatrixComponent).

#include "game/obj/matrix_component.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00645F24);

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
