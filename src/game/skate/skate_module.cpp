// The skate module (GameModules::Skate): the singleton and its skater count (skate_module.h).

#include <cstdint>

#include "game/skate/skate_module.h"

namespace GameModules {

Skate* g_skateModule;

Skate* Skate::Sole() { return g_skateModule; }

// 0x0041AD90
int Skate::CountRiders()
{
    MemSys::ListNode* node = m_riders.m_listLink.next;
    int count = 0;
    while (reinterpret_cast<intptr_t>(node->data) != -1 && node) {
        node = node->next;
        ++count;
    }
    return count;
}

} // namespace Mdl
