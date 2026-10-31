#include "game/script/struct.h"
#include "game/script/cscript.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"

namespace Scripts {

int g_structCopyPeak{18000};
MemSys::PoolPair g_arrayPools;
MemSys::PoolPair g_componentPools;
bool g_usePermanentStrings;
char* g_permanentStringsStart;
char* g_permanentStringsNext;
uint32_t g_permanentStringsSize;
uint32_t g_permanentStringCount;
int g_structCopyCount;
CSymbolTableEntry** g_symbolTable;
MemSys::PoolPair g_symbolPools;
CSymbolTableEntry* g_iteratorEntry;
uint32_t g_iteratorBucket;
MemSys::PoolPair g_pairPools;
MemSys::PoolPair g_vectorPools;

namespace {

CSymbolTableEntry** SymbolTable() { return g_symbolTable; }
constexpr uint32_t kSymbolTableMask = 0xFFF;

} // namespace

// 0x00478CC0
CSymbolTableEntry* FindSymbol(uint32_t name)
{
    CSymbolTableEntry* entry = SymbolTable()[name & kSymbolTableMask];
    while (entry && entry->name != name)
        entry = entry->next;
    return entry;
}

namespace {

// The global structure an unnamed name component includes, or null.
CStruct* IncludedStructure(const CComponent* component)
{
    if (component->name != 0 || component->KindTag() != QBVAL_NAME)
        return nullptr;
    CSymbolTableEntry* symbol = ResolveSymbol(component->checksum);
    return symbol && symbol->type == QBVAL_STRUCT ? symbol->structure : nullptr;
}

} // namespace

// 0x00478CF0
CSymbolTableEntry* ResolveSymbol(uint32_t name)
{
    CSymbolTableEntry* entry = FindSymbol(name);
    if (!entry)
        return nullptr;
    while (entry->type == QBVAL_NAME) {
        CSymbolTableEntry* target = FindSymbol(entry->checksum);
        if (!target)
            return entry;
        entry = target;
    }
    return entry;
}

// 0x00476410
CStruct* CStruct::PrepareAll()
{
    m_head = nullptr;
    m_unknown1 = 0;
    return this;
}

// 0x00476420
bool CStruct::IsVacant()
{
    return m_head == nullptr;
}

// 0x00476490
CComponent* CStruct::StepToEntry(CComponent* component)
{
    return component ? component->next : m_head;
}

// 0x00476430
CComponent* CStruct::FindLastComponent(uint32_t name)
{
    CComponent* found = nullptr;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name == name) {
            found = component;
        } else if (CStruct* included = IncludedStructure(component)) {
            if (CComponent* inner = included->FindLastComponent(name))
                found = inner;
        }
    }
    return found;
}

// 0x00476A90
CComponent* CStruct::FindComponent(uint32_t name)
{
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name == name)
            return component;
        if (component->name == 0 && component->KindTag() == QBVAL_NAME) {
            if (component->checksum == name)
                return component;
            CSymbolTableEntry* symbol = ResolveSymbol(component->checksum);
            if (symbol && symbol->type == QBVAL_STRUCT) {
                if (CComponent* inner = symbol->structure->FindComponent(name))
                    return inner;
            }
        }
    }
    return nullptr;
}

// 0x004764B0 (the last match wins)
bool CStruct::Hunt(uint32_t name, uint32_t type, void* out)
{
    auto* value = static_cast<uint32_t*>(out);
    bool found = false;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name == name) {
            EQbValueKind componentType = component->KindTag();
            if (componentType == type) {
                *value = component->raw;
                found = true;
            } else if (componentType == QBVAL_INTEGER && type == QBVAL_FLOAT) {
                *static_cast<float*>(out) = static_cast<float>(component->integer);
                found = true;
            } else if (componentType == QBVAL_NAME) {
                // A name standing for a global value.
                CSymbolTableEntry* symbol = ResolveSymbol(component->checksum);
                if (symbol) {
                    if (symbol->type == type) {
                        *value = symbol->raw;
                        found = true;
                    } else if (symbol->type == QBVAL_INTEGER && type == QBVAL_FLOAT) {
                        *static_cast<float*>(out) = static_cast<float>(symbol->integer);
                        found = true;
                    }
                }
            }
        } else if (CStruct* included = IncludedStructure(component)) {
            uint32_t inner;
            if (included->Hunt(name, type, &inner)) {
                *value = inner;
                found = true;
            }
        }
    }
    return found;
}

// 0x00476AF0
bool CStruct::HasEntryNamed(uint32_t name)
{
    return FindComponent(name) != nullptr;
}

// 0x00476B10
bool CStruct::HasEntryNamed(const char* name)
{
    return FindComponent(Checksums::HashFromText(name)) != nullptr;
}

// 0x00476B40
bool CStruct::HasBareFlag(uint32_t flag)
{
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name != 0 || component->KindTag() != QBVAL_NAME)
            continue;
        if (component->checksum == flag)
            return true;
        CSymbolTableEntry* symbol = ResolveSymbol(component->checksum);
        if (symbol && symbol->type == QBVAL_STRUCT && symbol->structure->HasBareFlag(flag))
            return true;
    }
    return false;
}

// 0x00476BA0
bool CStruct::HasBareFlag(const char* flag)
{
    return HasBareFlag(Checksums::HashFromText(flag));
}

// 0x00476BC0
bool CStruct::RefTally(CStruct* other)
{
    if (other == this)
        return true;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->KindTag() == QBVAL_STRUCT && component->structure->RefTally(other))
            return true;
    }
    return false;
}

// 0x00476C10
bool CStruct::IsSubsetOf(CStruct* other)
{
    for (CComponent* mine = m_head; mine; mine = mine->next) {
        CComponent* theirs = other->m_head;
        while (theirs && !(mine->name == theirs->name && ComponentsEqual(mine, theirs)))
            theirs = theirs->next;
        if (!theirs)
            return false;
    }
    return true;
}

// --- the typed getters -------------------------------------------------------------------------------------

