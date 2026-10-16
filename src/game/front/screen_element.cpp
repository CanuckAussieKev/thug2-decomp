// Screen elements: the pieces rewritten so far.

#include "game/front/screen_element.h"

#include <cstddef>

#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

static_assert(offsetof(CScreenElement, m_flags) == 0x08 && offsetof(CScreenElement, mp_parent) == 0x4C,
              "layout must match the original");

// 0x004A8F40
bool CScreenElement::IsHidden() const
{
    if (m_flags & kHidden)
        return true;
    return mp_parent && mp_parent->IsHidden();
}

void GetElement(ElementPtr* out, uint32_t id)
{
    void* manager = GameVariable<void*>(0x00701440);
    GameFunction<ElementPtr*(__thiscall*)(void*, ElementPtr*, uint32_t, int)>(0x004AAE20)(manager, out, id, 0);
}

} // namespace Front

REPLACE(0x004A8F40, &Front::CScreenElement::IsHidden);
