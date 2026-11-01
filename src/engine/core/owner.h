#pragma once

// NOT ORIGINAL - the ownership mark for raw pointers (the RAII helpers: engine/util/com_ptr.h, unique_handle.h).
//
// `owner<T*>` (C++ Core Guidelines I.11 / R.3, our own alias - no GSL dependency): a raw pointer that owns what it
// points to, where a std::unique_ptr can't be used yet - the object's lifetime follows the original's rules (it is
// freed by a manager, by the script runtime, at a level unload or not at all), or its memory comes from one of the
// game's heaps with a lifetime the heap scope decides. The alias changes nothing: `owner<T*>` is `T*`. It marks
// where the delete lives so a later pass can turn the pointer into a unique_ptr once the rules are clear.
namespace memory {

template <typename Pointer>
// NOLINTNEXTLINE(readability-identifier-naming): named as the Guidelines' gsl::owner it stands for
using owner = Pointer;

} // namespace memory
