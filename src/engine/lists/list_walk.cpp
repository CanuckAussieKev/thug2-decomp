// List searches (ListKit::Hunt<T>): the iteration is inline (list_walk.h); here are the destructors.

#include "engine/lists/list_walk.h"

#include "engine/core/manager.h"
#include "engine/core/code_address.h"

namespace ListKit {

namespace {
extern void** const kSearchVtable;
} // namespace

// 0x005BDE30
void SearchDestructors::DestroyBody() { m_vtable = kSearchVtable; }

// 0x005BDE40
SearchDestructors* SearchDestructors::Teardown(unsigned int flags)
{
    m_vtable = kSearchVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

namespace {
static VtableSlot kSearchVtable_slots[] = {&ListKit::SearchDestructors::Teardown};
void** const kSearchVtable = reinterpret_cast<void**>(kSearchVtable_slots);
} // namespace

} // namespace Lst
