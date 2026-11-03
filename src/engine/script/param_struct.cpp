#include "engine/script/param_struct.h"
#include "engine/script/cscript.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include <gsl/pointers>
#include <algorithm>
#include <array>
#include <span>
#include "engine/core/checked_index.h"
#include "engine/script/tokens.h"
#include "platform/crt.h"
#include "engine/core/no_destroy.h"
#include "engine/script/script_state.h"
#include "engine/maths/vec2.h"

namespace script {

namespace {
// NOT ORIGINAL grouping - param_struct.cpp's state (the original's globals). Made on first use, never destroyed
// (param_struct_state()).
struct ParamStructState {
    int struct_copy_peak_{18000};
    bool use_permanent_strings_{};
    int struct_copy_count_{};
    SymbolTableEntry** name_table_{};
    SymbolTableEntry* iterator_entry_{};
    uint32_t iterator_bucket_{};
};

ParamStructState& param_struct_state()
{
    static core::NoDestroy<ParamStructState> state;
    return state.get();
}

} // namespace
memory::PoolPair& array_pools()
{
    static core::NoDestroy<memory::PoolPair> array_pools;
    return array_pools.get();
}

memory::PoolPair& component_pools()
{
    static core::NoDestroy<memory::PoolPair> component_pools;
    return component_pools.get();
}

memory::PoolPair& symbol_pools()
{
    static core::NoDestroy<memory::PoolPair> symbol_pools;
    return symbol_pools.get();
}

memory::PoolPair& vector_pools()
{
    static core::NoDestroy<memory::PoolPair> vector_pools;
    return vector_pools.get();
}

namespace {

constexpr uint32_t kSymbolTableMask = 0xFFF;
// The table's 0x1000 buckets (the names' low 12 bits).
std::span<SymbolTableEntry*, kSymbolTableMask + 1> script_symbol_slots()
{
    return std::span<SymbolTableEntry*, kSymbolTableMask + 1>(param_struct_state().name_table_, kSymbolTableMask + 1);
}

} // namespace

// 0x00478CC0
SymbolTableEntry* find_symbol(uint32_t name)
{
    SymbolTableEntry* entry = core::at(script_symbol_slots(), name & kSymbolTableMask);
    while (entry && entry->name_value() != name)
        entry = entry->next_value();
    return entry;
}

namespace {

// The global structure an unnamed name component includes, or null.
Struct* included_structure(const Component* component)
{
    if (component->name != 0 || value_kind(*component) != kQbvalName)
        return nullptr;
    const SymbolTableEntry* symbol = resolve_symbol(component->value.word());
    return symbol && symbol->type() == kQbvalStruct ? symbol->value().structure() : nullptr;
}

} // namespace

// 0x00478CF0
SymbolTableEntry* resolve_symbol(uint32_t name)
{
    SymbolTableEntry* entry = find_symbol(name);
    if (!entry)
        return nullptr;
    while (entry->type() == kQbvalName) {
        SymbolTableEntry* target = find_symbol(entry->value().word());
        if (!target)
            return entry;
        entry = target;
    }
    return entry;
}

// 0x00476410
Struct::Struct() = default;

// 0x00476420
bool Struct::is_vacant() { return head_ == nullptr; }

// 0x00476490
Component* Struct::step_to_entry(Component* component) { return component ? component->next : head_; }

// 0x00476430
Component* Struct::find_last_component(uint32_t name)
{
    Component* found = nullptr;
    for (Component* component = head_; component; component = component->next) {
        if (component->name == name) {
            found = component;
        } else if (Struct* included = included_structure(component)) {
            if (Component* inner = included->find_last_component(name))
                found = inner;
        }
    }
    return found;
}

// 0x00476A90
Component* Struct::find_component(uint32_t name)
{
    for (Component* component = head_; component; component = component->next) {
        if (component->name == name)
            return component;
        if (component->name == 0 && value_kind(*component) == kQbvalName) {
            if (component->value.word() == name)
                return component;
            const SymbolTableEntry* symbol = resolve_symbol(component->value.word());
            if (symbol && symbol->type() == kQbvalStruct) {
                if (Component* inner = symbol->value().structure()->find_component(name))
                    return inner;
            }
        }
    }
    return nullptr;
}

// 0x004764B0 (the last match wins)
bool Struct::get_hunt(uint32_t name, uint32_t type, Cell* out)
{
    bool found = false;
    for (const Component* component = head_; component; component = component->next) {
        if (component->name == name) {
            const QbValueKind component_type = value_kind(*component);
            if (component_type == type) {
                *out = component->value;
                found = true;
            } else if (component_type == kQbvalInteger && type == kQbvalFloat) {
                out->set_real(static_cast<float>(component->value.integer()));
                found = true;
            } else if (component_type == kQbvalName) {
                // A name standing for a global value.
                const SymbolTableEntry* symbol = resolve_symbol(component->value.word());
                if (symbol) {
                    if (symbol->type() == type) {
                        *out = symbol->value();
                        found = true;
                    } else if (symbol->type() == kQbvalInteger && type == kQbvalFloat) {
                        out->set_real(static_cast<float>(symbol->value().integer()));
                        found = true;
                    }
                }
            }
        } else if (Struct* included = included_structure(component)) {
            Cell inner;
            if (included->get_hunt(name, type, &inner)) {
                *out = inner;
                found = true;
            }
        }
    }
    return found;
}

// 0x00476AF0
bool Struct::has_entry_named(uint32_t name) { return find_component(name) != nullptr; }

// 0x00476B10
bool Struct::has_entry_named(const char* name) { return find_component(checksums::hash_from_text(name)) != nullptr; }

// 0x00476B40
bool Struct::has_bare_flag(uint32_t flag)
{
    for (const Component* component = head_; component; component = component->next) {
        if (component->name != 0 || value_kind(*component) != kQbvalName)
            continue;
        if (component->value.word() == flag)
            return true;
        const SymbolTableEntry* symbol = resolve_symbol(component->value.word());
        if (symbol && symbol->type() == kQbvalStruct && symbol->value().structure()->has_bare_flag(flag))
            return true;
    }
    return false;
}

// 0x00476BA0
bool Struct::has_bare_flag(const char* flag) { return has_bare_flag(checksums::hash_from_text(flag)); }

// 0x00476BC0
bool Struct::ref_tally(Struct* other)
{
    if (other == this)
        return true;
    for (const Component* component = head_; component; component = component->next) {
        if (value_kind(*component) == kQbvalStruct && component->value.structure()->ref_tally(other))
            return true;
    }
    return false;
}

// 0x00476C10
bool Struct::is_subset_of(Struct* other)
{
    for (Component* mine = head_; mine; mine = mine->next) {
        Component* theirs = other->head_;
        while (theirs && !(mine->name == theirs->name && components_equal(mine, theirs)))
            theirs = theirs->next;
        if (!theirs)
            return false;
    }
    return true;
}

// --- the typed getters -------------------------------------------------------------------------------------

// 0x00476590 (strings and local strings)
bool Struct::fetch_text(uint32_t name, const char** out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalString, &value) && !get_hunt(name, kQbvalInlineText, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value.text();
    return true;
}

// 0x004765F0
bool Struct::fetch_text(const char* name, const char** out, int assert)
{
    return fetch_text(checksums::hash_from_text(name), out, assert);
}

// 0x00476630
bool Struct::fetch_int_value(uint32_t name, int* out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalInteger, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value.integer();
    return true;
}

// 0x00476680
bool Struct::fetch_int_value(const char* name, int* out, int assert)
{
    return fetch_int_value(checksums::hash_from_text(name), out, assert);
}

// 0x004766D0
bool Struct::fetch_float(uint32_t name, float* out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalFloat, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value.real();
    return true;
}

// 0x00476720
bool Struct::fetch_float(const char* name, float* out, int assert)
{
    return fetch_float(checksums::hash_from_text(name), out, assert);
}

// 0x00476770
bool Struct::fetch_pair(uint32_t name, maths::Vec2* out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalPair, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    const Pair* pair = value.pair();
    out->x = pair->x();
    out->y = pair->y();
    return true;
}

// 0x00476DF0 (zero if missing)
bool Struct::fetch_pair(uint32_t name, Pair* out, bool assert)
{
    Cell value;
    if (get_hunt(name, kQbvalPair, &value)) {
        const Pair* pair = value.pair();
        out->x() = pair->x();
        out->y() = pair->y();
        return true;
    }
    if (assert)
        dump_to_log(this);
    out->x() = 0.0f;
    out->y() = 0.0f;
    return false;
}

// 0x004767C0
bool Struct::read_sub_struct(uint32_t name, Struct** out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalStruct, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value.structure();
    return true;
}

// 0x00476810
bool Struct::read_sub_struct(const char* name, Struct** out, int assert)
{
    return read_sub_struct(checksums::hash_from_text(name), out, assert);
}

// 0x00476860
bool Struct::fetch_list(uint32_t name, Array** out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalArray, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value.array();
    return true;
}

// 0x004768B0
bool Struct::fetch_list(const char* name, Array** out, int assert)
{
    return fetch_list(checksums::hash_from_text(name), out, assert);
}

// 0x00476900
bool Struct::fetch_script(uint32_t name, ParamScript* out, int assert)
{
    Cell code;
    if (!get_hunt(name, kQbvalScriptBody, &code)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    out->own_name_hash_ = name;
    out->code_stream_ = code.code();
    return true;
}

// 0x00476950 (the last match wins; an unnamed name that includes a global structure isn't a value)
bool Struct::read_hash_value(uint32_t name, uint32_t* out, int assert)
{
    bool found = false;
    for (const Component* component = head_; component; component = component->next) {
        if (component->name == name) {
            if (value_kind(*component) == kQbvalName) {
                const SymbolTableEntry* symbol = nullptr;
                // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): the original's, as written
                if (name != 0 || !(symbol = resolve_symbol(component->value.word())) || symbol->type() != kQbvalStruct) {
                    *out = component->value.word();
                    found = true;
                }
            }
        } else if (Struct* included = included_structure(component)) {
            if (included->read_hash_value(name, out, 0))
                found = true;
        }
    }
    if (assert && !found)
        dump_to_log(this);
    return found;
}

// 0x00476A00
bool Struct::read_hash_value(const char* name, uint32_t* out, int assert)
{
    return read_hash_value(checksums::hash_from_text(name), out, assert);
}

// 0x00476A30
bool Struct::hash_from_name_or_text(uint32_t name, uint32_t* out, int assert)
{
    if (read_hash_value(name, out, assert))
        return true;
    const char* string = nullptr;
    if (!fetch_text(name, &string, assert))
        return false;
    *out = checksums::hash_from_text(string);
    return true;
}

// 0x00476FB0
bool Struct::fetch_vec(uint32_t name, VectorValue* out, int assert)
{
    Cell value;
    if (!get_hunt(name, kQbvalVector, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    const Vector* vector = value.vector();
    out->x = vector->x();
    out->y = vector->y();
    out->z = vector->z();
    out->w = 1.0f;
    return true;
}

// 0x00477010
bool Struct::fetch_vec(const char* name, VectorValue* out, int assert)
{
    return fetch_vec(checksums::hash_from_text(name), out, assert);
}

} // namespace script

// --- owning values: strings, pooled objects, cleaning up and copying ------------------------------------------

namespace script {

// The CStruct pool pair (the current pool's index, then the two pools).
memory::PoolPair& struct_pools()
{
    static core::NoDestroy<memory::PoolPair> struct_pools;
    return struct_pools.get();
}

// A permanent string table entry: the string's checksum and the string.
struct PermanentString {
    uint32_t checksum;
    char* string;
};
namespace {

// The pool pairs of the script's value objects.

// The permanent string cache (used while loading the game's scripts): one block, strings shared by checksum.
bool& use_permanent_strings() { return param_struct_state().use_permanent_strings_; }

// 0x00476260 (the string in EAX: called only by spawn_text, so registered source-only)
char* create_permanent_string(const char* string)
{
    const int length = static_cast<int>(std::strlen(string));
    const uint32_t checksum = checksums::generate_crc_from_buffer(string, length);
    const std::span<PermanentString> table(script_state().permanent_string_table(), script_state().permanent_string_capacity());
    for (uint32_t i = 0; i < script_state().permanent_string_count(); ++i) {
        if (core::at(table, i).checksum == checksum)
            return core::at(table, i).string;
    }
    // (the strings' block: the copy where the last one ended)
    const std::span<char> strings(script_state().permanent_strings_start(), script_state().permanent_strings_size());
    const std::span<char> copy =
        strings.subspan(static_cast<size_t>(script_state().permanent_strings_next() - script_state().permanent_strings_start()),
                        static_cast<size_t>(length) + 1);
    std::memcpy(copy.data(), string, copy.size());
    script_state().set_permanent_strings_next(strings.subspan(static_cast<size_t>(copy.data() - strings.data()) + copy.size()).data());
    core::at(table, script_state().permanent_string_count()) = {.checksum = checksum, .string = copy.data()};
    ++script_state().permanent_string_count();
    return copy.data();
}

void delete_pair(Pair* pair) { script_state().pair_pools().dealloc(pair); }
void delete_vector(Vector* vector) { vector_pools().dealloc(vector); }

void merge_structure(Struct* to, Struct* from) { to->merge_structure(from); }

} // namespace

// 0x004763A0
char* spawn_text(const char* string)
{
    if (use_permanent_strings())
        return create_permanent_string(string);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    const size_t size = std::strlen(string) + 1;
    auto* copy = static_cast<char*>(memory::alloc_block(static_cast<unsigned int>(size)));
    std::memcpy(copy, string, size);
    memory::leave_heap_scope();
    return copy;
}

// 0x00476300
void begin_permanent_strings() { use_permanent_strings() = true; }

// 0x00476310
void end_permanent_strings()
{
    use_permanent_strings() = false;
    if (script_state().permanent_string_table()) {
        memory::free_malloc(script_state().permanent_string_table());
        script_state().permanent_string_table() = nullptr;
    }
}

namespace {
// The strings SetScriptString / GetScriptString use, 128 bytes each. The original has one slot (0x006F3590) and
// indexes past it into the structure pools that follow; the game's scripts use only index 0. Not original: 16 slots,
// an index outside them ignored (an empty string read back).
constexpr int kScriptStringSlots = 16;
// NOT ORIGINAL grouping - the script strings' slots and the empty one (the original's globals). Made on first use,
// never destroyed (script_strings()).
struct ScriptStrings {
    std::array<std::array<char, 128>, kScriptStringSlots> script_strings_{};
    std::array<char, 128> no_script_string_{};
};

ScriptStrings& script_strings()
{
    static core::NoDestroy<ScriptStrings> state;
    return state.get();
}

char* script_string_slot(int index)
{
    if (index < 0 || index >= kScriptStringSlots) {
        core::at(script_strings().no_script_string_, 0) = 0;
        return script_strings().no_script_string_.data();
    }
    return core::at(script_strings().script_strings_, index).data();
}
} // namespace

// 0x00476370
int set_script_string(int index, const char* string)
{
    return crt::sprintf_from_data(script_string_slot(index),
                                  string); // (unbounded, the string the format: as the original)
}

// 0x00476390
char* get_script_string(int index) { return script_string_slot(index); }

// 0x00476340 (strings in the permanent block are never freed)
void free_string(char* string)
{
    const char* start = script_state().permanent_strings_start();
    if (start && !std::less<>()(string, start) && std::cmp_less(string - start, script_state().permanent_strings_size()))
        return;
    memory::free_malloc(string);
}

// 0x00409030 / 0x00409070
gsl::owner<Struct*> spawn_param_bag() { return struct_pools().allocate<Struct>(); }

gsl::owner<Struct*> new_struct()
{
    gsl::owner<Struct*> memory = spawn_param_bag();
    return memory ? memory::construct_in<Struct>(memory) : nullptr;
}
gsl::owner<Array*> new_array()
{
    gsl::owner<Array*> memory = alloc_script_array();
    return memory ? memory::construct_in<Array>(memory) : nullptr;
}
void free_struct(gsl::owner<Struct*> structure)
{
    if (structure) {
        structure->get_clear();
        delete_struct(structure);
    }
}
void free_array(gsl::owner<Array*> array)
{
    if (array) {
        release_array_storage(array);
        array->get_clear();
        delete_array(array);
    }
}
void delete_struct(Struct* structure) { struct_pools().dealloc(structure); }
// 0x00408F90 / 0x00408FD0
gsl::owner<Array*> alloc_script_array() { return array_pools().allocate<Array>(); }
void delete_array(Array* array) { array_pools().dealloc(array); }
void free_component(Component* component) { component_pools().dealloc(component); }

// 0x0046CAA0
Array::Array() { storage_.clear(); }

// 0x0046CAB0
void Array::get_clear()
{
    if (extent_ == 1) {
        // (the single element is in place: nothing to free)
        extent_ = 0;
        kind_tag_ = kQbvalNone;
        storage_.clear();
        return;
    }
    if (!storage_.empty()) {
        memory::free_malloc(storage_.block());
        storage_.clear();
    }
    kind_tag_ = kQbvalNone;
    extent_ = 0;
}

// 0x0046CAF0
void Array::change_species(QbValueKind type) { kind_tag_ = type; }

// 0x0046CB00
Cell* Array::find_array_value() { return extent_ == 1 ? &storage_ : storage_.block(); }

// 0x00477140
void release_array_storage(Array* array)
{
    const uint32_t count = array->extent();
    const QbValueKind type = array->kind_tag();
    const std::span<Cell> elements = array->cells();
    for (uint32_t i = 0; i < count; ++i) {
        Cell& element = core::at(elements, i);
        if (element.empty())
            continue;
        switch (type) {
        case kQbvalString:
        case kQbvalInlineText:
            free_string(element.text());
            break;
        case kQbvalPair:
            delete_pair(element.pair());
            break;
        case kQbvalVector:
            delete_vector(element.vector());
            break;
        case kQbvalStruct: {
            Struct* structure = element.structure();
            structure->get_clear();
            delete_struct(structure);
            break;
        }
        case kQbvalArray: {
            Array* inner = element.array();
            release_array_storage(inner);
            inner->get_clear();
            delete_array(inner);
            break;
        }
        default:
            continue; // (other elements own nothing, and are left as they are)
        }
        element.clear();
    }
    array->get_clear();
}

// 0x004772A0
void release_component(Component* component)
{
    switch (value_kind(*component)) {
    case kQbvalString:
    case kQbvalInlineText:
        if (component->value.text())
            free_string(component->value.text());
        break;
    case kQbvalPair:
        if (component->value.pair())
            delete_pair(component->value.pair());
        break;
    case kQbvalVector:
        if (component->value.vector())
            delete_vector(component->value.vector());
        break;
    case kQbvalScriptBody:
        if (component->value.word()) {
            forget_code_block(component->value.code());
            memory::free_malloc(component->value.code());
        }
        component->unknown2 = 0; // the script's size
        break;
    case kQbvalStruct:
        if (Struct* structure = component->value.structure()) {
            structure->get_clear();
            delete_struct(structure);
        }
        break;
    case kQbvalArray:
        if (Array* array = component->value.array()) {
            release_array_storage(array);
            array->get_clear();
            delete_array(array);
        }
        break;
    default:
        break;
    }
    component->value.set_word(0);
    component->type_and_flag_ &= 1;
}

// 0x004773E0
void duplicate_component(Component* dest, const Component* source, int reference)
{
    if (dest->value.word())
        release_component(dest);
    dest->type_and_flag_ = static_cast<uint8_t>((source->type_and_flag_ & ~1) | (dest->type_and_flag_ & 1));
    dest->name = source->name;
    switch (value_kind(*source)) {
    case kQbvalString:
    case kQbvalInlineText:
        dest->value.set_text(spawn_text(source->value.text()));
        break;
    case kQbvalPair: {
        auto* pair = script_state().pair_pools().allocate<Pair>();
        if (pair) {
            // (the constructor: only the first byte of the first word is cleared)
            pair->y() = 0.0f;
            pair->x() = 0.0f;
            pair->set_unknown0(0);
        }
        dest->value.set_pair(pair);
        pair->x() = source->value.pair()->x();
        dest->value.pair()->y() = source->value.pair()->y();
        break;
    }
    case kQbvalVector: {
        auto* vector = vector_pools().allocate<Vector>();
        if (vector) {
            vector->z() = 0.0f;
            vector->y() = 0.0f;
            vector->x() = 0.0f;
            vector->set_unknown0(0);
        }
        dest->value.set_vector(vector);
        vector->x() = source->value.vector()->x();
        dest->value.vector()->y() = source->value.vector()->y();
        dest->value.vector()->z() = source->value.vector()->z();
        break;
    }
    case kQbvalScriptBody: {
        // The compiled script, `unknown2` bytes.
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
        dest->value.set_code(static_cast<uint8_t*>(memory::alloc_block(source->unknown2)));
        dest->unknown2 = source->unknown2;
        memory::leave_heap_scope();
        // (`unknown2` bytes of the source's: its block - or the pre-compiled image it is in - holds at least that many)
        const std::span<uint8_t> to(dest->value.code(), source->unknown2);
        note_code_block(to);
        const Tokens from = tokens_at(source->value.code());
        std::ranges::copy(from.rest().first(to.size()), to.begin());
        break;
    }
    case kQbvalStruct: {
        if (reference) {
            dest->type_and_flag_ = static_cast<uint8_t>((dest->type_and_flag_ & 1) | (kQbvalStructRef << 1));
            dest->value.set_structure(source->value.structure());
            break;
        }
        gsl::owner<Struct*> structure = spawn_param_bag();
        if (structure)
            new (structure) script::Struct;
        dest->value.set_structure(structure);
        if (source->value.structure() != structure) {
            structure->get_clear();
            merge_structure(structure, source->value.structure());
        }
        break;
    }
    case kQbvalArray: {
        if (!reference) {
            gsl::owner<Array*> array = alloc_script_array();
            if (array)
                new (array) script::Array;
            dest->value.set_array(array);
            duplicate_array(array, source->value.array());
            break;
        }
        dest->type_and_flag_ = static_cast<uint8_t>((dest->type_and_flag_ & 1) | (kEsymboltypeArraypointer << 1));
        dest->value.set_word(source->value.word());
        break;
    }
    case kQbvalInteger:
    case kQbvalFloat:
    case kQbvalName:
    case kQbvalStructRef:
    case kEsymboltypeArraypointer:
        dest->value.set_word(source->value.word());
        break;
    default:
        break;
    }
}

// 0x00477090
void Struct::get_clear()
{
    Component* component = head_;
    while (component) {
        Component* next = component->next;
        release_component(component);
        free_component(component);
        component = next;
    }
    head_ = nullptr;
}

// 0x004776B0
void Struct::erase_module(uint32_t name)
{
    Component* previous = nullptr;
    Component* component = head_;
    while (component) {
        Component* next = component->next;
        if (component->name == name) {
            if (previous)
                previous->next = next;
            else
                head_ = next;
            release_component(component);
            free_component(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

// 0x00477770
void Struct::erase_module(const char* name) { erase_module(checksums::hash_from_text(name)); }

// 0x00477790
void Struct::remove_component_of_type(uint32_t name, uint8_t type)
{
    Component* previous = nullptr;
    Component* component = head_;
    while (component) {
        Component* next = component->next;
        if (component->name == name && value_kind(*component) == type) {
            if (previous)
                previous->next = next;
            else
                head_ = next;
            release_component(component);
            free_component(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

} // namespace script

// --- adding components ---------------------------------------------------------------------------------------

namespace script {

// 0x0046D140 (the constructor: the second byte is left as it was)
Component* construct_component(Component* component)
{
    component->name = 0;
    component->unknown2 = 0;
    component->value.set_word(0);
    component->next = nullptr;
    component->type_and_flag_ = 0;
    return component;
}

namespace {

gsl::owner<Component*> new_typed_component(uint32_t name, QbValueKind type)
{
    gsl::owner<Component*> component = new_component();
    component->name = name;
    component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (type << 1));
    return component;
}

} // namespace

// 0x0044C130 (+ the constructor, as the callers do)
gsl::owner<Component*> new_component()
{
    void* memory = component_pools().carve(sizeof(Component));
    return memory ? construct_component(static_cast<Component*>(memory)) : nullptr;
}

// 0x00477860
void Struct::remove_flag(uint32_t flag)
{
    Component* previous = nullptr;
    Component* component = head_;
    while (component) {
        Component* next = component->next;
        if (component->name == 0 && component->value.word() == flag) {
            if (previous)
                previous->next = next;
            else
                head_ = next;
            release_component(component);
            free_component(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

// 0x00477920
void Struct::attach_module(Component* added)
{
    Component* last = nullptr;
    Component* component = head_;
    while (component) {
        bool replace = false;
        if (component->name == added->name) {
            const QbValueKind mine = value_kind(*component);
            const QbValueKind theirs = value_kind(*added);
            if (mine == theirs)
                replace = !(mine == kQbvalName && component->name == 0) || component->value.word() == added->value.word();
            else
                replace = (mine == kQbvalFloat && theirs == kQbvalInteger) ||
                          (mine == kQbvalInteger && theirs == kQbvalFloat);
        }
        if (replace) {
            Component* next = component->next;
            if (last)
                last->next = next;
            else
                head_ = next;
            release_component(component);
            free_component(component);
            component = next;
        } else {
            last = component;
            component = component->next;
        }
    }
    if (last)
        last->next = added;
    else
        head_ = added;
    added->next = nullptr;
}

// 0x004779D0
void Struct::append_word(uint32_t name, const char* value)
{
    gsl::owner<Component*> component = new_typed_component(name, kQbvalString);
    component->value.set_text(spawn_text(value));
    attach_module(component);
}

// 0x00477A90
void Struct::append_word(const char* name, const char* value) { append_word(checksums::hash_from_text(name), value); }

// 0x00477AC0
void Struct::append_inline_text(uint32_t name, const char* value)
{
    gsl::owner<Component*> component = new_typed_component(name, kQbvalInlineText);
    component->value.set_text(spawn_text(value));
    attach_module(component);
}

// 0x00478390
void Struct::append_word(uint32_t name, QbValueKind type, const char* value)
{
    if (type == kQbvalString)
        append_word(name, value);
    else
        append_inline_text(name, value);
}

// 0x00477B80
void Struct::put_int(checksums::Name name, int value)
{
    gsl::owner<Component*> component = new_typed_component(name.value(), kQbvalInteger);
    component->value.set_integer(value);
    attach_module(component);
}

// 0x00477C30
void Struct::put_int(const char* name, int value) { put_int(checksums::Name{checksums::hash_from_text(name)}, value); }

// 0x00477C60
void Struct::push_float(checksums::Name name, float value)
{
    gsl::owner<Component*> component = new_typed_component(name.value(), kQbvalFloat);
    component->value.set_real(value);
    attach_module(component);
}

// 0x00477D10
void Struct::push_float(const char* name, float value) { push_float(checksums::Name{checksums::hash_from_text(name)}, value); }

// 0x00477D40
void Struct::put_name_key(checksums::Name name, uint32_t value)
{
    gsl::owner<Component*> component = new_typed_component(name.value(), kQbvalName);
    component->value.set_word(value);
    attach_module(component);
}

// 0x00477DF0
void Struct::put_name_key(const char* name, uint32_t value)
{
    put_name_key(checksums::Name{checksums::hash_from_text(name)}, value);
}

// 0x00477E20 (x, y, z) and 0x00477F70 (the vector by value, calling it)
void Struct::put_vec3(checksums::Name name, VectorValue value)
{
    const float x = value.x;
    const float y = value.y;
    const float z = value.z;
    gsl::owner<Component*> component = new_typed_component(name.value(), kQbvalVector);
    auto* vector = vector_pools().allocate<Vector>();
    if (vector) {
        vector->z() = 0.0f;
        vector->y() = 0.0f;
        vector->x() = 0.0f;
        vector->set_unknown0(0);
    }
    component->value.set_vector(vector);
    vector->x() = x;
    component->value.vector()->y() = y;
    component->value.vector()->z() = z;
    attach_module(component);
}

// 0x00477F40 (x, y, z) and 0x00477F90 (the vector by value)
void Struct::put_vec3(const char* name, VectorValue value)
{
    put_vec3(checksums::Name{checksums::hash_from_text(name)}, value);
}

// 0x00477FC0
void Struct::insert_pair(checksums::Name name, maths::Vec2 value)
{
    const float x = value.x;
    const float y = value.y;
    gsl::owner<Component*> component = new_typed_component(name.value(), kQbvalPair);
    auto* pair = script_state().pair_pools().allocate<Pair>();
    if (pair) {
        pair->y() = 0.0f;
        pair->x() = 0.0f;
        pair->set_unknown0(0);
    }
    component->value.set_pair(pair);
    pair->x() = x;
    component->value.pair()->y() = y;
    attach_module(component);
}

// 0x004780D0
void Struct::append_array_ref(uint32_t name, gsl::owner<Array*> value)
{
    gsl::owner<Component*> component = new_typed_component(name, kQbvalArray);
    component->value.set_array(value);
    attach_module(component);
}

// 0x00478180
void Struct::append_array_ref(const char* name, gsl::owner<Array*> value)
{
    append_array_ref(checksums::hash_from_text(name), value);
}

// 0x004781B0
void Struct::append_struct_ref(uint32_t name, gsl::owner<Struct*> value)
{
    gsl::owner<Component*> component = new_typed_component(name, kQbvalStruct);
    component->value.set_structure(value);
    attach_module(component);
}

// 0x00478260
void Struct::append_struct_ref(const char* name, gsl::owner<Struct*> value)
{
    append_struct_ref(checksums::hash_from_text(name), value);
}

// 0x00478670
void Struct::put_sub_struct(uint32_t name, Struct* value)
{
    gsl::owner<Component*> component = new_typed_component(name, kQbvalStruct);
    gsl::owner<Struct*> copy = spawn_param_bag();
    if (copy)
        new (copy) script::Struct;
    component->value.set_structure(copy);
    if (value != copy) {
        copy->get_clear();
        copy->merge_structure(value);
    }
    attach_module(component);
}

// 0x00478770
void Struct::put_sub_struct(const char* name, Struct* value) { put_sub_struct(checksums::hash_from_text(name), value); }

// 0x00478290
void Struct::append_script(uint32_t name, std::span<const uint8_t> code)
{
    const auto size = static_cast<uint32_t>(code.size());
    gsl::owner<Component*> component = new_typed_component(name, kQbvalScriptBody);
    component->unknown2 = static_cast<uint16_t>(size);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    auto* block = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::leave_heap_scope();
    const std::span<uint8_t> copy(block, size);
    note_code_block(copy);
    std::ranges::copy(code, copy.begin());
    component->value.set_code(block);
    attach_module(component);
}

// 0x004783C0
void Struct::add_component_of_type(uint32_t name, QbValueKind type, uint32_t value)
{
    switch (type) {
    case kQbvalInteger:
        put_int(checksums::Name{name}, static_cast<int>(value));
        break;
    case kQbvalFloat: {
        auto number = std::bit_cast<float>(value);
        push_float(checksums::Name{name}, number);
        break;
    }
    case 11:
        append_struct_ref(name, memory::owned_element(Cell::of_word(value).structure())); // (handed over in the word)
        break;
    case kQbvalArray:
        append_array_ref(name, memory::owned_element(Cell::of_word(value).array())); // (handed over in the word)
        break;
    case kQbvalName:
        put_name_key(checksums::Name{name}, value);
        break;
    default:
        break;
    }
}

// 0x00478480
Struct* Struct::merge_structure(Struct* source)
{
    for (const Component* component = source->head_; component; component = component->next) {
        gsl::owner<Component*> copy = new_component();
        duplicate_component(copy, component, 0);
        attach_module(copy);
    }
    return this;
}

// 0x00478540
void Struct::append_structure_if_any(Struct* source)
{
    if (source)
        merge_structure(source);
}

// 0x00478650
Struct* Struct::get_assign(Struct* source)
{
    if (source != this) {
        get_clear();
        merge_structure(source);
    }
    return this;
}

// 0x00478560
void Struct::inline_into(Struct* dest, int depth)
{
    for (const Component* component = head_; component; component = component->next) {
        if (Struct* included = included_structure(component)) {
            included->inline_into(dest, depth + 1);
            continue;
        }
        gsl::owner<Component*> copy = new_component();
        duplicate_component(copy, component, 0);
        dest->attach_module(copy);
    }
}

} // namespace script

// --- arrays: sizing, elements, copying ------------------------------------------------------------------------

namespace script {

namespace {

// Allocate `count` elements on the script heap.
Cell* allocate_elements(uint32_t count)
{
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    auto* elements = static_cast<Cell*>(memory::alloc_block(count * sizeof(Cell)));
    memory::leave_heap_scope();
    return elements;
}

// 0x006F3610 / 0x0067DA30: how many structures were made by copying, and the most there were (debug counters).
int& struct_copy_count() { return param_struct_state().struct_copy_count_; }
int& struct_copy_peak() { return param_struct_state().struct_copy_peak_; }

} // namespace

// 0x0046D410 / 0x0046D3D0 (operator new; the size is always the object's)
Pair* new_pair() { return script_state().pair_pools().allocate<Pair>(); }
Vector* alloc_script_vector() { return vector_pools().allocate<Vector>(); }

// 0x0047AC00
Pair::Pair() = default; // (its first word zeroed)

// 0x0047ABF0
Vector::Vector() = default; // (its first word zeroed)

// 0x0046CC50
void Array::configure_size_kind(uint32_t size, QbValueKind type)
{
    if (extent_ == 1) {
        // (the single element is in place: nothing to free)
        storage_.clear();
    } else if (!storage_.empty()) {
        memory::free_malloc(storage_.block());
        storage_.clear();
    }
    kind_tag_ = type;
    extent_ = size;
    if (size == 0) {
        kind_tag_ = kQbvalNone;
        return;
    }
    if (size == 1)
        return;
    Cell* block = allocate_elements(extent_);
    for (Cell& element : std::span<Cell>(block, extent_))
        element.clear();
    storage_ = Cell::of_block(block);
}

// 0x0046CCF0
void Array::rescale(uint32_t size)
{
    if (size == extent_)
        return;
    Cell* block = allocate_elements(size);
    const std::span<Cell> elements(block, size);
    const std::span<const Cell> from = cells();
    for (uint32_t i = 0; i < extent_; ++i)
        core::at(elements, i) = core::at(from, i);
    for (uint32_t i = extent_; i < size; ++i)
        core::at(elements, i).clear();
    if (extent_ > 1)
        memory::free_malloc(storage_.block());
    // (growing to one element leaves the block's pointer in place of the single value, as the original does)
    storage_ = Cell::of_block(block);
    extent_ = size;
}

// 0x0046CB10
void Array::set_element(uint32_t index, uint32_t value) { set_cell(index, Cell::of_word(value)); }

void Array::set_cell(uint32_t index, Cell value)
{
    if (extent_ == 1)
        storage_ = value;
    else
        core::at(cells(), index) = value;
}

// 0x0046CB40
void Array::change_float(uint32_t index, float value) { set_cell(index, Cell::of_real(value)); }

// 0x0046CC30
uint32_t Array::fetch_part(uint32_t index) { return cell_at(index).word(); }

Cell Array::cell_at(uint32_t index) { return extent_ == 1 ? storage_ : core::at(cells(), index); }

// 0x0046CBB0
const char* Array::fetch_text(uint32_t index) { return cell_at(index).text(); }

// 0x0046CB70
char* Array::find_inline_text(uint32_t index) { return cell_at(index).text(); }

Struct* Array::struct_at(uint32_t index) { return cell_at(index).structure(); }
Array* Array::array_at(uint32_t index) { return cell_at(index).array(); }
Pair* Array::pair_at(uint32_t index) { return cell_at(index).pair(); }
Vector* Array::vector_at(uint32_t index) { return cell_at(index).vector(); }

// 0x0046CBF0
float Array::fetch_float(uint32_t index)
{
    const Cell cell = cell_at(index);
    if (kind_tag_ == kQbvalFloat)
        return cell.real();
    return static_cast<float>(cell.integer());
}

// 0x004787A0
void copy_array_elements(std::span<Cell> dest, std::span<const Cell> source, uint32_t type)
{
    for (size_t i = 0; i < source.size(); ++i) {
        const Cell element = core::at(source, i);
        Cell& to = core::at(dest, i);
        switch (type) {
        case kQbvalInteger:
        case kQbvalFloat:
        case kQbvalName:
            to = element;
            break;
        case kQbvalString:
        case kQbvalInlineText:
            if (!element.empty())
                to = Cell::of_text(spawn_text(element.text()));
            break;
        case kQbvalPair:
            if (!element.empty()) {
                const Pair* from = element.pair();
                Pair* pair = new_pair();
                pair = pair ? new (pair) script::Pair : nullptr;
                pair->x() = from->x();
                pair->y() = from->y();
                to = Cell::of(pair);
            }
            break;
        case kQbvalVector:
            if (!element.empty()) {
                const Vector* from = element.vector();
                Vector* vector = alloc_script_vector();
                vector = vector ? new (vector) script::Vector : nullptr;
                vector->x() = from->x();
                vector->y() = from->y();
                vector->z() = from->z();
                to = Cell::of(vector);
            }
            break;
        case kQbvalStruct:
            if (!element.empty()) {
                Struct* from = element.structure();
                gsl::owner<Struct*> structure = spawn_param_bag();
                if (structure)
                    new (structure) script::Struct;
                if (from != structure) {
                    structure->get_clear();
                    structure->merge_structure(from);
                }
                to = Cell::of(structure);
            }
            break;
        case kQbvalArray:
            if (!element.empty()) {
                gsl::owner<Array*> array = alloc_script_array();
                if (array)
                    array = new (array) script::Array;
                duplicate_array(array, element.array());
                to = Cell::of(array);
            }
            break;
        default:
            // Scripts, functions, unknowns: nothing is copied.
            return;
        }
    }
}

std::vector<uint32_t> words_of(Array* array)
{
    std::vector<uint32_t> words(array->extent());
    for (uint32_t i = 0; i < array->extent(); ++i)
        core::at(words, i) = array->fetch_part(i);
    return words;
}

// 0x00478A60
void duplicate_array(Array* dest, Array* source)
{
    release_array_storage(dest);
    const QbValueKind type = source->kind_tag();
    const uint32_t size = source->extent();
    dest->configure_size_kind(size, type);
    if (size)
        copy_array_elements(dest->cells(), source->cells().first(size), type);
}

// 0x00478AB0
Struct* Struct::init_copy(Struct* source)
{
    head_ = nullptr;
    unknown1_ = 0;
    if (source != this) {
        get_clear();
        merge_structure(source);
    }
    if (++struct_copy_count() > struct_copy_peak())
        struct_copy_peak() = struct_copy_count();
    return this;
}

} // namespace script

namespace {

using script::Array;

// 0x0046D410 / 0x0046D3D0 take the size (always the object's).
void* new_pair_memory(unsigned int) { return script::new_pair(); }
void* new_vector_memory(unsigned int) { return script::alloc_script_vector(); }

} // namespace

// --- the symbol table and global values; comparing components; adding array copies --------------------------

namespace script {

namespace {

// The symbol iterator's position: the current entry and its bucket.
SymbolTableEntry*& iterator_entry() { return param_struct_state().iterator_entry_; }
uint32_t& iterator_bucket() { return param_struct_state().iterator_bucket_; }

// A global symbol's value as an integer / float (other types: 0).
int symbol_integer(SymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type() == kQbvalInteger)
            return entry->value().integer();
        if (entry->type() == kQbvalFloat)
            return static_cast<int>(entry->value().real());
    }
    return 0;
}
float symbol_float(SymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type() == kQbvalInteger)
            return static_cast<float>(entry->value().integer());
        if (entry->type() == kQbvalFloat)
            return entry->value().real();
    }
    return 0.0f;
}
Cell symbol_value_of_type(SymbolTableEntry* entry, QbValueKind type)
{
    return entry && entry->type() == type ? entry->value() : Cell{};
}

// The game's empty string (what the string getters give for missing symbols).
const char* empty_string() { return ""; }

} // namespace

// 0x00478C90
void create_symbol_table()
{
    param_struct_state().name_table_ = static_cast<SymbolTableEntry**>(memory::alloc_block(0x1000 * sizeof(SymbolTableEntry*)));
    std::ranges::fill(script_symbol_slots(), nullptr);
}

// 0x004790F0
SymbolTableEntry::SymbolTableEntry()
{
    unknown0_ &= 0xF8;
    value_.set_word(0);
}

// 0x00479110
SymbolTableEntry* create_symbol(uint32_t name)
{
    void* memory = symbol_pools().carve(sizeof(SymbolTableEntry));
    SymbolTableEntry* entry = memory ? memory::construct_in<script::SymbolTableEntry>(memory) : nullptr;
    entry->set_name(name);
    entry->unknown0() |= 1;
    link_symbol(entry);
    return entry;
}

// 0x00478DC0
void link_symbol(SymbolTableEntry* entry)
{
    SymbolTableEntry*& head = core::at(script_symbol_slots(), entry->name_value() & kSymbolTableMask);
    entry->set_next(head);
    head = entry;
}

// 0x00478D60
void unlink_symbol(SymbolTableEntry* entry)
{
    SymbolTableEntry** bucket = &core::at(script_symbol_slots(), entry->name_value() & kSymbolTableMask);
    SymbolTableEntry* previous = nullptr;
    for (SymbolTableEntry* each = *bucket; each; previous = each, each = each->next_value()) {
        if (each != entry)
            continue;
        if (previous)
            previous->set_next(entry->next_value());
        else
            *bucket = entry->next_value();
        entry->set_next(nullptr);
        return;
    }
}

// 0x00478E50
SymbolTableEntry* get_next_symbol(SymbolTableEntry* previous)
{
    SymbolTableEntry* entry = nullptr;
    if (!previous) {
        iterator_bucket() = 0;
        entry = core::at(script_symbol_slots(), 0);
    } else {
        entry = previous->next_value();
    }
    while (!entry) {
        if (++iterator_bucket() >= 0x1000) {
            iterator_bucket() = 0;
            iterator_entry() = nullptr;
            return nullptr;
        }
        entry = core::at(script_symbol_slots(), iterator_bucket());
    }
    iterator_entry() = entry;
    return entry;
}

// 0x00478F60 / 0x004790C0 (an unused second argument)
int fetch_int_value(uint32_t name) { return symbol_integer(resolve_symbol(name)); }
int fetch_int_value(const char* name) { return fetch_int_value(checksums::hash_from_text(name)); }
// 0x00478F00 / 0x00478F30
float fetch_float(uint32_t name) { return symbol_float(resolve_symbol(name)); }
float fetch_float(const char* name) { return fetch_float(checksums::hash_from_text(name)); }
// 0x00478F90 / 0x00478FB0
uint32_t get_symbol_value(uint32_t name)
{
    const SymbolTableEntry* entry = resolve_symbol(name);
    return entry ? entry->value().word() : 0;
}
uint32_t get_symbol_value(const char* name) { return get_symbol_value(checksums::hash_from_text(name)); }
// 0x00478FD0 / 0x00478FF0 (any type: the raw value)
const char* fetch_text(uint32_t name)
{
    const SymbolTableEntry* entry = resolve_symbol(name);
    return entry ? entry->value().text() : empty_string();
}
const char* fetch_text(const char* name) { return fetch_text(checksums::hash_from_text(name)); }
// 0x00479020 / 0x00479040
Struct* read_sub_struct(uint32_t name)
{
    return symbol_value_of_type(resolve_symbol(name), kQbvalStruct).structure();
}
Struct* read_sub_struct(const char* name) { return read_sub_struct(checksums::hash_from_text(name)); }
// 0x00479070 / 0x00479090
Array* fetch_list(uint32_t name)
{
    return symbol_value_of_type(resolve_symbol(name), kQbvalArray).array();
}
Array* fetch_list(const char* name) { return fetch_list(checksums::hash_from_text(name)); }

// 0x004791A0
bool components_equal(Component* a, Component* b)
{
    // A name standing for a global symbol counts as the symbol's value.
    auto resolve = [](Component* component, uint32_t* type, Cell* value) {
        *type = value_kind(*component);
        *value = component->value;
        if (*type == kQbvalName) {
            if (const SymbolTableEntry* symbol = resolve_symbol(value->word())) {
                *value = symbol->value();
                *type = symbol->type();
            }
        }
    };
    uint32_t type_a = 0, type_b = 0;
    Cell value_a, value_b;
    resolve(a, &type_a, &value_a);
    resolve(b, &type_b, &value_b);
    auto as_float = [](Cell value) { return static_cast<double>(value.real()); };
    // (integers are compared with floats exactly, as the x87 does)
    auto as_integer = [](Cell value) { return static_cast<double>(value.integer()); };
    switch (type_a) {
    case kQbvalInteger:
        if (type_b == kQbvalFloat)
            return as_integer(value_a) == as_float(value_b);
        return type_b == kQbvalInteger && value_a.word() == value_b.word();
    case kQbvalFloat:
        if (type_b == kQbvalInteger)
            return as_float(value_a) == as_integer(value_b);
        return type_b == kQbvalFloat && as_float(value_a) == as_float(value_b);
    case kQbvalString:
    case kQbvalInlineText:
        return type_b == type_a && std::strcmp(value_a.text(), value_b.text()) == 0;
    case kQbvalPair: {
        if (type_b != kQbvalPair)
            return false;
        const Pair* pa = value_a.pair();
        const Pair* pb = value_b.pair();
        return pa->x() == pb->x() && pa->y() == pb->y();
    }
    case kQbvalVector: {
        if (type_b != kQbvalVector)
            return false;
        const Vector* va = value_a.vector();
        const Vector* vb = value_b.vector();
        return va->x() == vb->x() && va->y() == vb->y() && va->z() == vb->z();
    }
    // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
    case kQbvalScriptBody:
    case kQbvalName:
        return type_b == type_a && value_a.word() == value_b.word();
    case kQbvalStruct:
    case kQbvalArray:
        // (different arrays are never equal: their comparison 0x0049F4B0 just returns false)
        return type_b == type_a && value_a.word() == value_b.word();
    default:
        return false;
    }
}

// 0x00478B00
void Struct::append_array_value(uint32_t name, Array* value)
{
    gsl::owner<Component*> component = new_typed_component(name, kQbvalArray);
    gsl::owner<Array*> array = alloc_script_array();
    if (array)
        array = new (array) script::Array;
    component->value.set_array(array);
    duplicate_array(array, value);
    attach_module(component);
}

// 0x00478C50
void Struct::append_array_value(const char* name, Array* value)
{
    append_array_value(checksums::hash_from_text(name), value);
}

} // namespace script

