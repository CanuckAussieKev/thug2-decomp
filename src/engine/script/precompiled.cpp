#include "engine/script/precompiled.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <span>
#include <utility>

#include <gsl/assert>

#include "base/core/checksums.h"
#include "engine/script/cell.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "formats/byte_stream.h"
#include "base/core/pointer_cell.h"
#include "base/core/raw_view.h"
#include "base/core/checked_index.h"


namespace script {

namespace {

// A stored offset made an address.
void relocate(Cell& value, uint32_t base) { value = Cell::of_bits(value.bits() + base); }
template <typename T>
void relocate(T*& pointer, uint32_t base)
{
    pointer = cell::pointer<T>(cell::of(pointer) + base);
}
uint32_t align4(uint32_t size) { return (size + 3) & ~3U; }
// The size of a string's text in the file (with its terminator, padded).
uint32_t stored_string_size(const char* text) { return align4(static_cast<uint32_t>(std::strlen(text)) + 1); }

// NOT ORIGINAL - the images noted as blocks of code (their structures' scripts run from them: tokens.h), by their
// file's checksum, forgotten when the file is unloaded.
std::map<uint32_t, const uint8_t*>& noted_images()
{
    static std::map<uint32_t, const uint8_t*> images;
    return images;
}

// Where `item` (in the image) ends: its offset in the image and its size, checked to be inside the image.
template <typename T>
uint32_t end_of(std::span<uint8_t> image, const T* item)
{
    const void* place = item;
    const std::ptrdiff_t at = static_cast<const uint8_t*>(place) - image.data();
    Expects(at >= 0 && std::cmp_less_equal(at + static_cast<std::ptrdiff_t>(sizeof(T)), image.size()));
    return static_cast<uint32_t>(at) + static_cast<uint32_t>(sizeof(T));
}

} // namespace

// 0x0046C7B0 (a CArray method)
void relocate_array_elements(Array* array, uint32_t base)
{
    for (Cell& element : array->cells()) {
        relocate(element, base);
    }
}

// 0x0046C830
uint32_t relocate_array(std::span<uint8_t> image, Array* array, uint32_t base)
{
    const uint32_t size = array->extent();
    uint32_t end = end_of(image, array);
    if (size > 1) {
        if (!array->storage().empty()) {
            relocate(array->storage(), base);
        }
        end += size * static_cast<uint32_t>(sizeof(Cell)); // (the element table follows the array)
    }
    const std::span<const Cell> elements = array->cells();
    switch (array->kind_tag()) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText: {
        relocate_array_elements(array, base);
        // (the texts one after another, padded once at the end)
        uint32_t total = 0;
        for (const Cell& element : elements) {
            total += static_cast<uint32_t>(std::strlen(element.text())) + 1;
        }
        return end + align4(total);
    }
    case QbValueKind::kPair:
        relocate_array_elements(array, base);
        return end + (size * static_cast<uint32_t>(sizeof(Pair)));
    case QbValueKind::kVector:
        relocate_array_elements(array, base);
        return end + (size * static_cast<uint32_t>(sizeof(Vector)));
    case QbValueKind::kStruct:
        relocate_array_elements(array, base);
        for (const Cell& element : elements) {
            end = relocate_struct(image, element.structure(), base);
        }
        return end;
    case QbValueKind::kArray:
        relocate_array_elements(array, base);
        for (const Cell& element : elements) {
            end = relocate_array(image, element.array(), base);
        }
        return end;
    default: // (values kept in the element table: nothing to relocate)
        return end;
    }
}

// 0x0046C6B0
uint32_t relocate_component(std::span<uint8_t> image, Component* component, uint32_t base)
{
    if (component->next) {
        relocate(component->next, base);
    }
    const uint32_t end = end_of(image, component);
    switch (value_kind(*component)) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        relocate(component->value, base);
        return end + stored_string_size(component->value.text());
    case QbValueKind::kPair:
        relocate(component->value, base);
        return end + static_cast<uint32_t>(sizeof(Pair));
    case QbValueKind::kVector:
        relocate(component->value, base);
        return end + static_cast<uint32_t>(sizeof(Vector));
    case QbValueKind::kScriptBody: {
        // A script component: its tokens, up to and including the '$' (in the image).
        relocate(component->value, base);
        const Tokens code = Tokens(image).moved_to(component->value.code());
        return end + align4(static_cast<uint32_t>(code_between(code, skip_to_end_of_script(code)).size()));
    }
    case QbValueKind::kStruct:
        relocate(component->value, base);
        return relocate_struct(image, component->value.structure(), base);
    case QbValueKind::kArray:
        relocate(component->value, base);
        return relocate_array(image, component->value.array(), base);
    default:
        return end;
    }
}

