#include "engine/script/precompiled.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <span>

#include "engine/core/checksums.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "formats/byte_stream.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"


namespace script {

namespace {

// A stored offset made an address.
void relocate(Cell& value, uint32_t base) { value = Cell::of_word(value.word() + base); }
template <typename T>
void relocate(T*& pointer, uint32_t base)
{
    pointer = cell::pointer<T>(cell::of(pointer) + base);
}
uint32_t align4(uint32_t size) { return (size + 3) & ~3u; }
// The size of a string's text in the file (with its terminator, padded).
uint32_t stored_string_size(const char* text) { return align4(static_cast<uint32_t>(std::strlen(text)) + 1); }

// NOT ORIGINAL - the images noted as blocks of code (their structures' scripts run from them: tokens.h), by their
// file's checksum, forgotten when the file is unloaded.
std::map<uint32_t, const uint8_t*>& noted_images()
{
    static std::map<uint32_t, const uint8_t*> images;
    return images;
}

} // namespace

// 0x0046C7B0 (a CArray method)
void relocate_array_elements(Array* array, uint32_t base)
{
    Cell* elements = array->find_array_value();
    for (uint32_t i = 0; i < array->extent_; ++i)
        relocate(elements[i], base);
}

// 0x0046C830
uint8_t* relocate_array(Array* array, uint32_t base)
{
    const uint32_t size = array->extent_;
    auto* end = raw::bytes(array + 1);
    if (size > 1) {
        if (!array->storage_.empty())
            relocate(array->storage_, base);
        end += size * sizeof(Cell); // (the element table follows the array)
    }
    const Cell* elements = array->find_array_value();
    switch (array->kind_tag_) {
    case kQbvalString:
    case kQbvalInlineText: {
        relocate_array_elements(array, base);
        // (the texts one after another, padded once at the end)
        uint32_t total = 0;
        for (uint32_t i = 0; i < size; ++i)
            total += static_cast<uint32_t>(std::strlen(elements[i].text())) + 1;
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
            end = relocate_struct(elements[i].structure(), base);
        return end;
    case kQbvalArray:
        relocate_array_elements(array, base);
        for (uint32_t i = 0; i < size; ++i)
            end = relocate_array(elements[i].array(), base);
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
    auto* end = raw::bytes(component + 1);
    switch (component->kind_tag()) {
    case kQbvalString:
    case kQbvalInlineText:
        relocate(component->value, base);
        return end + stored_string_size(component->value.text());
    case kQbvalPair:
        relocate(component->value, base);
        return end + sizeof(Pair);
    case kQbvalVector:
        relocate(component->value, base);
        return end + sizeof(Vector);
    case kQbvalScriptBody: {
        // A script component: its tokens, up to and including the '$'.
        relocate(component->value, base);
        const Tokens code = tokens_at(component->value.code());
        return end + align4(static_cast<uint32_t>(code_between(code, skip_to_end_of_script(code)).size()));
    }
    case kQbvalStruct:
        relocate(component->value, base);
        return relocate_struct(component->value.structure(), base);
    case kQbvalArray:
        relocate(component->value, base);
        return relocate_array(component->value.array(), base);
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
    auto* end = raw::bytes(structure + 1);
    for (Component* component = structure->head_; component; component = component->next)
        end = relocate_component(component, base);
    return end;
}

// 0x0046C970
uint8_t* relocate_symbol(SymbolTableEntry* symbol, uint32_t base)
{
    auto* end = raw::bytes(symbol + 1);
    switch (symbol->type) {
    case kQbvalString:
    case kQbvalInlineText:
        relocate(symbol->value, base);
        return end + stored_string_size(symbol->value.text());
    case kQbvalPair:
        relocate(symbol->value, base);
        return end + sizeof(Pair);
    case kQbvalVector:
        relocate(symbol->value, base);
        return end + sizeof(Vector);
    case kQbvalScriptBody: {
        // {checksum, size, stored size, code}: the stored size is read from right after the entry (where the
        // script is), not through the value.
        const uint32_t stored = raw::as<const uint32_t>(end)[2];
        relocate(symbol->value, base);
        return end + 12 + align4(stored);
    }
    case kQbvalStruct:
        relocate(symbol->value, base);
        return relocate_struct(symbol->value.structure(), base);
    case kQbvalArray:
        relocate(symbol->value, base);
        return relocate_array(symbol->value.array(), base);
    default: // (integers, floats, names, C functions...: the value is in the entry)
        return end;
    }
}

// 0x0046CA50
void relocate_precompiled_symbols(uint8_t* file, uint32_t base, PrecompiledSymbolCallback callback, uint32_t argument,
                                  uint32_t flag)
{
    // The header's size word (read through util::ByteReader); the symbols are relocated in place - the image is
    // its own in-memory form (precompiled.h).
    util::ByteReader header(file, kPrecompiledHeaderSize);
    header.u32(); // the magic
    const uint32_t size = header.u32();
    // Not original - a size short of the header is refused, and the walk stops at the file's end: the original
    // walked until a symbol ended exactly there (a damaged image walked on past it).
    if (size < kPrecompiledHeaderSize)
        return;
    const uint8_t* end = file + size;
    uint8_t* p = file + kPrecompiledHeaderSize;
    while (p < end) {
        auto* symbol = raw::as<SymbolTableEntry>(p);
        uint8_t* next = relocate_symbol(symbol, base);
        if (callback)
            callback(symbol, argument, flag);
        p = next;
    }
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
void parse_precompiled_qb(const char* filename, std::span<uint8_t> data, int unused)
{
    const uint32_t flag = unused != 0;
    const uint32_t file = checksums::hash_from_text(filename);
    note_code_block(data);
    noted_images()[file] = data.data();
    relocate_precompiled_symbols(data.data(), cell::of(data.data()), define_precompiled_symbol, file, flag);
}

void forget_precompiled_image(uint32_t file_checksum)
{
    const auto image = noted_images().find(file_checksum);
    if (image == noted_images().end())
        return;
    forget_code_block(image->second);
    noted_images().erase(image);
}

} // namespace script

