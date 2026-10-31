// List searches (ListKit::Hunt<T>): the iteration is inline (search.h); here are the destructors.

#include "game/lst/search.h"

#include "game/core/manager.h"
#include "game/core/code_address.h"

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
