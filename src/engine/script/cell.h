#pragma once

#include <bit>
#include <cstdint>

#include "engine/core/pointer_cell.h"

namespace script {

class Array;
class Struct;
struct Pair;
struct Vector;
struct MemberFunctionEntry;
class Script;

// A native script function (a kQbvalNativeFunc symbol's value): run with its parameters and the running script.
using NativeFunction = bool (*)(Struct* params, Script* script);

// NOT ORIGINAL - one script value's 32 bits: a component's, a global symbol's or an array element's value. The kind
// isn't kept in the cell but by its holder (the component's / symbol's type, the array's element type): an
// externally tagged union, as the original's - not a std::variant, since the holders are pooled records of the
// original's sizes and a value is copied whole, whatever its kind. Every read and write goes through the accessor of
// the holder's kind; `word` is the cell's 32 bits as they are (a name / checksum, an integer's bits, or a copy of
// any kind's value); each kind is the same 32 bits read as that kind (std::bit_cast; a pointer through cell::pointer).
class Cell {
public:
    constexpr Cell() noexcept = default;

    // The cell holding `word` as its 32 bits (any kind: a copy of another cell's bits).
    static Cell of_word(uint32_t word) noexcept { return Cell{word}; }
    static Cell of_integer(int32_t value) noexcept { return Cell{std::bit_cast<uint32_t>(value)}; }
    static Cell of_real(float value) noexcept { return Cell{std::bit_cast<uint32_t>(value)}; }
    static Cell of_text(char* text) noexcept { return Cell{cell::of(text)}; }
    static Cell of(Struct* structure) noexcept { return Cell{cell::of(structure)}; }
    static Cell of(Array* array) noexcept { return Cell{cell::of(array)}; }
    static Cell of(Pair* pair) noexcept { return Cell{cell::of(pair)}; }
    static Cell of(Vector* vector) noexcept { return Cell{cell::of(vector)}; }
    static Cell of_code(uint8_t* code) noexcept { return Cell{cell::of(code)}; }
    static Cell of(NativeFunction function) noexcept { return Cell{std::bit_cast<uint32_t>(function)}; }
    static Cell of(const MemberFunctionEntry* entry) noexcept { return Cell{cell::of(entry)}; }
    // An array's element block (Array keeps it in its one cell when it has more than one element).
    static Cell of_block(Cell* block) noexcept { return Cell{cell::of(block)}; }

    // The 32 bits as they are.
    [[nodiscard]] uint32_t word() const noexcept { return word_; }
    // Whether the cell is all zero bits (no value: a null pointer, 0, 0.0f).
    [[nodiscard]] bool empty() const noexcept { return word_ == 0; }

    [[nodiscard]] int32_t integer() const noexcept { return std::bit_cast<int32_t>(word_); }
    [[nodiscard]] float real() const noexcept { return std::bit_cast<float>(word_); }
    [[nodiscard]] char* text() const noexcept { return cell::pointer<char>(word_); }
    [[nodiscard]] Struct* structure() const noexcept { return cell::pointer<Struct>(word_); }
    [[nodiscard]] Array* array() const noexcept { return cell::pointer<Array>(word_); }
    [[nodiscard]] Pair* pair() const noexcept { return cell::pointer<Pair>(word_); }
    [[nodiscard]] Vector* vector() const noexcept { return cell::pointer<Vector>(word_); }
    [[nodiscard]] uint8_t* code() const noexcept { return cell::pointer<uint8_t>(word_); }
    [[nodiscard]] NativeFunction function() const noexcept { return std::bit_cast<NativeFunction>(word_); }
    [[nodiscard]] const MemberFunctionEntry* method() const noexcept { return cell::pointer<const MemberFunctionEntry>(word_); }
    [[nodiscard]] Cell* block() const noexcept { return cell::pointer<Cell>(word_); }

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
    constexpr explicit Cell(uint32_t word) noexcept : word_(word) {}

    // An integer's / a float's bits, or a pointer as its 32 bits (a string's / local string's characters - owned by
    // the holder -, a structure, an array, a pair, a vector, a compiled script's code, a native function, a member
    // function's entry, an array's element block).
    uint32_t word_{0};
};
static_assert(sizeof(Cell) == 4, "one 32-bit cell (x86)");

} // namespace script