// 0x00476590 (strings and local strings)
bool CStruct::FetchText(uint32_t name, const char** out, int assert)
{
    const char* value;
    if (!Hunt(name, QBVAL_STRING, &value) && !Hunt(name, QBVAL_INLINE_TEXT, &value)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x004765F0
bool CStruct::FetchText(const char* name, const char** out, int assert)
{
    return FetchText(Checksums::HashFromText(name), out, assert);
}

// 0x00476630
bool CStruct::FetchIntValue(uint32_t name, int* out, int assert)
{
    int value;
    if (!Hunt(name, QBVAL_INTEGER, &value)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476680
bool CStruct::FetchIntValue(const char* name, int* out, int assert)
{
    return FetchIntValue(Checksums::HashFromText(name), out, assert);
}

// 0x004766D0
bool CStruct::FetchFloat(uint32_t name, float* out, int assert)
{
    float value;
    if (!Hunt(name, QBVAL_FLOAT, &value)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476720
bool CStruct::FetchFloat(const char* name, float* out, int assert)
{
    return FetchFloat(Checksums::HashFromText(name), out, assert);
}

// 0x00476770
bool CStruct::FetchPair(uint32_t name, float* x, float* y, int assert)
{
    CPair* pair;
    if (!Hunt(name, QBVAL_PAIR, &pair)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    *x = pair->x;
    *y = pair->y;
    return true;
}

// 0x00476DF0 (zero if missing)
bool CStruct::FetchPair(uint32_t name, CPair* out, bool assert)
{
    CPair* pair;
    if (Hunt(name, QBVAL_PAIR, &pair)) {
        out->x = pair->x;
        out->y = pair->y;
        return true;
    }
    if (assert)
        DumpToLog(this);
    out->x = 0.0f;
    out->y = 0.0f;
    return false;
}

// 0x004767C0
bool CStruct::ReadSubStruct(uint32_t name, CStruct** out, int assert)
{
    CStruct* value;
    if (!Hunt(name, QBVAL_STRUCT, &value)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476810
bool CStruct::ReadSubStruct(const char* name, CStruct** out, int assert)
{
    return ReadSubStruct(Checksums::HashFromText(name), out, assert);
}

// 0x00476860
bool CStruct::FetchList(uint32_t name, CArray** out, int assert)
{
    CArray* value;
    if (!Hunt(name, QBVAL_ARRAY, &value)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x004768B0
bool CStruct::FetchList(const char* name, CArray** out, int assert)
{
    return FetchList(Checksums::HashFromText(name), out, assert);
}

// 0x00476900
bool CStruct::FetchScript(uint32_t name, SParamScript* out, int assert)
{
    const uint8_t* code;
    if (!Hunt(name, QBVAL_SCRIPT_BODY, &code)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    out->m_ownNameHash = name;
    out->m_codeStream = code;
    return true;
}

// 0x00476950 (the last match wins; an unnamed name that includes a global structure isn't a value)
bool CStruct::ReadHashValue(uint32_t name, uint32_t* out, int assert)
{
    bool found = false;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name == name) {
            if (component->KindTag() == QBVAL_NAME) {
                CSymbolTableEntry* symbol = nullptr;
                if (name != 0 || !(symbol = ResolveSymbol(component->checksum)) ||
                    symbol->type != QBVAL_STRUCT) {
                    *out = component->checksum;
                    found = true;
                }
            }
        } else if (CStruct* included = IncludedStructure(component)) {
            if (included->ReadHashValue(name, out, 0))
                found = true;
        }
    }
    if (assert && !found)
        DumpToLog(this);
    return found;
}

// 0x00476A00
bool CStruct::ReadHashValue(const char* name, uint32_t* out, int assert)
{
    return ReadHashValue(Checksums::HashFromText(name), out, assert);
}

// 0x00476A30
bool CStruct::HashFromNameOrText(uint32_t name, uint32_t* out, int assert)
{
    if (ReadHashValue(name, out, assert))
        return true;
    const char* string = nullptr;
    if (!FetchText(name, &string, assert))
        return false;
    *out = Checksums::HashFromText(string);
    return true;
}

// 0x00476FB0
bool CStruct::FetchVec(uint32_t name, VectorValue* out, int assert)
{
    CVector* vector;
    if (!Hunt(name, QBVAL_VECTOR, &vector)) {
        if (assert)
            DumpToLog(this);
        return false;
    }
    out->x = vector->x;
    out->y = vector->y;
    out->z = vector->z;
    out->w = 1.0f;
    return true;
}

// 0x00477010
bool CStruct::FetchVec(const char* name, VectorValue* out, int assert)
{
    return FetchVec(Checksums::HashFromText(name), out, assert);
}

} // namespace Script

namespace {

using Scripts::CArray;
using Scripts::CStruct;
using Scripts::VectorValue;

// The overloads taking the assert flag as a bool (they pass it on as 0 or 1).
struct BoolAssertGetters {
    CStruct* Self() { return reinterpret_cast<CStruct*>(this); }
    bool FetchText(uint32_t name, const char** out, bool assert) { return Self()->FetchText(name, out, assert); }
    bool GetStringNamed(const char* name, const char** out, bool assert) { return Self()->FetchText(name, out, assert); }
    bool FetchIntValue(uint32_t name, int* out, bool assert) { return Self()->FetchIntValue(name, out, assert); }
    bool GetIntegerNamed(const char* name, int* out, bool assert) { return Self()->FetchIntValue(name, out, assert); }
    bool FetchFloat(uint32_t name, float* out, bool assert) { return Self()->FetchFloat(name, out, assert); }
    bool GetFloatNamed(const char* name, float* out, bool assert) { return Self()->FetchFloat(name, out, assert); }
    bool ReadSubStruct(uint32_t name, CStruct** out, bool assert) { return Self()->ReadSubStruct(name, out, assert); }
    bool ReadHashValue(uint32_t name, uint32_t* out, bool assert) { return Self()->ReadHashValue(name, out, assert); }
    bool GetChecksumNamed(const char* name, uint32_t* out, bool assert) { return Self()->ReadHashValue(name, out, assert); }
    bool FetchList(uint32_t name, CArray** out, bool assert) { return Self()->FetchList(name, out, assert); }
    bool GetArrayNamed(const char* name, CArray** out, bool assert) { return Self()->FetchList(name, out, assert); }
    bool FetchVec(uint32_t name, VectorValue* out, bool assert) { return Self()->FetchVec(name, out, assert); }
    bool GetVectorNamed(const char* name, VectorValue* out, bool assert) { return Self()->FetchVec(name, out, assert); }
};

template <typename R, typename... A>
using Method = R (CStruct::*)(A...);

} // namespace

// --- owning values: strings, pooled objects, cleaning up and copying ------------------------------------------

namespace Scripts {

// The CStruct pool pair (the current pool's index, then the two pools).
MemSys::PoolPair g_structPools;

// A permanent string table entry: the string's checksum and the string.
struct PermanentString {
    uint32_t checksum;
    char* string;
};
PermanentString* g_permanentStringTable;
namespace {

// The pool pairs of the script's value objects.
MemSys::PoolPair& ComponentPools() { return g_componentPools; }
MemSys::PoolPair& StructPools() { return g_structPools; }
MemSys::PoolPair& ArrayPools() { return g_arrayPools; }
MemSys::PoolPair& PairPools() { return g_pairPools; }
MemSys::PoolPair& VectorPools() { return g_vectorPools; }

// The permanent string cache (used while loading the game's scripts): one block, strings shared by checksum.
bool& UsePermanentStrings() { return g_usePermanentStrings; }
char*& PermanentStringsStart() { return g_permanentStringsStart; }
char*& PermanentStringsNext() { return g_permanentStringsNext; }
uint32_t& PermanentStringsSize() { return g_permanentStringsSize; }
PermanentString*& PermanentStringTable() { return g_permanentStringTable; }
uint32_t& PermanentStringCount() { return g_permanentStringCount; }

// 0x00476260 (the string in EAX: called only by SpawnText, so registered source-only)
char* CreatePermanentString(const char* string)
{
    int length = static_cast<int>(std::strlen(string));
    uint32_t checksum = Checksums::GenerateCRCFromBuffer(string, length);
    PermanentString* table = PermanentStringTable();
    for (uint32_t i = 0; i < PermanentStringCount(); ++i) {
        if (table[i].checksum == checksum)
            return table[i].string;
    }
    char* copy = PermanentStringsNext();
    std::memcpy(copy, string, static_cast<size_t>(length) + 1);
    PermanentStringsNext() = copy + length + 1;
    table[PermanentStringCount()] = {checksum, copy};
    ++PermanentStringCount();
    return copy;
}

void DeletePair(CPair* pair) { PairPools().Dealloc(pair); }
void DeleteVector(CVector* vector) { VectorPools().Dealloc(vector); }

void MergeStructure(CStruct* to, CStruct* from) { to->MergeStructure(from); }

} // namespace

// 0x004763A0
char* SpawnText(const char* string)
{
    if (UsePermanentStrings())
        return CreatePermanentString(string);
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap);
    size_t size = std::strlen(string) + 1;
    auto* copy = static_cast<char*>(MemSys::AllocBlock(static_cast<unsigned int>(size)));
    std::memcpy(copy, string, size);
    MemSys::LeaveHeapScope();
    return copy;
}

// 0x00476300
void BeginPermanentStrings() { UsePermanentStrings() = true; }

// 0x00476310
void EndPermanentStrings()
{
    UsePermanentStrings() = false;
    if (PermanentStringTable()) {
        MemSys::FreeMalloc(PermanentStringTable());
        PermanentStringTable() = nullptr;
    }
}

namespace {
char g_scriptStrings[128]; // (the strings SetScriptString / GetScriptString use, 128 bytes each, indexed from here)
char (&ScriptStringSlot(int index))[128] { return reinterpret_cast<char(*)[128]>(&g_scriptStrings)[index]; }
} // namespace

// 0x00476370
int SetScriptString(int index, const char* string)
{
#pragma warning(suppress : 4996) // (unbounded, and the string is the format: as in the original)
    return std::sprintf(ScriptStringSlot(index), string);
}

// 0x00476390
char* GetScriptString(int index) { return ScriptStringSlot(index); }

// 0x00476340 (strings in the permanent block are never freed)
void FreeString(const char* string)
{
    const char* start = PermanentStringsStart();
    if (string >= start && string < start + PermanentStringsSize())
        return;
    MemSys::FreeMalloc(const_cast<char*>(string));
}

// 0x00409030 / 0x00409070
CStruct* NewStruct() { return static_cast<CStruct*>(StructPools().Carve(sizeof(CStruct))); }
void DeleteStruct(CStruct* structure) { StructPools().Dealloc(structure); }
// 0x00408F90 / 0x00408FD0
CArray* NewArray() { return static_cast<CArray*>(ArrayPools().Carve(sizeof(CArray))); }
void DeleteArray(CArray* array) { ArrayPools().Dealloc(array); }
void FreeComponent(CComponent* component) { ComponentPools().Dealloc(component); }

// 0x0046CAA0
CArray* CArray::PrepareAll()
{
    m_kindTag = QBVAL_NONE;
    m_elements = nullptr;
    m_extent = 0;
    m_unknown0 = 0;
    return this;
}

// 0x0046CAB0
void CArray::Clear()
{
    if (m_extent == 1) {
        // (the single element is in place: nothing to free)
        m_extent = 0;
        m_kindTag = QBVAL_NONE;
        if (m_elements)
            m_elements = nullptr;
        return;
    }
    if (m_elements) {
        MemSys::FreeMalloc(m_elements);
        m_elements = nullptr;
    }
    m_kindTag = QBVAL_NONE;
    m_extent = 0;
}

// 0x0046CAF0
void CArray::ChangeSpecies(EQbValueKind type) { m_kindTag = type; }

// 0x0046CB00
uint32_t* CArray::FindArrayValue()
{
    return m_extent == 1 ? &m_single : m_elements;
}

// 0x00477140
void ReleaseArrayStorage(CArray* array)
{
    uint32_t count = array->m_extent;
    EQbValueKind type = array->m_kindTag;
    uint32_t* elements = array->FindArrayValue();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t& element = elements[i];
        if (!element)
            continue;
        switch (type) {
        case QBVAL_STRING:
        case QBVAL_INLINE_TEXT:
            FreeString(reinterpret_cast<const char*>(element));
            break;
        case QBVAL_PAIR:
            DeletePair(reinterpret_cast<CPair*>(element));
            break;
        case QBVAL_VECTOR:
            DeleteVector(reinterpret_cast<CVector*>(element));
            break;
        case QBVAL_STRUCT: {
            auto* structure = reinterpret_cast<CStruct*>(element);
            structure->Clear();
            DeleteStruct(structure);
            break;
        }
        case QBVAL_ARRAY: {
            auto* inner = reinterpret_cast<CArray*>(element);
            ReleaseArrayStorage(inner);
            inner->Clear();
            DeleteArray(inner);
            break;
        }
        default:
            continue; // (other elements own nothing, and are left as they are)
        }
        element = 0;
    }
    array->Clear();
}

// 0x004772A0
void ReleaseComponent(CComponent* component)
{
    switch (component->KindTag()) {
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT:
        if (component->string)
            FreeString(component->string);
        break;
    case QBVAL_PAIR:
        if (component->pair)
            DeletePair(component->pair);
        break;
    case QBVAL_VECTOR:
        if (component->vector)
            DeleteVector(component->vector);
        break;
    case QBVAL_SCRIPT_BODY:
        if (component->raw)
            MemSys::FreeMalloc(reinterpret_cast<void*>(component->raw));
        component->unknown2 = 0; // the script's size
        break;
    case QBVAL_STRUCT:
        if (CStruct* structure = component->structure) {
            structure->Clear();
            DeleteStruct(structure);
        }
        break;
    case QBVAL_ARRAY:
        if (CArray* array = component->array) {
            ReleaseArrayStorage(array);
            array->Clear();
            DeleteArray(array);
        }
        break;
    default:
        break;
    }
    component->raw = 0;
    component->typeAndFlag &= 1;
}

// 0x004773E0
void DuplicateComponent(CComponent* dest, CComponent* source, int reference)
{
    if (dest->raw)
        ReleaseComponent(dest);
    dest->typeAndFlag = static_cast<uint8_t>((source->typeAndFlag & ~1) | (dest->typeAndFlag & 1));
    dest->name = source->name;
    switch (source->KindTag()) {
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT:
        dest->string = SpawnText(source->string);
        break;
    case QBVAL_PAIR: {
        auto* pair = static_cast<CPair*>(PairPools().Carve(sizeof(CPair)));
        if (pair) {
            // (the constructor: only the first byte of the first word is cleared)
            pair->y = 0.0f;
            pair->x = 0.0f;
            reinterpret_cast<uint8_t*>(pair)[0] = 0;
        }
        dest->pair = pair;
        pair->x = source->pair->x;
        dest->pair->y = source->pair->y;
        break;
    }
    case QBVAL_VECTOR: {
        auto* vector = static_cast<CVector*>(VectorPools().Carve(sizeof(CVector)));
        if (vector) {
            vector->z = 0.0f;
            vector->y = 0.0f;
            vector->x = 0.0f;
            reinterpret_cast<uint8_t*>(vector)[0] = 0;
        }
        dest->vector = vector;
        vector->x = source->vector->x;
        dest->vector->y = source->vector->y;
        dest->vector->z = source->vector->z;
        break;
    }
    case QBVAL_SCRIPT_BODY: {
        // The compiled script, `unknown2` bytes.
        MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap);
        dest->raw = reinterpret_cast<uint32_t>(MemSys::AllocBlock(source->unknown2));
        dest->unknown2 = source->unknown2;
        MemSys::LeaveHeapScope();
        auto* from = reinterpret_cast<const uint8_t*>(source->raw);
        auto* to = reinterpret_cast<uint8_t*>(dest->raw);
        for (uint32_t i = 0; i < source->unknown2; ++i)
            to[i] = from[i];
        break;
    }
    case QBVAL_STRUCT: {
        if (reference) {
            dest->typeAndFlag = static_cast<uint8_t>((dest->typeAndFlag & 1) | (QBVAL_STRUCT_REF << 1));
            dest->structure = source->structure;
            break;
        }
        CStruct* structure = NewStruct();
        if (structure)
            structure->PrepareAll();
        dest->structure = structure;
        if (source->structure != structure) {
            structure->Clear();
            MergeStructure(structure, source->structure);
        }
        break;
    }
    case QBVAL_ARRAY: {
        if (!reference) {
            CArray* array = NewArray();
            if (array)
                array->PrepareAll();
            dest->array = array;
            DuplicateArray(array, source->array);
            break;
        }
        dest->typeAndFlag = static_cast<uint8_t>((dest->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAYPOINTER << 1));
        dest->raw = source->raw;
        break;
    }
    case QBVAL_INTEGER:
    case QBVAL_FLOAT:
    case QBVAL_NAME:
    case QBVAL_STRUCT_REF:
    case ESYMBOLTYPE_ARRAYPOINTER:
        dest->raw = source->raw;
        break;
    default:
        break;
    }
}

// 0x00477090
void CStruct::Clear()
{
    CComponent* component = m_head;
    while (component) {
        CComponent* next = component->next;
        ReleaseComponent(component);
        FreeComponent(component);
        component = next;
    }
    m_head = nullptr;
}

// 0x004776B0
void CStruct::EraseModule(uint32_t name)
{
    CComponent* previous = nullptr;
    CComponent* component = m_head;
    while (component) {
        CComponent* next = component->next;
        if (component->name == name) {
            if (previous)
                previous->next = next;
            else
                m_head = next;
            ReleaseComponent(component);
            FreeComponent(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

// 0x00477770
void CStruct::EraseModule(const char* name)
{
    EraseModule(Checksums::HashFromText(name));
}

// 0x00477790
void CStruct::RemoveComponentOfType(uint32_t name, uint8_t type)
{
    CComponent* previous = nullptr;
    CComponent* component = m_head;
    while (component) {
        CComponent* next = component->next;
        if (component->name == name && component->KindTag() == type) {
            if (previous)
                previous->next = next;
            else
                m_head = next;
            ReleaseComponent(component);
            FreeComponent(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

} // namespace Script

// --- adding components ---------------------------------------------------------------------------------------

namespace Scripts {

// 0x0046D140 (the constructor: the second byte is left as it was)
CComponent* ConstructComponent(CComponent* component)
{
    component->name = 0;
    component->unknown2 = 0;
    component->raw = 0;
    component->next = nullptr;
    component->typeAndFlag = 0;
    return component;
}

namespace {

CComponent* NewTypedComponent(uint32_t name, EQbValueKind type)
{
    CComponent* component = NewComponent();
    component->name = name;
    component->typeAndFlag = static_cast<uint8_t>((component->typeAndFlag & 1) | (type << 1));
    return component;
}

} // namespace

// 0x0044C130 (+ the constructor, as the callers do)
CComponent* NewComponent()
{
    void* memory = ComponentPools().Carve(sizeof(CComponent));
    return memory ? ConstructComponent(static_cast<CComponent*>(memory)) : nullptr;
}

// 0x00477860
void CStruct::RemoveFlag(uint32_t flag)
{
    CComponent* previous = nullptr;
    CComponent* component = m_head;
    while (component) {
        CComponent* next = component->next;
        if (component->name == 0 && component->raw == flag) {
            if (previous)
                previous->next = next;
            else
                m_head = next;
            ReleaseComponent(component);
            FreeComponent(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

// 0x00477920
void CStruct::AttachModule(CComponent* added)
{
    CComponent* last = nullptr;
    CComponent* component = m_head;
    while (component) {
        bool replace = false;
        if (component->name == added->name) {
            EQbValueKind mine = component->KindTag();
            EQbValueKind theirs = added->KindTag();
            if (mine == theirs)
                replace = !(mine == QBVAL_NAME && component->name == 0) || component->raw == added->raw;
            else
                replace = (mine == QBVAL_FLOAT && theirs == QBVAL_INTEGER) ||
                          (mine == QBVAL_INTEGER && theirs == QBVAL_FLOAT);
        }
        if (replace) {
            CComponent* next = component->next;
            if (last)
                last->next = next;
            else
                m_head = next;
            ReleaseComponent(component);
            FreeComponent(component);
            component = next;
        } else {
            last = component;
            component = component->next;
        }
    }
    if (last)
        last->next = added;
    else
        m_head = added;
    added->next = nullptr;
}

// 0x004779D0
void CStruct::AppendWord(uint32_t name, const char* value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_STRING);
    component->string = SpawnText(value);
    AttachModule(component);
}

// 0x00477A90
void CStruct::AppendWord(const char* name, const char* value)
{
    AppendWord(Checksums::HashFromText(name), value);
}

// 0x00477AC0
void CStruct::AppendInlineText(uint32_t name, const char* value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_INLINE_TEXT);
    component->string = SpawnText(value);
    AttachModule(component);
}

// 0x00478390
void CStruct::AppendWord(uint32_t name, uint32_t type, const char* value)
{
    if (type == QBVAL_STRING)
        AppendWord(name, value);
    else
        AppendInlineText(name, value);
}

// 0x00477B80
void CStruct::PutInt(uint32_t name, int value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_INTEGER);
    component->integer = value;
    AttachModule(component);
}

// 0x00477C30
void CStruct::PutInt(const char* name, int value)
{
    PutInt(Checksums::HashFromText(name), value);
}

// 0x00477C60
void CStruct::PushFloat(uint32_t name, float value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_FLOAT);
    component->floatValue = value;
    AttachModule(component);
}

// 0x00477D10
void CStruct::PushFloat(const char* name, float value)
{
    PushFloat(Checksums::HashFromText(name), value);
}

// 0x00477D40
void CStruct::PutNameKey(uint32_t name, uint32_t value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_NAME);
    component->checksum = value;
    AttachModule(component);
}

// 0x00477DF0
void CStruct::PutNameKey(const char* name, uint32_t value)
{
    PutNameKey(Checksums::HashFromText(name), value);
}

// 0x00477E20
void CStruct::PutVec3(uint32_t name, float x, float y, float z)
{
    CComponent* component = NewTypedComponent(name, QBVAL_VECTOR);
    auto* vector = static_cast<CVector*>(VectorPools().Carve(sizeof(CVector)));
    if (vector) {
        vector->z = 0.0f;
        vector->y = 0.0f;
        vector->x = 0.0f;
        reinterpret_cast<uint8_t*>(vector)[0] = 0;
    }
    component->vector = vector;
    vector->x = x;
    component->vector->y = y;
    component->vector->z = z;
    AttachModule(component);
}

// 0x00477F40
void CStruct::PutVec3(const char* name, float x, float y, float z)
{
    PutVec3(Checksums::HashFromText(name), x, y, z);
}

// 0x00477F70
void CStruct::PutVec3(uint32_t name, VectorValue value)
{
    PutVec3(name, value.x, value.y, value.z);
}

// 0x00477F90
void CStruct::PutVec3(const char* name, VectorValue value)
{
    PutVec3(Checksums::HashFromText(name), value.x, value.y, value.z);
}

// 0x00477FC0
void CStruct::InsertPair(uint32_t name, float x, float y)
{
    CComponent* component = NewTypedComponent(name, QBVAL_PAIR);
    auto* pair = static_cast<CPair*>(PairPools().Carve(sizeof(CPair)));
    if (pair) {
        pair->y = 0.0f;
        pair->x = 0.0f;
        reinterpret_cast<uint8_t*>(pair)[0] = 0;
    }
    component->pair = pair;
    pair->x = x;
    component->pair->y = y;
    AttachModule(component);
}

// 0x004780D0
void CStruct::AppendArrayRef(uint32_t name, CArray* value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_ARRAY);
    component->array = value;
    AttachModule(component);
}

// 0x00478180
void CStruct::AppendArrayRef(const char* name, CArray* value)
{
    AppendArrayRef(Checksums::HashFromText(name), value);
}

// 0x004781B0
void CStruct::AppendStructRef(uint32_t name, CStruct* value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_STRUCT);
    component->structure = value;
    AttachModule(component);
}

// 0x00478260
void CStruct::AppendStructRef(const char* name, CStruct* value)
{
    AppendStructRef(Checksums::HashFromText(name), value);
}

// 0x00478670
void CStruct::PutSubStruct(uint32_t name, CStruct* value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_STRUCT);
    CStruct* copy = NewStruct();
    if (copy)
        copy->PrepareAll();
    component->structure = copy;
    if (value != copy) {
        copy->Clear();
        copy->MergeStructure(value);
    }
    AttachModule(component);
}

// 0x00478770
void CStruct::PutSubStruct(const char* name, CStruct* value)
{
    PutSubStruct(Checksums::HashFromText(name), value);
}

// 0x00478290
void CStruct::AppendScript(uint32_t name, const uint8_t* code, uint32_t size)
{
    CComponent* component = NewTypedComponent(name, QBVAL_SCRIPT_BODY);
    component->unknown2 = static_cast<uint16_t>(size);
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap);
    auto* copy = static_cast<uint8_t*>(MemSys::AllocBlock(size));
    MemSys::LeaveHeapScope();
    for (uint32_t i = 0; i < size; ++i)
        copy[i] = code[i];
    component->raw = reinterpret_cast<uint32_t>(copy);
    AttachModule(component);
}

// 0x004783C0
void CStruct::AddComponentOfType(uint32_t name, uint32_t type, uint32_t value)
{
    switch (type) {
    case QBVAL_INTEGER:
        PutInt(name, static_cast<int>(value));
        break;
    case QBVAL_FLOAT: {
        float number;
        std::memcpy(&number, &value, 4);
        PushFloat(name, number);
        break;
    }
    case 11:
        AppendStructRef(name, reinterpret_cast<CStruct*>(value));
        break;
    case QBVAL_ARRAY:
        AppendArrayRef(name, reinterpret_cast<CArray*>(value));
        break;
    case QBVAL_NAME:
        PutNameKey(name, value);
        break;
    default:
        break;
    }
}

// 0x00478480
CStruct* CStruct::MergeStructure(CStruct* source)
{
    for (CComponent* component = source->m_head; component; component = component->next) {
        CComponent* copy = NewComponent();
        DuplicateComponent(copy, component, 0);
        AttachModule(copy);
    }
    return this;
}

// 0x00478540
void CStruct::AppendStructureIfAny(CStruct* source)
{
    if (source)
        MergeStructure(source);
}

// 0x00478650
CStruct* CStruct::Assign(CStruct* source)
{
    if (source != this) {
        Clear();
        MergeStructure(source);
    }
    return this;
}

// 0x00478560
void CStruct::InlineInto(CStruct* dest, int depth)
{
    for (CComponent* component = m_head; component; component = component->next) {
        if (CStruct* included = IncludedStructure(component)) {
            included->InlineInto(dest, depth + 1);
            continue;
        }
        CComponent* copy = NewComponent();
        DuplicateComponent(copy, component, 0);
        dest->AttachModule(copy);
    }
}

} // namespace Script

namespace {

using Scripts::CArray;
using Scripts::CStruct;
using Scripts::VectorValue;
template <typename... A>
using AddMethod = void (CStruct::*)(A...);

// 0x0044C170: free a component (pooled)
void DeleteComponentAdapter(Scripts::CComponent* component) { Scripts::FreeComponent(component); }
// 0x0044C130: operator new (the size is always 16)
void* NewComponentMemory(unsigned int size) { return Scripts::g_componentPools.Carve(size); }
struct ComponentAdapter {
    Scripts::CComponent* MakeNew() { return Scripts::ConstructComponent(reinterpret_cast<Scripts::CComponent*>(this)); }
};

} // namespace

// --- arrays: sizing, elements, copying ------------------------------------------------------------------------

namespace Scripts {

namespace {

// Allocate `count` elements on the script heap.
uint32_t* AllocateElements(uint32_t count)
{
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap);
    auto* elements = static_cast<uint32_t*>(MemSys::AllocBlock(count * 4));
    MemSys::LeaveHeapScope();
    return elements;
}

// 0x006F3610 / 0x0067DA30: how many structures were made by copying, and the most there were (debug counters).
int& StructCopyCount() { return g_structCopyCount; }
int& StructCopyPeak() { return g_structCopyPeak; }

} // namespace

// 0x0046D410 / 0x0046D3D0 (operator new; the size is always the object's)
CPair* NewPair() { return static_cast<CPair*>(g_pairPools.Carve(sizeof(CPair))); }
CVector* NewVector()
{
    return static_cast<CVector*>(g_vectorPools.Carve(sizeof(CVector)));
}

// 0x0047AC00
CPair* CPair::PrepareAll()
{
    y = 0.0f;
    x = 0.0f;
    reinterpret_cast<uint8_t*>(this)[0] = 0;
    return this;
}

// 0x0047ABF0
CVector* CVector::PrepareAll()
{
    z = 0.0f;
    y = 0.0f;
    x = 0.0f;
    reinterpret_cast<uint8_t*>(this)[0] = 0;
    return this;
}

// 0x0046CC50
void CArray::ConfigureSizeKind(uint32_t size, EQbValueKind type)
{
    if (m_extent == 1) {
        // (the single element is in place: nothing to free)
        if (m_elements)
            m_elements = nullptr;
    } else if (m_elements) {
        MemSys::FreeMalloc(m_elements);
        m_elements = nullptr;
    }
    m_kindTag = type;
    m_extent = size;
    if (size == 0) {
        m_kindTag = QBVAL_NONE;
        return;
    }
    if (size == 1)
        return;
    m_elements = AllocateElements(m_extent);
    for (uint32_t i = 0; i < m_extent; ++i)
        m_elements[i] = 0;
}

// 0x0046CCF0
void CArray::Rescale(uint32_t size)
{
    if (size == m_extent)
        return;
    uint32_t* elements = AllocateElements(size);
    const uint32_t* from = FindArrayValue();
    for (uint32_t i = 0; i < m_extent; ++i)
        elements[i] = from[i];
    for (uint32_t i = m_extent; i < size; ++i)
        elements[i] = 0;
    if (m_extent > 1)
        MemSys::FreeMalloc(m_elements);
    // (growing to one element leaves the block's pointer in place of the single value, as the original does)
    m_elements = elements;
    m_extent = size;
}

// 0x0046CB10
void CArray::SetElement(uint32_t index, uint32_t value)
{
    if (m_extent == 1)
        m_single = value;
    else
        m_elements[index] = value;
}

// 0x0046CB40
void CArray::ChangeFloat(uint32_t index, float value)
{
    if (m_extent == 1)
        std::memcpy(&m_single, &value, 4);
    else
        std::memcpy(&m_elements[index], &value, 4);
}

// 0x0046CC30
uint32_t CArray::FetchPart(uint32_t index)
{
    return m_extent == 1 ? m_single : m_elements[index];
}

// 0x0046CBB0
const char* CArray::FetchText(uint32_t index)
{
    return reinterpret_cast<const char*>(FetchPart(index));
}

// 0x0046CB70
const char* CArray::FindInlineText(uint32_t index)
{
    return reinterpret_cast<const char*>(FetchPart(index));
}

// 0x0046CBF0
float CArray::FetchFloat(uint32_t index)
{
    uint32_t bits = FetchPart(index);
    if (m_kindTag == QBVAL_FLOAT) {
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    return static_cast<float>(static_cast<int32_t>(bits));
}

// 0x004787A0
void CopyArrayElements(uint32_t* dest, const uint32_t* source, uint32_t count, uint32_t type)
{
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t element = source[i];
        switch (type) {
        case QBVAL_INTEGER:
        case QBVAL_FLOAT:
        case QBVAL_NAME:
            dest[i] = element;
            break;
        case QBVAL_STRING:
        case QBVAL_INLINE_TEXT:
            if (element)
                dest[i] = reinterpret_cast<uint32_t>(SpawnText(reinterpret_cast<const char*>(element)));
            break;
        case QBVAL_PAIR:
            if (element) {
                auto* from = reinterpret_cast<const CPair*>(element);
                CPair* pair = NewPair();
                pair = pair ? pair->PrepareAll() : nullptr;
                pair->x = from->x;
                pair->y = from->y;
                dest[i] = reinterpret_cast<uint32_t>(pair);
            }
            break;
        case QBVAL_VECTOR:
            if (element) {
                auto* from = reinterpret_cast<const CVector*>(element);
                CVector* vector = NewVector();
                vector = vector ? vector->PrepareAll() : nullptr;
                vector->x = from->x;
                vector->y = from->y;
                vector->z = from->z;
                dest[i] = reinterpret_cast<uint32_t>(vector);
            }
            break;
        case QBVAL_STRUCT:
            if (element) {
                auto* from = reinterpret_cast<CStruct*>(element);
                CStruct* structure = NewStruct();
                if (structure)
                    structure->PrepareAll();
                if (from != structure) {
                    structure->Clear();
                    structure->MergeStructure(from);
                }
                dest[i] = reinterpret_cast<uint32_t>(structure);
            }
            break;
        case QBVAL_ARRAY:
            if (element) {
                CArray* array = NewArray();
                if (array)
                    array = array->PrepareAll();
                DuplicateArray(array, reinterpret_cast<CArray*>(element));
                dest[i] = reinterpret_cast<uint32_t>(array);
            }
            break;
        default:
            // Scripts, functions, unknowns: nothing is copied.
            return;
        }
    }
}

// 0x00478A60
void DuplicateArray(CArray* dest, CArray* source)
{
    ReleaseArrayStorage(dest);
    EQbValueKind type = source->m_kindTag;
    uint32_t size = source->m_extent;
    dest->ConfigureSizeKind(size, type);
    if (size)
        CopyArrayElements(dest->FindArrayValue(), source->FindArrayValue(), size, type);
}

// 0x00478AB0
CStruct* CStruct::InitCopy(CStruct* source)
{
    m_head = nullptr;
    m_unknown1 = 0;
    if (source != this) {
        Clear();
        MergeStructure(source);
    }
    if (++StructCopyCount() > StructCopyPeak())
        StructCopyPeak() = StructCopyCount();
    return this;
}

} // namespace Script

namespace {

using Scripts::CArray;

// 0x0046D410 / 0x0046D3D0 take the size (always the object's).
void* NewPairMemory(unsigned int) { return Scripts::NewPair(); }
void* NewVectorMemory(unsigned int) { return Scripts::NewVector(); }

} // namespace

// --- the symbol table and global values; comparing components; adding array copies --------------------------

namespace Scripts {

namespace {

// The symbol iterator's position: the current entry and its bucket.
CSymbolTableEntry*& IteratorEntry() { return g_iteratorEntry; }
uint32_t& IteratorBucket() { return g_iteratorBucket; }

// A global symbol's value as an integer / float (other types: 0).
int SymbolInteger(CSymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type == QBVAL_INTEGER)
            return entry->integer;
        if (entry->type == QBVAL_FLOAT)
            return static_cast<int>(entry->floatValue);
    }
    return 0;
}
float SymbolFloat(CSymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type == QBVAL_INTEGER)
            return static_cast<float>(entry->integer);
        if (entry->type == QBVAL_FLOAT)
            return entry->floatValue;
    }
    return 0.0f;
}
uint32_t SymbolValueOfType(CSymbolTableEntry* entry, EQbValueKind type)
{
    return entry && entry->type == type ? entry->raw : 0;
}

// The game's empty string (what the string getters give for missing symbols).
const char* EmptyString() { return ""; }

} // namespace

// 0x00478C90
void CreateSymbolTable()
{
    auto* table = static_cast<CSymbolTableEntry**>(MemSys::AllocBlock(0x1000 * sizeof(CSymbolTableEntry*)));
    g_symbolTable = table;
    for (uint32_t i = 0; i < 0x1000; ++i)
        table[i] = nullptr;
}

// 0x004790F0
CSymbolTableEntry* CSymbolTableEntry::PrepareAll()
{
    unknown0 &= 0xF8;
    name = 0;
    type = QBVAL_NONE;
    raw = 0;
    next = nullptr;
    unknown8 = 0;
    return this;
}

// 0x00479110
CSymbolTableEntry* CreateSymbol(uint32_t name)
{
    void* memory = g_symbolPools.Carve(sizeof(CSymbolTableEntry));
    CSymbolTableEntry* entry = memory ? static_cast<CSymbolTableEntry*>(memory)->PrepareAll() : nullptr;
    entry->name = name;
    entry->unknown0 |= 1;
    LinkSymbol(entry);
    return entry;
}

// 0x00478DC0
void LinkSymbol(CSymbolTableEntry* entry)
{
    CSymbolTableEntry*& head = SymbolTable()[entry->name & kSymbolTableMask];
    entry->next = head;
    head = entry;
}

// 0x00478D60
void UnlinkSymbol(CSymbolTableEntry* entry)
{
    CSymbolTableEntry** bucket = &SymbolTable()[entry->name & kSymbolTableMask];
    CSymbolTableEntry* previous = nullptr;
    for (CSymbolTableEntry* each = *bucket; each; previous = each, each = each->next) {
        if (each != entry)
            continue;
        if (previous)
            previous->next = entry->next;
        else
            *bucket = entry->next;
        entry->next = nullptr;
        return;
    }
}

// 0x00478E50
CSymbolTableEntry* GetNextSymbol(CSymbolTableEntry* previous)
{
    CSymbolTableEntry* entry;
    if (!previous) {
        IteratorBucket() = 0;
        entry = SymbolTable()[0];
    } else {
        entry = previous->next;
    }
    while (!entry) {
        if (++IteratorBucket() >= 0x1000) {
            IteratorBucket() = 0;
            IteratorEntry() = nullptr;
            return nullptr;
        }
        entry = SymbolTable()[IteratorBucket()];
    }
    IteratorEntry() = entry;
    return entry;
}

// 0x00478F60 / 0x004790C0 (an unused second argument)
int FetchIntValue(uint32_t name) { return SymbolInteger(ResolveSymbol(name)); }
int FetchIntValue(const char* name) { return FetchIntValue(Checksums::HashFromText(name)); }
// 0x00478F00 / 0x00478F30
float FetchFloat(uint32_t name) { return SymbolFloat(ResolveSymbol(name)); }
float FetchFloat(const char* name) { return FetchFloat(Checksums::HashFromText(name)); }
// 0x00478F90 / 0x00478FB0
uint32_t GetSymbolValue(uint32_t name)
{
    CSymbolTableEntry* entry = ResolveSymbol(name);
    return entry ? entry->raw : 0;
}
uint32_t GetSymbolValue(const char* name) { return GetSymbolValue(Checksums::HashFromText(name)); }
// 0x00478FD0 / 0x00478FF0 (any type: the raw value)
const char* FetchText(uint32_t name)
{
    CSymbolTableEntry* entry = ResolveSymbol(name);
    return entry ? reinterpret_cast<const char*>(entry->raw) : EmptyString();
}
const char* FetchText(const char* name) { return FetchText(Checksums::HashFromText(name)); }
// 0x00479020 / 0x00479040
CStruct* ReadSubStruct(uint32_t name)
{
    return reinterpret_cast<CStruct*>(SymbolValueOfType(ResolveSymbol(name), QBVAL_STRUCT));
}
CStruct* ReadSubStruct(const char* name) { return ReadSubStruct(Checksums::HashFromText(name)); }
// 0x00479070 / 0x00479090
CArray* FetchList(uint32_t name)
{
    return reinterpret_cast<CArray*>(SymbolValueOfType(ResolveSymbol(name), QBVAL_ARRAY));
}
CArray* FetchList(const char* name) { return FetchList(Checksums::HashFromText(name)); }

// 0x004791A0
bool ComponentsEqual(CComponent* a, CComponent* b)
{
    // A name standing for a global symbol counts as the symbol's value.
    auto resolve = [](CComponent* component, uint32_t* type, uint32_t* value) {
        *type = component->KindTag();
        *value = component->raw;
        if (*type == QBVAL_NAME) {
            if (CSymbolTableEntry* symbol = ResolveSymbol(*value)) {
                *value = symbol->raw;
                *type = symbol->type;
            }
        }
    };
    uint32_t typeA, valueA, typeB, valueB;
    resolve(a, &typeA, &valueA);
    resolve(b, &typeB, &valueB);
    auto as_float = [](uint32_t bits) {
        float value;
        std::memcpy(&value, &bits, 4);
        return static_cast<double>(value);
    };
    // (integers are compared with floats exactly, as the x87 does)
    auto as_integer = [](uint32_t bits) { return static_cast<double>(static_cast<int32_t>(bits)); };
    switch (typeA) {
    case QBVAL_INTEGER:
        if (typeB == QBVAL_FLOAT)
            return as_integer(valueA) == as_float(valueB);
        return typeB == QBVAL_INTEGER && valueA == valueB;
    case QBVAL_FLOAT:
        if (typeB == QBVAL_INTEGER)
            return as_float(valueA) == as_integer(valueB);
        return typeB == QBVAL_FLOAT && as_float(valueA) == as_float(valueB);
    case QBVAL_STRING:
    case QBVAL_INLINE_TEXT:
        return typeB == typeA &&
               std::strcmp(reinterpret_cast<const char*>(valueA), reinterpret_cast<const char*>(valueB)) == 0;
    case QBVAL_PAIR: {
        if (typeB != QBVAL_PAIR)
            return false;
        auto* pa = reinterpret_cast<const CPair*>(valueA);
        auto* pb = reinterpret_cast<const CPair*>(valueB);
        return pa->x == pb->x && pa->y == pb->y;
    }
    case QBVAL_VECTOR: {
        if (typeB != QBVAL_VECTOR)
            return false;
        auto* va = reinterpret_cast<const CVector*>(valueA);
        auto* vb = reinterpret_cast<const CVector*>(valueB);
        return va->x == vb->x && va->y == vb->y && va->z == vb->z;
    }
    case QBVAL_SCRIPT_BODY:
    case QBVAL_NAME:
        return typeB == typeA && valueA == valueB;
    case QBVAL_STRUCT:
    case QBVAL_ARRAY:
        // (different arrays are never equal: their comparison 0x0049F4B0 just returns false)
        return typeB == typeA && valueA == valueB;
    default:
        return false;
    }
}

// 0x00478B00
void CStruct::AppendArrayValue(uint32_t name, CArray* value)
{
    CComponent* component = NewTypedComponent(name, QBVAL_ARRAY);
    CArray* array = NewArray();
    if (array)
        array = array->PrepareAll();
    component->array = array;
    DuplicateArray(array, value);
    AttachModule(component);
}

// 0x00478C50
void CStruct::AppendArrayValue(const char* name, CArray* value)
{
    AppendArrayValue(Checksums::HashFromText(name), value);
}

} // namespace Script

namespace {

using Scripts::CStruct;

// 0x00479190: the type-name lookup was compiled out: it gives a "(Hey, don't use this...)" message.
const char* GetTypeName() { return "(Hey, don't use GetTypeName on the CD!)"; }

// ComponentsEqual is a member of the first component in the game.
struct ComponentEqualsAdapter {
    bool Equals(Scripts::CComponent* other)
    {
        return Scripts::ComponentsEqual(reinterpret_cast<Scripts::CComponent*>(this), other);
    }
};

template <typename R, typename... A>
using Routine = R (*)(A...);

} // namespace
