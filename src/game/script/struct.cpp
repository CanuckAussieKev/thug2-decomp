#include "game/script/struct.h"
#include "game/script/cscript.h"

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

namespace {

CSymbolTableEntry** SymbolTable() { return GameVariable<CSymbolTableEntry**>(0x006F3624); }
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
    if (component->name != 0 || component->Type() != ESYMBOLTYPE_NAME)
        return nullptr;
    CSymbolTableEntry* symbol = LookUpSymbol(component->checksum);
    return symbol && symbol->type == ESYMBOLTYPE_STRUCTURE ? symbol->structure : nullptr;
}

} // namespace

// 0x00478CF0
CSymbolTableEntry* LookUpSymbol(uint32_t name)
{
    CSymbolTableEntry* entry = FindSymbol(name);
    if (!entry)
        return nullptr;
    while (entry->type == ESYMBOLTYPE_NAME) {
        CSymbolTableEntry* target = FindSymbol(entry->checksum);
        if (!target)
            return entry;
        entry = target;
    }
    return entry;
}

// 0x00476410
CStruct* CStruct::Init()
{
    m_head = nullptr;
    m_unknown1 = 0;
    return this;
}

// 0x00476420
bool CStruct::IsEmpty()
{
    return m_head == nullptr;
}

// 0x00476490
CComponent* CStruct::GetNextComponent(CComponent* component)
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
        if (component->name == 0 && component->Type() == ESYMBOLTYPE_NAME) {
            if (component->checksum == name)
                return component;
            CSymbolTableEntry* symbol = LookUpSymbol(component->checksum);
            if (symbol && symbol->type == ESYMBOLTYPE_STRUCTURE) {
                if (CComponent* inner = symbol->structure->FindComponent(name))
                    return inner;
            }
        }
    }
    return nullptr;
}

// 0x004764B0 (the last match wins)
bool CStruct::Search(uint32_t name, uint32_t type, void* out)
{
    auto* value = static_cast<uint32_t*>(out);
    bool found = false;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name == name) {
            ESymbolType componentType = component->Type();
            if (componentType == type) {
                *value = component->raw;
                found = true;
            } else if (componentType == ESYMBOLTYPE_INTEGER && type == ESYMBOLTYPE_FLOAT) {
                *static_cast<float*>(out) = static_cast<float>(component->integer);
                found = true;
            } else if (componentType == ESYMBOLTYPE_NAME) {
                // A name standing for a global value.
                CSymbolTableEntry* symbol = LookUpSymbol(component->checksum);
                if (symbol) {
                    if (symbol->type == type) {
                        *value = symbol->raw;
                        found = true;
                    } else if (symbol->type == ESYMBOLTYPE_INTEGER && type == ESYMBOLTYPE_FLOAT) {
                        *static_cast<float*>(out) = static_cast<float>(symbol->integer);
                        found = true;
                    }
                }
            }
        } else if (CStruct* included = IncludedStructure(component)) {
            uint32_t inner;
            if (included->Search(name, type, &inner)) {
                *value = inner;
                found = true;
            }
        }
    }
    return found;
}

// 0x00476AF0
bool CStruct::ContainsComponentNamed(uint32_t name)
{
    return FindComponent(name) != nullptr;
}

// 0x00476B10
bool CStruct::ContainsComponentNamed(const char* name)
{
    return FindComponent(Crc::GenerateCRCFromString(name)) != nullptr;
}

// 0x00476B40
bool CStruct::ContainsFlag(uint32_t flag)
{
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name != 0 || component->Type() != ESYMBOLTYPE_NAME)
            continue;
        if (component->checksum == flag)
            return true;
        CSymbolTableEntry* symbol = LookUpSymbol(component->checksum);
        if (symbol && symbol->type == ESYMBOLTYPE_STRUCTURE && symbol->structure->ContainsFlag(flag))
            return true;
    }
    return false;
}

// 0x00476BA0
bool CStruct::ContainsFlag(const char* flag)
{
    return ContainsFlag(Crc::GenerateCRCFromString(flag));
}

