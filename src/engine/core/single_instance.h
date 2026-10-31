#pragma once

// LazyInstance<T>: a reference to one of the reference-counted singletons (the managers and
// modules sys::Main makes), taken when it's constructed - T's Fabricate(create): with `create` the first one makes
// the instance - and dropped when it's destroyed (T's Release: the last one deletes the instance).
//
// The game's LazyInstance objects are locals (sys::Main, the movie player, the network code...): their
// constructors are inline (only one isn't, 0x004E1A90), and what's left out of line are each instance's
// destructor (called from the exception-unwind code of the functions with such a local) and its scalar deleting
// destructor, the only entry of each instance's vtable. Each instance here is named by a Singleton type:
//
//   struct Something {
//       using Type = ...;                          // what the pointer points to
//       static void** Vtable();                    // the instance's vtable
//       static Type* Fabricate(bool create);
//       static void Release();
//   };

#include <cstdint>

#include "engine/core/manager.h"

namespace single {

template <typename Singleton>
struct LazyInstance {
    using KindTag = typename Singleton::KindTag;

    void** vtable_;
    KindTag* instance_;

    static void** get_vtable() { return Singleton::Vtable(); }

    // The constructor: a reference taken (and the instance made if `create` and there's none). Returns this.
    LazyInstance* make_new(bool create)
    {
        vtable_ = get_vtable();
        instance_ = Singleton::Fabricate(create);
        return this;
    }
    // The destructor: the reference dropped.
    void destroy_body()
    {
        vtable_ = get_vtable();
        Singleton::Release();
    }
    // The scalar deleting destructor (vtable slot 0).
    LazyInstance* teardown(unsigned int flags)
    {
        destroy_body();
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(this);
        return this;
    }
};

// 0x004E1970: the constructor of a one-pointer member (store it, return this). The linker folded the
// constructors of several such classes into this one: LazyInstance's out-of-line constructor (0x004E1A90) builds
// its instance pointer with it, the network code (0x00597740-0x0059D410, 17 calls) its smart pointers.
struct PointerMember {
    void* pointer_;

    PointerMember* make_new(void* pointer);
    // 0x00469490 (3 bytes: source only): its pointer - folded likewise (12 callers: the park editor's list
    // holders, ...).
    void* get_pull();
};

} // namespace single
