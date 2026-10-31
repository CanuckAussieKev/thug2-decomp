// Smart pointers: each object keeps a list of the smart pointers to it.

#include <cstdint>

#include "game/obj/smtptr.h"

#include "game/core/mem.h"
#include "game/core/code_address.h"

namespace GameObjects {

namespace {
extern void** const kRefCountedVtable;
} // namespace

// 0x004637B0
CSharedBase* CSharedBase::MakeNew()
{
    m_vtable = kRefCountedVtable;
    m_smartPointers = nullptr;
    return this;
}

// 0x00463870
void CSharedBase::Destruct()
{
    m_vtable = kRefCountedVtable;
    ClearSmartPointers();
}

// 0x00463880
CSharedBase* CSharedBase::ScalarDelete(uint8_t flags)
{
    Destruct(); // (inlined in the original)
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00463820 (each pointer is taken out of the list of the object it points at, which is this one)
void CSharedBase::ClearSmartPointers()
{
    for (CSharedRef<CSharedBase>* pointer = m_smartPointers; pointer;) {
        CSharedRef<CSharedBase>* next = pointer->m_following;
        if (CSharedBase* object = pointer->m_ownerObject) {
            if (pointer->m_preceding)
                pointer->m_preceding->m_following = next;
            else
                object->m_smartPointers = next;
            if (pointer->m_following)
                pointer->m_following->m_preceding = pointer->m_preceding;
            pointer->m_preceding = nullptr;
            pointer->m_following = nullptr;
            pointer->m_ownerObject = nullptr;
        }
        pointer->m_ownerObject = nullptr;
        pointer = next;
    }
}

// 0x004637C0
void CSharedBase::AttachSharedRef(CSharedRef<CSharedBase>* pointer)
{
    pointer->m_following = m_smartPointers;
    if (m_smartPointers)
        m_smartPointers->m_preceding = pointer;
    m_smartPointers = pointer;
}

// 0x004637E0
void CSharedBase::DetachSharedRef(CSharedRef<CSharedBase>* pointer)
{
    if (pointer->m_preceding)
        pointer->m_preceding->m_following = pointer->m_following;
    else
        m_smartPointers = pointer->m_following;
    if (pointer->m_following)
        pointer->m_following->m_preceding = pointer->m_preceding;
    pointer->m_preceding = nullptr;
    pointer->m_following = nullptr;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kRefCountedVtable_slots[] = {&GameObjects::CSharedBase::ScalarDelete};
void** const kRefCountedVtable = reinterpret_cast<void**>(kRefCountedVtable_slots);

} // namespace
} // namespace Obj
