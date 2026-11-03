#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "engine/core/slab_bank.h"
#include <gsl/pointers>
#include "engine/core/checksums.h"
#include "engine/maths/vec2.h"
#include "engine/maths/vec4.h"
#include "engine/script/cell.h"
#include <memory>

// The script system's data: structures of named components (Scripts::CStruct - also every script function's
// parameters), and the global symbol table they can refer to.
//
// A component's name is the checksum of the name used in the script (0 for an unnamed one). A structure can
// include a global structure by an unnamed name component that names it: lookups search those too.
namespace script {

class Array;
class Struct;
struct Pair;
struct Vector;
struct ParamScript;

// The types of components and symbols.
using QbValueKind = uint8_t;
constexpr int kQbvalNone = 0;
constexpr int kQbvalInteger = 1;
constexpr int kQbvalFloat = 2;
constexpr int kQbvalString = 3;
constexpr int kQbvalInlineText = 4;
constexpr int kQbvalPair = 5;
constexpr int kQbvalVector = 6;
constexpr int kQbvalScriptBody = 7;
constexpr int kQbvalNativeFunc = 8;
constexpr int kQbvalNativeMethod = 9;
constexpr int kQbvalStruct = 10;
constexpr int kQbvalArray = 12;
constexpr int kQbvalName = 13;
// A component referring to a structure / array it doesn't own (made by duplicate_component when asked to).
constexpr int kQbvalStructRef = 20;
constexpr int kEsymboltypeArraypointer = 21;

// A component of this kind holds a structure handed over in its word (the elements of an array of it aren't read).
constexpr uint32_t kQbvalStructHandedOver = 11;

constexpr size_t kArrayBytes = 0xC;
// A script array: `size` elements of one type. A single element is kept in place of the pointer.
class Array {
public:
    Array();
    void get_clear();
    // The elements (for one element, the value stored in the array itself).
    Cell* find_array_value();
    // NOT ORIGINAL - the same as a run of the array's elements (extent_ of them).
    [[nodiscard]] std::span<Cell> cells()
    {
        return extent_ == 1 ? std::span<Cell>(&storage_, 1) : std::span<Cell>(storage_.block(), extent_);
    }
    // Make the array `size` zeroed elements of `type` (frees the old element block, not what it refers to).
    void configure_size_kind(uint32_t size, QbValueKind type);
    // Only the type (the elements are left as they are).
    void change_species(QbValueKind type);
    // Grow the array to `size` elements, keeping the old ones (the new ones are zero; shrinking isn't supported).
    void rescale(uint32_t size);

    // The elements. Any 32-bit value (integer, checksum, structure, array...) by set_element / fetch_part.
    void set_element(uint32_t index, uint32_t value);
    void change_float(uint32_t index, float value);
    uint32_t fetch_part(uint32_t index);
    const char* fetch_text(uint32_t index);
    char* find_inline_text(uint32_t index);
    // An integer array's elements are converted.
    float fetch_float(uint32_t index);
    // The element as the pointer its 32-bit cell holds, by the array's type (a structure / array / pair / vector
    // array's element).
    Struct* struct_at(uint32_t index);
    Array* array_at(uint32_t index);
    Pair* pair_at(uint32_t index);
    Vector* vector_at(uint32_t index);
    // The element's cell, and the cell stored (its kind the array's type: a structure / array / pair / vector /
    // string array's element - the array takes it over).
    [[nodiscard]] Cell cell_at(uint32_t index);
    void set_cell(uint32_t index, Cell value);
    void set_struct(uint32_t index, Struct* value) { set_cell(index, Cell::of(value)); }
    void set_array(uint32_t index, Array* value) { set_cell(index, Cell::of(value)); }
    void set_pair(uint32_t index, Pair* value) { set_cell(index, Cell::of(value)); }
    void set_vector(uint32_t index, Vector* value) { set_cell(index, Cell::of(value)); }
    void set_text(uint32_t index, char* value) { set_cell(index, Cell::of_text(value)); }

    // The fields other code reads and sets.
    [[nodiscard]] QbValueKind kind_tag() const { return kind_tag_; }
    uint32_t& extent() { return extent_; }
    [[nodiscard]] uint32_t extent() const { return extent_; }
    Cell& storage() { return storage_; }
    [[nodiscard]] const Cell& storage() const { return storage_; }

private:
    uint8_t unknown0_{0};
    QbValueKind kind_tag_{kQbvalNone};
    uint16_t unknown2_{};
    uint32_t extent_{0};
    // One cell: the element block (Cell::block), or the one element itself - which by extent_ (the arrays are
    // pooled 12-byte records, as the original's).
    Cell storage_;
};
static_assert(sizeof(Array) == kArrayBytes, "layout must match the original");

// Pair and vector values are separate objects; the coordinates start at +4.
// Their constructors clear only byte 0 of the first word.
struct Pair {
    Pair();

