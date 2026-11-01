#include "engine/script/param_struct.h"
#include "engine/script/cscript.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/owner.h"
#include "platform/compiler.h"

namespace script {

int g_struct_copy_peak{18000};
memory::PoolPair g_array_pools;
memory::PoolPair g_component_pools;
bool g_use_permanent_strings;
char* g_permanent_strings_start;
char* g_permanent_strings_next;
uint32_t g_permanent_strings_size;
uint32_t g_permanent_string_count;
int g_struct_copy_count;
SymbolTableEntry** g_name_table;
memory::PoolPair g_symbol_pools;
SymbolTableEntry* g_iterator_entry;
uint32_t g_iterator_bucket;
memory::PoolPair g_pair_pools;
memory::PoolPair g_vector_pools;

namespace {

SymbolTableEntry** script_symbol_slots() { return g_name_table; }
constexpr uint32_t kSymbolTableMask = 0xFFF;

} // namespace

// 0x00478CC0
SymbolTableEntry* find_symbol(uint32_t name)
{
    SymbolTableEntry* entry = script_symbol_slots()[name & kSymbolTableMask];
    while (entry && entry->name != name)
        entry = entry->next;
    return entry;
}

namespace {

// The global structure an unnamed name component includes, or null.
Struct* included_structure(const Component* component)
{
    if (component->name != 0 || component->kind_tag() != kQbvalName)
        return nullptr;
    SymbolTableEntry* symbol = resolve_symbol(component->checksum);
    return symbol && symbol->type == kQbvalStruct ? symbol->structure : nullptr;
}

} // namespace

// 0x00478CF0
SymbolTableEntry* resolve_symbol(uint32_t name)
{
    SymbolTableEntry* entry = find_symbol(name);
    if (!entry)
        return nullptr;
    while (entry->type == kQbvalName) {
        SymbolTableEntry* target = find_symbol(entry->checksum);
        if (!target)
            return entry;
        entry = target;
    }
    return entry;
}

// 0x00476410
Struct::Struct()
{
    head_ = nullptr;
    unknown1_ = 0;
}

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
        if (component->name == 0 && component->kind_tag() == kQbvalName) {
            if (component->checksum == name)
                return component;
            SymbolTableEntry* symbol = resolve_symbol(component->checksum);
            if (symbol && symbol->type == kQbvalStruct) {
                if (Component* inner = symbol->structure->find_component(name))
                    return inner;
            }
        }
    }
    return nullptr;
}

