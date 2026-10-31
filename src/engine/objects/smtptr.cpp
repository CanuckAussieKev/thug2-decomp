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
    smart_pointers_ = nullptr;
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
void SharedBase::clear_smart_pointers()
{
    for (SharedRef<SharedBase>* pointer = smart_pointers_; pointer;) {
        SharedRef<SharedBase>* next = pointer->following_;
        if (SharedBase* object = pointer->owner_object_) {
            if (pointer->preceding_)
                pointer->preceding_->following_ = next;
            else
                object->smart_pointers_ = next;
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
void SharedBase::attach_shared_ref(SharedRef<SharedBase>* pointer)
{
    pointer->following_ = smart_pointers_;
    if (smart_pointers_)
        smart_pointers_->preceding_ = pointer;
    smart_pointers_ = pointer;
}

// 0x004637E0
void SharedBase::detach_shared_ref(SharedRef<SharedBase>* pointer)
{
    if (pointer->preceding_)
        pointer->preceding_->following_ = pointer->following_;
    else
        smart_pointers_ = pointer->following_;
    if (pointer->following_)
        pointer->following_->preceding_ = pointer->preceding_;
    pointer->preceding_ = nullptr;
    pointer->following_ = nullptr;
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_ref_counted_vtable_slots[] = {&objects::SharedBase::scalar_delete};
void** const kRefCountedVtable = reinterpret_cast<void**>(g_ref_counted_vtable_slots);

} // namespace
} // namespace Obj
