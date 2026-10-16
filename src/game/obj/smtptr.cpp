// Smart pointers: each object keeps a list of the smart pointers to it.

#include "game/obj/smtptr.h"

#include "hook/hook.h"

namespace Obj {

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

REPLACE(0x004637C0, &Obj::CRefCounted::AddSmartPointer);
REPLACE(0x004637E0, &Obj::CRefCounted::RemoveSmartPointer);
REPLACE(0x00416DD0, &Obj::CSmtPtr<Obj::CRefCounted>::Release);
