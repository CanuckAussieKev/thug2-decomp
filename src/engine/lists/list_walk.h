#pragma once

#include "engine/core/arena_alloc.h"

namespace lists {

// Walks the items of an intrusive list, from the list's head node (whose data is -1). Fetch the next item before
// deleting the current one. (The original's searches each have a vtable holding only a destructor that resets it -
// this one's 0x00658AA4: 0x005BDE30 / 0x005BDE40; it changes nothing, so it isn't kept.)
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
        list_link_ = next->data == memory::ListNode::head_marker() ? nullptr : next;
        return list_link_ ? static_cast<T*>(list_link_->data) : nullptr;
    }

private:
    memory::ListNode* list_link_ = nullptr;
};

} // namespace lists
