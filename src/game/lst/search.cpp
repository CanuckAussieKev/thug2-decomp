// List searches (Lst::Search<T>): the iteration is inline (search.h); here are the destructors.

#include "game/lst/search.h"

#include "game/core/manager.h"
#include "hook/hook.h"

namespace Lst {

namespace {
void** const kSearchVtable = reinterpret_cast<void**>(0x00658AA4);
} // namespace

// 0x005BDE30
void SearchDestructors::DestroyBody() { m_vtable = kSearchVtable; }

// 0x005BDE40
SearchDestructors* SearchDestructors::Destroy(unsigned int flags)
{
    m_vtable = kSearchVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

} // namespace Lst

REPLACE(0x005BDE30, &Lst::SearchDestructors::DestroyBody);
REPLACE(0x005BDE40, &Lst::SearchDestructors::Destroy);
