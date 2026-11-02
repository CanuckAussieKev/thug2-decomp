// Smart pointers: each object keeps a list of the smart pointers to it.

#include "engine/objects/smtptr.h"

namespace objects {

// 0x00463820 (each pointer is taken out of the list of the object it points at, which is this one)
void SharedRefList::clear()
{
    for (SharedLink* pointer = head_; pointer;) {
        SharedLink* next = pointer->following_;
        if (pointer->target_) {
            if (pointer->preceding_)
                pointer->preceding_->following_ = next;
            else
                head_ = next;
            if (pointer->following_)
                pointer->following_->preceding_ = pointer->preceding_;
            pointer->preceding_ = nullptr;
            pointer->following_ = nullptr;
            pointer->target_ = nullptr;
        }
        pointer->target_ = nullptr;
        pointer = next;
    }
}

// 0x004637C0
void SharedRefList::attach(SharedLink* pointer)
{
    pointer->following_ = head_;
    if (head_)
        head_->preceding_ = pointer;
    head_ = pointer;
}

// 0x004637E0
void SharedRefList::detach(SharedLink* pointer)
{
    if (pointer->preceding_)
        pointer->preceding_->following_ = pointer->following_;
    else
        head_ = pointer->following_;
    if (pointer->following_)
        pointer->following_->preceding_ = pointer->preceding_;
    pointer->preceding_ = nullptr;
    pointer->following_ = nullptr;
}

} // namespace objects
