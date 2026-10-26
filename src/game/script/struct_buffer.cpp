// Script structures to and from byte buffers (0x00479840-0x0047A8D1). See struct_buffer.h.

#include "game/script/struct_buffer.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

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
constexpr uint8_t kZeroFloat = 0x13;

uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
float BitsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t Written(const uint8_t* end, const uint8_t* start) { return static_cast<uint32_t>(end - start); }

} // namespace

// 0x00479840
uint8_t* WriteNameAndType(uint8_t* buffer, uint8_t type, uint32_t name)
{
    CArray* shortNames = GetArray(kShortNames);
    for (int i = 0; i < static_cast<int>(shortNames->m_size); ++i) {
        if (shortNames->GetElement(i) == name) {
            buffer[0] = type | 0x80;
            buffer[1] = static_cast<uint8_t>(i);
            return buffer + 2;
        }
    }
    CArray* mediumNames = GetArray(kMediumNames);
    for (int i = 0; i < static_cast<int>(mediumNames->m_size); ++i) {
        if (mediumNames->GetElement(i) == name) {
            buffer[0] = type | 0x40;
            return Write2Bytes(buffer + 1, static_cast<uint16_t>(i));
        }
    }
    buffer[0] = type;
    return Write4Bytes(buffer + 1, name);
}

// 0x004798F0
uint32_t WriteInteger(uint32_t name, int value, uint8_t* buffer, uint32_t size)
{
    uint8_t type = ESYMBOLTYPE_INTEGER;
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
    uint8_t* end = WriteNameAndType(scratch, type, name);
    switch (type) {
    case ESYMBOLTYPE_INTEGER:
        end = Write4Bytes(end, static_cast<uint32_t>(value));
        break;
    case kSignedByte:
    case kUnsignedByte:
        *end++ = static_cast<uint8_t>(value);
        break;
    case kSignedShort:
    case kUnsignedShort:
        end = Write2Bytes(end, static_cast<uint16_t>(value));
        break;
    default:
        break;
    }
    uint32_t length = Written(end, scratch);
    if (size < length)
        return 0;
    std::memcpy(buffer, scratch, length);
    return length;
}

// The name in ECX, the value in EBX; the buffer and size on the stack (the caller pops them).
__declspec(naked) void WriteIntegerStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        push ecx
        call WriteInteger
        add esp, 16
        ret
    }
}

// 0x00479A50
uint32_t WriteFloat(uint8_t* buffer, uint32_t name, uint32_t size, float value)
{
    float magnitude = value < 0.0f ? -value : value;
    // (a NaN counts as big)
    if (magnitude < 0.0001) {
        if (size < 5)
            return 0;
        return Written(WriteNameAndType(buffer, kZeroFloat, name), buffer);
    }
    if (size < 9)
        return 0;
    return Written(Write4Bytes(WriteNameAndType(buffer, ESYMBOLTYPE_FLOAT, name), FloatBits(value)), buffer);
}

// The buffer in ECX, the name in EDX, the size in ESI; the value on the stack (the caller pops it).
__declspec(naked) void WriteFloatStub()
{
    __asm {
        push dword ptr [esp + 4]
        push esi
        push edx
        push ecx
        call WriteFloat
        add esp, 16
        ret
    }
}

namespace {

uint32_t WriteStringOfType(uint8_t* buffer, const char* string, uint32_t name, uint32_t size, uint8_t type)
{
    uint32_t length = static_cast<uint32_t>(std::strlen(string));
    if (size < length + 6)
        return 0;
    uint8_t* p = WriteNameAndType(buffer, type, name);
    for (uint32_t i = 0; i < length; ++i)
        *p++ = static_cast<uint8_t>(string[i]);
    *p = 0;
    return Written(p, buffer) + 1;
}

} // namespace

// 0x00479AC0
uint32_t WriteString(uint8_t* buffer, const char* string, uint32_t name, uint32_t size)
{
    return WriteStringOfType(buffer, string, name, size, ESYMBOLTYPE_STRING);
}

// 0x00479B20
uint32_t WriteLocalString(uint8_t* buffer, const char* string, uint32_t name, uint32_t size)
{
    return WriteStringOfType(buffer, string, name, size, ESYMBOLTYPE_LOCALSTRING);
}

