// Script structures to and from byte buffers (0x00479840-0x0047A8D1). See struct_buffer.h.

#include "engine/script/struct_buffer.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/util/byte_stream.h"

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
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

uint32_t get_written(const uint8_t* end, const uint8_t* start) { return static_cast<uint32_t>(end - start); }

} // namespace

// 0x00479840
uint8_t* write_name_and_type(uint8_t* buffer, uint8_t type, uint32_t name)
{
    Array* short_names = fetch_list(kShortNames);
    for (int i = 0; i < static_cast<int>(short_names->extent_); ++i) {
        if (short_names->fetch_part(i) == name) {
            buffer[0] = type | 0x80;
            buffer[1] = static_cast<uint8_t>(i);
            return buffer + 2;
        }
    }
    Array* medium_names = fetch_list(kMediumNames);
    for (int i = 0; i < static_cast<int>(medium_names->extent_); ++i) {
        if (medium_names->fetch_part(i) == name) {
            buffer[0] = type | 0x40;
            return store_word16(buffer + 1, static_cast<uint16_t>(i));
        }
    }
    buffer[0] = type;
    return store_word32(buffer + 1, name);
}

// 0x004798F0
uint32_t emit_int(uint32_t name, int value, uint8_t* buffer, uint32_t size)
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
    uint8_t* end = write_name_and_type(scratch, type, name);
    switch (type) {
    case kQbvalInteger:
        end = store_word32(end, static_cast<uint32_t>(value));
        break;
    case kSignedByte:
    case kUnsignedByte:
        *end++ = static_cast<uint8_t>(value);
        break;
    case kSignedShort:
    case kUnsignedShort:
        end = store_word16(end, static_cast<uint16_t>(value));
        break;
    default:
        break;
    }
    uint32_t length = get_written(end, scratch);
    if (size < length)
        return 0;
    std::memcpy(buffer, scratch, length);
    return length;
}

// 0x00479A50
uint32_t emit_real(uint8_t* buffer, uint32_t name, uint32_t size, float value)
{
    float magnitude = value < 0.0f ? -value : value;
    // (a NaN counts as big)
    if (magnitude < 0.0001) {
        if (size < 5)
            return 0;
        return get_written(write_name_and_type(buffer, kTagZeroF, name), buffer);
    }
    if (size < 9)
        return 0;
    return get_written(store_word32(write_name_and_type(buffer, kQbvalFloat, name), float_bits(value)), buffer);
}

namespace {

uint32_t write_string_of_type(uint8_t* buffer, const char* string, uint32_t name, uint32_t size, uint8_t type)
{
    uint32_t length = static_cast<uint32_t>(std::strlen(string));
    if (size < length + 6)
        return 0;
    uint8_t* p = write_name_and_type(buffer, type, name);
    for (uint32_t i = 0; i < length; ++i)
        *p++ = static_cast<uint8_t>(string[i]);
    *p = 0;
    return get_written(p, buffer) + 1;
}

} // namespace

// 0x00479AC0
uint32_t write_string(uint8_t* buffer, const char* string, uint32_t name, uint32_t size)
{
    return write_string_of_type(buffer, string, name, size, kQbvalString);
}

// 0x00479B20
uint32_t write_local_string(uint8_t* buffer, const char* string, uint32_t name, uint32_t size)
{
    return write_string_of_type(buffer, string, name, size, kQbvalInlineText);
}

// 0x00479B80
uint32_t write_array_to_buffer(Array* array, uint8_t* buffer, uint32_t size, int assert)
{
    if (size < 3)
        return 0;
    uint8_t type = array->kind_tag_;
    uint32_t count = array->extent_;
    buffer[0] = type;
    uint8_t* p = store_word16(buffer + 1, static_cast<uint16_t>(count));
    uint32_t remaining = size - 3;
    switch (type) {
    case kQbvalInteger:
    case kQbvalFloat:
    case kQbvalName: {
        uint32_t bytes = count * 4;
        if (bytes > remaining)
            return 0;
        const uint32_t* elements = array->find_array_value();
        for (uint32_t i = 0; i < count; ++i)
            p = store_word32(p, elements[i]);
        remaining -= bytes;
        break;
    }
    case kQbvalString:
    case kQbvalInlineText: {
        const uint32_t* elements = array->find_array_value();
        for (uint32_t i = 0; i < count; ++i) {
            for (const char* c = reinterpret_cast<const char*>(elements[i]); *c; ++c) {
                if (remaining == 0)
                    return 0;
                *p++ = static_cast<uint8_t>(*c);
                --remaining;
            }
            if (remaining == 0)
                return 0;
            *p++ = 0;
            --remaining;
        }
        break;
    }
    case kQbvalPair: {
        uint32_t bytes = count * 8;
        if (bytes > remaining)
            return 0;
        auto** pairs = reinterpret_cast<Pair**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            p = store_word32(p, float_bits(pairs[i]->x));
            p = store_word32(p, float_bits(pairs[i]->y));
        }
        remaining -= bytes;
        break;
    }
    case kQbvalVector: {
        uint32_t bytes = count * 12;
        if (bytes > remaining)
            return 0;
        auto** vectors = reinterpret_cast<Vector**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            p = store_word32(p, float_bits(vectors[i]->x));
            p = store_word32(p, float_bits(vectors[i]->y));
            p = store_word32(p, float_bits(vectors[i]->z));
        }
        remaining -= bytes;
        break;
    }
    case kQbvalStruct: {
        auto** structures = reinterpret_cast<Struct**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t written = emit_into_buffer(structures[i], p, remaining, assert, nullptr);
            if (written == 0)
                return 0;
            remaining -= written;
            p += written;
        }
        break;
    }
    case kQbvalArray: {
        auto** arrays = reinterpret_cast<Array**>(array->find_array_value());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t written = write_array_to_buffer(arrays[i], p, remaining, assert);
            if (written == 0)
                return 0;
            remaining -= written;
            p += written;
        }
        break;
    }
    default:
        break; // (other types: just the type and count)
    }
    return size - remaining;
}