    // The fields other code reads and sets.
    void set_unknown0(uint32_t value) { unknown0_ = value; }
    float& x() { return x_; }
    [[nodiscard]] float x() const { return x_; }
    float& y() { return y_; }
    [[nodiscard]] float y() const { return y_; }

private:
    uint32_t unknown0_{};
    float x_{0.0f};
    float y_{0.0f};
};
struct Vector {
    Vector();

    // The fields other code reads and sets.
    void set_unknown0(uint32_t value) { unknown0_ = value; }
    float& x() { return x_; }
    [[nodiscard]] float x() const { return x_; }
    float& y() { return y_; }
    [[nodiscard]] float y() const { return y_; }
    float& z() { return z_; }
    [[nodiscard]] float z() const { return z_; }

private:
    uint32_t unknown0_{};
    float x_{0.0f};
    float y_{0.0f};
    float z_{0.0f};
};
Pair* new_pair();
Vector* alloc_script_vector();

constexpr size_t kComponentBytes = 0x10;
// type_and_flag_ without the flag (bit 0): the type in the top 7 bits.
constexpr uint8_t kComponentTypeBits = 0xFE;
struct Component {
    uint8_t type_and_flag_{}; // the type in the top 7 bits
    uint8_t unknown1{};
    uint16_t unknown2{};
    uint32_t name{};
    // The value, of the kind kind_tag() (a string: spawn_text's, owned; structures, arrays, pairs, vectors and
    // script bodies owned too).
    Cell value;
    Component* next{};

};

[[nodiscard]] inline QbValueKind value_kind(const Component& component)
{
    return static_cast<QbValueKind>(component.type_and_flag_ >> 1);
}
static_assert(sizeof(Component) == kComponentBytes, "layout must match the original");

struct SymbolTableEntry {
    SymbolTableEntry();

