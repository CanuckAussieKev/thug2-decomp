#pragma once

// LazyInstance<Singleton>: a reference to one of the reference-counted singletons (the managers and modules
// sys::Main makes), taken when it's constructed - Singleton::fabricate(create): with `create` the first one makes
// the instance - and dropped when it's destroyed (Singleton::release: the last one deletes the instance).
//
//   struct Something {
//       using Type = ...;                          // what the pointer points to
//       static Type* fabricate(bool create);
//       static void release();
//   };
//
// The game's LazyInstance objects are locals (sys::Main, the movie player, the network code, the park editor's
// front end and input manager references...): their constructors are inline (only one isn't, 0x004E1A90) and
// what's left out of line are each instance's destructor and its scalar deleting destructor, the only entry of a
// vtable of its own. Nothing derives from them or deletes one through a pointer, so here they're plain classes.
// The original has instances for the archive, memory card, serial port, async loader, script cache, main loop,
// module, sound, front end, object tracker, input, asset, net, composite object, trick object, game mode, profile,
// game records, skate module, online game, park editor, screen element, node and time of day managers.

namespace single {

template <typename Singleton>
class LazyInstance {
public:
    using Type = typename Singleton::Type;

    // A reference taken (and the instance made if `create` and there's none).
    explicit LazyInstance(bool create) : instance_(Singleton::fabricate(create)) {}
    // The reference dropped.
    ~LazyInstance() { Singleton::release(); }
    LazyInstance(const LazyInstance&) = delete;
    LazyInstance& operator=(const LazyInstance&) = delete;

    Type* get() const { return instance_; }
    Type* operator->() const { return instance_; }

private:
    Type* instance_;
};

// 0x004E1970: the constructor of a one-pointer member (store it, return this). The linker folded the
// constructors of several such classes into this one: LazyInstance's out-of-line constructor (0x004E1A90) builds
// its instance pointer with it, the network code (0x00597740-0x0059D410, 17 calls) its smart pointers.
struct PointerMember {
private:
    void* pointer_;

public:

    explicit PointerMember(void* pointer);
    // 0x00469490 (3 bytes: source only): its pointer - folded likewise (12 callers: the park editor's list
    // holders, ...).
    void* get_pull();
};

} // namespace single