// 0x00476BC0
bool CStruct::References(CStruct* other)
{
    if (other == this)
        return true;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->Type() == ESYMBOLTYPE_STRUCTURE && component->structure->References(other))
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
bool CStruct::GetString(uint32_t name, const char** out, int assert)
{
    const char* value;
    if (!Search(name, ESYMBOLTYPE_STRING, &value) && !Search(name, ESYMBOLTYPE_LOCALSTRING, &value)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x004765F0
bool CStruct::GetString(const char* name, const char** out, int assert)
{
    return GetString(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476630
bool CStruct::GetInteger(uint32_t name, int* out, int assert)
{
    int value;
    if (!Search(name, ESYMBOLTYPE_INTEGER, &value)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476680
bool CStruct::GetInteger(const char* name, int* out, int assert)
{
    return GetInteger(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x004766D0
bool CStruct::GetFloat(uint32_t name, float* out, int assert)
{
    float value;
    if (!Search(name, ESYMBOLTYPE_FLOAT, &value)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476720
bool CStruct::GetFloat(const char* name, float* out, int assert)
{
    return GetFloat(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476770
bool CStruct::GetPair(uint32_t name, float* x, float* y, int assert)
{
    CPair* pair;
    if (!Search(name, ESYMBOLTYPE_PAIR, &pair)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    *x = pair->x;
    *y = pair->y;
    return true;
}

// 0x00476DF0 (zero if missing)
bool CStruct::GetPair(uint32_t name, CPair* out, bool assert)
{
    CPair* pair;
    if (Search(name, ESYMBOLTYPE_PAIR, &pair)) {
        out->x = pair->x;
        out->y = pair->y;
        return true;
    }
    if (assert)
        PrintContents(this);
    out->x = 0.0f;
    out->y = 0.0f;
    return false;
}

// 0x004767C0
bool CStruct::GetStructure(uint32_t name, CStruct** out, int assert)
{
    CStruct* value;
    if (!Search(name, ESYMBOLTYPE_STRUCTURE, &value)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x00476810
bool CStruct::GetStructure(const char* name, CStruct** out, int assert)
{
    return GetStructure(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476860
bool CStruct::GetArray(uint32_t name, CArray** out, int assert)
{
    CArray* value;
    if (!Search(name, ESYMBOLTYPE_ARRAY, &value)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    *out = value;
    return true;
}

// 0x004768B0
bool CStruct::GetArray(const char* name, CArray** out, int assert)
{
    return GetArray(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476900
bool CStruct::GetScript(uint32_t name, SStructScript* out, int assert)
{
    const uint8_t* code;
    if (!Search(name, ESYMBOLTYPE_QSCRIPT, &code)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    out->mNameChecksum = name;
    out->mpScriptTokens = code;
    return true;
}

// 0x00476950 (the last match wins; an unnamed name that includes a global structure isn't a value)
bool CStruct::GetChecksum(uint32_t name, uint32_t* out, int assert)
{
    bool found = false;
    for (CComponent* component = m_head; component; component = component->next) {
        if (component->name == name) {
            if (component->Type() == ESYMBOLTYPE_NAME) {
                CSymbolTableEntry* symbol = nullptr;
                if (name != 0 || !(symbol = LookUpSymbol(component->checksum)) ||
                    symbol->type != ESYMBOLTYPE_STRUCTURE) {
                    *out = component->checksum;
                    found = true;
                }
            }
        } else if (CStruct* included = IncludedStructure(component)) {
            if (included->GetChecksum(name, out, 0))
                found = true;
        }
    }
    if (assert && !found)
        PrintContents(this);
    return found;
}

// 0x00476A00
bool CStruct::GetChecksum(const char* name, uint32_t* out, int assert)
{
    return GetChecksum(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476A30
bool CStruct::GetChecksumOrStringChecksum(uint32_t name, uint32_t* out, int assert)
{
    if (GetChecksum(name, out, assert))
        return true;
    const char* string = nullptr;
    if (!GetString(name, &string, assert))
        return false;
    *out = Crc::GenerateCRCFromString(string);
    return true;
}

// 0x00476FB0
bool CStruct::GetVector(uint32_t name, VectorValue* out, int assert)
{
    CVector* vector;
    if (!Search(name, ESYMBOLTYPE_VECTOR, &vector)) {
        if (assert)
            PrintContents(this);
        return false;
    }
    out->x = vector->x;
    out->y = vector->y;
    out->z = vector->z;
    out->w = 1.0f;
    return true;
}

// 0x00477010
bool CStruct::GetVector(const char* name, VectorValue* out, int assert)
{
    return GetVector(Crc::GenerateCRCFromString(name), out, assert);
}

} // namespace Script

namespace {

using Script::CArray;
using Script::CStruct;
using Script::VectorValue;

// The overloads taking the assert flag as a bool (they pass it on as 0 or 1).
struct BoolAssertGetters {
    CStruct* Self() { return reinterpret_cast<CStruct*>(this); }
    bool GetString(uint32_t name, const char** out, bool assert) { return Self()->GetString(name, out, assert); }
    bool GetStringNamed(const char* name, const char** out, bool assert) { return Self()->GetString(name, out, assert); }
    bool GetInteger(uint32_t name, int* out, bool assert) { return Self()->GetInteger(name, out, assert); }
    bool GetIntegerNamed(const char* name, int* out, bool assert) { return Self()->GetInteger(name, out, assert); }
    bool GetFloat(uint32_t name, float* out, bool assert) { return Self()->GetFloat(name, out, assert); }
    bool GetFloatNamed(const char* name, float* out, bool assert) { return Self()->GetFloat(name, out, assert); }
    bool GetStructure(uint32_t name, CStruct** out, bool assert) { return Self()->GetStructure(name, out, assert); }
    bool GetChecksum(uint32_t name, uint32_t* out, bool assert) { return Self()->GetChecksum(name, out, assert); }
    bool GetChecksumNamed(const char* name, uint32_t* out, bool assert) { return Self()->GetChecksum(name, out, assert); }
    bool GetArray(uint32_t name, CArray** out, bool assert) { return Self()->GetArray(name, out, assert); }
    bool GetArrayNamed(const char* name, CArray** out, bool assert) { return Self()->GetArray(name, out, assert); }
    bool GetVector(uint32_t name, VectorValue* out, bool assert) { return Self()->GetVector(name, out, assert); }
    bool GetVectorNamed(const char* name, VectorValue* out, bool assert) { return Self()->GetVector(name, out, assert); }
};

template <typename R, typename... A>
using Method = R (CStruct::*)(A...);

} // namespace

REPLACE(0x00478CF0, Script::LookUpSymbol);
REPLACE(0x00476410, &CStruct::Init);
REPLACE(0x00476420, &CStruct::IsEmpty);
REPLACE(0x00476490, &CStruct::GetNextComponent);
REPLACE(0x00476430, &CStruct::FindLastComponent);
REPLACE(0x00476A90, &CStruct::FindComponent);
REPLACE(0x004764B0, &CStruct::Search);
REPLACE(0x00476AF0, static_cast<Method<bool, uint32_t>>(&CStruct::ContainsComponentNamed));
REPLACE(0x00476B10, static_cast<Method<bool, const char*>>(&CStruct::ContainsComponentNamed));
REPLACE(0x00476B40, static_cast<Method<bool, uint32_t>>(&CStruct::ContainsFlag));
REPLACE(0x00476BA0, static_cast<Method<bool, const char*>>(&CStruct::ContainsFlag));
REPLACE(0x00476BC0, &CStruct::References);
REPLACE(0x00476C10, &CStruct::IsSubsetOf);
REPLACE(0x00476590, static_cast<Method<bool, uint32_t, const char**, int>>(&CStruct::GetString));
// (a jump to 0x00476590: a function of its own in the source whose body is just that call)
REPLACE_SOURCE_ONLY(0x00476620, static_cast<Method<bool, uint32_t, const char**, int>>(&CStruct::GetString));
REPLACE(0x004765F0, static_cast<Method<bool, const char*, const char**, int>>(&CStruct::GetString));
REPLACE(0x00476630, static_cast<Method<bool, uint32_t, int*, int>>(&CStruct::GetInteger));
REPLACE(0x00476680, static_cast<Method<bool, const char*, int*, int>>(&CStruct::GetInteger));
REPLACE(0x004766D0, static_cast<Method<bool, uint32_t, float*, int>>(&CStruct::GetFloat));
REPLACE(0x00476720, static_cast<Method<bool, const char*, float*, int>>(&CStruct::GetFloat));
REPLACE(0x00476770, static_cast<Method<bool, uint32_t, float*, float*, int>>(&CStruct::GetPair));
REPLACE(0x00476DF0, static_cast<Method<bool, uint32_t, Script::CPair*, bool>>(&CStruct::GetPair));
REPLACE(0x004767C0, static_cast<Method<bool, uint32_t, CStruct**, int>>(&CStruct::GetStructure));
REPLACE(0x00476810, static_cast<Method<bool, const char*, CStruct**, int>>(&CStruct::GetStructure));
REPLACE(0x00476860, static_cast<Method<bool, uint32_t, CArray**, int>>(&CStruct::GetArray));
REPLACE(0x004768B0, static_cast<Method<bool, const char*, CArray**, int>>(&CStruct::GetArray));
REPLACE(0x00476900, &CStruct::GetScript);
REPLACE(0x00476950, static_cast<Method<bool, uint32_t, uint32_t*, int>>(&CStruct::GetChecksum));
REPLACE(0x00476A00, static_cast<Method<bool, const char*, uint32_t*, int>>(&CStruct::GetChecksum));
REPLACE(0x00476A30, &CStruct::GetChecksumOrStringChecksum);
REPLACE(0x00476FB0, static_cast<Method<bool, uint32_t, VectorValue*, int>>(&CStruct::GetVector));
REPLACE(0x00477010, static_cast<Method<bool, const char*, VectorValue*, int>>(&CStruct::GetVector));
REPLACE(0x00476C60, &BoolAssertGetters::GetString);
REPLACE(0x00476C80, &BoolAssertGetters::GetStringNamed);
REPLACE(0x00476CB0, &BoolAssertGetters::GetInteger);
REPLACE(0x00476D00, &BoolAssertGetters::GetIntegerNamed);
REPLACE(0x00476D50, &BoolAssertGetters::GetFloat);
REPLACE(0x00476DA0, &BoolAssertGetters::GetFloatNamed);
REPLACE(0x00476E70, &BoolAssertGetters::GetStructure);
REPLACE(0x00476EC0, &BoolAssertGetters::GetChecksum);
REPLACE(0x00476EE0, &BoolAssertGetters::GetChecksumNamed);
REPLACE(0x00476F10, &BoolAssertGetters::GetArray);
REPLACE(0x00476F60, &BoolAssertGetters::GetArrayNamed);
REPLACE(0x00477040, &BoolAssertGetters::GetVector);
REPLACE(0x00477060, &BoolAssertGetters::GetVectorNamed);

// --- owning values: strings, pooled objects, cleaning up and copying ------------------------------------------

namespace Script {

namespace {

// The pool pairs of the script's value objects.
Mem::PoolPair& ComponentPools() { return GameVariable<Mem::PoolPair>(0x006F2338); }
Mem::PoolPair& StructPools() { return GameVariable<Mem::PoolPair>(0x006F3614); }
Mem::PoolPair& ArrayPools() { return GameVariable<Mem::PoolPair>(0x006F22B4); }
Mem::PoolPair& PairPools() { return GameVariable<Mem::PoolPair>(0x006F3644); }
Mem::PoolPair& VectorPools() { return GameVariable<Mem::PoolPair>(0x006F3654); }

// The permanent string cache (used while loading the game's scripts): one block, strings shared by checksum.
bool& UsePermanentStrings() { return GameVariable<bool>(0x006F3570); }
char*& PermanentStringsStart() { return GameVariable<char*>(0x006F3574); }
char*& PermanentStringsNext() { return GameVariable<char*>(0x006F3578); }
uint32_t& PermanentStringsSize() { return GameVariable<uint32_t>(0x006F357C); }
struct PermanentString {
    uint32_t checksum;
    char* string;
};
PermanentString*& PermanentStringTable() { return GameVariable<PermanentString*>(0x006F3580); }
uint32_t& PermanentStringCount() { return GameVariable<uint32_t>(0x006F3584); }

// 0x00476260 (the string in EAX: called only by CreateString, so registered source-only)
char* CreatePermanentString(const char* string)
{
    int length = static_cast<int>(std::strlen(string));
    uint32_t checksum = Crc::GenerateCRCFromBuffer(string, length);
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

void DeletePair(CPair* pair) { PairPools().Free(pair); }
void DeleteVector(CVector* vector) { VectorPools().Free(vector); }

void AppendStructure(CStruct* to, CStruct* from) { to->AppendStructure(from); }

} // namespace

// 0x004763A0
char* CreateString(const char* string)
{
    if (UsePermanentStrings())
        return CreatePermanentString(string);
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    size_t size = std::strlen(string) + 1;
    auto* copy = static_cast<char*>(Mem::Malloc(static_cast<unsigned int>(size)));
    std::memcpy(copy, string, size);
    Mem::PopContext();
    return copy;
}

// 0x00476300
void BeginPermanentStrings() { UsePermanentStrings() = true; }

// 0x00476310
void EndPermanentStrings()
{
    UsePermanentStrings() = false;
    if (PermanentStringTable()) {
        Mem::FreeMalloc(PermanentStringTable());
        PermanentStringTable() = nullptr;
    }
}

namespace {
char (&ScriptStringSlot(int index))[128] { return reinterpret_cast<char(*)[128]>(0x006F3590)[index]; }
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
    Mem::FreeMalloc(const_cast<char*>(string));
}

// 0x00409030 / 0x00409070
CStruct* NewStruct() { return static_cast<CStruct*>(StructPools().Allocate(sizeof(CStruct))); }
void DeleteStruct(CStruct* structure) { StructPools().Free(structure); }
// 0x00408F90 / 0x00408FD0
CArray* NewArray() { return static_cast<CArray*>(ArrayPools().Allocate(sizeof(CArray))); }
void DeleteArray(CArray* array) { ArrayPools().Free(array); }
void FreeComponent(CComponent* component) { ComponentPools().Free(component); }

// 0x0046CAA0
CArray* CArray::Init()
{
    m_type = ESYMBOLTYPE_NONE;
    m_elements = nullptr;
    m_size = 0;
    m_unknown0 = 0;
    return this;
}

// 0x0046CAB0
void CArray::Clear()
{
    if (m_size == 1) {
        // (the single element is in place: nothing to free)
        m_size = 0;
        m_type = ESYMBOLTYPE_NONE;
        if (m_elements)
            m_elements = nullptr;
        return;
    }
    if (m_elements) {
        Mem::FreeMalloc(m_elements);
        m_elements = nullptr;
    }
    m_type = ESYMBOLTYPE_NONE;
    m_size = 0;
}

// 0x0046CAF0
void CArray::SetType(ESymbolType type) { m_type = type; }

// 0x0046CB00
uint32_t* CArray::GetArrayPointer()
{
    return m_size == 1 ? &m_single : m_elements;
}

// 0x00477140
void CleanUpArray(CArray* array)
{
    uint32_t count = array->m_size;
    ESymbolType type = array->m_type;
    uint32_t* elements = array->GetArrayPointer();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t& element = elements[i];
        if (!element)
            continue;
        switch (type) {
        case ESYMBOLTYPE_STRING:
        case ESYMBOLTYPE_LOCALSTRING:
            FreeString(reinterpret_cast<const char*>(element));
            break;
        case ESYMBOLTYPE_PAIR:
            DeletePair(reinterpret_cast<CPair*>(element));
            break;
        case ESYMBOLTYPE_VECTOR:
            DeleteVector(reinterpret_cast<CVector*>(element));
            break;
        case ESYMBOLTYPE_STRUCTURE: {
            auto* structure = reinterpret_cast<CStruct*>(element);
            structure->Clear();
            DeleteStruct(structure);
            break;
        }
        case ESYMBOLTYPE_ARRAY: {
            auto* inner = reinterpret_cast<CArray*>(element);
            CleanUpArray(inner);
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
void CleanUpComponent(CComponent* component)
{
    switch (component->Type()) {
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        if (component->string)
            FreeString(component->string);
        break;
    case ESYMBOLTYPE_PAIR:
        if (component->pair)
            DeletePair(component->pair);
        break;
    case ESYMBOLTYPE_VECTOR:
        if (component->vector)
            DeleteVector(component->vector);
        break;
    case ESYMBOLTYPE_QSCRIPT:
        if (component->raw)
            Mem::FreeMalloc(reinterpret_cast<void*>(component->raw));
        component->unknown2 = 0; // the script's size
        break;
    case ESYMBOLTYPE_STRUCTURE:
        if (CStruct* structure = component->structure) {
            structure->Clear();
            DeleteStruct(structure);
        }
        break;
    case ESYMBOLTYPE_ARRAY:
        if (CArray* array = component->array) {
            CleanUpArray(array);
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
void CopyComponent(CComponent* dest, CComponent* source, int reference)
{
    if (dest->raw)
        CleanUpComponent(dest);
    dest->typeAndFlag = static_cast<uint8_t>((source->typeAndFlag & ~1) | (dest->typeAndFlag & 1));
    dest->name = source->name;
    switch (source->Type()) {
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        dest->string = CreateString(source->string);
        break;
    case ESYMBOLTYPE_PAIR: {
        auto* pair = static_cast<CPair*>(PairPools().Allocate(sizeof(CPair)));
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
    case ESYMBOLTYPE_VECTOR: {
        auto* vector = static_cast<CVector*>(VectorPools().Allocate(sizeof(CVector)));
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
    case ESYMBOLTYPE_QSCRIPT: {
        // The compiled script, `unknown2` bytes.
        Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
        dest->raw = reinterpret_cast<uint32_t>(Mem::Malloc(source->unknown2));
        dest->unknown2 = source->unknown2;
        Mem::PopContext();
        auto* from = reinterpret_cast<const uint8_t*>(source->raw);
        auto* to = reinterpret_cast<uint8_t*>(dest->raw);
        for (uint32_t i = 0; i < source->unknown2; ++i)
            to[i] = from[i];
        break;
    }
    case ESYMBOLTYPE_STRUCTURE: {
        if (reference) {
            dest->typeAndFlag = static_cast<uint8_t>((dest->typeAndFlag & 1) | (ESYMBOLTYPE_STRUCTUREPOINTER << 1));
            dest->structure = source->structure;
            break;
        }
        CStruct* structure = NewStruct();
        if (structure)
            structure->Init();
        dest->structure = structure;
        if (source->structure != structure) {
            structure->Clear();
            AppendStructure(structure, source->structure);
        }
        break;
    }
    case ESYMBOLTYPE_ARRAY: {
        if (!reference) {
            CArray* array = NewArray();
            if (array)
                array->Init();
            dest->array = array;
            CopyArray(array, source->array);
            break;
        }
        dest->typeAndFlag = static_cast<uint8_t>((dest->typeAndFlag & 1) | (ESYMBOLTYPE_ARRAYPOINTER << 1));
        dest->raw = source->raw;
        break;
    }
    case ESYMBOLTYPE_INTEGER:
    case ESYMBOLTYPE_FLOAT:
    case ESYMBOLTYPE_NAME:
    case ESYMBOLTYPE_STRUCTUREPOINTER:
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
        CleanUpComponent(component);
        FreeComponent(component);
        component = next;
    }
    m_head = nullptr;
}

// 0x004776B0
void CStruct::RemoveComponent(uint32_t name)
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
            CleanUpComponent(component);
            FreeComponent(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

// 0x00477770
void CStruct::RemoveComponent(const char* name)
{
    RemoveComponent(Crc::GenerateCRCFromString(name));
}

// 0x00477790
void CStruct::RemoveComponentOfType(uint32_t name, uint8_t type)
{
    CComponent* previous = nullptr;
    CComponent* component = m_head;
    while (component) {
        CComponent* next = component->next;
        if (component->name == name && component->Type() == type) {
            if (previous)
                previous->next = next;
            else
                m_head = next;
            CleanUpComponent(component);
            FreeComponent(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

} // namespace Script

REPLACE(0x004763A0, Script::CreateString);
REPLACE(0x00476340, Script::FreeString);
REPLACE_SOURCE_ONLY(0x00476260, Script::CreatePermanentString); // the string in EAX
REPLACE(0x00476300, Script::BeginPermanentStrings);
REPLACE(0x00476310, Script::EndPermanentStrings);
REPLACE(0x00476370, Script::SetScriptString);
REPLACE(0x00476390, Script::GetScriptString);
REPLACE(0x0046CAF0, &Script::CArray::SetType);
REPLACE(0x00409030, Script::NewStruct);
REPLACE(0x00409070, Script::DeleteStruct);
REPLACE(0x00408F90, Script::NewArray);
REPLACE(0x00408FD0, Script::DeleteArray);
REPLACE(0x0046CAA0, &Script::CArray::Init);
REPLACE(0x0046CAB0, &Script::CArray::Clear);
REPLACE(0x0046CB00, &Script::CArray::GetArrayPointer);
REPLACE(0x00477140, Script::CleanUpArray);
REPLACE(0x004772A0, Script::CleanUpComponent);
REPLACE(0x004773E0, Script::CopyComponent);
REPLACE(0x00477090, &Script::CStruct::Clear);
REPLACE_SOURCE_ONLY(0x00477130, &Script::CStruct::Clear); // a jump to 0x00477090
REPLACE(0x004776B0, static_cast<void (Script::CStruct::*)(uint32_t)>(&Script::CStruct::RemoveComponent));
REPLACE(0x00477770, static_cast<void (Script::CStruct::*)(const char*)>(&Script::CStruct::RemoveComponent));
REPLACE(0x00477790, &Script::CStruct::RemoveComponentOfType);

// --- adding components ---------------------------------------------------------------------------------------

namespace Script {

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

CComponent* NewTypedComponent(uint32_t name, ESymbolType type)
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
    void* memory = ComponentPools().Allocate(sizeof(CComponent));
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
            CleanUpComponent(component);
            FreeComponent(component);
        } else {
            previous = component;
        }
        component = next;
    }
}

// 0x00477920
void CStruct::AddComponent(CComponent* added)
{
    CComponent* last = nullptr;
    CComponent* component = m_head;
    while (component) {
        bool replace = false;
        if (component->name == added->name) {
            ESymbolType mine = component->Type();
            ESymbolType theirs = added->Type();
            if (mine == theirs)
                replace = !(mine == ESYMBOLTYPE_NAME && component->name == 0) || component->raw == added->raw;
            else
                replace = (mine == ESYMBOLTYPE_FLOAT && theirs == ESYMBOLTYPE_INTEGER) ||
                          (mine == ESYMBOLTYPE_INTEGER && theirs == ESYMBOLTYPE_FLOAT);
        }
        if (replace) {
            CComponent* next = component->next;
            if (last)
                last->next = next;
            else
                m_head = next;
            CleanUpComponent(component);
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
void CStruct::AddString(uint32_t name, const char* value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_STRING);
    component->string = CreateString(value);
    AddComponent(component);
}

// 0x00477A90
void CStruct::AddString(const char* name, const char* value)
{
    AddString(Crc::GenerateCRCFromString(name), value);
}

// 0x00477AC0
void CStruct::AddLocalString(uint32_t name, const char* value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_LOCALSTRING);
    component->string = CreateString(value);
    AddComponent(component);
}

// 0x00478390
void CStruct::AddString(uint32_t name, uint32_t type, const char* value)
{
    if (type == ESYMBOLTYPE_STRING)
        AddString(name, value);
    else
        AddLocalString(name, value);
}

// 0x00477B80
void CStruct::AddInteger(uint32_t name, int value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_INTEGER);
    component->integer = value;
    AddComponent(component);
}

// 0x00477C30
void CStruct::AddInteger(const char* name, int value)
{
    AddInteger(Crc::GenerateCRCFromString(name), value);
}

// 0x00477C60
void CStruct::AddFloat(uint32_t name, float value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_FLOAT);
    component->floatValue = value;
    AddComponent(component);
}

// 0x00477D10
void CStruct::AddFloat(const char* name, float value)
{
    AddFloat(Crc::GenerateCRCFromString(name), value);
}

// 0x00477D40
void CStruct::AddChecksum(uint32_t name, uint32_t value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_NAME);
    component->checksum = value;
    AddComponent(component);
}

// 0x00477DF0
void CStruct::AddChecksum(const char* name, uint32_t value)
{
    AddChecksum(Crc::GenerateCRCFromString(name), value);
}

// 0x00477E20
void CStruct::AddVector(uint32_t name, float x, float y, float z)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_VECTOR);
    auto* vector = static_cast<CVector*>(VectorPools().Allocate(sizeof(CVector)));
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
    AddComponent(component);
}

// 0x00477F40
void CStruct::AddVector(const char* name, float x, float y, float z)
{
    AddVector(Crc::GenerateCRCFromString(name), x, y, z);
}

// 0x00477F70
void CStruct::AddVector(uint32_t name, VectorValue value)
{
    AddVector(name, value.x, value.y, value.z);
}

// 0x00477F90
void CStruct::AddVector(const char* name, VectorValue value)
{
    AddVector(Crc::GenerateCRCFromString(name), value.x, value.y, value.z);
}

// 0x00477FC0
void CStruct::AddPair(uint32_t name, float x, float y)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_PAIR);
    auto* pair = static_cast<CPair*>(PairPools().Allocate(sizeof(CPair)));
    if (pair) {
        pair->y = 0.0f;
        pair->x = 0.0f;
        reinterpret_cast<uint8_t*>(pair)[0] = 0;
    }
    component->pair = pair;
    pair->x = x;
    component->pair->y = y;
    AddComponent(component);
}

// 0x004780D0
void CStruct::AddArrayPointer(uint32_t name, CArray* value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_ARRAY);
    component->array = value;
    AddComponent(component);
}

// 0x00478180
void CStruct::AddArrayPointer(const char* name, CArray* value)
{
    AddArrayPointer(Crc::GenerateCRCFromString(name), value);
}

// 0x004781B0
void CStruct::AddStructurePointer(uint32_t name, CStruct* value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_STRUCTURE);
    component->structure = value;
    AddComponent(component);
}

// 0x00478260
void CStruct::AddStructurePointer(const char* name, CStruct* value)
{
    AddStructurePointer(Crc::GenerateCRCFromString(name), value);
}

// 0x00478670
void CStruct::AddStructure(uint32_t name, CStruct* value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_STRUCTURE);
    CStruct* copy = NewStruct();
    if (copy)
        copy->Init();
    component->structure = copy;
    if (value != copy) {
        copy->Clear();
        copy->AppendStructure(value);
    }
    AddComponent(component);
}

// 0x00478770
void CStruct::AddStructure(const char* name, CStruct* value)
{
    AddStructure(Crc::GenerateCRCFromString(name), value);
}

// 0x00478290
void CStruct::AddScript(uint32_t name, const uint8_t* code, uint32_t size)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_QSCRIPT);
    component->unknown2 = static_cast<uint16_t>(size);
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    auto* copy = static_cast<uint8_t*>(Mem::Malloc(size));
    Mem::PopContext();
    for (uint32_t i = 0; i < size; ++i)
        copy[i] = code[i];
    component->raw = reinterpret_cast<uint32_t>(copy);
    AddComponent(component);
}

// 0x004783C0
void CStruct::AddComponentOfType(uint32_t name, uint32_t type, uint32_t value)
{
    switch (type) {
    case ESYMBOLTYPE_INTEGER:
        AddInteger(name, static_cast<int>(value));
        break;
    case ESYMBOLTYPE_FLOAT: {
        float number;
        std::memcpy(&number, &value, 4);
        AddFloat(name, number);
        break;
    }
    case 11:
        AddStructurePointer(name, reinterpret_cast<CStruct*>(value));
        break;
    case ESYMBOLTYPE_ARRAY:
        AddArrayPointer(name, reinterpret_cast<CArray*>(value));
        break;
    case ESYMBOLTYPE_NAME:
        AddChecksum(name, value);
        break;
    default:
        break;
    }
}

// 0x00478480
CStruct* CStruct::AppendStructure(CStruct* source)
{
    for (CComponent* component = source->m_head; component; component = component->next) {
        CComponent* copy = NewComponent();
        CopyComponent(copy, component, 0);
        AddComponent(copy);
    }
    return this;
}

// 0x00478540
void CStruct::AppendStructureIfAny(CStruct* source)
{
    if (source)
        AppendStructure(source);
}

// 0x00478650
CStruct* CStruct::Assign(CStruct* source)
{
    if (source != this) {
        Clear();
        AppendStructure(source);
    }
    return this;
}

// 0x00478560
void CStruct::ExpandInto(CStruct* dest, int depth)
{
    for (CComponent* component = m_head; component; component = component->next) {
        if (CStruct* included = IncludedStructure(component)) {
            included->ExpandInto(dest, depth + 1);
            continue;
        }
        CComponent* copy = NewComponent();
        CopyComponent(copy, component, 0);
        dest->AddComponent(copy);
    }
}

} // namespace Script

namespace {

using Script::CArray;
using Script::CStruct;
using Script::VectorValue;
template <typename... A>
using AddMethod = void (CStruct::*)(A...);

// 0x0044C170: free a component (pooled)
void DeleteComponentAdapter(Script::CComponent* component) { Script::FreeComponent(component); }
// 0x0044C130: operator new (the size is always 16)
void* NewComponentMemory(unsigned int size) { return GameVariable<Mem::PoolPair>(0x006F2338).Allocate(size); }
struct ComponentAdapter {
    Script::CComponent* Construct() { return Script::ConstructComponent(reinterpret_cast<Script::CComponent*>(this)); }
};

} // namespace

REPLACE(0x0046D140, &ComponentAdapter::Construct);
REPLACE(0x0044C130, NewComponentMemory);
REPLACE(0x0044C170, DeleteComponentAdapter);
REPLACE(0x00477860, &CStruct::RemoveFlag);
REPLACE(0x00477920, &CStruct::AddComponent);
REPLACE(0x004779D0, static_cast<AddMethod<uint32_t, const char*>>(&CStruct::AddString));
REPLACE(0x00477A90, static_cast<AddMethod<const char*, const char*>>(&CStruct::AddString));
REPLACE(0x00477AC0, &CStruct::AddLocalString);
REPLACE(0x00478390, static_cast<AddMethod<uint32_t, uint32_t, const char*>>(&CStruct::AddString));
REPLACE(0x00477B80, static_cast<AddMethod<uint32_t, int>>(&CStruct::AddInteger));
REPLACE(0x00477C30, static_cast<AddMethod<const char*, int>>(&CStruct::AddInteger));
REPLACE(0x00477C60, static_cast<AddMethod<uint32_t, float>>(&CStruct::AddFloat));
REPLACE(0x00477D10, static_cast<AddMethod<const char*, float>>(&CStruct::AddFloat));
REPLACE(0x00477D40, static_cast<AddMethod<uint32_t, uint32_t>>(&CStruct::AddChecksum));
REPLACE(0x00477DF0, static_cast<AddMethod<const char*, uint32_t>>(&CStruct::AddChecksum));
REPLACE(0x00477E20, static_cast<AddMethod<uint32_t, float, float, float>>(&CStruct::AddVector));
REPLACE_SOURCE_ONLY(0x00478460, static_cast<AddMethod<uint32_t, float, float, float>>(&CStruct::AddVector)); // a jump
REPLACE(0x00477F40, static_cast<AddMethod<const char*, float, float, float>>(&CStruct::AddVector));
REPLACE(0x00477F70, static_cast<AddMethod<uint32_t, VectorValue>>(&CStruct::AddVector));
REPLACE(0x00477F90, static_cast<AddMethod<const char*, VectorValue>>(&CStruct::AddVector));
REPLACE(0x00477FC0, &CStruct::AddPair);
REPLACE_SOURCE_ONLY(0x00478470, &CStruct::AddPair); // a jump to 0x00477FC0
REPLACE(0x004780D0, static_cast<AddMethod<uint32_t, CArray*>>(&CStruct::AddArrayPointer));
REPLACE(0x00478180, static_cast<AddMethod<const char*, CArray*>>(&CStruct::AddArrayPointer));
REPLACE(0x004781B0, static_cast<AddMethod<uint32_t, CStruct*>>(&CStruct::AddStructurePointer));
REPLACE(0x00478260, static_cast<AddMethod<const char*, CStruct*>>(&CStruct::AddStructurePointer));
REPLACE(0x00478670, static_cast<AddMethod<uint32_t, CStruct*>>(&CStruct::AddStructure));
REPLACE(0x00478770, static_cast<AddMethod<const char*, CStruct*>>(&CStruct::AddStructure));
REPLACE(0x00478290, &CStruct::AddScript);
REPLACE(0x004783C0, &CStruct::AddComponentOfType);
REPLACE(0x00478480, &CStruct::AppendStructure);
REPLACE(0x00478540, &CStruct::AppendStructureIfAny);
REPLACE(0x00478650, &CStruct::Assign);
REPLACE(0x00478560, &CStruct::ExpandInto);

// --- arrays: sizing, elements, copying ------------------------------------------------------------------------

namespace Script {

namespace {

// Allocate `count` elements on the script heap.
uint32_t* AllocateElements(uint32_t count)
{
    Mem::PushContext(Mem::Manager::Get()->m_scriptHeap);
    auto* elements = static_cast<uint32_t*>(Mem::Malloc(count * 4));
    Mem::PopContext();
    return elements;
}

// 0x006F3610 / 0x0067DA30: how many structures were made by copying, and the most there were (debug counters).
int& StructCopyCount() { return GameVariable<int>(0x006F3610); }
int& StructCopyPeak() { return GameVariable<int>(0x0067DA30); }

} // namespace

// 0x0046D410 / 0x0046D3D0 (operator new; the size is always the object's)
CPair* NewPair() { return static_cast<CPair*>(GameVariable<Mem::PoolPair>(0x006F3644).Allocate(sizeof(CPair))); }
CVector* NewVector()
{
    return static_cast<CVector*>(GameVariable<Mem::PoolPair>(0x006F3654).Allocate(sizeof(CVector)));
}

// 0x0047AC00
CPair* CPair::Init()
{
    y = 0.0f;
    x = 0.0f;
    reinterpret_cast<uint8_t*>(this)[0] = 0;
    return this;
}

// 0x0047ABF0
CVector* CVector::Init()
{
    z = 0.0f;
    y = 0.0f;
    x = 0.0f;
    reinterpret_cast<uint8_t*>(this)[0] = 0;
    return this;
}

// 0x0046CC50
void CArray::SetSizeAndType(uint32_t size, ESymbolType type)
{
    if (m_size == 1) {
        // (the single element is in place: nothing to free)
        if (m_elements)
            m_elements = nullptr;
    } else if (m_elements) {
        Mem::FreeMalloc(m_elements);
        m_elements = nullptr;
    }
    m_type = type;
    m_size = size;
    if (size == 0) {
        m_type = ESYMBOLTYPE_NONE;
        return;
    }
    if (size == 1)
        return;
    m_elements = AllocateElements(m_size);
    for (uint32_t i = 0; i < m_size; ++i)
        m_elements[i] = 0;
}

// 0x0046CCF0
void CArray::Resize(uint32_t size)
{
    if (size == m_size)
        return;
    uint32_t* elements = AllocateElements(size);
    const uint32_t* from = GetArrayPointer();
    for (uint32_t i = 0; i < m_size; ++i)
        elements[i] = from[i];
    for (uint32_t i = m_size; i < size; ++i)
        elements[i] = 0;
    if (m_size > 1)
        Mem::FreeMalloc(m_elements);
    // (growing to one element leaves the block's pointer in place of the single value, as the original does)
    m_elements = elements;
    m_size = size;
}

// 0x0046CB10
void CArray::SetElement(uint32_t index, uint32_t value)
{
    if (m_size == 1)
        m_single = value;
    else
        m_elements[index] = value;
}

// 0x0046CB40
void CArray::SetFloat(uint32_t index, float value)
{
    if (m_size == 1)
        std::memcpy(&m_single, &value, 4);
    else
        std::memcpy(&m_elements[index], &value, 4);
}

// 0x0046CC30
uint32_t CArray::GetElement(uint32_t index)
{
    return m_size == 1 ? m_single : m_elements[index];
}

// 0x0046CBB0
const char* CArray::GetString(uint32_t index)
{
    return reinterpret_cast<const char*>(GetElement(index));
}

// 0x0046CB70
const char* CArray::GetLocalString(uint32_t index)
{
    return reinterpret_cast<const char*>(GetElement(index));
}

// 0x0046CBF0
float CArray::GetFloat(uint32_t index)
{
    uint32_t bits = GetElement(index);
    if (m_type == ESYMBOLTYPE_FLOAT) {
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
        case ESYMBOLTYPE_INTEGER:
        case ESYMBOLTYPE_FLOAT:
        case ESYMBOLTYPE_NAME:
            dest[i] = element;
            break;
        case ESYMBOLTYPE_STRING:
        case ESYMBOLTYPE_LOCALSTRING:
            if (element)
                dest[i] = reinterpret_cast<uint32_t>(CreateString(reinterpret_cast<const char*>(element)));
            break;
        case ESYMBOLTYPE_PAIR:
            if (element) {
                auto* from = reinterpret_cast<const CPair*>(element);
                CPair* pair = NewPair();
                pair = pair ? pair->Init() : nullptr;
                pair->x = from->x;
                pair->y = from->y;
                dest[i] = reinterpret_cast<uint32_t>(pair);
            }
            break;
        case ESYMBOLTYPE_VECTOR:
            if (element) {
                auto* from = reinterpret_cast<const CVector*>(element);
                CVector* vector = NewVector();
                vector = vector ? vector->Init() : nullptr;
                vector->x = from->x;
                vector->y = from->y;
                vector->z = from->z;
                dest[i] = reinterpret_cast<uint32_t>(vector);
            }
            break;
        case ESYMBOLTYPE_STRUCTURE:
            if (element) {
                auto* from = reinterpret_cast<CStruct*>(element);
                CStruct* structure = NewStruct();
                if (structure)
                    structure->Init();
                if (from != structure) {
                    structure->Clear();
                    structure->AppendStructure(from);
                }
                dest[i] = reinterpret_cast<uint32_t>(structure);
            }
            break;
        case ESYMBOLTYPE_ARRAY:
            if (element) {
                CArray* array = NewArray();
                if (array)
                    array = array->Init();
                CopyArray(array, reinterpret_cast<CArray*>(element));
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
void CopyArray(CArray* dest, CArray* source)
{
    CleanUpArray(dest);
    ESymbolType type = source->m_type;
    uint32_t size = source->m_size;
    dest->SetSizeAndType(size, type);
    if (size)
        CopyArrayElements(dest->GetArrayPointer(), source->GetArrayPointer(), size, type);
}

// 0x00478AB0
CStruct* CStruct::InitCopy(CStruct* source)
{
    m_head = nullptr;
    m_unknown1 = 0;
    if (source != this) {
        Clear();
        AppendStructure(source);
    }
    if (++StructCopyCount() > StructCopyPeak())
        StructCopyPeak() = StructCopyCount();
    return this;
}

} // namespace Script

namespace {

using Script::CArray;

// 0x0046D410 / 0x0046D3D0 take the size (always the object's).
void* NewPairMemory(unsigned int) { return Script::NewPair(); }
void* NewVectorMemory(unsigned int) { return Script::NewVector(); }

} // namespace

REPLACE(0x0046D410, NewPairMemory);
REPLACE(0x0046D3D0, NewVectorMemory);
REPLACE(0x0047AC00, &Script::CPair::Init);
REPLACE(0x0047ABF0, &Script::CVector::Init);
REPLACE(0x0046CC50, &CArray::SetSizeAndType);
REPLACE(0x0046CCF0, &CArray::Resize);
REPLACE(0x0046CB10, &CArray::SetElement);
REPLACE(0x0046CB40, &CArray::SetFloat);
REPLACE(0x0046CC30, &CArray::GetElement);
REPLACE_SOURCE_ONLY(0x0041CB80, &CArray::GetElement); // a jump to 0x0046CC30
REPLACE(0x0046CBB0, &CArray::GetString);
REPLACE(0x0046CB70, &CArray::GetLocalString);
REPLACE(0x0046CBF0, &CArray::GetFloat);
REPLACE(0x004787A0, Script::CopyArrayElements);
REPLACE(0x00478A60, Script::CopyArray);
REPLACE(0x00478AB0, &Script::CStruct::InitCopy);

// --- the symbol table and global values; comparing components; adding array copies --------------------------

namespace Script {

namespace {

// The symbol iterator's position: the current entry and its bucket.
CSymbolTableEntry*& IteratorEntry() { return GameVariable<CSymbolTableEntry*>(0x006F363C); }
uint32_t& IteratorBucket() { return GameVariable<uint32_t>(0x006F3640); }

// A global symbol's value as an integer / float (other types: 0).
int SymbolInteger(CSymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type == ESYMBOLTYPE_INTEGER)
            return entry->integer;
        if (entry->type == ESYMBOLTYPE_FLOAT)
            return static_cast<int>(entry->floatValue);
    }
    return 0;
}
float SymbolFloat(CSymbolTableEntry* entry)
{
    if (entry) {
        if (entry->type == ESYMBOLTYPE_INTEGER)
            return static_cast<float>(entry->integer);
        if (entry->type == ESYMBOLTYPE_FLOAT)
            return entry->floatValue;
    }
    return 0.0f;
}
uint32_t SymbolValueOfType(CSymbolTableEntry* entry, ESymbolType type)
{
    return entry && entry->type == type ? entry->raw : 0;
}

// The game's empty string (what the string getters give for missing symbols).
const char* EmptyString() { return reinterpret_cast<const char*>(0x006469B4); }

} // namespace

// 0x00478C90
void CreateSymbolTable()
{
    auto* table = static_cast<CSymbolTableEntry**>(Mem::Malloc(0x1000 * sizeof(CSymbolTableEntry*)));
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    for (uint32_t i = 0; i < 0x1000; ++i)
        table[i] = nullptr;
}

// 0x004790F0
CSymbolTableEntry* CSymbolTableEntry::Init()
{
    unknown0 &= 0xF8;
    name = 0;
    type = ESYMBOLTYPE_NONE;
    raw = 0;
    next = nullptr;
    unknown8 = 0;
    return this;
}

// 0x00479110
CSymbolTableEntry* CreateSymbol(uint32_t name)
{
    void* memory = GameVariable<Mem::PoolPair>(0x006F362C).Allocate(sizeof(CSymbolTableEntry));
    CSymbolTableEntry* entry = memory ? static_cast<CSymbolTableEntry*>(memory)->Init() : nullptr;
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
int GetInteger(uint32_t name) { return SymbolInteger(LookUpSymbol(name)); }
int GetInteger(const char* name) { return GetInteger(Crc::GenerateCRCFromString(name)); }
// 0x00478F00 / 0x00478F30
float GetFloat(uint32_t name) { return SymbolFloat(LookUpSymbol(name)); }
float GetFloat(const char* name) { return GetFloat(Crc::GenerateCRCFromString(name)); }
// 0x00478F90 / 0x00478FB0
uint32_t GetSymbolValue(uint32_t name)
{
    CSymbolTableEntry* entry = LookUpSymbol(name);
    return entry ? entry->raw : 0;
}
uint32_t GetSymbolValue(const char* name) { return GetSymbolValue(Crc::GenerateCRCFromString(name)); }
// 0x00478FD0 / 0x00478FF0 (any type: the raw value)
const char* GetString(uint32_t name)
{
    CSymbolTableEntry* entry = LookUpSymbol(name);
    return entry ? reinterpret_cast<const char*>(entry->raw) : EmptyString();
}
const char* GetString(const char* name) { return GetString(Crc::GenerateCRCFromString(name)); }
// 0x00479020 / 0x00479040
CStruct* GetStructure(uint32_t name)
{
    return reinterpret_cast<CStruct*>(SymbolValueOfType(LookUpSymbol(name), ESYMBOLTYPE_STRUCTURE));
}
CStruct* GetStructure(const char* name) { return GetStructure(Crc::GenerateCRCFromString(name)); }
// 0x00479070 / 0x00479090
CArray* GetArray(uint32_t name)
{
    return reinterpret_cast<CArray*>(SymbolValueOfType(LookUpSymbol(name), ESYMBOLTYPE_ARRAY));
}
CArray* GetArray(const char* name) { return GetArray(Crc::GenerateCRCFromString(name)); }

// 0x004791A0
bool ComponentsEqual(CComponent* a, CComponent* b)
{
    // A name standing for a global symbol counts as the symbol's value.
    auto resolve = [](CComponent* component, uint32_t* type, uint32_t* value) {
        *type = component->Type();
        *value = component->raw;
        if (*type == ESYMBOLTYPE_NAME) {
            if (CSymbolTableEntry* symbol = LookUpSymbol(*value)) {
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
    case ESYMBOLTYPE_INTEGER:
        if (typeB == ESYMBOLTYPE_FLOAT)
            return as_integer(valueA) == as_float(valueB);
        return typeB == ESYMBOLTYPE_INTEGER && valueA == valueB;
    case ESYMBOLTYPE_FLOAT:
        if (typeB == ESYMBOLTYPE_INTEGER)
            return as_float(valueA) == as_integer(valueB);
        return typeB == ESYMBOLTYPE_FLOAT && as_float(valueA) == as_float(valueB);
    case ESYMBOLTYPE_STRING:
    case ESYMBOLTYPE_LOCALSTRING:
        return typeB == typeA &&
               std::strcmp(reinterpret_cast<const char*>(valueA), reinterpret_cast<const char*>(valueB)) == 0;
    case ESYMBOLTYPE_PAIR: {
        if (typeB != ESYMBOLTYPE_PAIR)
            return false;
        auto* pa = reinterpret_cast<const CPair*>(valueA);
        auto* pb = reinterpret_cast<const CPair*>(valueB);
        return pa->x == pb->x && pa->y == pb->y;
    }
    case ESYMBOLTYPE_VECTOR: {
        if (typeB != ESYMBOLTYPE_VECTOR)
            return false;
        auto* va = reinterpret_cast<const CVector*>(valueA);
        auto* vb = reinterpret_cast<const CVector*>(valueB);
        return va->x == vb->x && va->y == vb->y && va->z == vb->z;
    }
    case ESYMBOLTYPE_QSCRIPT:
    case ESYMBOLTYPE_NAME:
        return typeB == typeA && valueA == valueB;
    case ESYMBOLTYPE_STRUCTURE:
    case ESYMBOLTYPE_ARRAY:
        // (different arrays are never equal: their comparison 0x0049F4B0 just returns false)
        return typeB == typeA && valueA == valueB;
    default:
        return false;
    }
}

// 0x00478B00
void CStruct::AddArray(uint32_t name, CArray* value)
{
    CComponent* component = NewTypedComponent(name, ESYMBOLTYPE_ARRAY);
    CArray* array = NewArray();
    if (array)
        array = array->Init();
    component->array = array;
    CopyArray(array, value);
    AddComponent(component);
}

// 0x00478C50
void CStruct::AddArray(const char* name, CArray* value)
{
    AddArray(Crc::GenerateCRCFromString(name), value);
}

} // namespace Script

namespace {

using Script::CStruct;

// 0x00479190: the type-name lookup was compiled out: it gives a "(Hey, don't use this...)" message.
const char* GetTypeName() { return reinterpret_cast<const char*>(0x00648474); }

// ComponentsEqual is a member of the first component in the game.
struct ComponentEqualsAdapter {
    bool Equals(Script::CComponent* other)
    {
        return Script::ComponentsEqual(reinterpret_cast<Script::CComponent*>(this), other);
    }
};

template <typename R, typename... A>
using Function = R (*)(A...);

} // namespace

REPLACE(0x00478C90, Script::CreateSymbolTable);
REPLACE(0x00478CC0, Script::FindSymbol);
REPLACE(0x004790F0, &Script::CSymbolTableEntry::Init);
REPLACE(0x00479110, Script::CreateSymbol);
REPLACE(0x00478DC0, Script::LinkSymbol);
REPLACE(0x00478D60, Script::UnlinkSymbol);
REPLACE(0x00478E50, Script::GetNextSymbol);
REPLACE(0x00478F60, static_cast<Function<int, uint32_t>>(Script::GetInteger));
REPLACE(0x004790C0, static_cast<Function<int, const char*>>(Script::GetInteger));
REPLACE(0x00478F00, static_cast<Function<float, uint32_t>>(Script::GetFloat));
REPLACE(0x00478F30, static_cast<Function<float, const char*>>(Script::GetFloat));
REPLACE(0x00478F90, static_cast<Function<uint32_t, uint32_t>>(Script::GetSymbolValue));
REPLACE(0x00478FB0, static_cast<Function<uint32_t, const char*>>(Script::GetSymbolValue));
REPLACE(0x00478FD0, static_cast<Function<const char*, uint32_t>>(Script::GetString));
REPLACE(0x00478FF0, static_cast<Function<const char*, const char*>>(Script::GetString));
REPLACE(0x00479020, static_cast<Function<Script::CStruct*, uint32_t>>(Script::GetStructure));
REPLACE(0x00479040, static_cast<Function<Script::CStruct*, const char*>>(Script::GetStructure));
REPLACE(0x00479070, static_cast<Function<Script::CArray*, uint32_t>>(Script::GetArray));
REPLACE(0x00479090, static_cast<Function<Script::CArray*, const char*>>(Script::GetArray));
REPLACE(0x004791A0, &ComponentEqualsAdapter::Equals);
REPLACE(0x00478B00, static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArray));
REPLACE_SOURCE_ONLY(0x00478C80, static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArray)); // a jump
REPLACE(0x00478C50, static_cast<void (CStruct::*)(const char*, Script::CArray*)>(&CStruct::AddArray));
REPLACE(0x00479190, GetTypeName);
