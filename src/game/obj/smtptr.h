#pragma once

namespace Obj {

template <typename T>
class CSmtPtr;

// The base of objects that smart pointers point at: after the vtable, the list of the smart pointers to it
// (so they can be cleared when it dies instead of dangling).
class CRefCounted {
public:
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
