#pragma once

#include "game/core/alloc.h"

namespace ListKit {

// Walks the items of an intrusive list (Neversoft's ListKit::Hunt<T>), from the list's head node (whose data is
// -1). Fetch the next item before deleting the current one. The original also has a vtable (0x00658AA4: a
// virtual destructor, see SearchDestructors) that it stores in each local search; it changes nothing.
template <typename T>
class Hunt {
public:
    T* HeadElement(MemSys::ListNode* head)
    {
        m_listLink = head;
        return FollowingItem();
    }
    T* FollowingItem()
    {
        MemSys::ListNode* next = m_listLink->next;
        m_listLink = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        return m_listLink ? static_cast<T*>(m_listLink->data) : nullptr;
    }

private:
    MemSys::ListNode* m_listLink = nullptr;
};

// The destructors behind Search's vtable.
struct SearchDestructors {
    void** m_vtable;
    MemSys::ListNode* m_listLink;

    // 0x005BDE30 (only resets the vtable)
    void DestroyBody();
    // 0x005BDE40 (scalar deleting)
    SearchDestructors* Teardown(unsigned int flags);
};

} // namespace Lst