// 0x0046C7F0
uint32_t relocate_struct(std::span<uint8_t> image, Struct* structure, uint32_t base)
{
    if (structure->head()) {
        relocate(structure->head(), base);
    }
    // (the end of the last component's data, or of the structure itself when it has none)
    uint32_t end = end_of(image, structure);
    for (Component* component = structure->head(); component; component = component->next) {
        end = relocate_component(image, component, base);
    }
    return end;
}

// 0x0046C970
uint32_t relocate_symbol(std::span<uint8_t> image, SymbolTableEntry* symbol, uint32_t base)
{
    const uint32_t end = end_of(image, symbol);
    switch (symbol->type()) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        relocate(symbol->value(), base);
        return end + stored_string_size(symbol->value().text());
    case QbValueKind::kPair:
        relocate(symbol->value(), base);
        return end + static_cast<uint32_t>(sizeof(Pair));
    case QbValueKind::kVector:
        relocate(symbol->value(), base);
        return end + static_cast<uint32_t>(sizeof(Vector));
    case QbValueKind::kScriptBody: {
        // {checksum, size, stored size, code}: the stored size is read from right after the entry (where the
        // script is), not through the value.
        util::ByteReader record(image.subspan(end, kStoredScriptHeader));
        record.u32(); // the checksum
        record.u32(); // the size
        const uint32_t stored = record.u32();
        relocate(symbol->value(), base);
        return end + static_cast<uint32_t>(kStoredScriptHeader) + align4(stored);
    }
    case QbValueKind::kStruct:
        relocate(symbol->value(), base);
        return relocate_struct(image, symbol->value().structure(), base);
    case QbValueKind::kArray:
        relocate(symbol->value(), base);
        return relocate_array(image, symbol->value().array(), base);
    default: // (integers, floats, names, C functions...: the value is in the entry)
        return end;
    }
}

// 0x0046CA50
void relocate_precompiled_symbols(std::span<uint8_t> file, uint32_t base, PrecompiledSymbolCallback callback,
                                  uint32_t argument, uint32_t flag)
{
    // The header's size word (read through util::ByteReader); the symbols are relocated in place - the image is
    // its own in-memory form (precompiled.h).
    util::ByteReader header(file.first(kPrecompiledHeaderSize));
    header.u32(); // the magic
    const uint32_t size = header.u32();
    // Not original - a size short of the header is refused, and the walk stops at the file's end: the original
    // walked until a symbol ended exactly there (a damaged image walked on past it).
    if (size < kPrecompiledHeaderSize) {
        return;
    }
    for (uint32_t at = kPrecompiledHeaderSize; at < size;) {
        Expects(std::cmp_less_equal(at + sizeof(SymbolTableEntry), file.size()));
        auto* symbol = raw::as<SymbolTableEntry>(&core::at(file, at));
        const uint32_t next = relocate_symbol(file, symbol, base);
        if (callback) {
            callback(symbol, argument, flag);
        }
        at = next;
    }
}

// 0x00478DE0
void define_precompiled_symbol(SymbolTableEntry* symbol, uint32_t file_checksum, uint32_t /*unused*/)
{
    symbol->set_unknown8(file_checksum);
    if (SymbolTableEntry* existing = find_symbol(symbol->name_value())) {
        clean_up_and_remove_symbol(existing);
    }
    link_symbol(symbol);
}

// 0x0046FBA0
void parse_precompiled_qb(const char* filename, std::span<uint8_t> data, int unused)
{
    const auto flag = static_cast<uint32_t>(unused != 0);
    const uint32_t file = checksums::hash_from_text(filename);
    note_code_block(data);
    noted_images()[file] = data.data();
    relocate_precompiled_symbols(data, cell::of(data.data()), define_precompiled_symbol, file, flag);
}

void forget_precompiled_image(uint32_t file_checksum)
{
    const auto image = noted_images().find(file_checksum);
    if (image == noted_images().end()) {
        return;
    }
    forget_code_block(image->second);
    noted_images().erase(image);
}

} // namespace script
