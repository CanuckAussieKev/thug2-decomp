// Script structures to and from byte buffers (0x00479840-0x0047A8D1). See struct_buffer.h.

#include "engine/script/struct_buffer.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/util/byte_stream.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag

namespace script {

namespace {

// The names written as one-byte / two-byte indexes (two global arrays of checksums).
constexpr uint32_t kShortNames = 0x35115A20;
constexpr uint32_t kMediumNames = 0x25231F42;
// What a script component is written as (a name component of this value).
constexpr uint32_t kScriptPlaceholder = 0xB9C4F664;

// The buffer's own types beyond the component types.
constexpr uint8_t kSignedByte = 0x0E;
constexpr uint8_t kSignedShort = 0x0F;
constexpr uint8_t kUnsignedByte = 0x10;
constexpr uint8_t kUnsignedShort = 0x11;
constexpr uint8_t kZeroInteger = 0x12;
constexpr uint8_t kTagZeroF = 0x13;

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

} // namespace

// 0x00479840
void write_name_and_type(util::ByteWriter& out, uint8_t type, uint32_t name)
{
    Array* short_names = fetch_list(kShortNames);
    for (int i = 0; i < static_cast<int>(short_names->extent_); ++i) {
        if (short_names->fetch_part(i) == name) {
            out.u8(type | 0x80);
            out.u8(static_cast<uint8_t>(i));
            return;
        }
    }
    Array* medium_names = fetch_list(kMediumNames);
    for (int i = 0; i < static_cast<int>(medium_names->extent_); ++i) {
        if (medium_names->fetch_part(i) == name) {
            out.u8(type | 0x40);
            out.u16(static_cast<uint16_t>(i));
            return;
        }
    }
    out.u8(type);
    out.u32(name);
}

// 0x004798F0
uint32_t emit_int(util::ByteWriter& out, uint32_t name, int value)
{
    uint8_t type = kQbvalInteger;
    if (value == 0)
        type = kZeroInteger;
    else if (value > 0 && value <= 0xFF)
        type = kUnsignedByte;
    else if (value > 0 && value <= 0xFFFF)
        type = kUnsignedShort;
    else if (value >= -0x80 && value <= 0x7F)
        type = kSignedByte;
    else if (value >= -0x8000 && value <= 0x7FFF)
        type = kSignedShort;
    // written in a scratch buffer first, then copied if it fits
    uint8_t scratch[0x14];
    util::ByteWriter component(scratch, sizeof(scratch));
    write_name_and_type(component, type, name);
    switch (type) {
    case kQbvalInteger:
        component.u32(static_cast<uint32_t>(value));
        break;
    case kSignedByte:
    case kUnsignedByte:
        component.u8(static_cast<uint8_t>(value));
        break;
    case kSignedShort:
    case kUnsignedShort:
        component.u16(static_cast<uint16_t>(value));
        break;
    default:
        break;
    }
    auto length = static_cast<uint32_t>(component.position());
    if (out.remaining() < length)
        return 0;
    out.bytes(scratch, length);
    return length;
}

// 0x00479A50
uint32_t emit_real(util::ByteWriter& out, uint32_t name, float value)
{
    size_t start = out.position();
    float magnitude = value < 0.0f ? -value : value;
    // (a NaN counts as big)
    if (magnitude < 0.0001) {
        if (out.remaining() < 5)
            return 0;
        write_name_and_type(out, kTagZeroF, name);
        return written_since(out, start);
    }
    if (out.remaining() < 9)
        return 0;
    write_name_and_type(out, kQbvalFloat, name);
    out.u32(float_bits(value));
    return written_since(out, start);
}

namespace {

uint32_t write_string_of_type(util::ByteWriter& out, const char* string, uint32_t name, uint8_t type)
{
    auto length = static_cast<uint32_t>(std::strlen(string));
    if (out.remaining() < length + 6) // (room for the longest name form, as the original)
        return 0;
    size_t start = out.position();
    write_name_and_type(out, type, name);
    out.c_string(string);
    return written_since(out, start);
}

} // namespace

// 0x00479AC0
uint32_t write_string(util::ByteWriter& out, const char* string, uint32_t name)
{
    return write_string_of_type(out, string, name, kQbvalString);
}

// 0x00479B20
uint32_t write_local_string(util::ByteWriter& out, const char* string, uint32_t name)
{
    return write_string_of_type(out, string, name, kQbvalInlineText);
}

namespace {

uint32_t emit_structure(util::ByteWriter& out, Struct* structure, int assert, void (*callback)());

} // namespace

// 0x00479B80
uint32_t write_array_to_buffer(util::ByteWriter& out, Array* array, int assert)
{
    if (out.remaining() < 3)
        return 0;
    size_t start = out.position();
    uint8_t type = array->kind_tag_;
    uint32_t count = array->extent_;
    out.u8(type);
    out.u16(static_cast<uint16_t>(count));
    switch (type) {
    case kQbvalInteger:
    case kQbvalFloat:
    case kQbvalName: {
        if (uint64_t{count} * 4 > out.remaining())
            return 0;
        const uint32_t* elements = array->find_array_value();
        for (uint32_t i = 0; i < count; ++i)
            out.u32(elements[i]);
        break;
    }
    case kQbvalString:
    case kQbvalInlineText: {
        const uint32_t* elements = array->find_array_value();
        for (uint32_t i = 0; i < count; ++i) {
            if (!out.c_string(reinterpret_cast<const char*>(elements[i])))
                return 0;
        }
        break;
    }
    case kQbvalPair: {
        if (uint64_t{count} * 8 > out.remaining())
            return 0;
        auto** pairs = reinterpret_cast<Pair**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            out.f32(pairs[i]->x);
            out.f32(pairs[i]->y);
        }
        break;
    }
    case kQbvalVector: {
        if (uint64_t{count} * 12 > out.remaining())
            return 0;
        auto** vectors = reinterpret_cast<Vector**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            out.f32(vectors[i]->x);
            out.f32(vectors[i]->y);
            out.f32(vectors[i]->z);
        }
        break;
    }
    case kQbvalStruct: {
        auto** structures = reinterpret_cast<Struct**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            if (emit_structure(out, structures[i], assert, nullptr) == 0)
                return 0;
        }
        break;
    }
    case kQbvalArray: {
        auto** arrays = reinterpret_cast<Array**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            if (write_array_to_buffer(out, arrays[i], assert) == 0)
                return 0;
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
    size_t start = out.position();
    if (structure) {
        for (Component* component = structure->step_to_entry(nullptr); component;
             component = structure->step_to_entry(component)) {
            if (callback)
                callback();
            uint32_t written = 0;
            size_t at = out.position();
            switch (component->kind_tag()) {
            case kQbvalInteger:
                written = emit_int(out, component->name, component->integer);
                break;
            case kQbvalFloat:
                written = emit_real(out, component->name, component->float_value_);
                break;
            case kQbvalString:
                written = write_string(out, component->string, component->name);
                break;
            case kQbvalInlineText:
                written = write_local_string(out, component->string, component->name);
                break;
            case kQbvalPair:
                if (out.remaining() < 13)
                    return 0;
                write_name_and_type(out, kQbvalPair, component->name);
                out.f32(component->pair->x);
                out.f32(component->pair->y);
                written = written_since(out, at);
                break;
            case kQbvalVector:
                if (out.remaining() < 17)
                    return 0;
                write_name_and_type(out, kQbvalVector, component->name);
                out.f32(component->vector->x);
                out.f32(component->vector->y);
                out.f32(component->vector->z);
                written = written_since(out, at);
                break;
            case kQbvalScriptBody:
                // (a script can't be sent: a placeholder name instead)
                if (out.remaining() < 9)
                    return 0;
                write_name_and_type(out, kQbvalName, component->name);
                out.u32(kScriptPlaceholder);
                written = written_since(out, at);
                break;
            case kQbvalStruct:
                if (out.remaining() < 5)
                    return 0;
                write_name_and_type(out, kQbvalStruct, component->name);
                written = emit_structure(out, component->structure, assert, nullptr);
                break;
            case kQbvalArray:
                if (out.remaining() < 5)
                    return 0;
                write_name_and_type(out, kQbvalArray, component->name);
                written = write_array_to_buffer(out, component->array, assert);
                break;
            case kQbvalName:
                if (out.remaining() < 9)
                    return 0;
                write_name_and_type(out, kQbvalName, component->name);
                out.u32(component->checksum);
                written = written_since(out, at);
                break;
            default:
                return 0; // (C functions, member functions, pointers...)
            }
            if (written == 0 || !out.ok())
                return 0;
        }
    }
    if (!out.u8(0)) // (the end marker: no room for it - nothing)
        return 0;
    return written_since(out, start);
}

} // namespace

