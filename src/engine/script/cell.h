#pragma once

#include <bit>
#include <cstdint>

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
// any kind's value), read and made with std::bit_cast of the whole cell (defined for any kind it holds).
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the one place the cell's kinds are read and written, each by
// the accessor of the kind its holder says it is
class Cell {
public:
    constexpr Cell() noexcept : word_{0} {}

    // The cell holding `word` as its 32 bits (any kind: a copy of another cell's bits).
    static Cell of_word(uint32_t word) noexcept { return std::bit_cast<Cell>(word); }
    static Cell of_integer(int32_t value) noexcept
    {
        Cell cell;
        cell.integer_ = value;
        return cell;
    }
    static Cell of_real(float value) noexcept
    {
        Cell cell;
        cell.real_ = value;
        return cell;
    }
    static Cell of_text(char* text) noexcept
    {
        Cell cell;
        cell.text_ = text;
        return cell;
    }
    static Cell of(Struct* structure) noexcept
    {
        Cell cell;
        cell.structure_ = structure;
        return cell;
    }
    static Cell of(Array* array) noexcept
    {
        Cell cell;
        cell.array_ = array;
        return cell;
    }
    static Cell of(Pair* pair) noexcept
    {
        Cell cell;
        cell.pair_ = pair;
        return cell;
    }
    static Cell of(Vector* vector) noexcept
    {
        Cell cell;
        cell.vector_ = vector;
        return cell;
    }
    static Cell of_code(uint8_t* code) noexcept
    {
        Cell cell;
        cell.code_ = code;
        return cell;
    }
    static Cell of(NativeFunction function) noexcept
    {
        Cell cell;
        cell.function_ = function;
        return cell;
    }
    static Cell of(const MemberFunctionEntry* entry) noexcept
    {
        Cell cell;
        cell.method_ = entry;
        return cell;
    }
    // An array's element block (Array keeps it in its one cell when it has more than one element).
    static Cell of_block(Cell* block) noexcept
    {
        Cell cell;
        cell.block_ = block;
        return cell;
    }

    // The 32 bits as they are.
    [[nodiscard]] uint32_t word() const noexcept { return std::bit_cast<uint32_t>(*this); }
    // Whether the cell is all zero bits (no value: a null pointer, 0, 0.0f).
    [[nodiscard]] bool empty() const noexcept { return word() == 0; }

    [[nodiscard]] int32_t integer() const noexcept { return integer_; }
    [[nodiscard]] float real() const noexcept { return real_; }
    [[nodiscard]] char* text() const noexcept { return text_; }
    [[nodiscard]] Struct* structure() const noexcept { return structure_; }
    [[nodiscard]] Array* array() const noexcept { return array_; }
    [[nodiscard]] Pair* pair() const noexcept { return pair_; }
    [[nodiscard]] Vector* vector() const noexcept { return vector_; }
    [[nodiscard]] uint8_t* code() const noexcept { return code_; }
    [[nodiscard]] NativeFunction function() const noexcept { return function_; }
    [[nodiscard]] const MemberFunctionEntry* method() const noexcept { return method_; }
    [[nodiscard]] Cell* block() const noexcept { return block_; }

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
    union {
        uint32_t word_;
        int32_t integer_;
        float real_;
        char* text_; // a string's / local string's characters (owned by the holder: spawn_text's)
        Struct* structure_;
        Array* array_;
        Pair* pair_;
        Vector* vector_;
        uint8_t* code_; // a compiled script's code (a script body)
        NativeFunction function_;
        const MemberFunctionEntry* method_;
        Cell* block_; // an array's elements
    };
};
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
static_assert(sizeof(Cell) == 4, "one 32-bit cell (x86)");

} // namespace script
