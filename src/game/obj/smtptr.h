#pragma once

#include <cstdint>

namespace Obj {

template <typename T>
class CSmtPtr;

// The base of objects that smart pointers point at: after the vtable, the list of the smart pointers to it
// (so they can be cleared when it dies instead of dangling).
class CRefCounted {
public:
    // 0x004637B0: the constructor (vtable 0x006478E4, no smart pointers; returns this).
    CRefCounted* Construct();
    // 0x00463870: the destructor: every smart pointer to it is cleared (0x00463820). 0x00463880: the scalar
    // deleting destructor (vtable slot 0; returns this).
    void Destruct();
    CRefCounted* ScalarDelete(uint8_t flags);
    // 0x00463820: clear every smart pointer to it (each is taken out of its object's list and nulled).
    void ClearSmartPointers();

    // 0x004637C0: onto the front of the list (the pointer's own `prev` isn't touched).
    void AddSmartPointer(CSmtPtr<CRefCounted>* pointer);
    // 0x004637E0: out of the list; its links are cleared.
    void RemoveSmartPointer(CSmtPtr<CRefCounted>* pointer);

    void** m_vtable;
    CSmtPtr<CRefCounted>* mp_smartPointers;
};

// A pointer the object knows about (12 bytes).
template <typename T>
class CSmtPtr {
public:
    CSmtPtr() = default;
    CSmtPtr(const CSmtPtr&) = delete;
    ~CSmtPtr()
    {
        if (mp_object)
            Object()->RemoveSmartPointer(Generic());
    }
    CSmtPtr& operator=(const CSmtPtr& other)
    {
        if (mp_object)
            Object()->RemoveSmartPointer(Generic());
        mp_object = other.mp_object;
        if (mp_object)
            Object()->AddSmartPointer(Generic());
        return *this;
    }
    // 0x00416DD0 (as CSmtPtr<CRefCounted>): let go of the object.
    void Release()
    {
        if (mp_object) {
            Object()->RemoveSmartPointer(Generic());
            mp_object = nullptr;
        }
    }

    // Point at `object` instead: out of the old object's list, onto the front of the new one's.
    void Set(T* object)
    {
        Release();
        mp_object = object;
        if (object)
            Object()->AddSmartPointer(Generic());
    }

    T* Get() const { return mp_object; }
    T* operator->() const { return mp_object; }
    explicit operator bool() const { return mp_object != nullptr; }

    T* mp_object = nullptr;
    CSmtPtr* mp_prev = nullptr;
    CSmtPtr* mp_next = nullptr;

private:
    CRefCounted* Object() const { return reinterpret_cast<CRefCounted*>(mp_object); }
    CSmtPtr<CRefCounted>* Generic() { return reinterpret_cast<CSmtPtr<CRefCounted>*>(this); }
};
static_assert(sizeof(CSmtPtr<CRefCounted>) == 12, "a smart pointer is 12 bytes");

} // namespace Obj
