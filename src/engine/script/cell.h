#pragma once

#include <bit>
#include <cstdint>

#include <gsl/assert>

#include "base/core/address.h"

namespace script {

class Array;
class Struct;
struct Pair;
struct Vector;
struct MemberFunctionEntry;
class Context;

// A native script function, a command (a QbValueKind::kNativeFunc symbol's value): run with its parameters and the
// running script (context.h).
using NativeFunction = bool (*)(Struct* params, Context context);

// NOT ORIGINAL - one script value: a component's, a global symbol's or an array element's value. The kind isn't kept
// in the cell but by its holder (the component's / symbol's type, the array's element type): an externally tagged
// union, as the original's - not a std::variant, since the holders are pooled records and a value is copied whole,
// whatever its kind. Every read and write goes through the accessor of the holder's kind.
//
// The cell is a pointer wide (uintptr_t: 32 bits on x86, as the original's, 64 on x64 - docs/design/x64-port.md).
// A name (checksum), an integer and a float are 32-bit values: `word` / `integer` / `real` read the low 32 bits, and
// a cell made from one has the rest zero. A pointer (a structure, an array, text, code, a function...) takes the
// whole cell. A copy of a cell of any kind is the cell itself (`Cell` is copied whole, or `bits` / `of_bits`), never
// its `word`: on x64 `word` of a cell holding a pointer above 4 GB stops the game (GSL's contract check) instead of
// dropping the pointer's top half.
class Cell {
public:
    constexpr Cell() noexcept = default;

    // The cell holding a 32-bit value (a name / checksum, an integer's bits, a flag; the rest zero).
    static Cell of_word(uint32_t word) noexcept { return Cell{word}; }
    static Cell of_integer(int32_t value) noexcept { return Cell{std::bit_cast<uint32_t>(value)}; }
    static Cell of_real(float value) noexcept { return Cell{std::bit_cast<uint32_t>(value)}; }
    static Cell of_text(char* text) noexcept { return Cell{core::address(text)}; }
    static Cell of(Struct* structure) noexcept { return Cell{core::address(structure)}; }
    static Cell of(Array* array) noexcept { return Cell{core::address(array)}; }
    static Cell of(Pair* pair) noexcept { return Cell{core::address(pair)}; }
    static Cell of(Vector* vector) noexcept { return Cell{core::address(vector)}; }
    static Cell of_code(uint8_t* code) noexcept { return Cell{core::address(code)}; }
    static Cell of(NativeFunction function) noexcept { return Cell{std::bit_cast<uintptr_t>(function)}; }
    static Cell of(const MemberFunctionEntry* entry) noexcept { return Cell{core::address(entry)}; }
    // An array's element block (Array keeps it in its one cell when it has more than one element).
    static Cell of_block(Cell* block) noexcept { return Cell{core::address(block)}; }
    // The cell with these bits, whole (a copy of another cell of any kind, an address moved by an offset).
    static Cell of_bits(uintptr_t bits) noexcept { return Cell{bits}; }

    // The 32-bit value: a name / checksum, an integer's bits, a flag (not a pointer: on x64 a cell above 4 GB stops).
    [[nodiscard]] uint32_t word() const noexcept
    {
        if constexpr (sizeof(uintptr_t) > sizeof(uint32_t)) {
            Expects(bits_ <= UINT32_MAX); // (x64: a pointer read as a 32-bit value)
        }
        return static_cast<uint32_t>(bits_);
    }
    // The low 32 bits, whatever the kind: where the game reads a value whose kind it doesn't check as a name or an
    // integer (a component expected to be a name; a cell holding a pointer gives its low 32 bits, as on x86).
    [[nodiscard]] uint32_t low_word() const noexcept { return static_cast<uint32_t>(bits_); }
    // The whole cell's bits, whatever its kind.
    [[nodiscard]] uintptr_t bits() const noexcept { return bits_; }
    // Whether the cell is all zero bits (no value: a null pointer, 0, 0.0f).
    [[nodiscard]] bool empty() const noexcept { return bits_ == 0; }
    // Whether two cells hold the same bits (whatever their kind: names, integers, or pointers by address).
    [[nodiscard]] bool same_bits(Cell other) const noexcept { return bits_ == other.bits_; }
    // Whether the cell holds this 32-bit value - a cell of any kind compared with a name (a pointer never is one).
    [[nodiscard]] bool holds_word(uint32_t word) const noexcept { return bits_ == word; }

    [[nodiscard]] int32_t integer() const noexcept { return std::bit_cast<int32_t>(low_word()); }
    [[nodiscard]] float real() const noexcept { return std::bit_cast<float>(low_word()); }
    [[nodiscard]] char* text() const noexcept { return core::at_address<char>(bits_); }
    [[nodiscard]] Struct* structure() const noexcept { return core::at_address<Struct>(bits_); }
    [[nodiscard]] Array* array() const noexcept { return core::at_address<Array>(bits_); }
    [[nodiscard]] Pair* pair() const noexcept { return core::at_address<Pair>(bits_); }
    [[nodiscard]] Vector* vector() const noexcept { return core::at_address<Vector>(bits_); }
    [[nodiscard]] uint8_t* code() const noexcept { return core::at_address<uint8_t>(bits_); }
    [[nodiscard]] NativeFunction function() const noexcept { return std::bit_cast<NativeFunction>(bits_); }
    [[nodiscard]] const MemberFunctionEntry* method() const noexcept { return core::at_address<const MemberFunctionEntry>(bits_); }
    [[nodiscard]] Cell* block() const noexcept { return core::at_address<Cell>(bits_); }

    void set_word(uint32_t word) noexcept { *this = of_word(word); }
    void set_integer(int32_t value) noexcept { *this = of_integer(value); }
    void set_real(float value) noexcept { *this = of_real(value); }
    void set_text(char* text) noexcept { *this = of_text(text); }
    void set_structure(Struct* structure) noexcept { *this = of(structure); }
    void set_array(Array* array) noexcept { *this = of(array); }
    void set_pair(Pair* pair) noexcept { *this = of(pair); }
    void set_vector(Vector* vector) noexcept { *this = of(vector); }
    void set_code(uint8_t* code) noexcept { *this = of_code(code); }
    void clear() noexcept { *this = Cell{}; }

private:
    constexpr explicit Cell(uintptr_t bits) noexcept : bits_(bits) {}

    // A 32-bit value (an integer's / a float's bits, a name), or a pointer (a string's / local string's characters -
    // owned by the holder -, a structure, an array, a pair, a vector, a compiled script's code, a native function, a
    // member function's entry, an array's element block).
    uintptr_t bits_{0};
};
static_assert(sizeof(Cell) == sizeof(uintptr_t), "one pointer-wide cell");
static_assert(sizeof(void*) != sizeof(uint32_t) || sizeof(Cell) == sizeof(uint32_t), "x86: the original's 32-bit cell");

} // namespace script
