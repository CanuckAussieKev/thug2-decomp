#pragma once

#include <cstddef>
#include <cstdint>

#include "base/core/layout_32bit.h"
#include "engine/memory/mem.h"

namespace objects {

class SharedBase;

// A smart pointer's link (every SharedRef<T> is one): the object it points at, as its shared base, and its
// neighbours in that object's list of smart pointers. The original's code saw each SharedRef<T> as a
// SharedRef<SharedBase> to link it; here they share this base.
class SharedLink {
public:
    SharedBase* target_ = nullptr; // +0x00
    SharedLink* preceding_ = nullptr;
    SharedLink* following_ = nullptr;
};

// The list of the smart pointers to an object (one word), so they can be cleared when it dies instead of dangling.
// SharedBase's second word (after the vptr).
class SharedRefList {
public:
    // 0x004637C0: onto the front of the list (the pointer's own `prev` isn't touched).
    void attach(SharedLink* pointer);
    // 0x004637E0: out of the list; its links are cleared.
    void detach(SharedLink* pointer);
    // 0x00463820: every smart pointer cleared (each is taken out of the list and nulled).
    void clear();

private:
    SharedLink* head_ = nullptr;
};

constexpr std::size_t kSharedBaseBytes = 8;
// The base of objects that smart pointers point at (vtable 0x006478E4): after the vptr, the list of the smart
// pointers to it. Allocated zeroed from the current heap, as the game's objects are. (Not a memory::HeapObject: an
// empty base would change the member-function-pointer form of the classes whose methods the scripts' member
// function tables name.)
class SharedBase {
public:
    // 0x004637B0: no smart pointers.
    SharedBase() = default;
    // 0x00463870 (0x00463880 the deleting one): every smart pointer to it is cleared (0x00463820).
    virtual ~SharedBase() { clear_smart_pointers(); }
    SharedBase(const SharedBase&) = delete;
    SharedBase& operator=(const SharedBase&) = delete;

    static void* operator new(std::size_t size) noexcept
    {
        return memory::alloc_zeroed(static_cast<unsigned int>(size));
    }
    static void operator delete(void* memory) { memory::dealloc(memory); }
    static void* operator new(std::size_t /*unused*/, void* place) noexcept { return place; }
    static void operator delete(void* /*unused*/, void* /*unused*/) noexcept {}

    // Clear every smart pointer to it / attach / detach one (SharedRefList's).
    void clear_smart_pointers() { smart_pointers_.clear(); }
    void attach_shared_ref(SharedLink* pointer) { smart_pointers_.attach(pointer); }
    void detach_shared_ref(SharedLink* pointer) { smart_pointers_.detach(pointer); }

    // The fields other code reads and sets.
    [[nodiscard]] const SharedRefList& smart_pointers() const { return smart_pointers_; }
    void set_smart_pointers(const SharedRefList& value) { smart_pointers_ = value; }

private:
    SharedRefList smart_pointers_; // +0x04
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(SharedBase, smart_pointers_) == 4 && sizeof(SharedBase) == kSharedBaseBytes), "layout must match the original");
    }
};

// A pointer the object knows about (12 bytes).
template <typename T>
class SharedRef : public SharedLink {
public:
    SharedRef() = default;
    SharedRef(const SharedRef&) = delete;
    ~SharedRef()
    {
        if (target_) {
            target_->detach_shared_ref(this);
        }
    }
    SharedRef& operator=(const SharedRef& other)
    {
        if (this == &other) {
            return *this;
        }
        if (target_) {
            target_->detach_shared_ref(this);
        }
        target_ = other.target_;
        if (target_) {
            target_->attach_shared_ref(this);
        }
        return *this;
    }
    // 0x00416DD0 (as SharedRef<SharedBase>): let go of the object.
    void get_release()
    {
        if (target_) {
            target_->detach_shared_ref(this);
            target_ = nullptr;
        }
    }

    // 0x0042B580 (one instance, shared): the in-place default constructor - all three words cleared (returns this).
    SharedRef* make_new()
    {
        following_ = nullptr;
        preceding_ = nullptr;
        target_ = nullptr;
        return this;
    }
    // In-place constructors (return this): from a pointer, from another smart pointer (the screen elements'
    // instances are 0x004A7820 / 0x004A7690).
    SharedRef* construct_from(T* object)
    {
        preceding_ = nullptr;
        following_ = nullptr;
        target_ = object;
        if (object) {
            target_->attach_shared_ref(this);
        }
        return this;
    }
    SharedRef* construct_copy(const SharedRef* other) { return construct_from(other->get_pull()); }
    // Assignment as a function (returns this; the screen elements' instance is 0x004A8F60). Unlike operator=,
    // the old object is let go first, so a pointer assigned to itself ends up null.
    SharedRef* get_assign(const SharedRef* other)
    {
        get_release();
        target_ = other->target_;
        if (target_) {
            target_->attach_shared_ref(this);
        }
        return this;
    }

    // 0x0041D1D0 (as SharedRef<SharedBase>): Set, returning this (the assignment from a pointer).
    SharedRef* assign_object(T* object)
    {
        assign_value(object);
        return this;
    }
    // Point at `object` instead: out of the old object's list, onto the front of the new one's.
    void assign_value(T* object)
    {
        get_release();
        target_ = object;
        if (object) {
            target_->attach_shared_ref(this);
        }
    }

    [[nodiscard]] T* get_pull() const { return static_cast<T*>(target_); }
    // 0x00442670 (the screen elements' instance, written out in the unoptimised lens flare code).
    [[nodiscard]] bool is_null() const { return target_ == nullptr; }
    T* operator->() const { return get_pull(); }
    explicit operator bool() const { return target_ != nullptr; }
};
constexpr std::size_t kSharedRefBytes = 12;
static_assert(core::layout_32bit(sizeof(SharedRef<SharedBase>) == kSharedRefBytes), "a smart pointer is 12 bytes");

} // namespace objects
