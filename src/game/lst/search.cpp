// List searches (Lst::Search<T>): the iteration is inline (search.h); here are the destructors.

#include "game/lst/search.h"

#include "game/core/manager.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace Lst {

namespace {
extern void** const kSearchVtable;
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

namespace {
GAME_VTABLE(kSearchVtable, 0x00658AA4, &Lst::SearchDestructors::Destroy);
} // namespace

} // namespace Lst

REPLACE(0x005BDE30, &Lst::SearchDestructors::DestroyBody);
REPLACE(0x005BDE40, &Lst::SearchDestructors::Destroy);
