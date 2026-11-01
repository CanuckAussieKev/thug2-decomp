#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"

namespace objects {

template <typename T>
class SharedRef;
class SharedBase;

// The list of the smart pointers to an object (one word), so they can be cleared when it dies instead of dangling.
// SharedBase's second word (after the vptr).
class SharedRefList {
public:
    // 0x004637C0: onto the front of the list (the pointer's own `prev` isn't touched).
    void attach(SharedRef<SharedBase>* pointer);
    // 0x004637E0: out of the list; its links are cleared.
    void detach(SharedRef<SharedBase>* pointer);
    // 0x00463820: every smart pointer cleared (each is taken out of the list and nulled).
    void clear();

    SharedRef<SharedBase>* head_ = nullptr;
};

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
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void*, void*) noexcept {}

    // Clear every smart pointer to it / attach / detach one (SharedRefList's).
    void clear_smart_pointers() { smart_pointers_.clear(); }
    void attach_shared_ref(SharedRef<SharedBase>* pointer) { smart_pointers_.attach(pointer); }
    void detach_shared_ref(SharedRef<SharedBase>* pointer) { smart_pointers_.detach(pointer); }

    SharedRefList smart_pointers_; // +0x04
};
static_assert(offsetof(SharedBase, smart_pointers_) == 4 && sizeof(SharedBase) == 8, "layout must match the original");

// A pointer the object knows about (12 bytes).
template <typename T>
class SharedRef {
public:
    SharedRef() = default;
    SharedRef(const SharedRef&) = delete;
    ~SharedRef()
    {
        if (owner_object_)
            get_object()->detach_shared_ref(get_generic());
    }
    SharedRef& operator=(const SharedRef& other)
    {
        if (this == &other)
            return *this;
        if (owner_object_)
            get_object()->detach_shared_ref(get_generic());
        owner_object_ = other.owner_object_;
        if (owner_object_)
            get_object()->attach_shared_ref(get_generic());
        return *this;
    }
    // 0x00416DD0 (as SharedRef<SharedBase>): let go of the object.
    void get_release()
    {
        if (owner_object_) {
            get_object()->detach_shared_ref(get_generic());
            owner_object_ = nullptr;
        }
    }

    // 0x0042B580 (one instance, shared): the in-place default constructor - all three words cleared (returns this).
    SharedRef* make_new()
    {
        following_ = nullptr;
        preceding_ = nullptr;
        owner_object_ = nullptr;
        return this;
    }
    // In-place constructors (return this): from a pointer, from another smart pointer (the screen elements'
    // instances are 0x004A7820 / 0x004A7690).
    SharedRef* construct_from(T* object)
    {
        preceding_ = nullptr;
        following_ = nullptr;
        owner_object_ = object;
        if (object)
            get_object()->attach_shared_ref(get_generic());
        return this;
    }
    SharedRef* construct_copy(const SharedRef* other) { return construct_from(other->owner_object_); }
    // Assignment as a function (returns this; the screen elements' instance is 0x004A8F60). Unlike operator=,
    // the old object is let go first, so a pointer assigned to itself ends up null.
    SharedRef* get_assign(const SharedRef* other)
    {
        get_release();
        owner_object_ = other->owner_object_;
        if (owner_object_)
            get_object()->attach_shared_ref(get_generic());
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
        owner_object_ = object;
        if (object)
            get_object()->attach_shared_ref(get_generic());
    }

    T* get_pull() const { return owner_object_; }
    // 0x00442670 (the screen elements' instance, written out in the unoptimised lens flare code).
    bool is_null() const { return owner_object_ == nullptr; }
    T* operator->() const { return owner_object_; }
    explicit operator bool() const { return owner_object_ != nullptr; }

    T* owner_object_ = nullptr;
    SharedRef* preceding_ = nullptr;
    SharedRef* following_ = nullptr;

private:
    SharedBase* get_object() const { return reinterpret_cast<SharedBase*>(owner_object_); }
    SharedRef<SharedBase>* get_generic() { return reinterpret_cast<SharedRef<SharedBase>*>(this); }
};
static_assert(sizeof(SharedRef<SharedBase>) == 12, "a smart pointer is 12 bytes");

} // namespace objects
