#include "engine/script/precompiled.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

#include <gsl/assert>
#include <gsl/pointers>

#include "base/core/checksums.h"
#include "base/core/raw_view.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/script/cell.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "formats/byte_stream.h"

namespace script {

namespace {

// The image's records (precompiled.h): their sizes in the file and their fields' offsets. Every field is 32 bits, a
// pointer an offset from the start of the file.
constexpr uint32_t kImageWordBytes = 4;
constexpr uint32_t kImageSymbolBytes = 0x14;
constexpr uint32_t kImageSymbolFlags = 0x00;  // byte: bit 0 new, bit 2 not owned by the table
constexpr uint32_t kImageSymbolType = 0x01;   // byte
constexpr uint32_t kImageSymbolName = 0x04;
constexpr uint32_t kImageSymbolValue = 0x0C;
constexpr uint32_t kImageStructBytes = 8;
constexpr uint32_t kImageStructHead = 0x04;
constexpr uint32_t kImageComponentBytes = 0x10;
constexpr uint32_t kImageComponentKind = 0x00; // byte: the kind in the top 7 bits, a flag in bit 0
constexpr uint32_t kImageComponentByte1 = 0x01;
constexpr uint32_t kImageComponentHalf2 = 0x02;
constexpr uint32_t kImageComponentName = 0x04;
constexpr uint32_t kImageComponentValue = 0x08;
constexpr uint32_t kImageComponentNext = 0x0C;
constexpr uint32_t kImageArrayBytes = 0x0C;
constexpr uint32_t kImageArrayType = 0x01;     // byte
constexpr uint32_t kImageArraySize = 0x04;
constexpr uint32_t kImageArrayElement = 0x08;  // the element table, or the one element itself
constexpr uint32_t kImagePairBytes = 0x0C;     // a word, then x, y
constexpr uint32_t kImageVectorBytes = 0x10;   // a word, then x, y, z
constexpr uint32_t kImageCoordinates = 0x04;
// The symbol's flag bit "not owned by the table" (clean_up_and_remove_symbol leaves such a symbol's value alone): the
// image's entries in the original. An unpacked symbol owns its value.
constexpr uint8_t kSymbolNotOwned = 0x04;

uint32_t align4(uint32_t size) { return (size + 3) & ~3U; }

// The image's bytes, read at offsets (every read checked against the image: a damaged image stops the game, where
// the original read on past it).
class Image {
public:
    explicit Image(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::span<const uint8_t> bytes() const { return bytes_; }
    [[nodiscard]] std::span<const uint8_t> at(uint32_t offset, uint32_t size) const
    {
        Expects(offset <= bytes_.size() && size <= bytes_.size() - offset);
        return bytes_.subspan(offset, size);
    }
    [[nodiscard]] uint8_t u8(uint32_t offset) const { return at(offset, 1).front(); }
    [[nodiscard]] uint16_t u16(uint32_t offset) const { return util::ByteReader(at(offset, 2)).u16(); }
    [[nodiscard]] uint32_t u32(uint32_t offset) const { return util::ByteReader(at(offset, kImageWordBytes)).u32(); }
    [[nodiscard]] float f32(uint32_t offset) const { return util::ByteReader(at(offset, kImageWordBytes)).f32(); }
    // The null-terminated text at `offset` (checked to end inside the image).
    [[nodiscard]] const char* text(uint32_t offset) const
    {
        const std::span<const uint8_t> rest = at(offset, static_cast<uint32_t>(bytes_.size()) - offset);
        Expects(std::ranges::find(rest, uint8_t{0}) != rest.end());
        return raw::chars(rest.data());
    }
    // The size of the text at `offset` in the image (with its terminator, padded).
    [[nodiscard]] uint32_t stored_text_size(uint32_t offset) const
    {
        return align4(static_cast<uint32_t>(std::strlen(text(offset))) + 1);
    }

private:
    std::span<const uint8_t> bytes_;
};

// A value unpacked: the cell that holds it and where its data ends in the image (an offset in it).
struct Unpacked {
    Cell value;
    uint32_t end{};
};

gsl::owner<Struct*> unpack_struct(const Image& image, uint32_t at, uint32_t* end);
gsl::owner<Array*> unpack_array(const Image& image, uint32_t at, uint32_t* end);

char* unpack_text(const Image& image, uint32_t at) { return spawn_text(image.text(at)); }
gsl::owner<Pair*> unpack_pair(const Image& image, uint32_t at)
{
    void* memory = new_pair();
    Expects(memory != nullptr);
    gsl::owner<Pair*> pair{memory::construct_in<Pair>(memory)};
    pair->x() = image.f32(at + kImageCoordinates);
    pair->y() = image.f32(at + kImageCoordinates + kImageWordBytes);
    return pair;
}
gsl::owner<Vector*> unpack_vector(const Image& image, uint32_t at)
{
    void* memory = alloc_script_vector();
    Expects(memory != nullptr);
    gsl::owner<Vector*> vector{memory::construct_in<Vector>(memory)};
    vector->x() = image.f32(at + kImageCoordinates);
    vector->y() = image.f32(at + kImageCoordinates + kImageWordBytes);
    vector->z() = image.f32(at + kImageCoordinates + (2 * kImageWordBytes));
    return vector;
}

// A component's or a symbol's value of kind `kind`, stored as `word` (an offset for the kinds held by pointer) in the
// record that ends at `record_end`: the cell and where the value's data ends (0x0046C6B0 / 0x0046C970's sizes).
// (A symbol's script is the {checksum, size, stored size, code} record and a component's only its tokens: the callers
// make those.)
Unpacked unpack_value(const Image& image, QbValueKind kind, uint32_t word, uint32_t record_end)
{
    switch (kind) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        return {.value = Cell::of_text(unpack_text(image, word)), .end = record_end + image.stored_text_size(word)};
    case QbValueKind::kPair: {
        gsl::owner<Pair*> pair = unpack_pair(image, word);
        return {.value = Cell::of(pair), .end = record_end + kImagePairBytes};
    }
    case QbValueKind::kVector: {
        gsl::owner<Vector*> vector = unpack_vector(image, word);
        return {.value = Cell::of(vector), .end = record_end + kImageVectorBytes};
    }
    case QbValueKind::kStruct: {
        uint32_t end = 0;
        gsl::owner<Struct*> structure = unpack_struct(image, word, &end);
        return {.value = Cell::of(structure), .end = end};
    }
    case QbValueKind::kArray: {
        uint32_t end = 0;
        gsl::owner<Array*> array = unpack_array(image, word, &end);
        return {.value = Cell::of(array), .end = end};
    }
    default: // (integers, floats, names, C functions...: the value is the word)
        return {.value = Cell::of_word(word), .end = record_end};
    }
}

// A structure component's script: its tokens at `at`, up to and including the '$', copied to a block of code of its
// own (as a .qb's script component is - token_reader.cpp); `stored_size` set to their size in the image (padded).
Cell copy_component_script(const Image& image, uint32_t at, uint32_t* stored_size)
{
    const Tokens start = Tokens(image.bytes()) + at;
    const std::span<const uint8_t> script = code_between(start, skip_to_end_of_script(start));
    const auto size = static_cast<uint32_t>(script.size());
    memory::enter_heap_scope(memory::Hub::get_pull()->script_heap());
    auto* code = static_cast<uint8_t*>(memory::alloc_block(size));
    memory::leave_heap_scope();
    const std::span<uint8_t> copy(code, size);
    note_code_block(copy);
    std::ranges::copy(script, copy.begin());
    *stored_size = align4(size);
    return Cell::of_code(code);
}

// A symbol's script: the {checksum, size, stored size, code} record right after the entry, copied (as
// define_script_symbol makes it).
Unpacked unpack_symbol_script(const Image& image, uint32_t record_end)
{
    const uint32_t stored = image.u32(record_end + (2 * kImageWordBytes));
    const std::span<const uint8_t> record = image.at(record_end, static_cast<uint32_t>(kStoredScriptHeader) + stored);
    auto* block = static_cast<uint8_t*>(memory::alloc_block(static_cast<unsigned int>(record.size())));
    std::ranges::copy(record, block);
    return {.value = Cell::of_code(block), .end = record_end + static_cast<uint32_t>(kStoredScriptHeader) + align4(stored)};
}

// The component at `at` (0x0046C6B0's walk): made, and where its data ends.
gsl::owner<Component*> unpack_component(const Image& image, uint32_t at, uint32_t* end)
{
    gsl::owner<Component*> component = new_component();
    component->type_and_flag_ = image.u8(at + kImageComponentKind);
    component->unknown1 = image.u8(at + kImageComponentByte1);
    component->unknown2 = image.u16(at + kImageComponentHalf2);
    component->name = image.u32(at + kImageComponentName);
    const uint32_t word = image.u32(at + kImageComponentValue);
    const uint32_t record_end = at + kImageComponentBytes;
    if (value_kind(*component) == QbValueKind::kScriptBody) {
        uint32_t stored_size = 0;
        component->value = copy_component_script(image, word, &stored_size);
        *end = record_end + stored_size;
    } else {
        const Unpacked value = unpack_value(image, value_kind(*component), word, record_end);
        component->value = value.value;
        *end = value.end;
    }
    return component;
}

// The structure at `at` (0x0046C7F0): its components in the image's order, linked as there (duplicates kept).
gsl::owner<Struct*> unpack_struct(const Image& image, uint32_t at, uint32_t* end)
{
    gsl::owner<Struct*> structure = new_struct();
    // (the end of the last component's data, or of the structure itself when it has none)
    *end = at + kImageStructBytes;
    Component** link = &structure->head();
    for (uint32_t next = image.u32(at + kImageStructHead); next != 0;) {
        gsl::owner<Component*> component = unpack_component(image, next, end);
        *link = component;
        link = &component->next;
        next = image.u32(next + kImageComponentNext);
    }
    return structure;
}

// The array at `at` (0x0046C830): its elements, and where their data ends.
gsl::owner<Array*> unpack_array(const Image& image, uint32_t at, uint32_t* end)
{
    const auto type = static_cast<QbValueKind>(image.u8(at + kImageArrayType));
    const uint32_t size = image.u32(at + kImageArraySize);
    const uint32_t element = image.u32(at + kImageArrayElement);
    gsl::owner<Array*> array = new_array();
    array->configure_size_kind(size, type);
    array->change_species(type); // (an empty array keeps its type, as the image's did)
    *end = at + kImageArrayBytes;
    // The elements' words: the table that follows the array, or the one element in its place.
    uint32_t table = at + kImageArrayElement;
    if (size > 1) {
        table = element;
        *end += size * kImageWordBytes;
    }
    const auto word_of = [&](uint32_t index) { return image.u32(table + (index * kImageWordBytes)); };
    switch (type) {
    case QbValueKind::kString:
    case QbValueKind::kInlineText: {
        // (the texts one after another, padded once at the end)
        uint32_t total = 0;
        for (uint32_t i = 0; i < size; ++i) {
            const char* text = image.text(word_of(i));
            total += static_cast<uint32_t>(std::strlen(text)) + 1;
            array->set_text(i, spawn_text(text));
        }
        *end += align4(total);
        break;
    }
    case QbValueKind::kPair:
        for (uint32_t i = 0; i < size; ++i) {
            gsl::owner<Pair*> pair = unpack_pair(image, word_of(i));
            array->set_pair(i, pair);
        }
        *end += size * kImagePairBytes;
        break;
    case QbValueKind::kVector:
        for (uint32_t i = 0; i < size; ++i) {
            gsl::owner<Vector*> vector = unpack_vector(image, word_of(i));
            array->set_vector(i, vector);
        }
        *end += size * kImageVectorBytes;
        break;
    case QbValueKind::kStruct:
        for (uint32_t i = 0; i < size; ++i) {
            gsl::owner<Struct*> structure = unpack_struct(image, word_of(i), end);
            array->set_struct(i, structure);
        }
        break;
    case QbValueKind::kArray:
        for (uint32_t i = 0; i < size; ++i) {
            gsl::owner<Array*> nested = unpack_array(image, word_of(i), end);
            array->set_array(i, nested);
        }
        break;
    default: // (values kept in the element table)
        for (uint32_t i = 0; i < size; ++i) {
            array->set_cell(i, Cell::of_word(word_of(i)));
        }
        break;
    }
    return array;
}

} // namespace

// 0x0046CA50 (relocated the image's symbols in place, each then defined by 0x00478DE0)
void unpack_precompiled_symbols(std::span<const uint8_t> file, uint32_t file_checksum)
{
    const Image image(file);
    // Not original - a size short of the header is refused, and the walk stops at the file's end: the original
    // walked until a symbol ended exactly there (a damaged image walked on past it).
    const uint32_t size = image.u32(kImageWordBytes);
    if (size < kPrecompiledHeaderSize) {
        return;
    }
    for (uint32_t at = kPrecompiledHeaderSize; at < size;) {
        const uint32_t name = image.u32(at + kImageSymbolName);
        const auto type = static_cast<QbValueKind>(image.u8(at + kImageSymbolType));
        const uint32_t word = image.u32(at + kImageSymbolValue);
        const uint32_t record_end = at + kImageSymbolBytes;
        // 0x00478DE0: the symbol of the same name goes first.
        if (SymbolTableEntry* existing = find_symbol(name)) {
            clean_up_and_remove_symbol(existing);
        }
        SymbolTableEntry* symbol = create_symbol(name);
        // (the image's flags, but for "not owned": the unpacked value is the symbol's own)
        symbol->unknown0() = static_cast<uint8_t>(image.u8(at + kImageSymbolFlags) & ~kSymbolNotOwned);
        symbol->set_type(type);
        symbol->set_unknown8(file_checksum);
        const Unpacked value =
            type == QbValueKind::kScriptBody ? unpack_symbol_script(image, record_end) : unpack_value(image, type, word, record_end);
        symbol->value() = value.value;
        at = value.end;
    }
}

// 0x0046FBA0
void parse_precompiled_qb(const char* filename, std::span<const uint8_t> data, int /*unused*/)
{
    unpack_precompiled_symbols(data, checksums::hash_from_text(filename));
}

} // namespace script