// 0x00479E20
uint32_t emit_into_buffer(Struct* structure, uint8_t* buffer, uint32_t size, int assert, void (*callback)())
{
    util::ByteWriter out(buffer, size);
    return emit_structure(out, structure, assert, callback);
}

// 0x0047A070
uint32_t measure_buffer_bytes(Struct* structure, uint32_t max_size)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    auto* scratch = static_cast<uint8_t*>(memory::alloc_block(max_size));
    uint32_t size = emit_into_buffer(structure, scratch, max_size, 1, nullptr);
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
    if (type & 0x80)
        return fetch_list(kShortNames)->fetch_part(reader.u8());
    if (type & 0x40)
        return fetch_list(kMediumNames)->fetch_part(reader.u16());
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
    manager->enter_heap_scope(manager->script_heap_);
    // (a buffer cut short reads as zeros: its type byte ends the structure)
    for (uint8_t type = reader.u8(); type != 0; type = reader.u8()) {
        uint32_t name = read_name(reader, type);
        if (!reader.ok())
            break;
        switch (type & 0x3F) {
        case kQbvalInteger:
            structure->put_int(name, reader.i32());
            break;
        case kQbvalFloat:
            structure->push_float(name, reader.f32());
            break;
        case kQbvalString:
            structure->append_word(name, read_text(reader));
            break;
        case kQbvalInlineText:
            structure->append_inline_text(name, read_text(reader));
            break;
        case kQbvalPair: {
            float x = reader.f32();
            float y = reader.f32();
            structure->insert_pair(name, x, y);
            break;
        }
        case kQbvalVector: {
            float x = reader.f32();
            float y = reader.f32();
            float z = reader.f32();
            structure->put_vec3(name, x, y, z);
            break;
        }
        case kQbvalStruct: {
            Struct* inner = spawn_param_bag();
            if (inner)
                new (inner) script::Struct;
            decode_structure(inner, reader);
            structure->append_struct_ref(name, inner);
            break;
        }
        case kQbvalArray: {
            Array* inner = alloc_script_array();
            if (inner)
                new (inner) script::Array;
            decode_array(inner, reader);
            structure->append_array_ref(name, inner);
            break;
        }
        case kQbvalName:
            structure->put_name_key(name, reader.u32());
            break;
        case kSignedByte:
            structure->put_int(name, reader.i8());
            break;
        case kSignedShort:
            structure->put_int(name, reader.i16());
            break;
        case kUnsignedByte:
            structure->put_int(name, reader.u8());
            break;
        case kUnsignedShort:
            structure->put_int(name, reader.u16());
            break;
        case kZeroInteger:
            structure->put_int(name, 0);
            break;
        case kTagZeroF:
            structure->push_float(name, 0.0f);
            break;
        default:
            break; // (no value: just the name was read)
        }
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// (past the end of the buffer every element is still made - zeros, empty strings and structures - so that no
// element is left null)
void decode_array(Array* array, util::ByteReader& reader)
{
    release_array_storage(array);
    uint8_t type = reader.u8();
    uint32_t count = reader.u16();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    array->configure_size_kind(count, static_cast<QbValueKind>(type));
    memory::Hub::get_pull()->leave_heap_scope();
    switch (type) {
    case kQbvalInteger:
    case kQbvalName:
        for (uint32_t i = 0; i < count; ++i)
            array->set_element(i, reader.u32());
        break;
    case kQbvalFloat:
        for (uint32_t i = 0; i < count; ++i)
            array->change_float(i, reader.f32());
        break;
    case kQbvalString:
    case kQbvalInlineText:
        for (uint32_t i = 0; i < count; ++i)
            array->set_element(i, reinterpret_cast<uint32_t>(spawn_text(read_text(reader))));
        break;
    case kQbvalPair:
        for (uint32_t i = 0; i < count; ++i) {
            Pair* pair = new_pair();
            if (pair)
                new (pair) script::Pair;
            pair->x = reader.f32();
            pair->y = reader.f32();
            array->set_element(i, reinterpret_cast<uint32_t>(pair));
        }
        break;
    case kQbvalVector:
        for (uint32_t i = 0; i < count; ++i) {
            Vector* vector = alloc_script_vector();
            if (vector)
                new (vector) script::Vector;
            vector->x = reader.f32();
            vector->y = reader.f32();
            vector->z = reader.f32();
            array->set_element(i, reinterpret_cast<uint32_t>(vector));
        }
        break;
    case kQbvalStruct:
        for (uint32_t i = 0; i < count; ++i) {
            Struct* inner = spawn_param_bag();
            if (inner)
                new (inner) script::Struct;
            decode_structure(inner, reader);
            array->set_element(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    case kQbvalArray:
        for (uint32_t i = 0; i < count; ++i) {
            Array* inner = alloc_script_array();
            if (inner)
                new (inner) script::Array;
            decode_array(inner, reader);
            array->set_element(i, reinterpret_cast<uint32_t>(inner));
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
// NOLINTEND(cppcoreguidelines-pro-type-union-access)
