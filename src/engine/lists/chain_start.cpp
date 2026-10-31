// Intrusive list heads (ListKit::Foremost<T>). See chain_start.h.

#include "engine/lists/chain_start.h"

namespace ListKit {

namespace {

// `node` linked in before `next`.
void InsertBefore(MemSys::ListNode* next, MemSys::ListNode* node)
{
    node->prev = next->prev;
    node->next = next;
    next->prev->next = node;
    next->prev = node;
}

// `node` linked in after `prev`.
void InsertAfter(MemSys::ListNode* prev, MemSys::ListNode* node)
{
    node->prev = prev;
    node->next = prev->next;
    prev->next->prev = node;
    prev->next = node;
}

} // namespace

// 0x0045A540
void Foremost::AppendVertex(MemSys::ListNode* node)
{
    int priority = node->priority;
    for (MemSys::ListNode* next = m_listLink.next;; next = next->next) {
        if (next->data == reinterpret_cast<void*>(-1) || !next)
            break;
        if (next->priority <= priority) {
            InsertBefore(next, node);
            return;
        }
    }
    InsertBefore(&m_listLink, node); // at the end
}

// 0x0045AEB0
bool Foremost::InsertByUniqueOrder(MemSys::ListNode* node)
{
    int priority = node->priority;
    for (MemSys::ListNode* next = m_listLink.next;; next = next->next) {
        if (next->data == reinterpret_cast<void*>(-1) || !next)
            break;
        if (next->priority == priority)
            return false;
        if (next->priority > priority) {
            InsertBefore(next, node);
            return true;
        }
    }
    InsertBefore(&m_listLink, node); // at the end
    return true;
}

// 0x00457350
void Foremost::AppendNode(MemSys::ListNode* node)
{
    int priority = node->priority;
    for (MemSys::ListNode* prev = m_listLink.prev;; prev = prev->prev) {
        if (prev->data == reinterpret_cast<void*>(-1) || !prev)
            break;
        if (prev->priority >= priority) {
            InsertAfter(prev, node);
            return;
        }
    }
    InsertAfter(&m_listLink, node); // at the front
}

} // namespace Lst
