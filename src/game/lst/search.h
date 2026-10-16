#pragma once

#include "game/core/alloc.h"

namespace Lst {

// Walks the items of an intrusive list (Neversoft's Lst::Search<T>), from the list's head node (whose data is
// -1). Fetch the next item before deleting the current one. The original also has a vtable (0x00658AA4: a
// virtual destructor, see SearchDestructors) that it stores in each local search; it changes nothing.
template <typename T>
class Search {
public:
    T* FirstItem(Mem::ListNode* head)
    {
        m_node = head;
        return NextItem();
    }
    T* NextItem()
    {
        Mem::ListNode* next = m_node->next;
        m_node = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        return m_node ? static_cast<T*>(m_node->data) : nullptr;
    }

private:
    Mem::ListNode* m_node = nullptr;
};

// The destructors behind Search's vtable.
struct SearchDestructors {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x005BDE30 (only resets the vtable)
    void DestroyBody();
    // 0x005BDE40 (scalar deleting)
    SearchDestructors* Destroy(unsigned int flags);
};

} // namespace Lst
