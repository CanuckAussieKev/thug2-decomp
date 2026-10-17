// Screen elements: the pieces rewritten so far.

#include "game/front/screen_element.h"

#include <cstddef>

#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

static_assert(offsetof(CScreenElement, m_flags) == 0x08 && offsetof(CScreenElement, mp_parent) == 0x4C &&
                  offsetof(CScreenElement, m_alpha) == 0x114 && offsetof(CScreenElement, m_x) == 0x134 &&
                  offsetof(CScreenElement, m_scaleX) == 0x13C && offsetof(CScreenElement, m_unknown144) == 0x144 &&
                  offsetof(CScreenElement, m_unknown160) == 0x160 && sizeof(CScreenElement) == 0x178,
              "layout must match the original");

// 0x004A87A0
double CScreenElement::GetLayoutWidth() const
{
    return static_cast<double>(m_unknown160) * m_unknown144;
}

// 0x004A87B0
double CScreenElement::GetLayoutHeight() const
{
    return static_cast<double>(m_unknown164) * m_unknown148;
}

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
REPLACE(0x004A87A0, &Front::CScreenElement::GetLayoutWidth);
REPLACE(0x004A87B0, &Front::CScreenElement::GetLayoutHeight);
