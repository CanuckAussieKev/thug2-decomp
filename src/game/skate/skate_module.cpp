// The skate module (Mdl::Skate): its list of skaters (skate_module.h).

#include "game/skate/skate_module.h"

#include "hook/game.h"
#include "hook/hook.h"

namespace Mdl {

Skate* Skate::Instance() { return GameVariable<Skate*>(0x007CE478); }

// 0x0041AD90
int Skate::GetNumSkaters()
{
    Mem::ListNode* node = m_skaters.m_node.next;
    int count = 0;
    while (reinterpret_cast<intptr_t>(node->data) != -1 && node) {
        node = node->next;
        ++count;
    }
    return count;
}

Obj::CCompositeObject* Skate::GetSkater(int index)
{
    return GameFunction<Obj::CCompositeObject*(__thiscall*)(Skate*, int)>(0x005665C0)(this, index);
}

} // namespace Mdl

REPLACE(0x0041AD90, &Mdl::Skate::GetNumSkaters);
