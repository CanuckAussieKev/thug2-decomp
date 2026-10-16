#pragma once

// Neversoft's Spt::SingletonPtr<T>: a reference to one of the reference-counted singletons (the managers and
// modules Sys::Main makes), taken when it's constructed - T's Create(create): with `create` the first one makes
// the instance - and dropped when it's destroyed (T's Release: the last one deletes the instance).
//
// The game's SingletonPtr objects are locals (Sys::Main, the movie player, the network code...): their
// constructors are inline (only one isn't, 0x004E1A90), and what's left out of line are each instance's
// destructor (called from the exception-unwind code of the functions with such a local) and its scalar deleting
// destructor, the only entry of each instance's vtable. Each instance here is named by a Singleton type:
//
//   struct Something {
//       using Type = ...;                          // what the pointer points to
//       static constexpr uint32_t kVtable = ...;   // the instance's vtable in the exe
//       static Type* Create(bool create);
//       static void Release();
//   };

#include <cstdint>

#include "game/core/manager.h"

namespace Spt {

template <typename Singleton>
struct SingletonPtr {
    using Type = typename Singleton::Type;

    void** m_vtable;
    Type* m_instance;

    static void** Vtable() { return reinterpret_cast<void**>(Singleton::kVtable); }

    // The constructor: a reference taken (and the instance made if `create` and there's none). Returns this.
    SingletonPtr* Construct(bool create)
    {
        m_vtable = Vtable();
        m_instance = Singleton::Create(create);
        return this;
    }
    // The destructor: the reference dropped.
    void DestroyBody()
    {
        m_vtable = Vtable();
        Singleton::Release();
    }
    // The scalar deleting destructor (vtable slot 0).
    SingletonPtr* Destroy(unsigned int flags)
    {
        DestroyBody();
        if (flags & 1)
            Mem::Manager::Get()->Free(this);
        return this;
    }
};

// 0x004E1970: the constructor of a one-pointer member (store it, return this). The linker folded the
// constructors of several such classes into this one: SingletonPtr's out-of-line constructor (0x004E1A90) builds
// its instance pointer with it, the network code (0x00597740-0x0059D410, 17 calls) its smart pointers.
struct PointerMember {
    void* m_pointer;

    PointerMember* Construct(void* pointer);
};

} // namespace Spt
