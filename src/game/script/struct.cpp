#include "game/script/struct.h"

#include "game/core/crc.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Script {

namespace {

CSymbolTableEntry** SymbolTable() { return GameVariable<CSymbolTableEntry**>(0x006F3624); }
constexpr uint32_t kSymbolTableMask = 0xFFF;

CSymbolTableEntry* FindSymbol(uint32_t name)
{
    CSymbolTableEntry* entry = SymbolTable()[name & kSymbolTableMask];
    while (entry && entry->name != name)
        entry = entry->next;
    return entry;
}

// The global structure an unnamed name component includes, or null.
CStruct* IncludedStructure(const CComponent* component)
{
    if (component->name != 0 || component->Type() != ESYMBOLTYPE_NAME)
        return nullptr;
    CSymbolTableEntry* symbol = LookUpSymbol(component->checksum);
    return symbol && symbol->type == ESYMBOLTYPE_STRUCTURE ? symbol->structure : nullptr;
}

// Two components' values equal (not rewritten yet).
bool ComponentsEqual(CComponent* a, CComponent* b)
{
    return GameFunction<bool(__thiscall*)(CComponent*, CComponent*)>(0x004791A0)(a, b);
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
bool CStruct::GetString(uint32_t name, const char** out, int /*assert*/)
{
    const char* value;
    if (!Search(name, ESYMBOLTYPE_STRING, &value) && !Search(name, ESYMBOLTYPE_LOCALSTRING, &value))
        return false;
    *out = value;
    return true;
}

// 0x004765F0
bool CStruct::GetString(const char* name, const char** out, int assert)
{
    return GetString(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476630
bool CStruct::GetInteger(uint32_t name, int* out, int /*assert*/)
{
    int value;
    if (!Search(name, ESYMBOLTYPE_INTEGER, &value))
        return false;
    *out = value;
    return true;
}

// 0x00476680
bool CStruct::GetInteger(const char* name, int* out, int assert)
{
    return GetInteger(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x004766D0
bool CStruct::GetFloat(uint32_t name, float* out, int /*assert*/)
{
    float value;
    if (!Search(name, ESYMBOLTYPE_FLOAT, &value))
        return false;
    *out = value;
    return true;
}

// 0x00476720
bool CStruct::GetFloat(const char* name, float* out, int assert)
{
    return GetFloat(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476770
bool CStruct::GetPair(uint32_t name, float* x, float* y, int /*assert*/)
{
    CPair* pair;
    if (!Search(name, ESYMBOLTYPE_PAIR, &pair))
        return false;
    *x = pair->x;
    *y = pair->y;
    return true;
}

// 0x00476DF0 (zero if missing)
bool CStruct::GetPair(uint32_t name, CPair* out, bool /*assert*/)
{
    CPair* pair;
    if (Search(name, ESYMBOLTYPE_PAIR, &pair)) {
        out->x = pair->x;
        out->y = pair->y;
        return true;
    }
    out->x = 0.0f;
    out->y = 0.0f;
    return false;
}

// 0x004767C0
bool CStruct::GetStructure(uint32_t name, CStruct** out, int /*assert*/)
{
    CStruct* value;
    if (!Search(name, ESYMBOLTYPE_STRUCTURE, &value))
        return false;
    *out = value;
    return true;
}

// 0x00476810
bool CStruct::GetStructure(const char* name, CStruct** out, int assert)
{
    return GetStructure(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476860
bool CStruct::GetArray(uint32_t name, CArray** out, int /*assert*/)
{
    CArray* value;
    if (!Search(name, ESYMBOLTYPE_ARRAY, &value))
        return false;
    *out = value;
    return true;
}

// 0x004768B0
bool CStruct::GetArray(const char* name, CArray** out, int assert)
{
    return GetArray(Crc::GenerateCRCFromString(name), out, assert);
}

// 0x00476950 (the last match wins; an unnamed name that includes a global structure isn't a value)
bool CStruct::GetChecksum(uint32_t name, uint32_t* out, int /*assert*/)
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
bool CStruct::GetVector(uint32_t name, VectorValue* out, int /*assert*/)
{
    CVector* vector;
    if (!Search(name, ESYMBOLTYPE_VECTOR, &vector))
        return false;
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