// 0x00479E20
uint32_t emit_into_buffer(Struct* structure, uint8_t* buffer, uint32_t size, int assert, void (*callback)())
{
    uint32_t remaining = size;
    uint8_t* p = buffer;
    if (structure) {
        for (Component* component = structure->step_to_entry(nullptr); component;
             component = structure->step_to_entry(component)) {
            if (callback)
                callback();
            uint32_t written;
            switch (component->kind_tag()) {
            case kQbvalInteger:
                written = emit_int(component->name, component->integer, p, remaining);
                break;
            case kQbvalFloat:
                written = emit_real(p, component->name, remaining, component->float_value_);
                break;
            case kQbvalString:
                written = write_string(p, component->string, component->name, remaining);
                break;
            case kQbvalInlineText:
                written = write_local_string(p, component->string, component->name, remaining);
                break;
            case kQbvalPair: {
                if (remaining < 13)
                    return 0;
                uint8_t* end = write_name_and_type(p, kQbvalPair, component->name);
                end = store_word32(end, float_bits(component->pair->x));
                end = store_word32(end, float_bits(component->pair->y));
                written = get_written(end, p);
                break;
            }
            case kQbvalVector: {
                if (remaining < 17)
                    return 0;
                uint8_t* end = write_name_and_type(p, kQbvalVector, component->name);
                end = store_word32(end, float_bits(component->vector->x));
                end = store_word32(end, float_bits(component->vector->y));
                end = store_word32(end, float_bits(component->vector->z));
                written = get_written(end, p);
                break;
            }
            case kQbvalScriptBody: {
                // (a script can't be sent: a placeholder name instead)
                if (remaining < 9)
                    return 0;
                uint8_t* end = write_name_and_type(p, kQbvalName, component->name);
                written = get_written(store_word32(end, kScriptPlaceholder), p);
                break;
            }
            case kQbvalStruct: {
                if (remaining < 5)
                    return 0;
                uint8_t* end = write_name_and_type(p, kQbvalStruct, component->name);
                uint32_t header = get_written(end, p);
                uint32_t inner = emit_into_buffer(component->structure, end, remaining - header, assert, nullptr);
                if (inner == 0)
                    return 0;
                written = inner + header;
                break;
            }
            case kQbvalArray: {
                if (remaining < 5)
                    return 0;
                uint8_t* end = write_name_and_type(p, kQbvalArray, component->name);
                uint32_t header = get_written(end, p);
                uint32_t inner = write_array_to_buffer(component->array, end, remaining - header, assert);
                if (inner == 0)
                    return 0;
                written = inner + header;
                break;
            }
            case kQbvalName: {
                if (remaining < 9)
                    return 0;
                uint8_t* end = write_name_and_type(p, kQbvalName, component->name);
                written = get_written(store_word32(end, component->checksum), p);
                break;
            }
            default:
                return 0; // (C functions, member functions, pointers...)
            }
            if (written == 0)
                return 0;
            remaining -= written;
            p += written;
        }
    }
    if (remaining == 0)
        return 0;
    *p = 0;
    return size - remaining + 1;
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
                inner->prepare_all();
            decode_structure(inner, reader);
            structure->append_struct_ref(name, inner);
            break;
        }
        case kQbvalArray: {
            Array* inner = alloc_script_array();
            if (inner)
                inner->prepare_all();
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
                pair->prepare_all();
            pair->x = reader.f32();
            pair->y = reader.f32();
            array->set_element(i, reinterpret_cast<uint32_t>(pair));
        }
        break;
    case kQbvalVector:
        for (uint32_t i = 0; i < count; ++i) {
            Vector* vector = alloc_script_vector();
            if (vector)
                vector->prepare_all();
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
                inner->prepare_all();
            decode_structure(inner, reader);
            array->set_element(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    case kQbvalArray:
        for (uint32_t i = 0; i < count; ++i) {
            Array* inner = alloc_script_array();
            if (inner)
                inner->prepare_all();
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
