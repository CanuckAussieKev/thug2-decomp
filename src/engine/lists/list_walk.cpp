// List searches (lists::Hunt<T>): the iteration is inline (list_walk.h); here are the destructors.

#include "engine/lists/list_walk.h"

#include "engine/core/manager.h"
#include "engine/core/code_address.h"

namespace lists {

namespace {
extern void** const kSearchVtable;
} // namespace

// 0x005BDE30
void SearchDestructors::destroy_body() { vtable_ = kSearchVtable; }

// 0x005BDE40
SearchDestructors* SearchDestructors::teardown(unsigned int flags)
{
    vtable_ = kSearchVtable;
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

namespace {
static VtableSlot g_search_vtable_slots[] = {&lists::SearchDestructors::teardown};
void** const kSearchVtable = reinterpret_cast<void**>(g_search_vtable_slots);
} // namespace

} // namespace lists
