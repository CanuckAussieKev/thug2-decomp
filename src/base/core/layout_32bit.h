#pragma once

// NOT ORIGINAL - the layout asserts of the records that hold pointers. The game's in-memory objects (heap blocks,
// pool items, components, singletons) keep the original's sizes and offsets on the 32-bit build, where its pointers
// are 4 bytes; on x64 every pointer is 8 and the same records are larger, which is fine: nothing outside the program
// reads them. So such an assert checks its layout on a 32-bit build only:
//     static_assert(core::layout_32bit(sizeof(Pool) == 0x24), "layout must match the original");
// The records read from or written to files, sent on the wire or used in place in the original's data have no
// pointers and keep their unconditional asserts: their bytes are the same on every build.
namespace core {

// Whether pointers are the original's 4 bytes (the 32-bit build).
inline constexpr bool kPointers32Bit = sizeof(void*) == 4;

// `layout_matches` on a 32-bit build; true on any other (the record's pointers are wider there).
constexpr bool layout_32bit(bool layout_matches) noexcept { return !kPointers32Bit || layout_matches; }

} // namespace core
