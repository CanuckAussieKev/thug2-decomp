// The skate module (Mdl::Skate): the singleton and its skater count (skate_module.h).

#include <cstdint>

#include "game/skate/skate_module.h"

namespace Mdl {

Skate* g_skateModule;

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
