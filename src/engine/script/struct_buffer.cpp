// Script structures to and from byte buffers (0x00479840-0x0047A8D1). See struct_buffer.h.

#include "engine/script/struct_buffer.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/param_struct.h"
#include "formats/byte_stream.h"
#include "engine/core/ints.h"
#include "engine/core/checksums.h"
#include "engine/core/enum_ops.h"
#include <gsl/pointers>


namespace script {

namespace {

// The names written as one-byte / two-byte indexes (two global arrays of checksums).
constexpr uint32_t kShortNames = 0x35115A20;
constexpr uint32_t kMediumNames = 0x25231F42;
// What a script component is written as (a name component of this value).
constexpr uint32_t kScriptPlaceholder = 0xB9C4F664;

// What a component is stored as (the low six bits of its type byte): the component types the buffer holds, and the
// buffer's own forms of integers and of a zero real.
enum class StoredType : uint8_t {
    kInteger = 1,
    kFloat = 2,
    kString = 3,
    kInlineText = 4,
    kPair = 5,
    kVector = 6,
    kStruct = 10,
    kArray = 12,
    kName = 13,
    kSignedByte = 0x0E,
    kSignedShort = 0x0F,
    kUnsignedByte = 0x10,
    kUnsignedShort = 0x11,
    kZeroInteger = 0x12,
    kZeroFloat = 0x13,
};
// A component's type byte also says how its name is written: an index in the short names (bit 7), in the medium names
// (bit 6), or the name's checksum following; the rest is the type.
constexpr uint8_t kShortNameFlag = 0x80;
constexpr uint8_t kMediumNameFlag = 0x40;
constexpr uint8_t kTypeBits = 0x3F;
// What each integer form holds.
constexpr int kUnsignedByteMax = 0xFF;
constexpr int kUnsignedShortMax = 0xFFFF;
constexpr int kSignedByteMin = -0x80;
constexpr int kSignedByteMax = 0x7F;
constexpr int kSignedShortMin = -0x8000;
constexpr int kSignedShortMax = 0x7FFF;
// A real this small is written as a zero.
constexpr double kTinyRealMagnitude = 0.0001;
// Room the forms need: a type byte and a 4-byte name, then the value.
constexpr size_t kNameFormBytes = 5;
constexpr size_t kNamedWordBytes = 9;
constexpr size_t kNamedPairBytes = 13;
constexpr size_t kNamedVectorBytes = 17;
constexpr uint32_t kStringRoomBytes = 6;
constexpr uint64_t kPairBytes = 8;
constexpr uint64_t kVectorBytes = 12;

uint32_t float_bits(float value)
{
    auto bits = std::bit_cast<uint32_t>(value);
    return bits;
}

// The bytes written since `start` (a writer's position).
uint32_t written_since(const util::ByteWriter& out, size_t start)
{
    return static_cast<uint32_t>(out.position() - start);
}

// 0x00479840: the type byte and the name (shortened when the name is in one of the two arrays).
void write_name_and_type(util::ByteWriter& out, StoredType stored, checksums::Name component)
{
    const uint32_t name = component.value();
    const uint8_t type = core::to_underlying(stored);
    Array* short_names = fetch_list(kShortNames);
    for (int i = 0; i < ints::as_signed(short_names->extent()); ++i) {
        if (short_names->fetch_part(i) == name) {
            out.u8(static_cast<uint8_t>(type | kShortNameFlag));
            out.u8(static_cast<uint8_t>(i));
            return;
        }
    }
    Array* medium_names = fetch_list(kMediumNames);
    for (int i = 0; i < ints::as_signed(medium_names->extent()); ++i) {
        if (medium_names->fetch_part(i) == name) {
            out.u8(static_cast<uint8_t>(type | kMediumNameFlag));
            out.u16(static_cast<uint16_t>(i));
            return;
        }
    }
    out.u8(type);
    out.u32(name);
}

} // namespace

// 0x004798F0
uint32_t emit_int(util::ByteWriter& out, checksums::Name name, int value)
{
    StoredType type = StoredType::kInteger;
    if (value == 0) {
        type = StoredType::kZeroInteger;
    } else if (value > 0 && value <= kUnsignedByteMax) {
        type = StoredType::kUnsignedByte;
    } else if (value > 0 && value <= kUnsignedShortMax) {
        type = StoredType::kUnsignedShort;
    } else if (value >= kSignedByteMin && value <= kSignedByteMax) {
        type = StoredType::kSignedByte;
    } else if (value >= kSignedShortMin && value <= kSignedShortMax) {
        type = StoredType::kSignedShort;
    }
    // written in a scratch buffer first, then copied if it fits
    constexpr size_t kScratchSize = 0x14;
    std::array<uint8_t, kScratchSize> scratch{};
    util::ByteWriter component(scratch.data(), sizeof(scratch));
    write_name_and_type(component, type, name);
    switch (type) {
    case StoredType::kInteger:
        component.u32(static_cast<uint32_t>(value));
        break;
    case StoredType::kSignedByte:
    case StoredType::kUnsignedByte:
        component.u8(static_cast<uint8_t>(value));
        break;
    case StoredType::kSignedShort:
    case StoredType::kUnsignedShort:
        component.u16(static_cast<uint16_t>(value));
        break;
    default:
        break;
    }
    auto length = static_cast<uint32_t>(component.position());
    if (out.remaining() < length) {
        return 0;
    }
    out.bytes(scratch.data(), length);
    return length;
}

// 0x00479A50
uint32_t emit_real(util::ByteWriter& out, checksums::Name name, float value)
{
    const size_t start = out.position();
    const float magnitude = value < 0.0F ? -value : value;
    // (a NaN counts as big)
    if (magnitude < kTinyRealMagnitude) {
        if (out.remaining() < kNameFormBytes) {
            return 0;
        }
        write_name_and_type(out, StoredType::kZeroFloat, name);
        return written_since(out, start);
    }
    if (out.remaining() < kNamedWordBytes) {
        return 0;
    }
    write_name_and_type(out, StoredType::kFloat, name);
    out.u32(float_bits(value));
    return written_since(out, start);
}

namespace {

uint32_t write_string_of_type(util::ByteWriter& out, const char* string, uint32_t name, StoredType type)
{
    auto length = static_cast<uint32_t>(std::strlen(string));
    if (out.remaining() < length + kStringRoomBytes) { // (room for the longest name form, as the original)
        return 0;
    }
    const size_t start = out.position();
    write_name_and_type(out, type, checksums::Name{name});
    out.c_string(string);
    return written_since(out, start);
}

} // namespace

// 0x00479AC0
uint32_t write_string(util::ByteWriter& out, const char* string, uint32_t name)
{
    return write_string_of_type(out, string, name, StoredType::kString);
}

// 0x00479B20
uint32_t write_local_string(util::ByteWriter& out, const char* string, uint32_t name)
{
    return write_string_of_type(out, string, name, StoredType::kInlineText);
}

namespace {

uint32_t emit_structure(util::ByteWriter& out, Struct* structure, int assert, void (*callback)());

} // namespace

// 0x00479B80
uint32_t write_array_to_buffer(util::ByteWriter& out, Array* array, int assert)
{
    if (out.remaining() < 3) {
        return 0;
    }
    const size_t start = out.position();
    const QbValueKind type = array->kind_tag();
    const uint32_t count = array->extent();
    out.u8(core::to_underlying(type));
    out.u16(static_cast<uint16_t>(count));
    switch (type) {
    case QbValueKind::kInteger:
    case QbValueKind::kFloat:
    case QbValueKind::kName: {
        if (uint64_t{count} * 4 > out.remaining()) {
            return 0;
        }
        for (uint32_t i = 0; i < count; ++i) {
            out.u32(array->fetch_part(i));
        }
        break;
    }
    case QbValueKind::kString:
    case QbValueKind::kInlineText: {
        for (uint32_t i = 0; i < count; ++i) {
            if (!out.c_string(array->fetch_text(i))) {
                return 0;
            }
        }
        break;
    }
    case QbValueKind::kPair: {
        if (uint64_t{count} * kPairBytes > out.remaining()) {
            return 0;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const Pair* pair = array->pair_at(i);
            out.f32(pair->x());
            out.f32(pair->y());
        }
        break;
    }
    case QbValueKind::kVector: {
        if (uint64_t{count} * kVectorBytes > out.remaining()) {
            return 0;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const Vector* vector = array->vector_at(i);
            out.f32(vector->x());
            out.f32(vector->y());
            out.f32(vector->z());
        }
        break;
    }
    case QbValueKind::kStruct: {
        for (uint32_t i = 0; i < count; ++i) {
            if (emit_structure(out, array->struct_at(i), assert, nullptr) == 0) {
                return 0;
            }
        }
        break;
    }
    case QbValueKind::kArray: {
        for (uint32_t i = 0; i < count; ++i) {
            if (write_array_to_buffer(out, array->array_at(i), assert) == 0) {
                return 0;
            }
        }
        break;
    }
    default:
        break; // (other types: just the type and count)
    }
    return written_since(out, start);
}

namespace {

// 0x00479E20's body: the structure's components and its terminating zero at the writer (each component's fit tested
// as the original: room for the longest name form and the value), the bytes written - 0 when they don't fit.
uint32_t emit_structure(util::ByteWriter& out, Struct* structure, int assert, void (*callback)())
{
    const size_t start = out.position();
    if (structure) {
        for (Component* component = structure->step_to_entry(nullptr); component;
             component = structure->step_to_entry(component)) {
            if (callback) {
                callback();
            }
            uint32_t written = 0;
            const size_t at = out.position();
            switch (value_kind(*component)) {
            case QbValueKind::kInteger:
                written = emit_int(out, checksums::Name{component->name}, component->value.integer());
                break;
            case QbValueKind::kFloat:
                written = emit_real(out, checksums::Name{component->name}, component->value.real());
                break;
            case QbValueKind::kString:
                written = write_string(out, component->value.text(), component->name);
                break;
            case QbValueKind::kInlineText:
                written = write_local_string(out, component->value.text(), component->name);
                break;
            case QbValueKind::kPair:
                if (out.remaining() < kNamedPairBytes) {
                    return 0;
                }
                write_name_and_type(out, StoredType::kPair, checksums::Name{component->name});
                out.f32(component->value.pair()->x());
                out.f32(component->value.pair()->y());
                written = written_since(out, at);
                break;
            case QbValueKind::kVector:
                if (out.remaining() < kNamedVectorBytes) {
                    return 0;
                }
                write_name_and_type(out, StoredType::kVector, checksums::Name{component->name});
                out.f32(component->value.vector()->x());
                out.f32(component->value.vector()->y());
                out.f32(component->value.vector()->z());
                written = written_since(out, at);
                break;
            case QbValueKind::kScriptBody:
                // (a script can't be sent: a placeholder name instead)
                if (out.remaining() < kNamedWordBytes) {
                    return 0;
                }
                write_name_and_type(out, StoredType::kName, checksums::Name{component->name});
                out.u32(kScriptPlaceholder);
                written = written_since(out, at);
                break;
            case QbValueKind::kStruct:
                if (out.remaining() < kNameFormBytes) {
                    return 0;
                }
                write_name_and_type(out, StoredType::kStruct, checksums::Name{component->name});
                written = emit_structure(out, component->value.structure(), assert, nullptr);
                break;
            case QbValueKind::kArray:
                if (out.remaining() < kNameFormBytes) {
                    return 0;
                }
                write_name_and_type(out, StoredType::kArray, checksums::Name{component->name});
                written = write_array_to_buffer(out, component->value.array(), assert);
                break;
            case QbValueKind::kName:
                if (out.remaining() < kNamedWordBytes) {
                    return 0;
                }
                write_name_and_type(out, StoredType::kName, checksums::Name{component->name});
                out.u32(component->value.word());
                written = written_since(out, at);
                break;
            default:
                return 0; // (C functions, member functions, pointers...)
            }
            if (written == 0 || !out.ok()) {
                return 0;
            }
        }
    }
    if (!out.u8(0)) { // (the end marker: no room for it - nothing)
        return 0;
    }
    return written_since(out, start);
}

} // namespace

// 0x00479E20
uint32_t emit_into_buffer(Struct* structure, uint8_t* buffer, uint32_t size, bool assert, void (*callback)())
{
    util::ByteWriter out(buffer, size);
    return emit_structure(out, structure, static_cast<int>(assert), callback);
}

// 0x0047A070
uint32_t measure_buffer_bytes(Struct* structure, uint32_t max_size)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    auto* scratch = static_cast<uint8_t*>(memory::alloc_block(max_size));
    const uint32_t size = emit_into_buffer(structure, scratch, max_size, true, nullptr);
    memory::free_malloc(scratch);
    memory::Hub::get_pull()->leave_heap_scope();
    return size;
}

