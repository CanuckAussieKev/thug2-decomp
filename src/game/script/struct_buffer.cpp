// Script structures to and from byte buffers (0x00479840-0x0047A8D1). See struct_buffer.h.

#include "game/script/struct_buffer.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/qb.h"
#include "game/script/param_struct.h"

namespace Scripts {

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
    CArray* shortNames = FetchList(kShortNames);
    for (int i = 0; i < static_cast<int>(shortNames->m_extent); ++i) {
        if (shortNames->FetchPart(i) == name) {
            buffer[0] = type | 0x80;
            buffer[1] = static_cast<uint8_t>(i);
            return buffer + 2;
        }
    }
    CArray* mediumNames = FetchList(kMediumNames);
    for (int i = 0; i < static_cast<int>(mediumNames->m_extent); ++i) {
        if (mediumNames->FetchPart(i) == name) {
            buffer[0] = type | 0x40;
            return StoreWord16(buffer + 1, static_cast<uint16_t>(i));
        }
    }
    buffer[0] = type;
    return StoreWord32(buffer + 1, name);
}

// 0x004798F0
uint32_t EmitInt(uint32_t name, int value, uint8_t* buffer, uint32_t size)
{
    uint8_t type = QBVAL_INTEGER;
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
    case QBVAL_INTEGER:
        end = StoreWord32(end, static_cast<uint32_t>(value));
        break;
    case kSignedByte:
    case kUnsignedByte:
        *end++ = static_cast<uint8_t>(value);
        break;
    case kSignedShort:
    case kUnsignedShort:
        end = StoreWord16(end, static_cast<uint16_t>(value));
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
        call EmitInt
        add esp, 16
        ret
    }
}

// 0x00479A50
uint32_t EmitReal(uint8_t* buffer, uint32_t name, uint32_t size, float value)
{
    float magnitude = value < 0.0f ? -value : value;
    // (a NaN counts as big)
    if (magnitude < 0.0001) {
        if (size < 5)
            return 0;
        return Written(WriteNameAndType(buffer, kTagZeroF, name), buffer);
    }
    if (size < 9)
        return 0;
    return Written(StoreWord32(WriteNameAndType(buffer, QBVAL_FLOAT, name), FloatBits(value)), buffer);
}

