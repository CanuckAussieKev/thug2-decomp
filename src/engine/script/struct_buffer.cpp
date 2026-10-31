// Script structures to and from byte buffers (0x00479840-0x0047A8D1). See struct_buffer.h.

#include "engine/script/struct_buffer.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"

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
float bits_float(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
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

// The name in ECX, the value in EBX; the buffer and size on the stack (the caller pops them).
__declspec(naked) void write_integer_stub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        push ecx
        call emit_int
        add esp, 16
        ret
    }
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

// The buffer in ECX, the name in EDX, the size in ESI; the value on the stack (the caller pops it).
__declspec(naked) void write_float_stub()
{
    __asm {
        push dword ptr [esp + 4]
        push esi
        push edx
        push ecx
        call emit_real
        add esp, 16
        ret
    }
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

// The buffer in EAX, the string in ECX; the name and size on the stack (the caller pops them).
__declspec(naked) void write_string_stub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push eax
        call write_string
        add esp, 16
        ret
    }
}
__declspec(naked) void write_local_string_stub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push eax
        call write_local_string
        add esp, 16
        ret
    }
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

// 0x0047A0C0
const uint8_t* decode_from_bytes(Struct* structure, const uint8_t* buffer)
{
    structure->get_clear();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    const uint8_t* p = buffer;
    for (uint8_t type = *p++; type != 0; type = *p++) {
        uint32_t name;
        if (type & 0x80) {
            Array* names = fetch_list(kShortNames);
            name = names->fetch_part(*p);
            p += 1;
        } else if (type & 0x40) {
            Array* names = fetch_list(kMediumNames);
            name = names->fetch_part(fetch_word16(p));
            p += 2;
        } else {
            name = fetch_word32(p);
            p += 4;
        }
        switch (type & 0x3F) {
        case kQbvalInteger:
            structure->put_int(name, static_cast<int>(fetch_word32(p)));
            p += 4;
            break;
        case kQbvalFloat:
            structure->push_float(name, bits_float(fetch_word32(p)));
            p += 4;
            break;
        case kQbvalString:
            structure->append_word(name, reinterpret_cast<const char*>(p));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
            break;
        case kQbvalInlineText:
            structure->append_inline_text(name, reinterpret_cast<const char*>(p));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
            break;
        case kQbvalPair: {
            uint32_t x = fetch_word32(p);
            uint32_t y = fetch_word32(p + 4);
            p += 8;
            structure->insert_pair(name, bits_float(x), bits_float(y));
            break;
        }
        case kQbvalVector: {
            uint32_t x = fetch_word32(p);
            uint32_t y = fetch_word32(p + 4);
            uint32_t z = fetch_word32(p + 8);
            p += 12;
            structure->put_vec3(name, bits_float(x), bits_float(y), bits_float(z));
            break;
        }
        case kQbvalStruct: {
            Struct* inner = spawn_param_bag();
            if (inner)
                inner->prepare_all();
            p = decode_from_bytes(inner, p);
            structure->append_struct_ref(name, inner);
            break;
        }
        case kQbvalArray: {
            Array* inner = alloc_script_array();
            if (inner)
                inner->prepare_all();
            p = read_array_from_buffer(inner, p);
            structure->append_array_ref(name, inner);
            break;
        }
        case kQbvalName:
            structure->put_name_key(name, fetch_word32(p));
            p += 4;
            break;
        case kSignedByte:
            structure->put_int(name, static_cast<int8_t>(*p));
            p += 1;
            break;
        case kSignedShort:
            structure->put_int(name, static_cast<int16_t>(fetch_word16(p)));
            p += 2;
            break;
        case kUnsignedByte:
            structure->put_int(name, *p);
            p += 1;
            break;
        case kUnsignedShort:
            structure->put_int(name, fetch_word16(p));
            p += 2;
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
    return p;
}

// 0x0047A440
const uint8_t* read_array_from_buffer(Array* array, const uint8_t* buffer)
{
    release_array_storage(array);
    uint8_t type = buffer[0];
    uint32_t count = buffer[1] | static_cast<uint32_t>(buffer[2]) << 8;
    const uint8_t* p = buffer + 3;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    array->configure_size_kind(count, static_cast<QbValueKind>(type));
    memory::Hub::get_pull()->leave_heap_scope();
    switch (type) {
    case kQbvalInteger:
    case kQbvalName:
        for (uint32_t i = 0; i < count; ++i, p += 4)
            array->set_element(i, fetch_word32(p));
        break;
    case kQbvalFloat:
        for (uint32_t i = 0; i < count; ++i, p += 4)
            array->change_float(i, bits_float(fetch_word32(p)));
        break;
    case kQbvalString:
    case kQbvalInlineText:
        for (uint32_t i = 0; i < count; ++i) {
            array->set_element(i, reinterpret_cast<uint32_t>(spawn_text(reinterpret_cast<const char*>(p))));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
        }
        break;
    case kQbvalPair:
        for (uint32_t i = 0; i < count; ++i) {
            Pair* pair = new_pair();
            if (pair)
                pair->prepare_all();
            pair->x = bits_float(fetch_word32(p));
            pair->y = bits_float(fetch_word32(p + 4));
            p += 8;
            array->set_element(i, reinterpret_cast<uint32_t>(pair));
        }
        break;
    case kQbvalVector:
        for (uint32_t i = 0; i < count; ++i) {
            Vector* vector = alloc_script_vector();
            if (vector)
                vector->prepare_all();
            vector->x = bits_float(fetch_word32(p));
            vector->y = bits_float(fetch_word32(p + 4));
            vector->z = bits_float(fetch_word32(p + 8));
            p += 12;
            array->set_element(i, reinterpret_cast<uint32_t>(vector));
        }
        break;
    case kQbvalStruct:
        for (uint32_t i = 0; i < count; ++i) {
            Struct* inner = spawn_param_bag();
            if (inner)
                inner->prepare_all();
            p = decode_from_bytes(inner, p);
            array->set_element(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    case kQbvalArray:
        for (uint32_t i = 0; i < count; ++i) {
            Array* inner = alloc_script_array();
            if (inner)
                inner->prepare_all();
            p = read_array_from_buffer(inner, p);
            array->set_element(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    default:
        break;
    }
    return p;
}

} // namespace script