namespace {

void decode_structure(Struct* structure, util::ByteReader& reader);
void decode_array(Array* array, util::ByteReader& reader);

// A component's name after its type byte: an index into one of the two name arrays, or the name itself.
uint32_t read_name(util::ByteReader& reader, uint8_t type)
{
    if ((type & kShortNameFlag) != 0) {
        return fetch_list(kShortNames)->fetch_part(reader.u8());
    }
    if ((type & kMediumNameFlag) != 0) {
        return fetch_list(kMediumNames)->fetch_part(reader.u16());
    }
    return reader.u32();
}

// A string value in place ("" past the end of the buffer).
const char* read_text(util::ByteReader& reader)
{
    const char* text = reader.c_string();
    return text ? text : "";
}

void decode_structure(Struct* structure, util::ByteReader& reader)
{
    structure->get_clear();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    // (a buffer cut short reads as zeros: its type byte ends the structure)
    for (uint8_t type = reader.u8(); type != 0; type = reader.u8()) {
        const uint32_t name = read_name(reader, type);
        if (!reader.ok()) {
            break;
        }
        switch (static_cast<StoredType>(type & kTypeBits)) {
        case StoredType::kInteger:
            structure->put_int(checksums::Name{name}, reader.i32());
            break;
        case StoredType::kFloat:
            structure->push_float(checksums::Name{name}, reader.f32());
            break;
        case StoredType::kString:
            structure->append_word(name, read_text(reader));
            break;
        case StoredType::kInlineText:
            structure->append_inline_text(name, read_text(reader));
            break;
        case StoredType::kPair: {
            const float x = reader.f32();
            const float y = reader.f32();
            structure->insert_pair(checksums::Name{name}, {.x = x, .y = y});
            break;
        }
        case StoredType::kVector: {
            const float x = reader.f32();
            const float y = reader.f32();
            const float z = reader.f32();
            structure->put_vec3(checksums::Name{name}, {.x = x, .y = y, .z = z});
            break;
        }
        case StoredType::kStruct: {
            gsl::owner<Struct*> inner = spawn_param_bag();
            if (inner) {
                new (inner) script::Struct;
            }
            decode_structure(inner, reader);
            structure->append_struct_ref(name, inner);
            break;
        }
        case StoredType::kArray: {
            gsl::owner<Array*> inner = alloc_script_array();
            if (inner) {
                new (inner) script::Array;
            }
            decode_array(inner, reader);
            structure->append_array_ref(name, inner);
            break;
        }
        case StoredType::kName:
            structure->put_name_key(checksums::Name{name}, reader.u32());
            break;
        case StoredType::kSignedByte:
            structure->put_int(checksums::Name{name}, reader.i8());
            break;
        case StoredType::kSignedShort:
            structure->put_int(checksums::Name{name}, reader.i16());
            break;
        case StoredType::kUnsignedByte:
            structure->put_int(checksums::Name{name}, reader.u8());
            break;
        case StoredType::kUnsignedShort:
            structure->put_int(checksums::Name{name}, reader.u16());
            break;
        case StoredType::kZeroInteger:
            structure->put_int(checksums::Name{name}, 0);
            break;
        case StoredType::kZeroFloat:
            structure->push_float(checksums::Name{name}, 0.0F);
            break;
        }
        // (another type: no value - just the name was read)
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// (past the end of the buffer every element is still made - zeros, empty strings and structures - so that no
// element is left null)
void decode_array(Array* array, util::ByteReader& reader)
{
    release_array_storage(array);
    const auto type = static_cast<QbValueKind>(reader.u8());
    const uint32_t count = reader.u16();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap());
    array->configure_size_kind(count, type);
    memory::Hub::get_pull()->leave_heap_scope();
    switch (type) {
    case QbValueKind::kInteger:
    case QbValueKind::kName:
        for (uint32_t i = 0; i < count; ++i) {
            array->set_element(i, reader.u32());
        }
        break;
    case QbValueKind::kFloat:
        for (uint32_t i = 0; i < count; ++i) {
            array->change_float(i, reader.f32());
        }
        break;
    case QbValueKind::kString:
    case QbValueKind::kInlineText:
        for (uint32_t i = 0; i < count; ++i) {
            array->set_text(i, spawn_text(read_text(reader)));
        }
        break;
    case QbValueKind::kPair:
        for (uint32_t i = 0; i < count; ++i) {
            Pair* pair = new_pair();
            if (pair) {
                new (pair) script::Pair;
            }
            pair->x() = reader.f32();
            pair->y() = reader.f32();
            array->set_pair(i, pair);
        }
        break;
    case QbValueKind::kVector:
        for (uint32_t i = 0; i < count; ++i) {
            Vector* vector = alloc_script_vector();
            if (vector) {
                new (vector) script::Vector;
            }
            vector->x() = reader.f32();
            vector->y() = reader.f32();
            vector->z() = reader.f32();
            array->set_vector(i, vector);
        }
        break;
    case QbValueKind::kStruct:
        for (uint32_t i = 0; i < count; ++i) {
            gsl::owner<Struct*> inner = spawn_param_bag();
            if (inner) {
                new (inner) script::Struct;
            }
            decode_structure(inner, reader);
            array->set_cell(i, Cell::of(inner));
        }
        break;
    case QbValueKind::kArray:
        for (uint32_t i = 0; i < count; ++i) {
            gsl::owner<Array*> inner = alloc_script_array();
            if (inner) {
                new (inner) script::Array;
            }
            decode_array(inner, reader);
            array->set_cell(i, Cell::of(inner));
        }
        break;
    default:
        break;
    }
}

} // namespace

// 0x0047A0C0
const uint8_t* decode_from_bytes(Struct* structure, const uint8_t* buffer, size_t size)
{
    util::ByteReader reader(buffer, size);
    decode_structure(structure, reader);
    return reader.cursor();
}

// 0x0047A440
const uint8_t* read_array_from_buffer(Array* array, const uint8_t* buffer, size_t size)
{
    util::ByteReader reader(buffer, size);
    decode_array(array, reader);
    return reader.cursor();
}

} // namespace script