// The buffer in ECX, the name in EDX, the size in ESI; the value on the stack (the caller pops it).
__declspec(naked) void WriteFloatStub()
{
    __asm {
        push dword ptr [esp + 4]
        push esi
        push edx
        push ecx
        call EmitReal
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
    return WriteStringOfType(buffer, string, name, size, QBVAL_STRING);
}

// 0x00479B20
uint32_t WriteLocalString(uint8_t* buffer, const char* string, uint32_t name, uint32_t size)
{
    return WriteStringOfType(buffer, string, name, size, QBVAL_INLINE_TEXT);
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
    uint8_t type = array->m_kindTag;
    uint32_t count = array->m_extent;
    buffer[0] = type;
    uint8_t* p = StoreWord16(buffer + 1, static_cast<uint16_t>(count));
    uint32_t remaining = size - 3;
    switch (type) {
    case QBVAL_INTEGER:
    case QBVAL_FLOAT:
    case QBVAL_NAME: {
        uint32_t bytes = count * 4;
        if (bytes > remaining)
            return 0;
        const uint32_t* elements = array->FindArrayValue();
        for (uint32_t i = 0; i < count; ++i)
            p = StoreWord32(p, elements[i]);
        remaining -= bytes;
        break;
    }
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT: {
        const uint32_t* elements = array->FindArrayValue();
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
    case QBVAL_PAIR: {
        uint32_t bytes = count * 8;
        if (bytes > remaining)
            return 0;
        auto** pairs = reinterpret_cast<CPair**>(array->FindArrayValue());
        for (uint32_t i = 0; i < count; ++i) {
            p = StoreWord32(p, FloatBits(pairs[i]->x));
            p = StoreWord32(p, FloatBits(pairs[i]->y));
        }
        remaining -= bytes;
        break;
    }
    case QBVAL_VECTOR: {
        uint32_t bytes = count * 12;
        if (bytes > remaining)
            return 0;
        auto** vectors = reinterpret_cast<CVector**>(array->FindArrayValue());
        for (uint32_t i = 0; i < count; ++i) {
            p = StoreWord32(p, FloatBits(vectors[i]->x));
            p = StoreWord32(p, FloatBits(vectors[i]->y));
            p = StoreWord32(p, FloatBits(vectors[i]->z));
        }
        remaining -= bytes;
        break;
    }
    case QBVAL_STRUCT: {
        auto** structures = reinterpret_cast<CStruct**>(array->FindArrayValue());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t written = EmitIntoBuffer(structures[i], p, remaining, assert, nullptr);
            if (written == 0)
                return 0;
            remaining -= written;
            p += written;
        }
        break;
    }
    case QBVAL_ARRAY: {
        auto** arrays = reinterpret_cast<CArray**>(array->FindArrayValue());
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
uint32_t EmitIntoBuffer(CStruct* structure, uint8_t* buffer, uint32_t size, int assert, void (*callback)())
{
    uint32_t remaining = size;
    uint8_t* p = buffer;
    if (structure) {
        for (CComponent* component = structure->StepToEntry(nullptr); component;
             component = structure->StepToEntry(component)) {
            if (callback)
                callback();
            uint32_t written;
            switch (component->KindTag()) {
            case QBVAL_INTEGER:
                written = EmitInt(component->name, component->integer, p, remaining);
                break;
            case QBVAL_FLOAT:
                written = EmitReal(p, component->name, remaining, component->floatValue);
                break;
            case QBVAL_STRING:
                written = WriteString(p, component->string, component->name, remaining);
                break;
            case QBVAL_INLINE_TEXT:
                written = WriteLocalString(p, component->string, component->name, remaining);
                break;
            case QBVAL_PAIR: {
                if (remaining < 13)
                    return 0;
                uint8_t* end = WriteNameAndType(p, QBVAL_PAIR, component->name);
                end = StoreWord32(end, FloatBits(component->pair->x));
                end = StoreWord32(end, FloatBits(component->pair->y));
                written = Written(end, p);
                break;
            }
            case QBVAL_VECTOR: {
                if (remaining < 17)
                    return 0;
                uint8_t* end = WriteNameAndType(p, QBVAL_VECTOR, component->name);
                end = StoreWord32(end, FloatBits(component->vector->x));
                end = StoreWord32(end, FloatBits(component->vector->y));
                end = StoreWord32(end, FloatBits(component->vector->z));
                written = Written(end, p);
                break;
            }
            case QBVAL_SCRIPT_BODY: {
                // (a script can't be sent: a placeholder name instead)
                if (remaining < 9)
                    return 0;
                uint8_t* end = WriteNameAndType(p, QBVAL_NAME, component->name);
                written = Written(StoreWord32(end, kScriptPlaceholder), p);
                break;
            }
            case QBVAL_STRUCT: {
                if (remaining < 5)
                    return 0;
                uint8_t* end = WriteNameAndType(p, QBVAL_STRUCT, component->name);
                uint32_t header = Written(end, p);
                uint32_t inner = EmitIntoBuffer(component->structure, end, remaining - header, assert, nullptr);
                if (inner == 0)
                    return 0;
                written = inner + header;
                break;
            }
            case QBVAL_ARRAY: {
                if (remaining < 5)
                    return 0;
                uint8_t* end = WriteNameAndType(p, QBVAL_ARRAY, component->name);
                uint32_t header = Written(end, p);
                uint32_t inner = WriteArrayToBuffer(component->array, end, remaining - header, assert);
                if (inner == 0)
                    return 0;
                written = inner + header;
                break;
            }
            case QBVAL_NAME: {
                if (remaining < 9)
                    return 0;
                uint8_t* end = WriteNameAndType(p, QBVAL_NAME, component->name);
                written = Written(StoreWord32(end, component->checksum), p);
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
uint32_t MeasureBufferBytes(CStruct* structure, uint32_t maxSize)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    auto* scratch = static_cast<uint8_t*>(MemSys::AllocBlock(maxSize));
    uint32_t size = EmitIntoBuffer(structure, scratch, maxSize, 1, nullptr);
    MemSys::FreeMalloc(scratch);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return size;
}

// 0x0047A0C0
const uint8_t* DecodeFromBytes(CStruct* structure, const uint8_t* buffer)
{
    structure->Clear();
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_scriptHeap);
    const uint8_t* p = buffer;
    for (uint8_t type = *p++; type != 0; type = *p++) {
        uint32_t name;
        if (type & 0x80) {
            CArray* names = FetchList(kShortNames);
            name = names->FetchPart(*p);
            p += 1;
        } else if (type & 0x40) {
            CArray* names = FetchList(kMediumNames);
            name = names->FetchPart(FetchWord16(p));
            p += 2;
        } else {
            name = FetchWord32(p);
            p += 4;
        }
        switch (type & 0x3F) {
        case QBVAL_INTEGER:
            structure->PutInt(name, static_cast<int>(FetchWord32(p)));
            p += 4;
            break;
        case QBVAL_FLOAT:
            structure->PushFloat(name, BitsFloat(FetchWord32(p)));
            p += 4;
            break;
        case QBVAL_STRING:
            structure->AppendWord(name, reinterpret_cast<const char*>(p));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
            break;
        case QBVAL_INLINE_TEXT:
            structure->AppendInlineText(name, reinterpret_cast<const char*>(p));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
            break;
        case QBVAL_PAIR: {
            uint32_t x = FetchWord32(p);
            uint32_t y = FetchWord32(p + 4);
            p += 8;
            structure->InsertPair(name, BitsFloat(x), BitsFloat(y));
            break;
        }
        case QBVAL_VECTOR: {
            uint32_t x = FetchWord32(p);
            uint32_t y = FetchWord32(p + 4);
            uint32_t z = FetchWord32(p + 8);
            p += 12;
            structure->PutVec3(name, BitsFloat(x), BitsFloat(y), BitsFloat(z));
            break;
        }
        case QBVAL_STRUCT: {
            CStruct* inner = SpawnParamBag();
            if (inner)
                inner->PrepareAll();
            p = DecodeFromBytes(inner, p);
            structure->AppendStructRef(name, inner);
            break;
        }
        case QBVAL_ARRAY: {
            CArray* inner = AllocScriptArray();
            if (inner)
                inner->PrepareAll();
            p = ReadArrayFromBuffer(inner, p);
            structure->AppendArrayRef(name, inner);
            break;
        }
        case QBVAL_NAME:
            structure->PutNameKey(name, FetchWord32(p));
            p += 4;
            break;
        case kSignedByte:
            structure->PutInt(name, static_cast<int8_t>(*p));
            p += 1;
            break;
        case kSignedShort:
            structure->PutInt(name, static_cast<int16_t>(FetchWord16(p)));
            p += 2;
            break;
        case kUnsignedByte:
            structure->PutInt(name, *p);
            p += 1;
            break;
        case kUnsignedShort:
            structure->PutInt(name, FetchWord16(p));
            p += 2;
            break;
        case kZeroInteger:
            structure->PutInt(name, 0);
            break;
        case kTagZeroF:
            structure->PushFloat(name, 0.0f);
            break;
        default:
            break; // (no value: just the name was read)
        }
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
    return p;
}

// 0x0047A440
const uint8_t* ReadArrayFromBuffer(CArray* array, const uint8_t* buffer)
{
    ReleaseArrayStorage(array);
    uint8_t type = buffer[0];
    uint32_t count = buffer[1] | static_cast<uint32_t>(buffer[2]) << 8;
    const uint8_t* p = buffer + 3;
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_scriptHeap);
    array->ConfigureSizeKind(count, static_cast<EQbValueKind>(type));
    MemSys::Hub::Pull()->LeaveHeapScope();
    switch (type) {
    case QBVAL_INTEGER:
    case QBVAL_NAME:
        for (uint32_t i = 0; i < count; ++i, p += 4)
            array->SetElement(i, FetchWord32(p));
        break;
    case QBVAL_FLOAT:
        for (uint32_t i = 0; i < count; ++i, p += 4)
            array->ChangeFloat(i, BitsFloat(FetchWord32(p)));
        break;
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT:
        for (uint32_t i = 0; i < count; ++i) {
            array->SetElement(i, reinterpret_cast<uint32_t>(SpawnText(reinterpret_cast<const char*>(p))));
            p += std::strlen(reinterpret_cast<const char*>(p)) + 1;
        }
        break;
    case QBVAL_PAIR:
        for (uint32_t i = 0; i < count; ++i) {
            CPair* pair = NewPair();
            if (pair)
                pair->PrepareAll();
            pair->x = BitsFloat(FetchWord32(p));
            pair->y = BitsFloat(FetchWord32(p + 4));
            p += 8;
            array->SetElement(i, reinterpret_cast<uint32_t>(pair));
        }
        break;
    case QBVAL_VECTOR:
        for (uint32_t i = 0; i < count; ++i) {
            CVector* vector = AllocScriptVector();
            if (vector)
                vector->PrepareAll();
            vector->x = BitsFloat(FetchWord32(p));
            vector->y = BitsFloat(FetchWord32(p + 4));
            vector->z = BitsFloat(FetchWord32(p + 8));
            p += 12;
            array->SetElement(i, reinterpret_cast<uint32_t>(vector));
        }
        break;
    case QBVAL_STRUCT:
        for (uint32_t i = 0; i < count; ++i) {
            CStruct* inner = SpawnParamBag();
            if (inner)
                inner->PrepareAll();
            p = DecodeFromBytes(inner, p);
            array->SetElement(i, reinterpret_cast<uint32_t>(inner));
        }
        break;
    case QBVAL_ARRAY:
        for (uint32_t i = 0; i < count; ++i) {
            CArray* inner = AllocScriptArray();
            if (inner)
                inner->PrepareAll();
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
