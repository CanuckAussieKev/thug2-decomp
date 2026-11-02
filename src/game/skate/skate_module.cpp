// The skate module (modules::Skate): the singleton and its skater count (skate_module.h).

#include "game/skate/skate_module.h"
#include "engine/core/arena_alloc.h"
namespace modules {

// 0x0041AD90
int Skate::count_riders()
{
    const memory::ListNode* node = riders_.list_link_.next;
    int count = 0;
    while (node->data != memory::ListNode::head_marker() && node) {
        node = node->next;
        ++count;
    }
    return count;
}

} // namespace modules
