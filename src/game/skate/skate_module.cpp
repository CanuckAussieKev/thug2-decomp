// The skate module (Mdl::Skate): the singleton and its skater count (skate_module.h).

#include "game/skate/skate_module.h"

#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Mdl {

GAME_GLOBAL(Skate*, g_skateModule, 0x007CE478);

Skate* Skate::Instance() { return g_skateModule; }

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

} // namespace Mdl

REPLACE(0x0041AD90, &Mdl::Skate::GetNumSkaters);