    // The fields other code reads and sets.
    uint8_t& unknown0() { return unknown0_; }
    [[nodiscard]] uint8_t unknown0() const { return unknown0_; }
    [[nodiscard]] QbValueKind type() const { return type_; }
    void set_type(QbValueKind value) { type_ = value; }
    [[nodiscard]] uint32_t name_value() const { return name; }
    void set_name(uint32_t value) { name = value; }
    [[nodiscard]] uint32_t unknown8() const { return unknown8_; }
    void set_unknown8(uint32_t value) { unknown8_ = value; }
    Cell& value() { return value_; }
    [[nodiscard]] const Cell& value() const { return value_; }
    [[nodiscard]] SymbolTableEntry* next_value() const { return next; }
    void set_next(SymbolTableEntry* value) { next = value; }

private:
    uint8_t unknown0_{}; // bit 0: in use
    QbValueKind type_{kQbvalNone};
    uint16_t unknown2{};
    uint32_t name{0};
    uint32_t unknown8_{0};
    // The value, of the kind `type` (as Component's).
    Cell value_;
    SymbolTableEntry* next{nullptr};
};

// 0x00478CF0: the global symbol `name` (following names that just name another symbol), or null.
SymbolTableEntry* resolve_symbol(uint32_t name);
// The global symbol `name` itself, or null.
SymbolTableEntry* find_symbol(uint32_t name);
// Make the (empty) symbol table: 0x1000 buckets, from the current heap.
void create_symbol_table();
// A new symbol `name` (from the symbol pool), linked in.
SymbolTableEntry* create_symbol(uint32_t name);
// Link `entry` in / take it out of the table (the entry is kept).
void link_symbol(SymbolTableEntry* entry);
void unlink_symbol(SymbolTableEntry* entry);
// Iterate over every symbol: pass null for the first, then the previous one; null at the end.
SymbolTableEntry* get_next_symbol(SymbolTableEntry* previous);

// The values of global symbols (by checksum or name). Integers and floats convert to each other; missing ones
// (or the wrong type) give 0 / 0.0 / null; get_symbol_value gives the raw value of any type; strings give "".
int fetch_int_value(uint32_t name);
int fetch_int_value(const char* name);
float fetch_float(uint32_t name);
float fetch_float(const char* name);
uint32_t get_symbol_value(uint32_t name);
uint32_t get_symbol_value(const char* name);
const char* fetch_text(uint32_t name);
const char* fetch_text(const char* name);
Struct* read_sub_struct(uint32_t name);
Struct* read_sub_struct(const char* name);
Array* fetch_list(uint32_t name);
Array* fetch_list(const char* name);

// Whether two components' values are equal (names standing for global symbols are looked up; integers and
// floats compare by value; structures and arrays only when they are the same one).
bool components_equal(Component* a, Component* b);

// Debug output of a structure's contents, indented by `indent` levels. Compiled out of this build (the empty
// function at 0x005480A0): the typed getters still call it on a miss when asked to assert.
inline void dump_to_log(const Struct* /*structure*/, int /*indent*/ = 0) {}

// Strings owned by components: copied to the script heap (or, while the permanent string cache is on, shared
// in its block).
char* spawn_text(const char* string);
void free_string(char* string);
// While on, spawn_text puts strings in the permanent block, sharing equal ones (startup scripts).
void begin_permanent_strings();
// Off again; the lookup table is freed (the strings stay).
void end_permanent_strings();
// The script string slots (128 characters each), for the SetScriptString / GetScriptString commands.
// `string` is used as a format, as in the original (a '%' in it is interpreted).
int set_script_string(int index, const char* string);
char* get_script_string(int index);

struct Component;
// Free whatever a component's value owns and make it empty (keeps its name and flag bit).
void release_component(Component* component);
// Free an array's elements (whatever they own too) and empty it.
void release_array_storage(Array* array);
// Make `dest` a deep copy of `source` (with `reference`, structures and arrays are referred to, not copied).
void duplicate_component(Component* dest, const Component* source, int reference);
// Make `dest` a deep copy of `source`.
void duplicate_array(Array* dest, Array* source);
// Deep copies of `count` elements of `type` (null elements are skipped).
void copy_array_elements(std::span<Cell> dest, std::span<const Cell> source, uint32_t type);

// An integer / name array's elements, in order.
std::vector<uint32_t> words_of(Array* array);

// The script structures and arrays made and freed whole (NOT ORIGINAL as shared functions - the original inlined
// them, and this source had a copy in every file): from their pools, constructed (null when the pool and the heap are
// out of memory, as the original); freed with what they own (the components / elements) and given back to the pool.
// A null pointer is let go of as nothing.
gsl::owner<Struct*> new_struct();
gsl::owner<Array*> new_array();
void free_struct(gsl::owner<Struct*> structure);
void free_array(gsl::owner<Array*> array);
// Their owners, for a structure or array that lives in a scope or a member (freed as above).
struct StructFree {
    void operator()(gsl::owner<Struct*> structure) const noexcept { free_struct(structure); }
};
struct ArrayFree {
    void operator()(gsl::owner<Array*> array) const noexcept { free_array(array); }
};
using StructPtr = std::unique_ptr<Struct, StructFree>;
using ArrayPtr = std::unique_ptr<Array, ArrayFree>;
inline StructPtr make_struct() { return StructPtr(new_struct()); }
inline ArrayPtr make_array() { return ArrayPtr(new_array()); }

// The pooled allocations of the script's value objects.
gsl::owner<Struct*> spawn_param_bag();
void delete_struct(Struct* structure);
gsl::owner<Array*> alloc_script_array();
void delete_array(Array* array);
void free_component(Component* component);
// A new, empty component (from the component pool).
gsl::owner<Component*> new_component();
// Construct an empty component in place (leaves the second byte alone).
Component* construct_component(Component* component);

// A 4-component vector as scripts' vectors are returned (w = 1): the engine's vector.
using VectorValue = maths::Vec4;

constexpr size_t kStructBytes = 8;
class Struct {
public:
    Struct();
    // Initialise as a copy of `source` (the copy constructor).
    Struct* init_copy(Struct* source);
    bool is_vacant();
    // The next component after `component` (null: the first).
    Component* step_to_entry(Component* component);
    // The component `name` - the last one there is, also in included structures.
    Component* find_last_component(uint32_t name);
    // The first component named `name`, or the flag `name`, also in included structures.
    Component* find_component(uint32_t name);
    // The value of the last component `name` of type `type` (integers are converted for floats; name components
    // naming a global symbol of the type count too), into `out`.
    bool get_hunt(uint32_t name, uint32_t type, Cell* out);

    bool has_entry_named(uint32_t name);
    bool has_entry_named(const char* name);
    // Whether the bare name `flag` is among the components.
    bool has_bare_flag(uint32_t flag);
    bool has_bare_flag(const char* flag);
    // Whether `other` is this structure or one of its structure components (recursively).
    bool ref_tally(Struct* other);
    // Whether every component of this structure is in `other` too (by name and value).
    bool is_subset_of(Struct* other);

    // Remove (and free) every component.
    void get_clear();
    // Remove every component named `name` (of type `type`).
    void erase_module(uint32_t name);
    void erase_module(const char* name);
    void remove_component_of_type(uint32_t name, uint8_t type);
    // Remove the bare name `flag`.
    void remove_flag(uint32_t flag);

