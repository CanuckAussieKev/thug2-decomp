#include "engine/script/precompiled.h"

#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/script/qb.h"

namespace script {

namespace {

// A stored offset made an address.
void relocate(uint32_t& offset, uint32_t base) { offset += base; }
template <typename T>
void relocate(T*& pointer, uint32_t base)
{
    pointer = reinterpret_cast<T*>(reinterpret_cast<uint32_t>(pointer) + base);
}
uint32_t align4(uint32_t size) { return (size + 3) & ~3u; }
// The size of a string's text in the file (with its terminator, padded).
uint32_t stored_string_size(const char* text) { return align4(static_cast<uint32_t>(std::strlen(text)) + 1); }

} // namespace

// 0x0046C7B0 (a CArray method)
void relocate_array_elements(Array* array, uint32_t base)
{
    if (array->extent_ > 1) {
        for (uint32_t i = 0; i < array->extent_; ++i)
            array->elements_[i] += base;
    } else if (array->extent_ == 1) {
        array->single_ += base;
    }
}

// 0x0046C830
uint8_t* relocate_array(Array* array, uint32_t base)
{
    uint32_t size = array->extent_;
    auto* end = reinterpret_cast<uint8_t*>(array + 1);
    uint32_t* elements;
    if (size > 1) {
        if (array->elements_)
            relocate(array->elements_, base);
        elements = array->elements_;
        end += size * 4; // (the element table follows the array)
    } else {
        elements = &array->single_;
    }
    switch (array->kind_tag_) {
    case kQbvalString:
    case kQbvalInlineText: {
        relocate_array_elements(array, base);
        // (the texts one after another, padded once at the end)
        uint32_t total = 0;
        for (uint32_t i = 0; i < size; ++i)
            total += static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(elements[i]))) + 1;
        return end + align4(total);
    }
    case kQbvalPair:
        relocate_array_elements(array, base);
        return end + size * sizeof(Pair);
    case kQbvalVector:
        relocate_array_elements(array, base);
        return end + size * sizeof(Vector);
    case kQbvalStruct:
        relocate_array_elements(array, base);
        for (uint32_t i = 0; i < size; ++i)
            end = relocate_struct(reinterpret_cast<Struct*>(elements[i]), base);
        return end;
    case kQbvalArray:
        relocate_array_elements(array, base);
        for (uint32_t i = 0; i < size; ++i)
            end = relocate_array(reinterpret_cast<Array*>(elements[i]), base);
        return end;
    default: // (values kept in the element table: nothing to relocate)
        return end;
    }
}

// 0x0046C6B0
uint8_t* relocate_component(Component* component, uint32_t base)
{
    if (component->next)
        relocate(component->next, base);
    auto* end = reinterpret_cast<uint8_t*>(component + 1);
    switch (component->kind_tag()) {
    case kQbvalString:
    case kQbvalInlineText:
        relocate(component->string, base);
        return end + stored_string_size(component->string);
    case kQbvalPair:
        relocate(component->pair, base);
        return end + sizeof(Pair);
    case kQbvalVector:
        relocate(component->vector, base);
        return end + sizeof(Vector);
    case kQbvalScriptBody: {
        // A script component: its tokens, up to and including the '$'.
        relocate(component->raw, base);
        auto* code = reinterpret_cast<const uint8_t*>(component->raw);
        return end + align4(static_cast<uint32_t>(skip_to_end_of_script(code) - code));
    }
    case kQbvalStruct:
        relocate(component->structure, base);
        return relocate_struct(component->structure, base);
    case kQbvalArray:
        relocate(component->array, base);
        return relocate_array(component->array, base);
    default:
        return end;
    }
}

// 0x0046C7F0
uint8_t* relocate_struct(Struct* structure, uint32_t base)
{
    if (structure->head_)
        relocate(structure->head_, base);
    // (the end of the last component's data, or of the structure itself when it has none)
    auto* end = reinterpret_cast<uint8_t*>(structure + 1);
    for (Component* component = structure->head_; component; component = component->next)
        end = relocate_component(component, base);
    return end;
}

// 0x0046C970
uint8_t* relocate_symbol(SymbolTableEntry* symbol, uint32_t base)
{
    auto* end = reinterpret_cast<uint8_t*>(symbol + 1);
    switch (symbol->type) {
    case kQbvalString:
    case kQbvalInlineText:
        relocate(symbol->raw, base);
        return end + stored_string_size(reinterpret_cast<const char*>(symbol->raw));
    case kQbvalPair:
        relocate(symbol->raw, base);
        return end + sizeof(Pair);
    case kQbvalVector:
        relocate(symbol->raw, base);
        return end + sizeof(Vector);
    case kQbvalScriptBody: {
        // {checksum, size, stored size, code}: the stored size is read from right after the entry (where the
        // script is), not through the value.
        uint32_t stored = reinterpret_cast<const uint32_t*>(end)[2];
        relocate(symbol->raw, base);
        return end + 12 + align4(stored);
    }
    case kQbvalStruct:
        relocate(symbol->structure, base);
        return relocate_struct(symbol->structure, base);
    case kQbvalArray:
        relocate(symbol->raw, base);
        return relocate_array(reinterpret_cast<Array*>(symbol->raw), base);
    default: // (integers, floats, names, C functions...: the value is in the entry)
        return end;
    }
}

// 0x0046CA50
void relocate_precompiled_symbols(uint8_t* file, uint32_t base, PrecompiledSymbolCallback callback, uint32_t argument,
                                  uint32_t flag)
{
    uint8_t* end = file + *reinterpret_cast<const uint32_t*>(file + 4);
    uint8_t* p = file + 0x1C;
    if (p == end)
        return;
    // (the symbols must end exactly at the file's end: there's no other check)
    do {
        auto* symbol = reinterpret_cast<SymbolTableEntry*>(p);
        uint8_t* next = relocate_symbol(symbol, base);
        if (callback)
            callback(symbol, argument, flag);
        p = next;
    } while (p != end);
}

// 0x00478DE0
void define_precompiled_symbol(SymbolTableEntry* symbol, uint32_t file_checksum, uint32_t unused)
{
    (void)unused;
    symbol->unknown8 = file_checksum;
    if (SymbolTableEntry* existing = find_symbol(symbol->name))
        clean_up_and_remove_symbol(existing);
    link_symbol(symbol);
}

// 0x0046FBA0
void parse_precompiled_qb(const char* filename, uint8_t* data, int unused)
{
    uint32_t flag = unused != 0;
    relocate_precompiled_symbols(data, reinterpret_cast<uint32_t>(data), define_precompiled_symbol,
                                 checksums::hash_from_text(filename), flag);
}

} // namespace script

namespace {
struct ArrayAdapter {
    void relocate_elements(uint32_t base)
    {
        script::relocate_array_elements(reinterpret_cast<script::Array*>(this), base);
    }
};
} // namespace