// 0x004764B0 (the last match wins)
bool Struct::get_hunt(uint32_t name, uint32_t type, void* out)
{
    auto* value = static_cast<uint32_t*>(out);
    bool found = false;
    for (Component* component = head_; component; component = component->next) {
        if (component->name == name) {
            QbValueKind component_type = component->kind_tag();
            if (component_type == type) {
                *value = component->raw;
                found = true;
            } else if (component_type == kQbvalInteger && type == kQbvalFloat) {
                *static_cast<float*>(out) = static_cast<float>(component->integer);
                found = true;
            } else if (component_type == kQbvalName) {
                // A name standing for a global value.
                SymbolTableEntry* symbol = resolve_symbol(component->checksum);
                if (symbol) {
                    if (symbol->type == type) {
                        *value = symbol->raw;
                        found = true;
                    } else if (symbol->type == kQbvalInteger && type == kQbvalFloat) {
                        *static_cast<float*>(out) = static_cast<float>(symbol->integer);
                        found = true;
                    }
                }
            }
        } else if (Struct* included = included_structure(component)) {
            uint32_t inner;
            if (included->get_hunt(name, type, &inner)) {
                *value = inner;
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
    for (Component* component = head_; component; component = component->next) {
        if (component->name != 0 || component->kind_tag() != kQbvalName)
            continue;
        if (component->checksum == flag)
            return true;
        SymbolTableEntry* symbol = resolve_symbol(component->checksum);
        if (symbol && symbol->type == kQbvalStruct && symbol->structure->has_bare_flag(flag))
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
    for (Component* component = head_; component; component = component->next) {
        if (component->kind_tag() == kQbvalStruct && component->structure->ref_tally(other))
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
    const char* value;
    if (!get_hunt(name, kQbvalString, &value) && !get_hunt(name, kQbvalInlineText, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value;
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
    int value;
    if (!get_hunt(name, kQbvalInteger, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value;
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
    float value;
    if (!get_hunt(name, kQbvalFloat, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476720
bool Struct::fetch_float(const char* name, float* out, int assert)
{
    return fetch_float(checksums::hash_from_text(name), out, assert);
}

// 0x00476770
bool Struct::fetch_pair(uint32_t name, float* x, float* y, int assert)
{
    Pair* pair;
    if (!get_hunt(name, kQbvalPair, &pair)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *x = pair->x;
    *y = pair->y;
    return true;
}

// 0x00476DF0 (zero if missing)
bool Struct::fetch_pair(uint32_t name, Pair* out, bool assert)
{
    Pair* pair;
    if (get_hunt(name, kQbvalPair, &pair)) {
        out->x = pair->x;
        out->y = pair->y;
        return true;
    }
    if (assert)
        dump_to_log(this);
    out->x = 0.0f;
    out->y = 0.0f;
    return false;
}

// 0x004767C0
bool Struct::read_sub_struct(uint32_t name, Struct** out, int assert)
{
    Struct* value;
    if (!get_hunt(name, kQbvalStruct, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value;
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
    Array* value;
    if (!get_hunt(name, kQbvalArray, &value)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    *out = value;
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
    const uint8_t* code;
    if (!get_hunt(name, kQbvalScriptBody, &code)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    out->own_name_hash_ = name;
    out->code_stream_ = code;
    return true;
}

// 0x00476950 (the last match wins; an unnamed name that includes a global structure isn't a value)
bool Struct::read_hash_value(uint32_t name, uint32_t* out, int assert)
{
    bool found = false;
    for (Component* component = head_; component; component = component->next) {
        if (component->name == name) {
            if (component->kind_tag() == kQbvalName) {
                SymbolTableEntry* symbol = nullptr;
                if (name != 0 || !(symbol = resolve_symbol(component->checksum)) || symbol->type != kQbvalStruct) {
                    *out = component->checksum;
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
    Vector* vector;
    if (!get_hunt(name, kQbvalVector, &vector)) {
        if (assert)
            dump_to_log(this);
        return false;
    }
    out->x = vector->x;
    out->y = vector->y;
    out->z = vector->z;
    out->w = 1.0f;
    return true;
}

// 0x00477010
bool Struct::fetch_vec(const char* name, VectorValue* out, int assert)
{
    return fetch_vec(checksums::hash_from_text(name), out, assert);
}

} // namespace script

namespace {

using script::Array;
using script::Struct;
using script::VectorValue;

// The overloads taking the assert flag as a bool (they pass it on as 0 or 1).
struct BoolAssertGetters {
    Struct* get_self() { return reinterpret_cast<Struct*>(this); }
    bool fetch_text(uint32_t name, const char** out, bool assert) { return get_self()->fetch_text(name, out, assert); }
    bool get_string_named(const char* name, const char** out, bool assert)
    {
        return get_self()->fetch_text(name, out, assert);
    }
    bool fetch_int_value(uint32_t name, int* out, bool assert)
    {
        return get_self()->fetch_int_value(name, out, assert);
    }
    bool get_integer_named(const char* name, int* out, bool assert)
    {
        return get_self()->fetch_int_value(name, out, assert);
    }
    bool fetch_float(uint32_t name, float* out, bool assert) { return get_self()->fetch_float(name, out, assert); }
    bool get_float_named(const char* name, float* out, bool assert)
    {
        return get_self()->fetch_float(name, out, assert);
    }
    bool read_sub_struct(uint32_t name, Struct** out, bool assert)
    {
        return get_self()->read_sub_struct(name, out, assert);
    }
    bool read_hash_value(uint32_t name, uint32_t* out, bool assert)
    {
        return get_self()->read_hash_value(name, out, assert);
    }
    bool get_checksum_named(const char* name, uint32_t* out, bool assert)
    {
        return get_self()->read_hash_value(name, out, assert);
    }
    bool fetch_list(uint32_t name, Array** out, bool assert) { return get_self()->fetch_list(name, out, assert); }
    bool get_array_named(const char* name, Array** out, bool assert)
    {
        return get_self()->fetch_list(name, out, assert);
    }
    bool fetch_vec(uint32_t name, VectorValue* out, bool assert) { return get_self()->fetch_vec(name, out, assert); }
    bool get_vector_named(const char* name, VectorValue* out, bool assert)
    {
        return get_self()->fetch_vec(name, out, assert);
    }
};

template <typename R, typename... A>
using Method = R (Struct::*)(A...);

} // namespace

// --- owning values: strings, pooled objects, cleaning up and copying ------------------------------------------

namespace script {

// The CStruct pool pair (the current pool's index, then the two pools).
memory::PoolPair g_struct_pools;

// A permanent string table entry: the string's checksum and the string.
struct PermanentString {
    uint32_t checksum;
    char* string;
};
PermanentString* g_permanent_string_table;
namespace {

// The pool pairs of the script's value objects.
memory::PoolPair& component_pools() { return g_component_pools; }
memory::PoolPair& struct_pools() { return g_struct_pools; }
memory::PoolPair& array_pools() { return g_array_pools; }
memory::PoolPair& pair_pools() { return g_pair_pools; }
memory::PoolPair& vector_pools() { return g_vector_pools; }

// The permanent string cache (used while loading the game's scripts): one block, strings shared by checksum.
bool& use_permanent_strings() { return g_use_permanent_strings; }
char*& permanent_strings_start() { return g_permanent_strings_start; }
char*& permanent_strings_next() { return g_permanent_strings_next; }
uint32_t& permanent_strings_size() { return g_permanent_strings_size; }
PermanentString*& permanent_string_table() { return g_permanent_string_table; }
uint32_t& permanent_string_count() { return g_permanent_string_count; }

// 0x00476260 (the string in EAX: called only by spawn_text, so registered source-only)
char* create_permanent_string(const char* string)
{
    int length = static_cast<int>(std::strlen(string));
    uint32_t checksum = checksums::generate_crc_from_buffer(string, length);
    PermanentString* table = permanent_string_table();
    for (uint32_t i = 0; i < permanent_string_count(); ++i) {
        if (table[i].checksum == checksum)
            return table[i].string;
    }
    char* copy = permanent_strings_next();
    std::memcpy(copy, string, static_cast<size_t>(length) + 1);
    permanent_strings_next() = copy + length + 1;
    table[permanent_string_count()] = {checksum, copy};
    ++permanent_string_count();
    return copy;
}

void delete_pair(Pair* pair) { pair_pools().dealloc(pair); }
void delete_vector(Vector* vector) { vector_pools().dealloc(vector); }

void merge_structure(Struct* to, Struct* from) { to->merge_structure(from); }

} // namespace

// 0x004763A0
char* spawn_text(const char* string)
{
    if (use_permanent_strings())
        return create_permanent_string(string);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    size_t size = std::strlen(string) + 1;
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
    if (permanent_string_table()) {
        memory::free_malloc(permanent_string_table());
        permanent_string_table() = nullptr;
    }
}

namespace {
char g_script_strings[128]; // (the strings SetScriptString / GetScriptString use, 128 bytes each, indexed from here)
char (&script_string_slot(int index))[128] { return reinterpret_cast<char (*)[128]>(&g_script_strings)[index]; }
} // namespace

// 0x00476370
int set_script_string(int index, const char* string)
{
    THUG2_MSVC_WARNING(suppress : 4996) // (unbounded, and the string is the format: as in the original)
    return std::sprintf(script_string_slot(index), string);
}

// 0x00476390
char* get_script_string(int index) { return script_string_slot(index); }

// 0x00476340 (strings in the permanent block are never freed)
void free_string(const char* string)
{
    const char* start = permanent_strings_start();
    if (string >= start && string < start + permanent_strings_size())
        return;
    memory::free_malloc(const_cast<char*>(string));
}

// 0x00409030 / 0x00409070
Struct* spawn_param_bag() { return struct_pools().allocate<Struct>(); }
void delete_struct(Struct* structure) { struct_pools().dealloc(structure); }
// 0x00408F90 / 0x00408FD0
Array* alloc_script_array() { return array_pools().allocate<Array>(); }
void delete_array(Array* array) { array_pools().dealloc(array); }
void free_component(Component* component) { component_pools().dealloc(component); }

// 0x0046CAA0
Array::Array()
{
    kind_tag_ = kQbvalNone;
    elements_ = nullptr;
    extent_ = 0;
    unknown0_ = 0;
}

// 0x0046CAB0
void Array::get_clear()
{
    if (extent_ == 1) {
        // (the single element is in place: nothing to free)
        extent_ = 0;
        kind_tag_ = kQbvalNone;
        if (elements_)
            elements_ = nullptr;
        return;
    }
    if (elements_) {
        memory::free_malloc(elements_);
        elements_ = nullptr;
    }
    kind_tag_ = kQbvalNone;
    extent_ = 0;
}

// 0x0046CAF0
void Array::change_species(QbValueKind type) { kind_tag_ = type; }

// 0x0046CB00
uint32_t* Array::find_array_value() { return extent_ == 1 ? &single_ : elements_; }

// 0x00477140
void release_array_storage(Array* array)
{
    uint32_t count = array->extent_;
    QbValueKind type = array->kind_tag_;
    uint32_t* elements = array->find_array_value();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t& element = elements[i];
        if (!element)
            continue;
        switch (type) {
        case kQbvalString:
        case kQbvalInlineText:
            free_string(reinterpret_cast<const char*>(element));
            break;
        case kQbvalPair:
            delete_pair(reinterpret_cast<Pair*>(element));
            break;
        case kQbvalVector:
            delete_vector(reinterpret_cast<Vector*>(element));
            break;
        case kQbvalStruct: {
            auto* structure = reinterpret_cast<Struct*>(element);
            structure->get_clear();
            delete_struct(structure);
            break;
        }
        case kQbvalArray: {
            auto* inner = reinterpret_cast<Array*>(element);
            release_array_storage(inner);
            inner->get_clear();
            delete_array(inner);
            break;
        }
        default:
            continue; // (other elements own nothing, and are left as they are)
        }
        element = 0;
    }
    array->get_clear();
}

// 0x004772A0
void release_component(Component* component)
{
    switch (component->kind_tag()) {
    case kQbvalString:
    case kQbvalInlineText:
        if (component->string)
            free_string(component->string);
        break;
    case kQbvalPair:
        if (component->pair)
            delete_pair(component->pair);
        break;
    case kQbvalVector:
        if (component->vector)
            delete_vector(component->vector);
        break;
    case kQbvalScriptBody:
        if (component->raw)
            memory::free_malloc(reinterpret_cast<void*>(component->raw));
        component->unknown2 = 0; // the script's size
        break;
    case kQbvalStruct:
        if (Struct* structure = component->structure) {
            structure->get_clear();
            delete_struct(structure);
        }
        break;
    case kQbvalArray:
        if (Array* array = component->array) {
            release_array_storage(array);
            array->get_clear();
            delete_array(array);
        }
        break;
    default:
        break;
    }
    component->raw = 0;
    component->type_and_flag_ &= 1;
}

// 0x004773E0
void duplicate_component(Component* dest, Component* source, int reference)
{
    if (dest->raw)
        release_component(dest);
    dest->type_and_flag_ = static_cast<uint8_t>((source->type_and_flag_ & ~1) | (dest->type_and_flag_ & 1));
    dest->name = source->name;
    switch (source->kind_tag()) {
    case kQbvalString:
    case kQbvalInlineText:
        dest->string = spawn_text(source->string);
        break;
    case kQbvalPair: {
        auto* pair = pair_pools().allocate<Pair>();
        if (pair) {
            // (the constructor: only the first byte of the first word is cleared)
            pair->y = 0.0f;
            pair->x = 0.0f;
            reinterpret_cast<uint8_t*>(pair)[0] = 0;
        }
        dest->pair = pair;
        pair->x = source->pair->x;
        dest->pair->y = source->pair->y;
        break;
    }
    case kQbvalVector: {
        auto* vector = vector_pools().allocate<Vector>();
        if (vector) {
            vector->z = 0.0f;
            vector->y = 0.0f;
            vector->x = 0.0f;
            reinterpret_cast<uint8_t*>(vector)[0] = 0;
        }
        dest->vector = vector;
        vector->x = source->vector->x;
        dest->vector->y = source->vector->y;
        dest->vector->z = source->vector->z;
        break;
    }
    case kQbvalScriptBody: {
        // The compiled script, `unknown2` bytes.
        memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
        dest->raw = reinterpret_cast<uint32_t>(memory::alloc_block(source->unknown2));
        dest->unknown2 = source->unknown2;
        memory::leave_heap_scope();
        auto* from = reinterpret_cast<const uint8_t*>(source->raw);
        auto* to = reinterpret_cast<uint8_t*>(dest->raw);
        for (uint32_t i = 0; i < source->unknown2; ++i)
            to[i] = from[i];
        break;
    }
    case kQbvalStruct: {
        if (reference) {
            dest->type_and_flag_ = static_cast<uint8_t>((dest->type_and_flag_ & 1) | (kQbvalStructRef << 1));
            dest->structure = source->structure;
            break;
        }
        Struct* structure = spawn_param_bag();
        if (structure)
            new (structure) script::Struct;
        dest->structure = structure;
        if (source->structure != structure) {
            structure->get_clear();
            merge_structure(structure, source->structure);
        }
        break;
    }
    case kQbvalArray: {
        if (!reference) {
            Array* array = alloc_script_array();
            if (array)
                new (array) script::Array;
            dest->array = array;
            duplicate_array(array, source->array);
            break;
        }
        dest->type_and_flag_ = static_cast<uint8_t>((dest->type_and_flag_ & 1) | (kEsymboltypeArraypointer << 1));
        dest->raw = source->raw;
        break;
    }
    case kQbvalInteger:
    case kQbvalFloat:
    case kQbvalName:
    case kQbvalStructRef:
    case kEsymboltypeArraypointer:
        dest->raw = source->raw;
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
        if (component->name == name && component->kind_tag() == type) {
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
    component->raw = 0;
    component->next = nullptr;
    component->type_and_flag_ = 0;
    return component;
}

namespace {

Component* new_typed_component(uint32_t name, QbValueKind type)
{
    Component* component = new_component();
    component->name = name;
    component->type_and_flag_ = static_cast<uint8_t>((component->type_and_flag_ & 1) | (type << 1));
    return component;
}

} // namespace

// 0x0044C130 (+ the constructor, as the callers do)
memory::owner<Component*> new_component()
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
        if (component->name == 0 && component->raw == flag) {
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
            QbValueKind mine = component->kind_tag();
            QbValueKind theirs = added->kind_tag();
            if (mine == theirs)
                replace = !(mine == kQbvalName && component->name == 0) || component->raw == added->raw;
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
    Component* component = new_typed_component(name, kQbvalString);
    component->string = spawn_text(value);
    attach_module(component);
}

// 0x00477A90
void Struct::append_word(const char* name, const char* value) { append_word(checksums::hash_from_text(name), value); }

// 0x00477AC0
void Struct::append_inline_text(uint32_t name, const char* value)
{
    Component* component = new_typed_component(name, kQbvalInlineText);
    component->string = spawn_text(value);
    attach_module(component);
}

// 0x00478390
void Struct::append_word(uint32_t name, uint32_t type, const char* value)
{
    if (type == kQbvalString)
        append_word(name, value);
    else
        append_inline_text(name, value);
}

// 0x00477B80
void Struct::put_int(uint32_t name, int value)
{
    Component* component = new_typed_component(name, kQbvalInteger);
    component->integer = value;
    attach_module(component);
}

// 0x00477C30
void Struct::put_int(const char* name, int value) { put_int(checksums::hash_from_text(name), value); }

// 0x00477C60
void Struct::push_float(uint32_t name, float value)
{
    Component* component = new_typed_component(name, kQbvalFloat);
    component->float_value_ = value;
    attach_module(component);
}

// 0x00477D10
void Struct::push_float(const char* name, float value) { push_float(checksums::hash_from_text(name), value); }

// 0x00477D40
void Struct::put_name_key(uint32_t name, uint32_t value)
{
    Component* component = new_typed_component(name, kQbvalName);
    component->checksum = value;
    attach_module(component);
}

// 0x00477DF0
void Struct::put_name_key(const char* name, uint32_t value) { put_name_key(checksums::hash_from_text(name), value); }

// 0x00477E20
void Struct::put_vec3(uint32_t name, float x, float y, float z)
{
    Component* component = new_typed_component(name, kQbvalVector);
    auto* vector = vector_pools().allocate<Vector>();
    if (vector) {
        vector->z = 0.0f;
        vector->y = 0.0f;
        vector->x = 0.0f;
        reinterpret_cast<uint8_t*>(vector)[0] = 0;
    }
    component->vector = vector;
    vector->x = x;
    component->vector->y = y;
    component->vector->z = z;
    attach_module(component);
}

// 0x00477F40
void Struct::put_vec3(const char* name, float x, float y, float z)
{
    put_vec3(checksums::hash_from_text(name), x, y, z);
}

// 0x00477F70
void Struct::put_vec3(uint32_t name, VectorValue value) { put_vec3(name, value.x, value.y, value.z); }

// 0x00477F90
void Struct::put_vec3(const char* name, VectorValue value)
{
    put_vec3(checksums::hash_from_text(name), value.x, value.y, value.z);
}

// 0x00477FC0
void Struct::insert_pair(uint32_t name, float x, float y)
{
    Component* component = new_typed_component(name, kQbvalPair);
    auto* pair = pair_pools().allocate<Pair>();
    if (pair) {
        pair->y = 0.0f;
        pair->x = 0.0f;
        reinterpret_cast<uint8_t*>(pair)[0] = 0;
    }
    component->pair = pair;
    pair->x = x;
    component->pair->y = y;
    attach_module(component);
}

// 0x004780D0
void Struct::append_array_ref(uint32_t name, Array* value)
{
    Component* component = new_typed_component(name, kQbvalArray);
    component->array = value;
    attach_module(component);
}

// 0x00478180
void Struct::append_array_ref(const char* name, Array* value)
{
    append_array_ref(checksums::hash_from_text(name), value);
}

// 0x004781B0
void Struct::append_struct_ref(uint32_t name, Struct* value)
{
    Component* component = new_typed_component(name, kQbvalStruct);
    component->structure = value;
    attach_module(component);
}

// 0x00478260
void Struct::append_struct_ref(const char* name, Struct* value)
{
    append_struct_ref(checksums::hash_from_text(name), value);
}

// 0x00478670
void Struct::put_sub_struct(uint32_t name, Struct* value)
{
    Component* component = new_typed_component(name, kQbvalStruct);
    Struct* copy = spawn_param_bag();
    if (copy)
        new (copy) script::Struct;
    component->structure = copy;
    if (value != copy) {
        copy->get_clear();
        copy->merge_structure(value);
    }
    attach_module(component);
}

// 0x00478770
void Struct::put_sub_struct(const char* name, Struct* value) { put_sub_struct(checksums::hash_from_text(name), value); }

// 0x00478290
void Struct::append_script(uint32_t name, const uint8_t* code, uint32_t size)
{
    Component* component = new_typed_component(name, kQbvalScriptBody);
    component->unknown2 = static_cast<uint16_t>(size);
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    auto* copy = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::leave_heap_scope();
    for (uint32_t i = 0; i < size; ++i)
        copy[i] = code[i];
    component->raw = reinterpret_cast<uint32_t>(copy);
    attach_module(component);
}

// 0x004783C0
void Struct::add_component_of_type(uint32_t name, uint32_t type, uint32_t value)
{
    switch (type) {
    case kQbvalInteger:
        put_int(name, static_cast<int>(value));
        break;
    case kQbvalFloat: {
        auto number = std::bit_cast<float>(value);
        push_float(name, number);
        break;
    }
    case 11:
        append_struct_ref(name, reinterpret_cast<Struct*>(value));
        break;
    case kQbvalArray:
        append_array_ref(name, reinterpret_cast<Array*>(value));
        break;
    case kQbvalName:
        put_name_key(name, value);
        break;
    default:
        break;
    }
}

// 0x00478480
Struct* Struct::merge_structure(Struct* source)
{
    for (Component* component = source->head_; component; component = component->next) {
        Component* copy = new_component();
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
    for (Component* component = head_; component; component = component->next) {
        if (Struct* included = included_structure(component)) {
            included->inline_into(dest, depth + 1);
            continue;
        }
        Component* copy = new_component();
        duplicate_component(copy, component, 0);
        dest->attach_module(copy);
    }
}

} // namespace script

namespace {

using script::Array;
using script::Struct;
template <typename... A>
using AddMethod = void (Struct::*)(A...);

// 0x0044C170: free a component (pooled)
void delete_component_adapter(script::Component* component) { script::free_component(component); }
// 0x0044C130: operator new (the size is always 16)
void* new_component_memory(unsigned int size) { return script::g_component_pools.carve(size); }
struct ComponentAdapter {
    script::Component* make_new() { return script::construct_component(reinterpret_cast<script::Component*>(this)); }
};

} // namespace

// --- arrays: sizing, elements, copying ------------------------------------------------------------------------

namespace script {

namespace {

// Allocate `count` elements on the script heap.
uint32_t* allocate_elements(uint32_t count)
{
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap_);
    auto* elements = static_cast<uint32_t*>(memory::alloc_block(count * 4));
    memory::leave_heap_scope();
    return elements;
}

// 0x006F3610 / 0x0067DA30: how many structures were made by copying, and the most there were (debug counters).
int& struct_copy_count() { return g_struct_copy_count; }
int& struct_copy_peak() { return g_struct_copy_peak; }

} // namespace

// 0x0046D410 / 0x0046D3D0 (operator new; the size is always the object's)
Pair* new_pair() { return g_pair_pools.allocate<Pair>(); }
Vector* alloc_script_vector() { return g_vector_pools.allocate<Vector>(); }

// 0x0047AC00
Pair::Pair()
{
    y = 0.0f;
    x = 0.0f;
    reinterpret_cast<uint8_t*>(this)[0] = 0;
}

// 0x0047ABF0
Vector::Vector()
{
    z = 0.0f;
    y = 0.0f;
    x = 0.0f;
    reinterpret_cast<uint8_t*>(this)[0] = 0;
}

// 0x0046CC50
void Array::configure_size_kind(uint32_t size, QbValueKind type)
{
    if (extent_ == 1) {
        // (the single element is in place: nothing to free)
        if (elements_)
            elements_ = nullptr;
    } else if (elements_) {
        memory::free_malloc(elements_);
        elements_ = nullptr;
    }
    kind_tag_ = type;
    extent_ = size;
    if (size == 0) {
        kind_tag_ = kQbvalNone;
        return;
    }
    if (size == 1)
        return;
    elements_ = allocate_elements(extent_);
    for (uint32_t i = 0; i < extent_; ++i)
        elements_[i] = 0;
}

// 0x0046CCF0
void Array::rescale(uint32_t size)
{
    if (size == extent_)
        return;
    uint32_t* elements = allocate_elements(size);
    const uint32_t* from = find_array_value();
    for (uint32_t i = 0; i < extent_; ++i)
        elements[i] = from[i];
    for (uint32_t i = extent_; i < size; ++i)
        elements[i] = 0;
    if (extent_ > 1)
        memory::free_malloc(elements_);
    // (growing to one element leaves the block's pointer in place of the single value, as the original does)
    elements_ = elements;
    extent_ = size;
}

// 0x0046CB10
void Array::set_element(uint32_t index, uint32_t value)
{
    if (extent_ == 1)
        single_ = value;
    else
        elements_[index] = value;
}

// 0x0046CB40
void Array::change_float(uint32_t index, float value)
{
    if (extent_ == 1)
        std::memcpy(&single_, &value, 4);
    else
        std::memcpy(&elements_[index], &value, 4);
}

// 0x0046CC30
uint32_t Array::fetch_part(uint32_t index) { return extent_ == 1 ? single_ : elements_[index]; }

// 0x0046CBB0
const char* Array::fetch_text(uint32_t index) { return reinterpret_cast<const char*>(fetch_part(index)); }

// 0x0046CB70
const char* Array::find_inline_text(uint32_t index) { return reinterpret_cast<const char*>(fetch_part(index)); }

// 0x0046CBF0
float Array::fetch_float(uint32_t index)
{
    uint32_t bits = fetch_part(index);
    if (kind_tag_ == kQbvalFloat) {
        auto value = std::bit_cast<float>(bits);
        return value;
    }
    return static_cast<float>(static_cast<int32_t>(bits));
}

// 0x004787A0
void copy_array_elements(uint32_t* dest, const uint32_t* source, uint32_t count, uint32_t type)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t element = source[i];
        switch (type) {
        case kQbvalInteger:
        case kQbvalFloat:
        case kQbvalName:
            dest[i] = element;
            break;
        case kQbvalString:
        case kQbvalInlineText:
            if (element)
                dest[i] = reinterpret_cast<uint32_t>(spawn_text(reinterpret_cast<const char*>(element)));
            break;
        case kQbvalPair:
            if (element) {
                auto* from = reinterpret_cast<const Pair*>(element);
                Pair* pair = new_pair();
                pair = pair ? new (pair) script::Pair : nullptr;
                pair->x = from->x;
                pair->y = from->y;
                dest[i] = reinterpret_cast<uint32_t>(pair);
            }
            break;
        case kQbvalVector:
            if (element) {
                auto* from = reinterpret_cast<const Vector*>(element);
                Vector* vector = alloc_script_vector();
                vector = vector ? new (vector) script::Vector : nullptr;
                vector->x = from->x;
                vector->y = from->y;
                vector->z = from->z;
                dest[i] = reinterpret_cast<uint32_t>(vector);
            }
            break;
        case kQbvalStruct:
            if (element) {
                auto* from = reinterpret_cast<Struct*>(element);
                Struct* structure = spawn_param_bag();
                if (structure)
                    new (structure) script::Struct;
                if (from != structure) {
                    structure->get_clear();
                    structure->merge_structure(from);
                }
                dest[i] = reinterpret_cast<uint32_t>(structure);
            }
            break;
        case kQbvalArray:
            if (element) {
                Array* array = alloc_script_array();
                if (array)
                    array = new (array) script::Array;
                duplicate_array(array, reinterpret_cast<Array*>(element));
                dest[i] = reinterpret_cast<uint32_t>(array);
            }
            break;
        default:
            // Scripts, functions, unknowns: nothing is copied.
            return;
        }
    }
}

// 0x00478A60
void duplicate_array(Array* dest, Array* source)
{
    release_array_storage(dest);
    QbValueKind type = source->kind_tag_;
    uint32_t size = source->extent_;
    dest->configure_size_kind(size, type);
    if (size)
        copy_array_elements(dest->find_array_value(), source->find_array_value(), size, type);
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
SymbolTableEntry*& iterator_entry() { return g_iterator_entry; }
uint32_t& iterator_bucket() { return g_iterator_bucket; }

// A global symbol's value as an integer / float (other types: 0).
int symbol_integer(SymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type == kQbvalInteger)
            return entry->integer;
        if (entry->type == kQbvalFloat)
            return static_cast<int>(entry->float_value_);
    }
    return 0;
}
float symbol_float(SymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type == kQbvalInteger)
            return static_cast<float>(entry->integer);
        if (entry->type == kQbvalFloat)
            return entry->float_value_;
    }
    return 0.0f;
}
uint32_t symbol_value_of_type(SymbolTableEntry* entry, QbValueKind type)
{
    return entry && entry->type == type ? entry->raw : 0;
}

// The game's empty string (what the string getters give for missing symbols).
const char* empty_string() { return ""; }

} // namespace

// 0x00478C90
void create_symbol_table()
{
    auto* table = static_cast<SymbolTableEntry**>(memory::alloc_block(0x1000 * sizeof(SymbolTableEntry*)));
    g_name_table = table;
    for (uint32_t i = 0; i < 0x1000; ++i)
        table[i] = nullptr;
}

// 0x004790F0
SymbolTableEntry::SymbolTableEntry()
{
    unknown0 &= 0xF8;
    name = 0;
    type = kQbvalNone;
    raw = 0;
    next = nullptr;
    unknown8 = 0;
}

// 0x00479110
SymbolTableEntry* create_symbol(uint32_t name)
{
    void* memory = g_symbol_pools.carve(sizeof(SymbolTableEntry));
    SymbolTableEntry* entry = memory ? new (memory) script::SymbolTableEntry : nullptr;
    entry->name = name;
    entry->unknown0 |= 1;
    link_symbol(entry);
    return entry;
}

// 0x00478DC0
void link_symbol(SymbolTableEntry* entry)
{
    SymbolTableEntry*& head = script_symbol_slots()[entry->name & kSymbolTableMask];
    entry->next = head;
    head = entry;
}

// 0x00478D60
void unlink_symbol(SymbolTableEntry* entry)
{
    SymbolTableEntry** bucket = &script_symbol_slots()[entry->name & kSymbolTableMask];
    SymbolTableEntry* previous = nullptr;
    for (SymbolTableEntry* each = *bucket; each; previous = each, each = each->next) {
        if (each != entry)
            continue;
        if (previous)
            previous->next = entry->next;
        else
            *bucket = entry->next;
        entry->next = nullptr;
        return;
    }
}

// 0x00478E50
SymbolTableEntry* get_next_symbol(SymbolTableEntry* previous)
{
    SymbolTableEntry* entry;
    if (!previous) {
        iterator_bucket() = 0;
        entry = script_symbol_slots()[0];
    } else {
        entry = previous->next;
    }
    while (!entry) {
        if (++iterator_bucket() >= 0x1000) {
            iterator_bucket() = 0;
            iterator_entry() = nullptr;
            return nullptr;
        }
        entry = script_symbol_slots()[iterator_bucket()];
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
    SymbolTableEntry* entry = resolve_symbol(name);
    return entry ? entry->raw : 0;
}
uint32_t get_symbol_value(const char* name) { return get_symbol_value(checksums::hash_from_text(name)); }
// 0x00478FD0 / 0x00478FF0 (any type: the raw value)
const char* fetch_text(uint32_t name)
{
    SymbolTableEntry* entry = resolve_symbol(name);
    return entry ? reinterpret_cast<const char*>(entry->raw) : empty_string();
}
const char* fetch_text(const char* name) { return fetch_text(checksums::hash_from_text(name)); }
// 0x00479020 / 0x00479040
Struct* read_sub_struct(uint32_t name)
{
    return reinterpret_cast<Struct*>(symbol_value_of_type(resolve_symbol(name), kQbvalStruct));
}
Struct* read_sub_struct(const char* name) { return read_sub_struct(checksums::hash_from_text(name)); }
// 0x00479070 / 0x00479090
Array* fetch_list(uint32_t name)
{
    return reinterpret_cast<Array*>(symbol_value_of_type(resolve_symbol(name), kQbvalArray));
}
Array* fetch_list(const char* name) { return fetch_list(checksums::hash_from_text(name)); }

// 0x004791A0
bool components_equal(Component* a, Component* b)
{
    // A name standing for a global symbol counts as the symbol's value.
    auto resolve = [](Component* component, uint32_t* type, uint32_t* value) {
        *type = component->kind_tag();
        *value = component->raw;
        if (*type == kQbvalName) {
            if (SymbolTableEntry* symbol = resolve_symbol(*value)) {
                *value = symbol->raw;
                *type = symbol->type;
            }
        }
    };
    uint32_t type_a, value_a, type_b, value_b;
    resolve(a, &type_a, &value_a);
    resolve(b, &type_b, &value_b);
    auto as_float = [](uint32_t bits) {
        auto value = std::bit_cast<float>(bits);
        return static_cast<double>(value);
    };
    // (integers are compared with floats exactly, as the x87 does)
    auto as_integer = [](uint32_t bits) { return static_cast<double>(static_cast<int32_t>(bits)); };
    switch (type_a) {
    case kQbvalInteger:
        if (type_b == kQbvalFloat)
            return as_integer(value_a) == as_float(value_b);
        return type_b == kQbvalInteger && value_a == value_b;
    case kQbvalFloat:
        if (type_b == kQbvalInteger)
            return as_float(value_a) == as_integer(value_b);
        return type_b == kQbvalFloat && as_float(value_a) == as_float(value_b);
    case kQbvalString:
    case kQbvalInlineText:
        return type_b == type_a &&
               std::strcmp(reinterpret_cast<const char*>(value_a), reinterpret_cast<const char*>(value_b)) == 0;
    case kQbvalPair: {
        if (type_b != kQbvalPair)
            return false;
        auto* pa = reinterpret_cast<const Pair*>(value_a);
        auto* pb = reinterpret_cast<const Pair*>(value_b);
        return pa->x == pb->x && pa->y == pb->y;
    }
    case kQbvalVector: {
        if (type_b != kQbvalVector)
            return false;
        auto* va = reinterpret_cast<const Vector*>(value_a);
        auto* vb = reinterpret_cast<const Vector*>(value_b);
        return va->x == vb->x && va->y == vb->y && va->z == vb->z;
    }
    case kQbvalScriptBody:
    case kQbvalName:
        return type_b == type_a && value_a == value_b;
    case kQbvalStruct:
    case kQbvalArray:
        // (different arrays are never equal: their comparison 0x0049F4B0 just returns false)
        return type_b == type_a && value_a == value_b;
    default:
        return false;
    }
}

// 0x00478B00
void Struct::append_array_value(uint32_t name, Array* value)
{
    Component* component = new_typed_component(name, kQbvalArray);
    Array* array = alloc_script_array();
    if (array)
        array = new (array) script::Array;
    component->array = array;
    duplicate_array(array, value);
    attach_module(component);
}

// 0x00478C50
void Struct::append_array_value(const char* name, Array* value)
{
    append_array_value(checksums::hash_from_text(name), value);
}

} // namespace script

namespace {


// 0x00479190: the type-name lookup was compiled out: it gives a "(Hey, don't use this...)" message.
const char* get_type_name() { return "(Hey, don't use GetTypeName on the CD!)"; }

// components_equal is a member of the first component in the game.
struct ComponentEqualsAdapter {
    bool get_equals(script::Component* other)
    {
        return script::components_equal(reinterpret_cast<script::Component*>(this), other);
    }
};

template <typename R, typename... A>
using Routine = R (*)(A...);

} // namespace