    // Add `component` at the end, replacing any component of the same name and compatible type (integers and
    // floats replace each other; a bare name only replaces the same name).
    void attach_module(Component* component);
    void append_word(uint32_t name, const char* value);
    void append_word(const char* name, const char* value);
    void append_inline_text(uint32_t name, const char* value);
    void append_word(uint32_t name, QbValueKind type, const char* value); // string or local string
    // The putters of a number, a name or a vector take the component's name as a checksums::Name: its value could
    // otherwise be passed for it (I.24).
    void put_int(checksums::Name name, int value);
    void put_int(const char* name, int value);
    void push_float(checksums::Name name, float value);
    void push_float(const char* name, float value);
    void put_name_key(checksums::Name name, uint32_t value);
    void put_name_key(const char* name, uint32_t value);
    // x, y and z of `value` (its w not kept).
    void put_vec3(checksums::Name name, VectorValue value);
    void put_vec3(const char* name, VectorValue value);
    void insert_pair(checksums::Name name, maths::Vec2 value);
    // Arrays and structures added by pointer (the structure takes them over).
    void append_array_ref(uint32_t name, gsl::owner<Array*> value);
    void append_array_ref(const char* name, gsl::owner<Array*> value);
    void append_struct_ref(uint32_t name, gsl::owner<Struct*> value);
    void append_struct_ref(const char* name, gsl::owner<Struct*> value);
    // A copy of `value`.
    void put_sub_struct(uint32_t name, Struct* value);
    void put_sub_struct(const char* name, Struct* value);
    void append_array_value(uint32_t name, Array* value);
    void append_array_value(const char* name, Array* value);
    // A compiled script, copied to the script heap.
    void append_script(uint32_t name, std::span<const uint8_t> code);
    // By type: integer, float, 11 structure pointer, 12 array pointer, 13 checksum (others are ignored).
    void add_component_of_type(uint32_t name, QbValueKind type, uint32_t value);

    // Copies of all `source`'s components, added.
    Struct* merge_structure(Struct* source);
    void append_structure_if_any(Struct* source);
    // Become a copy of `source`.
    Struct* get_assign(Struct* source);
    // Copies of this structure's components added to `dest`, with included global structures expanded.
    void inline_into(Struct* dest, int depth);

    // The typed getters: false if missing. With `assert` set, a miss also prints the structure (dump_to_log -
    // compiled out of this build) before a compiled-out assert.
    bool fetch_text(uint32_t name, const char** out, int assert);
    bool fetch_text(const char* name, const char** out, int assert);
    bool fetch_int_value(uint32_t name, int* out, int assert);
    // The same into an unsigned word (the integer's bits; left as it was when not found).
    bool fetch_int_value(uint32_t name, uint32_t* out, int assert)
    {
        int value = static_cast<int>(*out);
        const bool found = fetch_int_value(name, &value, assert);
        *out = static_cast<uint32_t>(value);
        return found;
    }
    bool fetch_int_value(const char* name, int* out, int assert);
    bool fetch_float(uint32_t name, float* out, int assert);
    bool fetch_float(const char* name, float* out, int assert);
    bool fetch_pair(uint32_t name, maths::Vec2* out, int assert);
    bool fetch_pair(uint32_t name, Pair* out, bool assert);
    bool read_sub_struct(uint32_t name, Struct** out, int assert);
    bool read_sub_struct(const char* name, Struct** out, int assert);
    bool fetch_list(uint32_t name, Array** out, int assert);
    bool fetch_list(const char* name, Array** out, int assert);
    // A script component: its name and code into `out`.
    bool fetch_script(uint32_t name, ParamScript* out, int assert);
    bool read_hash_value(uint32_t name, uint32_t* out, int assert);
    bool read_hash_value(const char* name, uint32_t* out, int assert);
    // The checksum `name`, or else the checksum of the string `name`.
    bool hash_from_name_or_text(uint32_t name, uint32_t* out, int assert);
    bool fetch_vec(uint32_t name, VectorValue* out, int assert);
    bool fetch_vec(const char* name, VectorValue* out, int assert);

    // The fields other code reads and sets.
    Component*& head() { return head_; }
    [[nodiscard]] Component* head() const { return head_; }
    void set_head(Component* value) { head_ = value; }

private:
    uint8_t unknown0_{};
    uint8_t unknown1_{0};
    uint16_t unknown2_{};
    Component* head_{nullptr};
};
static_assert(sizeof(Struct) == kStructBytes, "layout must match the original");

// The CStruct pool pair (script/struct.cpp).
memory::PoolPair& struct_pools();
memory::PoolPair& array_pools();
memory::PoolPair& component_pools();
memory::PoolPair& symbol_pools();
memory::PoolPair& vector_pools();

struct PermanentString;
} // namespace script
