#include "game/script/precompiled.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/script/qb.h"

namespace Script {

namespace {

// A stored offset made an address.
void Relocate(uint32_t& offset, uint32_t base) { offset += base; }
template <typename T>
void Relocate(T*& pointer, uint32_t base)
{
    pointer = reinterpret_cast<T*>(reinterpret_cast<uint32_t>(pointer) + base);
}
uint32_t Align4(uint32_t size) { return (size + 3) & ~3u; }
// The size of a string's text in the file (with its terminator, padded).
uint32_t StoredStringSize(const char* text) { return Align4(static_cast<uint32_t>(std::strlen(text)) + 1); }

} // namespace

// 0x0046C7B0 (a CArray method)
void RelocateArrayElements(CArray* array, uint32_t base)
{
    if (array->m_size > 1) {
        for (uint32_t i = 0; i < array->m_size; ++i)
            array->m_elements[i] += base;
    } else if (array->m_size == 1) {
        array->m_single += base;
    }
}

// 0x0046C830
uint8_t* RelocateArray(CArray* array, uint32_t base)
{
    uint32_t size = array->m_size;
    auto* end = reinterpret_cast<uint8_t*>(array + 1);
    uint32_t* elements;
    if (size > 1) {
        if (array->m_elements)
            Relocate(array->m_elements, base);
        elements = array->m_elements;
        end += size * 4; // (the element table follows the array)
    } else {
        elements = &array->m_single;
    }
    switch (array->m_type) {
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING: {
        RelocateArrayElements(array, base);
        // (the texts one after another, padded once at the end)
        uint32_t total = 0;
        for (uint32_t i = 0; i < size; ++i)
            total += static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(elements[i]))) + 1;
        return end + Align4(total);
    }
    case ESYMBOLTYPE_PAIR:
        RelocateArrayElements(array, base);
        return end + size * sizeof(CPair);
    case ESYMBOLTYPE_VECTOR:
        RelocateArrayElements(array, base);
        return end + size * sizeof(CVector);
    case ESYMBOLTYPE_STRUCTURE:
        RelocateArrayElements(array, base);
        for (uint32_t i = 0; i < size; ++i)
            end = RelocateStruct(reinterpret_cast<CStruct*>(elements[i]), base);
        return end;
    case ESYMBOLTYPE_ARRAY:
        RelocateArrayElements(array, base);
        for (uint32_t i = 0; i < size; ++i)
            end = RelocateArray(reinterpret_cast<CArray*>(elements[i]), base);
        return end;
    default: // (values kept in the element table: nothing to relocate)
        return end;
    }
}

// 0x0046C6B0
uint8_t* RelocateComponent(CComponent* component, uint32_t base)
{
    if (component->next)
        Relocate(component->next, base);
    auto* end = reinterpret_cast<uint8_t*>(component + 1);
    switch (component->Type()) {
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        Relocate(component->string, base);
        return end + StoredStringSize(component->string);
    case ESYMBOLTYPE_PAIR:
        Relocate(component->pair, base);
        return end + sizeof(CPair);
    case ESYMBOLTYPE_VECTOR:
        Relocate(component->vector, base);
        return end + sizeof(CVector);
    case ESYMBOLTYPE_QSCRIPT: {
        // A script component: its tokens, up to and including the '$'.
        Relocate(component->raw, base);
        auto* code = reinterpret_cast<const uint8_t*>(component->raw);
        return end + Align4(static_cast<uint32_t>(SkipToEndOfScript(code) - code));
    }
    case ESYMBOLTYPE_STRUCTURE:
        Relocate(component->structure, base);
        return RelocateStruct(component->structure, base);
    case ESYMBOLTYPE_ARRAY:
        Relocate(component->array, base);
        return RelocateArray(component->array, base);
    default:
        return end;
    }
}

// 0x0046C7F0
uint8_t* RelocateStruct(CStruct* structure, uint32_t base)
{
    if (structure->m_head)
        Relocate(structure->m_head, base);
    // (the end of the last component's data, or of the structure itself when it has none)
    auto* end = reinterpret_cast<uint8_t*>(structure + 1);
    for (CComponent* component = structure->m_head; component; component = component->next)
        end = RelocateComponent(component, base);
    return end;
}

// 0x0046C970
uint8_t* RelocateSymbol(CSymbolTableEntry* symbol, uint32_t base)
{
    auto* end = reinterpret_cast<uint8_t*>(symbol + 1);
    switch (symbol->type) {
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        Relocate(symbol->raw, base);
        return end + StoredStringSize(reinterpret_cast<const char*>(symbol->raw));
    case ESYMBOLTYPE_PAIR:
        Relocate(symbol->raw, base);
        return end + sizeof(CPair);
    case ESYMBOLTYPE_VECTOR:
        Relocate(symbol->raw, base);
        return end + sizeof(CVector);
    case ESYMBOLTYPE_QSCRIPT: {
        // {checksum, size, stored size, code}: the stored size is read from right after the entry (where the
        // script is), not through the value.
        uint32_t stored = reinterpret_cast<const uint32_t*>(end)[2];
        Relocate(symbol->raw, base);
        return end + 12 + Align4(stored);
    }
    case ESYMBOLTYPE_STRUCTURE:
        Relocate(symbol->structure, base);
        return RelocateStruct(symbol->structure, base);
    case ESYMBOLTYPE_ARRAY:
        Relocate(symbol->raw, base);
        return RelocateArray(reinterpret_cast<CArray*>(symbol->raw), base);
    default: // (integers, floats, names, C functions...: the value is in the entry)
        return end;
    }
}

// 0x0046CA50
void RelocatePrecompiledSymbols(uint8_t* file, uint32_t base, PrecompiledSymbolCallback callback, uint32_t argument,
                                uint32_t flag)
{
    uint8_t* end = file + *reinterpret_cast<const uint32_t*>(file + 4);
    uint8_t* p = file + 0x1C;
    if (p == end)
        return;
    // (the symbols must end exactly at the file's end: there's no other check)
    do {
        auto* symbol = reinterpret_cast<CSymbolTableEntry*>(p);
        uint8_t* next = RelocateSymbol(symbol, base);
        if (callback)
            callback(symbol, argument, flag);
        p = next;
    } while (p != end);
}

// 0x00478DE0
void DefinePrecompiledSymbol(CSymbolTableEntry* symbol, uint32_t fileChecksum, uint32_t unused)
{
    (void)unused;
    symbol->unknown8 = fileChecksum;
    if (CSymbolTableEntry* existing = FindSymbol(symbol->name))
        CleanUpAndRemoveSymbol(existing);
    LinkSymbol(symbol);
}

// 0x0046FBA0
void ParsePrecompiledQB(const char* filename, uint8_t* data, int unused)
{
    uint32_t flag = unused != 0;
    RelocatePrecompiledSymbols(data, reinterpret_cast<uint32_t>(data), DefinePrecompiledSymbol,
                               Crc::GenerateCRCFromString(filename), flag);
}

} // namespace Script

namespace {
struct ArrayAdapter {
    void RelocateElements(uint32_t base) { Script::RelocateArrayElements(reinterpret_cast<Script::CArray*>(this), base); }
};
} // namespace
