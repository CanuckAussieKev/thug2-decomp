// Intrusive list heads (Lst::Head<T>). See head.h.

#include "game/lst/head.h"

#include "hook/hook.h"

namespace Lst {

namespace {

// `node` linked in before `next`.
void InsertBefore(Mem::ListNode* next, Mem::ListNode* node)
{
    node->prev = next->prev;
    node->next = next;
    next->prev->next = node;
    next->prev = node;
}

} // namespace

// 0x0045A540
void Head::AddNode(Mem::ListNode* node)
{
    int priority = node->priority;
    for (Mem::ListNode* next = m_node.next;; next = next->next) {
        if (next->data == reinterpret_cast<void*>(-1) || !next)
            break;
        if (next->priority <= priority) {
            InsertBefore(next, node);
            return;
        }
    }
    InsertBefore(&m_node, node); // at the end
}

} // namespace Lst

REPLACE(0x0045A540, &Lst::Head::AddNode);