// The buffer in EAX, the string in ECX; the name and size on the stack (the caller pops them).
__declspec(naked) void WriteStringStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push eax
        call WriteString
        add esp, 16
        ret
    }
}
__declspec(naked) void WriteLocalStringStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        push eax
        call WriteLocalString
        add esp, 16
        ret
    }
}

// 0x00479B80
uint32_t WriteArrayToBuffer(CArray* array, uint8_t* buffer, uint32_t size, int assert)
{
    if (size < 3)
        return 0;
    uint8_t type = array->m_type;
    uint32_t count = array->m_size;
    buffer[0] = type;
    uint8_t* p = Write2Bytes(buffer + 1, static_cast<uint16_t>(count));
    uint32_t remaining = size - 3;
    switch (type) {
    case ESYMBOLTYPE_INTEGER:
    case ESYMBOLTYPE_FLOAT:
    case ESYMBOLTYPE_NAME: {
        uint32_t bytes = count * 4;
        if (bytes > remaining)
            return 0;
        const uint32_t* elements = array->GetArrayPointer();
        for (uint32_t i = 0; i < count; ++i)
            p = Write4Bytes(p, elements[i]);
        remaining -= bytes;
        break;
    }
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING: {
        const uint32_t* elements = array->GetArrayPointer();
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
    case ESYMBOLTYPE_PAIR: {
        uint32_t bytes = count * 8;
        if (bytes > remaining)
            return 0;
        auto** pairs = reinterpret_cast<CPair**>(array->GetArrayPointer());
        for (uint32_t i = 0; i < count; ++i) {
            p = Write4Bytes(p, FloatBits(pairs[i]->x));
            p = Write4Bytes(p, FloatBits(pairs[i]->y));
        }
        remaining -= bytes;
        break;
    }
    case ESYMBOLTYPE_VECTOR: {
        uint32_t bytes = count * 12;
        if (bytes > remaining)
            return 0;
        auto** vectors = reinterpret_cast<CVector**>(array->GetArrayPointer());
        for (uint32_t i = 0; i < count; ++i) {
            p = Write4Bytes(p, FloatBits(vectors[i]->x));
            p = Write4Bytes(p, FloatBits(vectors[i]->y));
            p = Write4Bytes(p, FloatBits(vectors[i]->z));
        }
        remaining -= bytes;
        break;
    }
    case ESYMBOLTYPE_STRUCTURE: {
        auto** structures = reinterpret_cast<CStruct**>(array->GetArrayPointer());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t written = WriteToBuffer(structures[i], p, remaining, assert, nullptr);
            if (written == 0)
                return 0;
            remaining -= written;
            p += written;
        }
        break;
    }
    case ESYMBOLTYPE_ARRAY: {
        auto** arrays = reinterpret_cast<CArray**>(array->GetArrayPointer());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t written = WriteArrayToBuffer(arrays[i], p, remaining, assert);
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
uint32_t WriteToBuffer(CStruct* structure, uint8_t* buffer, uint32_t size, int assert, void (*callback)())
{
    uint32_t remaining = size;
    uint8_t* p = buffer;
    if (structure) {
        for (CComponent* component = structure->GetNextComponent(nullptr); component;
             component = structure->GetNextComponent(component)) {
            if (callback)
                callback();
            uint32_t written;
            switch (component->Type()) {
            case ESYMBOLTYPE_INTEGER:
                written = WriteInteger(component->name, component->integer, p, remaining);
                break;
            case ESYMBOLTYPE_FLOAT:
                written = WriteFloat(p, component->name, remaining, component->floatValue);
                break;
            case ESYMBOLTYPE_STRING:
                written = WriteString(p, component->string, component->name, remaining);
                break;
            case ESYMBOLTYPE_LOCALSTRING:
                written = WriteLocalString(p, component->string, component->name, remaining);
                break;
            case ESYMBOLTYPE_PAIR: {
                if (remaining < 13)
                    return 0;
                uint8_t* end = WriteNameAndType(p, ESYMBOLTYPE_PAIR, component->name);
                end = Write4Bytes(end, FloatBits(component->pair->x));
                end = Write4Bytes(end, FloatBits(component->pair->y));
                written = Written(end, p);
                break;
            }
            case ESYMBOLTYPE_VECTOR: {
                if (remaining < 17)
                    return 0;
                uint8_t* end = WriteNameAndType(p, ESYMBOLTYPE_VECTOR, component->name);
                end = Write4Bytes(end, FloatBits(component->vector->x));
                end = Write4Bytes(end, FloatBits(component->vector->y));
                end = Write4Bytes(end, FloatBits(component->vector->z));
                written = Written(end, p);
                break;
            }
            case ESYMBOLTYPE_QSCRIPT: {
                // (a script can't be sent: a placeholder name instead)
                if (remaining < 9)
                    return 0;
                uint8_t* end = WriteNameAndType(p, ESYMBOLTYPE_NAME, component->name);
                written = Written(Write4Bytes(end, kScriptPlaceholder), p);
                break;
            }
            case ESYMBOLTYPE_STRUCTURE: {
                if (remaining < 5)
                    return 0;
                uint8_t* end = WriteNameAndType(p, ESYMBOLTYPE_STRUCTURE, component->name);
                uint32_t header = Written(end, p);
                uint32_t inner = WriteToBuffer(component->structure, end, remaining - header, assert, nullptr);
                if (inner == 0)
                    return 0;
                written = inner + header;
                break;
            }
            case ESYMBOLTYPE_ARRAY: {
                if (remaining < 5)
                    return 0;
                uint8_t* end = WriteNameAndType(p, ESYMBOLTYPE_ARRAY, component->name);
                uint32_t header = Written(end, p);
                uint32_t inner = WriteArrayToBuffer(component->array, end, remaining - header, assert);
                if (inner == 0)
                    return 0;
                written = inner + header;
                break;
            }
            case ESYMBOLTYPE_NAME: {
                if (remaining < 9)
                    return 0;
                uint8_t* end = WriteNameAndType(p, ESYMBOLTYPE_NAME, component->name);
                written = Written(Write4Bytes(end, component->checksum), p);
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
uint32_t CalculateBufferSize(CStruct* structure, uint32_t maxSize)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    auto* scratch = static_cast<uint8_t*>(Mem::Malloc(maxSize));
    uint32_t size = WriteToBuffer(structure, scratch, maxSize, 1, nullptr);
    Mem::FreeMalloc(scratch);
    Mem::Manager::Get()->PopContext();
    return size;
}

// 0x0047A0C0
const uint8_t* ReadFromBuffer(CStruct* structure, const uint8_t* buffer)
{
    structure->Clear();
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    const uint8_t* p = buffer;
    for (uint8_t type = *p++; type != 0; type = *p++) {
        uint32_t name;
        if (type & 0x80) {
            CArray* names = GetArray(kShortNames);
            name = names->GetElement(*p);
            p += 1;
        } else if (type & 0x40) {
            CArray* names = GetArray(kMediumNames);
            name = names->GetElement(Read2Bytes(p));
            p += 2;
        } else {
            name = Read4Bytes(p);
            p += 4;
        }
        switch (type & 0x3F) {
        case ESYMBOLTYPE_INTEGER:
            structure->AddInteger(name, static_cast<int>(Read4Bytes(p)));
            p += 4;
            break;
        case ESYMBOLTYPE_FLOAT:
            structure->AddFloat(name, BitsFloat(Read4Bytes(p)));
            p += 4;
            break;
        case ESYMBOLTYPE_STRING:
            structure->AddString(name, reinterpret_cast<const char*>(p));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
            break;
        case ESYMBOLTYPE_LOCALSTRING:
            structure->AddLocalString(name, reinterpret_cast<const char*>(p));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
            break;
        case ESYMBOLTYPE_PAIR: {
            uint32_t x = Read4Bytes(p);
            uint32_t y = Read4Bytes(p + 4);
            p += 8;
            structure->AddPair(name, BitsFloat(x), BitsFloat(y));
            break;
        }
        case ESYMBOLTYPE_VECTOR: {
            uint32_t x = Read4Bytes(p);
            uint32_t y = Read4Bytes(p + 4);
            uint32_t z = Read4Bytes(p + 8);
            p += 12;
            structure->AddVector(name, BitsFloat(x), BitsFloat(y), BitsFloat(z));
            break;
        }
        case ESYMBOLTYPE_STRUCTURE: {
            CStruct* inner = NewStruct();
            if (inner)
                inner->Init();
            p = ReadFromBuffer(inner, p);
            structure->AddStructurePointer(name, inner);
            break;
        }
        case ESYMBOLTYPE_ARRAY: {
            CArray* inner = NewArray();
            if (inner)
                inner->Init();
            p = ReadArrayFromBuffer(inner, p);
            structure->AddArrayPointer(name, inner);
            break;
        }
        case ESYMBOLTYPE_NAME:
            structure->AddChecksum(name, Read4Bytes(p));
            p += 4;
            break;
        case kSignedByte:
            structure->AddInteger(name, static_cast<int8_t>(*p));
            p += 1;
            break;
        case kSignedShort:
            structure->AddInteger(name, static_cast<int16_t>(Read2Bytes(p)));
            p += 2;
            break;
        case kUnsignedByte:
            structure->AddInteger(name, *p);
            p += 1;
            break;
        case kUnsignedShort:
            structure->AddInteger(name, Read2Bytes(p));
            p += 2;
            break;
        case kZeroInteger:
            structure->AddInteger(name, 0);
            break;
        case kZeroFloat:
            structure->AddFloat(name, 0.0f);
            break;
        default:
            break; // (no value: just the name was read)
        }
    }
    Mem::Manager::Get()->PopContext();
    return p;
}

// 0x0047A440
const uint8_t* ReadArrayFromBuffer(CArray* array, const uint8_t* buffer)
{
    CleanUpArray(array);
    uint8_t type = buffer[0];
    uint32_t count = buffer[1] | static_cast<uint32_t>(buffer[2]) << 8;
    const uint8_t* p = buffer + 3;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    array->SetSizeAndType(count, static_cast<ESymbolType>(type));
    Mem::Manager::Get()->PopContext();
    switch (type) {
    case ESYMBOLTYPE_INTEGER:
    case ESYMBOLTYPE_NAME:
        for (uint32_t i = 0; i < count; ++i, p += 4)
            array->SetElement(i, Read4Bytes(p));
        break;
    case ESYMBOLTYPE_FLOAT:
        for (uint32_t i = 0; i < count; ++i, p += 4)
            array->SetFloat(i, BitsFloat(Read4Bytes(p)));
        break;
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        for (uint32_t i = 0; i < count; ++i) {
            array->SetElement(i, reinterpret_cast<uint32_t>(CreateString(reinterpret_cast<const char*>(p))));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
        }
        break;
    case ESYMBOLTYPE_PAIR:
        for (uint32_t i = 0; i < count; ++i) {
            CPair* pair = NewPair();
            if (pair)
                pair->Init();
            pair->x = BitsFloat(Read4Bytes(p));
            pair->y = BitsFloat(Read4Bytes(p + 4));
            p += 8;
            array->SetElement(i, reinterpret_cast<uint32_t>(pair));
        }
        break;
    case ESYMBOLTYPE_VECTOR:
        for (uint32_t i = 0; i < count; ++i) {
            CVector* vector = NewVector();
            if (vector)
                vector->Init();
            vector->x = BitsFloat(Read4Bytes(p));
            vector->y = BitsFloat(Read4Bytes(p + 4));
            vector->z = BitsFloat(Read4Bytes(p + 8));
            p += 12;
            array->SetElement(i, reinterpret_cast<uint32_t>(vector));
        }
        break;
    case ESYMBOLTYPE_STRUCTURE:
        for (uint32_t i = 0; i < count; ++i) {
            CStruct* inner = NewStruct();
            if (inner)
                inner->Init();
            p = ReadFromBuffer(inner, p);
            array->SetElement(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    case ESYMBOLTYPE_ARRAY:
        for (uint32_t i = 0; i < count; ++i) {
            CArray* inner = NewArray();
            if (inner)
                inner->Init();
            p = ReadArrayFromBuffer(inner, p);
            array->SetElement(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    default:
        break;
    }
    return p;
}

} // namespace Script

REPLACE(0x00479840, Script::WriteNameAndType);
REPLACE_UNCHECKED(0x004798F0, Script::WriteIntegerStub);
REPLACE_UNCHECKED(0x00479A50, Script::WriteFloatStub);
REPLACE_UNCHECKED(0x00479AC0, Script::WriteStringStub);
REPLACE_UNCHECKED(0x00479B20, Script::WriteLocalStringStub);
REPLACE(0x00479B80, Script::WriteArrayToBuffer);
REPLACE(0x00479E20, Script::WriteToBuffer);
REPLACE(0x0047A070, Script::CalculateBufferSize);
REPLACE(0x0047A0C0, Script::ReadFromBuffer);
REPLACE(0x0047A440, Script::ReadArrayFromBuffer);
