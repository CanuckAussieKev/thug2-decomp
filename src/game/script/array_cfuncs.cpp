// Script commands (CFunctions) for arrays, 0x0044AF50-0x0044B530 (see array_cfuncs.h).

#include "game/script/array_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace CFuncs {

namespace {

using Script::CArray;
using Script::CStruct;

constexpr uint32_t kArray = 0x5EF31148;      // "array"
constexpr uint32_t kContains = 0x71059581;   // "contains"
constexpr uint32_t kIndex1 = 0x0BA453B9;     // "Index1"
constexpr uint32_t kIndex2 = 0x92AD0203;     // "index2"
constexpr uint32_t kIndex3 = 0xE5AA3295;     // "index3"
constexpr uint32_t kArraySize = 0x7F5B3D4D;  // "array_size"
constexpr uint32_t kArrayName = 0x8F61D602;  // "arrayName"
constexpr uint32_t kIndices = 0xFFC8E484;    // "indices"
constexpr uint32_t kVal = 0x6820459A;        // "val"

CArray* ElementArray(CArray* array, uint32_t index) { return reinterpret_cast<CArray*>(array->GetElement(index)); }

// The array named `name`: one in the calling script's parameters, else the global one.
CArray* FindArray(Script::CScript* script, uint32_t name)
{
    CArray* array = nullptr;
    if (!script->m_params->GetArray(name, &array, 0))
        array = Script::GetArray(name);
    return array;
}

// `array`'s element `index` added to `results` as val, by the array's type (strings through GetLocalString; types
// with no case - scripts, functions... - add nothing).
void AddElement(CStruct* results, CArray* array, uint32_t index)
{
    switch (array->m_type) {
    case Script::ESYMBOLTYPE_INTEGER:
        results->AddInteger(kVal, static_cast<int>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_FLOAT:
        results->AddFloat(kVal, array->GetFloat(index));
        break;
    case Script::ESYMBOLTYPE_STRING:
    case Script::ESYMBOLTYPE_LOCALSTRING:
        results->AddString(kVal, array->GetLocalString(index));
        break;
    case Script::ESYMBOLTYPE_PAIR: {
        auto* pair = reinterpret_cast<Script::CPair*>(array->GetElement(index));
        results->AddPair(kVal, pair->x, pair->y);
        break;
    }
    case Script::ESYMBOLTYPE_VECTOR: {
        auto* vector = reinterpret_cast<Script::CVector*>(array->GetElement(index));
        results->AddVector(kVal, vector->x, vector->y, vector->z);
        break;
    }
    case Script::ESYMBOLTYPE_STRUCTURE:
        results->AddStructure(kVal, reinterpret_cast<CStruct*>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_ARRAY:
        results->AddArray(kVal, ElementArray(array, index));
        break;
    case Script::ESYMBOLTYPE_NAME:
        results->AddChecksum(kVal, array->GetElement(index));
        break;
    default:
        break;
    }
}

} // namespace

// 0x0044AF50: ArrayContains array=[...] contains=<value>: whether an element equals the value - integer arrays by
// value, string arrays ignoring case, name arrays by checksum (any other array: false).
bool __cdecl CFunc_ArrayContains(CStruct* params, Script::CScript*)
{
    CArray* array = nullptr;
    params->GetArray(kArray, &array, 1);
    Script::ESymbolType type = array->m_type;
    for (uint32_t i = 0; i < array->m_size; ++i) {
        switch (type) {
        case Script::ESYMBOLTYPE_INTEGER: {
            int value = 0;
            params->GetInteger(kContains, &value, 1);
            if (static_cast<uint32_t>(value) == array->GetElement(i))
                return true;
            break;
        }
        case Script::ESYMBOLTYPE_STRING: {
            const char* value = "";
            params->GetString(kContains, &value, 1);
            if (_stricmp(value, array->GetLocalString(i)) == 0)
                return true;
            break;
        }
        case Script::ESYMBOLTYPE_NAME: {
            uint32_t value = 0;
            params->GetChecksum(kContains, &value, 1);
            if (value == array->GetElement(i))
                return true;
            break;
        }
        default:
            break;
        }
    }
    return false;
}

// 0x0044B030: GetArraySize <array> [Index1=i [index2=j [index3=k]]]: array_size = the size of the array, or of the
// element array the indices lead to.
bool __cdecl CFunc_GetArraySize(CStruct* params, Script::CScript* script)
{
    CArray* array = nullptr;
    params->GetArray(0u, &array, 1);
    int index = 0;
    if (params->GetInteger(kIndex1, &index, 0)) {
        array = ElementArray(array, static_cast<uint32_t>(index));
        if (params->GetInteger(kIndex2, &index, 0)) {
            array = ElementArray(array, static_cast<uint32_t>(index));
            if (params->GetInteger(kIndex3, &index, 0))
                array = ElementArray(array, static_cast<uint32_t>(index));
        }
    }
    // (NOT ORIGINAL: no array - its parameters had none, or an index led nowhere - is logged before the original's
    // crash on it, with the script's name: e.g. a converted level's script naming an array THUG2 hasn't)
    if (!array)
        Log("GetArraySize: no array in %s's parameters (or an index leading to none) - the next line crashes",
            Script::FindChecksumNameOrUnknown(script->m_scriptName));
    script->m_params->AddInteger(kArraySize, static_cast<int>(array->m_size));
    return true;
}

// 0x0044B0F0: Get2DArrayData arrayName=<name> Index1=i index2=j: val = array[i][j] (the array the calling script's
// own parameters name, else the global).
bool __cdecl CFunc_Get2DArrayData(CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kArrayName, &name, 1);
    CArray* array = FindArray(script, name);
    int i = 0;
    params->GetInteger(kIndex1, &i, 1);
    int j = 0;
    params->GetInteger(kIndex2, &j, 1);
    AddElement(script->m_params, ElementArray(array, static_cast<uint32_t>(i)), static_cast<uint32_t>(j));
    return true;
}

// 0x0044B300: Get3DArrayData arrayName=<name> Index1=i index2=j index3=k: val = array[i][j][k].
bool __cdecl CFunc_Get3DArrayData(CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kArrayName, &name, 1);
    CArray* array = FindArray(script, name);
    int i = 0;
    params->GetInteger(kIndex1, &i, 1);
    int j = 0;
    params->GetInteger(kIndex2, &j, 1);
    int k = 0;
    params->GetInteger(kIndex3, &k, 1);
    CArray* plane = ElementArray(ElementArray(array, static_cast<uint32_t>(i)), static_cast<uint32_t>(j));
    AddElement(script->m_params, plane, static_cast<uint32_t>(k));
    return true;
}

// 0x0044B530: GetNDArrayData arrayName=<name>: val = the element the calling script's own `indices` array leads
// to (read from the script's parameters, not the command's). An empty indices array runs off the end, as in the
// original.
bool __cdecl CFunc_GetNDArrayData(CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kArrayName, &name, 1);
    CArray* array = FindArray(script, name);
    CArray* indices = nullptr;
    script->m_params->GetArray(kIndices, &indices, 1);
    uint32_t i = 0;
    for (; i < indices->m_size - 1; ++i)
        array = ElementArray(array, indices->GetElement(i));
    AddElement(script->m_params, array, indices->GetElement(i));
    return true;
}

} // namespace CFuncs

REPLACE(0x0044AF50, CFuncs::CFunc_ArrayContains);
REPLACE(0x0044B030, CFuncs::CFunc_GetArraySize);
REPLACE(0x0044B0F0, CFuncs::CFunc_Get2DArrayData);
REPLACE(0x0044B300, CFuncs::CFunc_Get3DArrayData);
REPLACE(0x0044B530, CFuncs::CFunc_GetNDArrayData);
