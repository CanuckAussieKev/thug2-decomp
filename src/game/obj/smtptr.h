#pragma once

#include <cstdint>

namespace GameObjects {

template <typename T>
class CSharedRef;

// The base of objects that smart pointers point at: after the vtable, the list of the smart pointers to it
// (so they can be cleared when it dies instead of dangling).
class CSharedBase {
public:
    // 0x004637B0: the constructor (vtable 0x006478E4, no smart pointers; returns this).
    CSharedBase* MakeNew();
    // 0x00463870: the destructor: every smart pointer to it is cleared (0x00463820). 0x00463880: the scalar
    // deleting destructor (vtable slot 0; returns this).
    void Destruct();
    CSharedBase* ScalarDelete(uint8_t flags);
    // 0x00463820: clear every smart pointer to it (each is taken out of its object's list and nulled).
    void ClearSmartPointers();

    // 0x004637C0: onto the front of the list (the pointer's own `prev` isn't touched).
    void AttachSharedRef(CSharedRef<CSharedBase>* pointer);
    // 0x004637E0: out of the list; its links are cleared.
    void DetachSharedRef(CSharedRef<CSharedBase>* pointer);

    void** m_vtable;
    CSharedRef<CSharedBase>* mp_smartPointers;
};

// A pointer the object knows about (12 bytes).
template <typename T>
class CSharedRef {
public:
    CSharedRef() = default;
    CSharedRef(const CSharedRef&) = delete;
    ~CSharedRef()
    {
        if (m_ownerObject)
            Object()->DetachSharedRef(Generic());
    }
    CSharedRef& operator=(const CSharedRef& other)
    {
        if (m_ownerObject)
            Object()->DetachSharedRef(Generic());
        m_ownerObject = other.m_ownerObject;
        if (m_ownerObject)
            Object()->AttachSharedRef(Generic());
        return *this;
    }
    // 0x00416DD0 (as CSharedRef<CSharedBase>): let go of the object.
    void Release()
    {
        if (m_ownerObject) {
            Object()->DetachSharedRef(Generic());
            m_ownerObject = nullptr;
        }
    }

    // 0x0042B580 (one instance, shared): the in-place default constructor - all three words cleared (returns this).
    CSharedRef* MakeNew()
    {
        m_following = nullptr;
        m_preceding = nullptr;
        m_ownerObject = nullptr;
        return this;
    }
    // In-place constructors (return this): from a pointer, from another smart pointer (the screen elements'
    // instances are 0x004A7820 / 0x004A7690).
    CSharedRef* ConstructFrom(T* object)
    {
        m_preceding = nullptr;
        m_following = nullptr;
        m_ownerObject = object;
        if (object)
            Object()->AttachSharedRef(Generic());
        return this;
    }
    CSharedRef* ConstructCopy(const CSharedRef* other) { return ConstructFrom(other->m_ownerObject); }
    // Assignment as a function (returns this; the screen elements' instance is 0x004A8F60). Unlike operator=,
    // the old object is let go first, so a pointer assigned to itself ends up null.
    CSharedRef* Assign(const CSharedRef* other)
    {
        Release();
        m_ownerObject = other->m_ownerObject;
        if (m_ownerObject)
            Object()->AttachSharedRef(Generic());
        return this;
    }

    // 0x0041D1D0 (as CSharedRef<CSharedBase>): Set, returning this (the assignment from a pointer).
    CSharedRef* AssignObject(T* object)
    {
        AssignValue(object);
        return this;
    }
    // Point at `object` instead: out of the old object's list, onto the front of the new one's.
    void AssignValue(T* object)
    {
        Release();
        m_ownerObject = object;
        if (object)
            Object()->AttachSharedRef(Generic());
    }

    T* Pull() const { return m_ownerObject; }
    // 0x00442670 (the screen elements' instance, written out in the unoptimised lens flare code).
    bool IsNull() const { return m_ownerObject == nullptr; }
    T* operator->() const { return m_ownerObject; }
    explicit operator bool() const { return m_ownerObject != nullptr; }

    T* m_ownerObject = nullptr;
    CSharedRef* m_preceding = nullptr;
    CSharedRef* m_following = nullptr;

private:
    CSharedBase* Object() const { return reinterpret_cast<CSharedBase*>(m_ownerObject); }
    CSharedRef<CSharedBase>* Generic() { return reinterpret_cast<CSharedRef<CSharedBase>*>(this); }
};
static_assert(sizeof(CSharedRef<CSharedBase>) == 12, "a smart pointer is 12 bytes");

} // namespace Obj
