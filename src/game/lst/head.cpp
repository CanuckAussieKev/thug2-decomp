// Intrusive list heads (Lst::Head<T>). See head.h.

#include "game/lst/head.h"

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

// `node` linked in after `prev`.
void InsertAfter(Mem::ListNode* prev, Mem::ListNode* node)
{
    node->prev = prev;
    node->next = prev->next;
    prev->next->prev = node;
    prev->next = node;
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

// 0x0045AEB0
bool Head::AddUniqueSequence(Mem::ListNode* node)
{
    int priority = node->priority;
    for (Mem::ListNode* next = m_node.next;; next = next->next) {
        if (next->data == reinterpret_cast<void*>(-1) || !next)
            break;
        if (next->priority == priority)
            return false;
        if (next->priority > priority) {
            InsertBefore(next, node);
            return true;
        }
    }
    InsertBefore(&m_node, node); // at the end
    return true;
}

// 0x00457350
void Head::AddNodeFromTail(Mem::ListNode* node)
{
    int priority = node->priority;
    for (Mem::ListNode* prev = m_node.prev;; prev = prev->prev) {
        if (prev->data == reinterpret_cast<void*>(-1) || !prev)
            break;
        if (prev->priority >= priority) {
            InsertAfter(prev, node);
            return;
        }
    }
    InsertAfter(&m_node, node); // at the front
}

} // namespace Lst
