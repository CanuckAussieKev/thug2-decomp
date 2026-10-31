#pragma once

#include "engine/core/arena_alloc.h"

namespace lists {

// Walks the items of an intrusive list (Neversoft's lists::Hunt<T>), from the list's head node (whose data is
// -1). Fetch the next item before deleting the current one. The original also has a vtable (0x00658AA4: a
// virtual destructor, see SearchDestructors) that it stores in each local search; it changes nothing.
template <typename T>
class Hunt {
public:
    T* head_element(memory::ListNode* head)
    {
        list_link_ = head;
        return following_item();
    }
    T* following_item()
    {
        memory::ListNode* next = list_link_->next;
        list_link_ = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        return list_link_ ? static_cast<T*>(list_link_->data) : nullptr;
    }

private:
    memory::ListNode* list_link_ = nullptr;
};

// The destructors behind Search's vtable.
struct SearchDestructors {
    void** vtable_;
    memory::ListNode* list_link_;

    // 0x005BDE30 (only resets the vtable)
    void destroy_body();
    // 0x005BDE40 (scalar deleting)
    SearchDestructors* teardown(unsigned int flags);
};

} // namespace lists
