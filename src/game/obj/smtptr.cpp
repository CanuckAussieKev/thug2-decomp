// Smart pointers: each object keeps a list of the smart pointers to it.

#include <cstdint>

#include "game/obj/smtptr.h"

#include "game/core/mem.h"
#include "game/core/code_address.h"

namespace Obj {

namespace {
extern void** const kRefCountedVtable;
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

namespace Obj {
namespace {

static VtableSlot kRefCountedVtable_slots[] = {&Obj::CRefCounted::ScalarDelete};
void** const kRefCountedVtable = reinterpret_cast<void**>(kRefCountedVtable_slots);

} // namespace
} // namespace Obj
