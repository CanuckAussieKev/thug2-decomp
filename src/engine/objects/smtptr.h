#pragma once

#include <cstdint>

namespace objects {

template <typename T>
class SharedRef;

// The base of objects that smart pointers point at: after the vtable, the list of the smart pointers to it
// (so they can be cleared when it dies instead of dangling).
class SharedBase {
public:
    // 0x004637B0: the constructor (vtable 0x006478E4, no smart pointers; returns this).
    SharedBase* make_new();
    // 0x00463870: the destructor: every smart pointer to it is cleared (0x00463820). 0x00463880: the scalar
    // deleting destructor (vtable slot 0; returns this).
    void destruct();
    SharedBase* scalar_delete(uint8_t flags);
    // 0x00463820: clear every smart pointer to it (each is taken out of its object's list and nulled).
    void clear_smart_pointers();

    // 0x004637C0: onto the front of the list (the pointer's own `prev` isn't touched).
    void attach_shared_ref(SharedRef<SharedBase>* pointer);
    // 0x004637E0: out of the list; its links are cleared.
    void detach_shared_ref(SharedRef<SharedBase>* pointer);

    void** vtable_;
    SharedRef<SharedBase>* smart_pointers_;
};

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

} // namespace Obj
