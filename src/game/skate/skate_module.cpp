// The skate module (modules::Skate): the singleton and its skater count (skate_module.h).

#include <cstdint>

#include "game/skate/skate_module.h"
#include "engine/core/pointer_cell.h"

namespace modules {

Skate* g_skate_module;

Skate* Skate::sole() { return g_skate_module; }

// 0x0041AD90
int Skate::count_riders()
{
    const memory::ListNode* node = riders_.list_link_.next;
    int count = 0;
    while (static_cast<intptr_t>(cell::address(node->data)) != -1 && node) {
        node = node->next;
        ++count;
    }
    return count;
}

} // namespace modules
