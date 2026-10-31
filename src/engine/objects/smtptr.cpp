// Smart pointers: each object keeps a list of the smart pointers to it.

#include <cstdint>

#include "engine/objects/smtptr.h"

#include "engine/core/mem.h"
#include "engine/core/code_address.h"

namespace objects {

namespace {
extern void** const kRefCountedVtable;
} // namespace

// 0x004637B0
SharedBase* SharedBase::make_new()
{
    vtable_ = kRefCountedVtable;
    smart_pointers_.head_ = nullptr;
    return this;
}

// 0x00463870
void SharedBase::destruct()
{
    vtable_ = kRefCountedVtable;
    clear_smart_pointers();
}

// 0x00463880
SharedBase* SharedBase::scalar_delete(uint8_t flags)
{
    destruct(); // (inlined in the original)
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x00463820 (each pointer is taken out of the list of the object it points at, which is this one)
void SharedRefList::clear()
{
    for (SharedRef<SharedBase>* pointer = head_; pointer;) {
        SharedRef<SharedBase>* next = pointer->following_;
        if (pointer->owner_object_) {
            if (pointer->preceding_)
                pointer->preceding_->following_ = next;
            else
                head_ = next;
            if (pointer->following_)
                pointer->following_->preceding_ = pointer->preceding_;
            pointer->preceding_ = nullptr;
            pointer->following_ = nullptr;
            pointer->owner_object_ = nullptr;
        }
        pointer->owner_object_ = nullptr;
        pointer = next;
    }
}

// 0x004637C0
void SharedRefList::attach(SharedRef<SharedBase>* pointer)
{
    pointer->following_ = head_;
    if (head_)
        head_->preceding_ = pointer;
    head_ = pointer;
}

// 0x004637E0
void SharedRefList::detach(SharedRef<SharedBase>* pointer)
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

namespace objects {
namespace {

static VtableSlot g_ref_counted_vtable_slots[] = {&objects::SharedBase::scalar_delete};
void** const kRefCountedVtable = reinterpret_cast<void**>(g_ref_counted_vtable_slots);

} // namespace
} // namespace objects
