// Smart pointers: each object keeps a list of the smart pointers to it.

#include "game/obj/smtptr.h"

#include "game/core/mem.h"
#include "hook/hook.h"

namespace Obj {

namespace {
void** const kRefCountedVtable = reinterpret_cast<void**>(0x006478E4);
} // namespace

// 0x004637B0
CRefCounted* CRefCounted::Construct()
{
    m_vtable = kRefCountedVtable;
    mp_smartPointers = nullptr;
    return this;
}

// 0x00463870
void CRefCounted::Destruct()
{
    m_vtable = kRefCountedVtable;
    ClearSmartPointers();
}

// 0x00463880
CRefCounted* CRefCounted::ScalarDelete(uint8_t flags)
{
    Destruct(); // (inlined in the original)
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00463820 (each pointer is taken out of the list of the object it points at, which is this one)
void CRefCounted::ClearSmartPointers()
{
    for (CSmtPtr<CRefCounted>* pointer = mp_smartPointers; pointer;) {
        CSmtPtr<CRefCounted>* next = pointer->mp_next;
        if (CRefCounted* object = pointer->mp_object) {
            if (pointer->mp_prev)
                pointer->mp_prev->mp_next = next;
            else
                object->mp_smartPointers = next;
            if (pointer->mp_next)
                pointer->mp_next->mp_prev = pointer->mp_prev;
            pointer->mp_prev = nullptr;
            pointer->mp_next = nullptr;
            pointer->mp_object = nullptr;
        }
        pointer->mp_object = nullptr;
        pointer = next;
    }
}

// 0x004637C0
void CRefCounted::AddSmartPointer(CSmtPtr<CRefCounted>* pointer)
{
    pointer->mp_next = mp_smartPointers;
    if (mp_smartPointers)
        mp_smartPointers->mp_prev = pointer;
    mp_smartPointers = pointer;
}

// 0x004637E0
void CRefCounted::RemoveSmartPointer(CSmtPtr<CRefCounted>* pointer)
{
    if (pointer->mp_prev)
        pointer->mp_prev->mp_next = pointer->mp_next;
    else
        mp_smartPointers = pointer->mp_next;
    if (pointer->mp_next)
        pointer->mp_next->mp_prev = pointer->mp_prev;
    pointer->mp_prev = nullptr;
    pointer->mp_next = nullptr;
}

} // namespace Obj

REPLACE(0x004637B0, &Obj::CRefCounted::Construct);
REPLACE(0x00463820, &Obj::CRefCounted::ClearSmartPointers);
REPLACE(0x00463870, &Obj::CRefCounted::Destruct);
REPLACE(0x00463880, &Obj::CRefCounted::ScalarDelete);
REPLACE(0x004637C0, &Obj::CRefCounted::AddSmartPointer);
REPLACE(0x004637E0, &Obj::CRefCounted::RemoveSmartPointer);
REPLACE(0x00416DD0, &Obj::CSmtPtr<Obj::CRefCounted>::Release);
REPLACE(0x0041D1D0, &Obj::CSmtPtr<Obj::CRefCounted>::AssignObject);
