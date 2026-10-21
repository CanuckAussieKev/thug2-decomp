// The engine's general script commands, 0x0044B7E0-0x0044CFFF (see script_util_cfuncs.h).

#include "game/script/script_util_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/mth/random.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CArray;
using Script::CScript;
using Script::CStruct;

constexpr uint32_t kName = 0xA1DC81F9;           // "name"
constexpr uint32_t kInteger = 0xA5F054F4;        // "integer"
constexpr uint32_t kA = 0x174841BC;              // "a"
constexpr uint32_t kB = 0x8E411006;              // "b"
constexpr uint32_t kResolution = 0x022CF075;     // "resolution"
constexpr uint32_t kArraySize = 0x26828C6E;      // "ArraySize"
constexpr uint32_t kIndex = 0x7F8C98FE;          // "index"
constexpr uint32_t kElement = 0xBEBFA1C6;        // "element"
constexpr uint32_t kParams = 0x7031F10C;         // "params"
constexpr uint32_t kMangledId = 0xFBA40626;      // "mangled_id"
constexpr uint32_t kBase = 0x3F4B019E;           // "base"
constexpr uint32_t kSuffixString = 0x94542EEA;   // "SuffixString"
constexpr uint32_t kAppendedId = 0xAFB911C6;     // "appended_id"
constexpr uint32_t kUpperCaseString = 0xFE366B7B; // "UpperCaseString"
constexpr uint32_t kText = 0xC4745838;           // "text"
constexpr uint32_t kMaxLength = 0xA6931595;      // (name unknown) AppendSuffix: the longest result
constexpr uint32_t kSuffix = 0x4A4F7821;         // "suffix"
constexpr uint32_t kResultName = 0x50060694;     // (name unknown) AppendSuffix: the result's name

// The calling script's object (null: not an object's script).
Obj::CObject* ScriptObject(CScript* script) { return script->m_object.Get(); }

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x006234FE)(out, format, args...);
}

// strcpy, unbounded as the original's inlined copies are.
void CopyText(char* out, const char* text)
{
    while ((*out++ = *text++) != '\0') {
    }
}

// 0x00479190: the type-name lookup, compiled out (its "(Hey, don't use this...)" text; the type is ignored).
const char* TypeName(Script::ESymbolType /*type*/) { return reinterpret_cast<const char*>(0x00648474); }

// 0x0044A480 (not rewritten here): FormatText's work - the text put together from `params` into `out`.
void FormatTextInto(CStruct* out, CStruct* params)
{
    GameFunction<void(__cdecl*)(CStruct*, CStruct*)>(0x0044A480)(out, params);
}

} // namespace

// 0x0044B7E0: RemoveParameter <name>: the script's parameter (every component of that name) and the flag of
// that name removed. Always true.
bool __cdecl CFunc_RemoveParameter(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    CStruct* own = script->m_params;
    own->RemoveComponent(name);
    own->RemoveFlag(name);
    return true;
}

// 0x0044B820: RemoveFlag <flag>: the flag removed from the script's parameters. Always true.
bool __cdecl CFunc_RemoveFlag(CStruct* params, CScript* script)
{
    uint32_t flag = 0;
    params->GetChecksum(0u, &flag, 0);
    script->m_params->RemoveFlag(flag);
    return true;
}

// 0x0044B850: GetRandomValue name=<result> a=<low> b=<high> [integer] [resolution=n (1000)]: with `integer`, a
// random integer from a to b (either order) inclusive; else a random float from a to b in `resolution` steps
// (a + Rnd(resolution) * (b - a) / (resolution - 1)). Put in the script's parameters under `name`. Always true.
bool __cdecl CFunc_GetRandomValue(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(kName, &name, 0);
    if (params->ContainsFlag(kInteger)) {
        int a = 0;
        int b = 0;
        params->GetInteger(kA, &a, 0);
        params->GetInteger(kB, &b, 0);
        if (a > b) {
            int swap = a;
            a = b;
            b = swap;
        }
        script->m_params->AddInteger(name, Mth::Rnd(b - a + 1) + a);
        return true;
    }
    int resolution = 1000;
    params->GetInteger(kResolution, &resolution, 0);
    float a = 0.0f;
    float b = 0.0f;
    params->GetFloat(kA, &a, 0);
    params->GetFloat(kB, &b, 0);
    int step = Mth::Rnd(resolution);
    // x87 at 53 bits: step * (b - a), / (resolution - 1), + a, each rounded to double; then to float
    double value = static_cast<double>(step) * (static_cast<double>(b) - static_cast<double>(a));
    value /= static_cast<double>(resolution - 1);
    value += static_cast<double>(a);
    script->m_params->AddFloat(name, static_cast<float>(value));
    return true;
}

// 0x0044B9A0: SizeOf <array>: the array's size (0 without one) as the script's ArraySize. Always true.
bool __cdecl CFunc_SizeOf(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->GetArray(0u, &array, 0);
    uint32_t size = array ? array->m_size : 0;
    script->m_params->AddComponentOfType(kArraySize, Script::ESYMBOLTYPE_INTEGER, size);
    return true;
}

// 0x0044B9E0: GetNextArrayElement <array> [index=n]: the element after `index` (the first without one, or for a
// negative one) as the script's element, and its index as index (both taken out of the script's parameters
// first). False at the end of the array (or without one). Integers, floats, strings, local strings, structures,
// arrays and names; another type prints an error (and is still true, with only the index set).
bool __cdecl CFunc_GetNextArrayElement(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->GetArray(0u, &array, 0);
    int index = -1;
    script->m_params->GetInteger(kIndex, &index, 0);
    script->m_params->RemoveComponent(kIndex);
    script->m_params->RemoveComponent(kElement);
    if (!array)
        return false;
    uint32_t size = array->m_size;
    if (index < 0) {
        index = 0;
        if (size == 0)
            return false;
    } else {
        ++index;
        if (index >= static_cast<int>(size))
            return false;
    }
    script->m_params->AddInteger(kIndex, index);
    CStruct* own = script->m_params;
    switch (array->m_type) {
    case Script::ESYMBOLTYPE_INTEGER:
        own->AddInteger(kElement, static_cast<int>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_FLOAT:
        own->AddFloat(kElement, array->GetFloat(index));
        break;
    case Script::ESYMBOLTYPE_STRING:
        own->AddString(kElement, array->GetLocalString(index)); // (0x0046CB70: the same element read)
        break;
    case Script::ESYMBOLTYPE_LOCALSTRING:
        own->AddLocalString(kElement, array->GetString(index));
        break;
    case Script::ESYMBOLTYPE_STRUCTURE:
        own->AddStructure(kElement, reinterpret_cast<CStruct*>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_ARRAY:
        own->AddArray(kElement, reinterpret_cast<CArray*>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_NAME:
        own->AddChecksum(kElement, array->GetElement(index));
        break;
    default:
        Dbg::Printf(reinterpret_cast<const char*>(0x00647094), TypeName(array->m_type)); // "... does not support ..."
        break;
    }
    return true;
}

// 0x0044BBD0: GetRandomArrayElement <array>: a random element as the script's element and its index as index
// (both taken out of the script's parameters first). False for no array or an empty one. Integers, floats,
// structures, arrays and names; another type (strings too) prints an error (still true, only the index set).
bool __cdecl CFunc_GetRandomArrayElement(CStruct* params, CScript* script)
{
    script->m_params->RemoveComponent(kElement);
    script->m_params->RemoveComponent(kIndex);
    CArray* array = nullptr;
    params->GetArray(0u, &array, 0);
    if (!array || array->m_size == 0)
        return false;
    int index = Mth::Rnd(static_cast<int>(array->m_size));
    script->m_params->AddInteger(kIndex, index);
    CStruct* own = script->m_params;
    switch (array->m_type) {
    case Script::ESYMBOLTYPE_INTEGER:
        own->AddInteger(kElement, static_cast<int>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_FLOAT:
        own->AddFloat(kElement, array->GetFloat(index));
        break;
    case Script::ESYMBOLTYPE_STRUCTURE:
        own->AddStructure(kElement, reinterpret_cast<CStruct*>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_ARRAY:
        own->AddArray(kElement, reinterpret_cast<CArray*>(array->GetElement(index)));
        break;
    case Script::ESYMBOLTYPE_NAME:
        own->AddChecksum(kElement, array->GetElement(index));
        break;
    default:
        Dbg::Printf(reinterpret_cast<const char*>(0x006470D0), TypeName(array->m_type)); // "... does not support ..."
        break;
    }
    return true;
}

// 0x0044BD30: GotParam <name>: whether the script's parameters have a component (or flag) of that name.
bool __cdecl CFunc_GotParam(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    return script->m_params->ContainsComponentNamed(name);
}

// 0x0044BD60: Goto <script> [params={...}]: the script continues as that script (on the same object). Always
// true.
bool __cdecl CFunc_Goto(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    script->SetScript(name, scriptParams, ScriptObject(script));
    return true;
}

// 0x0044BDC0: GotoRandomScript [<names>]: the script continues as one of the scripts named in the array (picked at
// random; no parameters). Nothing without a non-empty array of names. Always true.
bool __cdecl CFunc_GotoRandomScript(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->GetArray(0u, &array, 0);
    if (array && array->m_size && array->m_type == Script::ESYMBOLTYPE_NAME) {
        Obj::CObject* object = ScriptObject(script);
        uint32_t name = array->GetElement(Mth::Rnd(static_cast<int>(array->m_size)));
        script->SetScript(name, nullptr, object);
    }
    return true;
}

// 0x0044BE20: FormatText: the text put together into the script's parameters (0x0044A480). Always true.
bool __cdecl CFunc_FormatText(CStruct* params, CScript* script)
{
    FormatTextInto(script->m_params, params);
    return true;
}

// 0x0044BE40: MangleChecksums a=<checksum or integer> b=<...>: the sum a + b as the script's mangled_id. Always
// true. (Original quirk: a value given as neither is left uninitialised; here 0.)
bool __cdecl CFunc_MangleChecksums(CStruct* params, CScript* script)
{
    uint32_t a = 0;
    uint32_t b = 0;
    if (!params->GetChecksum(kA, &a, 0))
        params->GetInteger(kA, reinterpret_cast<int*>(&a), 1);
    if (!params->GetChecksum(kB, &b, 0))
        params->GetInteger(kB, reinterpret_cast<int*>(&b), 1);
    script->m_params->AddChecksum(kMangledId, a + b);
    return true;
}

// 0x0044BEC0: AppendSuffixToChecksum base=<checksum> SuffixString=<text>: the checksum continued with the text
// (as if the name had the text appended) as the script's appended_id. Always true. (Original quirk: missing ones
// are left uninitialised; here 0 / null.)
bool __cdecl CFunc_AppendSuffixToChecksum(CStruct* params, CScript* script)
{
    uint32_t base = 0;
    const char* suffix = nullptr;
    params->GetChecksum(kBase, &base, 1);
    params->GetString(kSuffixString, &suffix, 1);
    script->m_params->AddChecksum(kAppendedId, Crc::ExtendCRCWithString(base, suffix));
    return true;
}

// 0x0044BF20: GetUpperCaseString <text>: the text in capitals as the script's UpperCaseString. Always true.
// (Original quirks: text of 128 characters or more overruns the buffer; no text is read through an
// uninitialised pointer - here null.)
bool __cdecl CFunc_GetUpperCaseString(CStruct* params, CScript* script)
{
    const char* text = nullptr;
    params->GetString(0u, &text, 1);
    char upper[0x80];
    CopyText(upper, text);
    Str::UpperCase(upper);
    script->m_params->AddString(kUpperCaseString, upper);
    return true;
}

// 0x0044BFA0: AddFlag <flag>: the flag added to the script's parameters. Always true. (Original quirk: no flag
// leaves the name uninitialised; here 0.)
bool __cdecl CFunc_AddFlag(CStruct* params, CScript* script)
{
    uint32_t flag = 0;
    params->GetChecksum(0u, &flag, 0);
    script->m_params->AddChecksum(0u, flag);
    return true;
}

// 0x0044BFD0: AppendSuffix text=<text> suffix=<n (1)> [0xA6931595=<longest (15)>] [0x50060694=<result name>]: the
// text with a space and the number appended (sprintf " %d"), the text cut so the result is at most the longest,
// put in the script's parameters under the result name. Always true. (Original quirks: a text of 100 characters
// or more overruns the buffer; a number longer than the limit cuts before the result - into the number's buffer,
// which lies just before it, as here.)
bool __cdecl CFunc_AppendSuffix(CStruct* params, CScript* script)
{
    const char* text = reinterpret_cast<const char*>(0x006469B4); // ""
    params->GetString(kText, &text, 0);
    size_t textLength = std::strlen(text);
    struct {
        char number[20];
        char result[100];
    } buffers;
    CopyText(buffers.result, text);
    int maxLength = 15;
    params->GetInteger(kMaxLength, &maxLength, 0);
    int suffix = 1;
    params->GetInteger(kSuffix, &suffix, 0);
    Sprintf(buffers.number, reinterpret_cast<const char*>(0x0064710C), suffix); // " %d"
    int numberLength = static_cast<int>(std::strlen(buffers.number));
    if (numberLength + static_cast<int>(textLength) > maxLength)
        reinterpret_cast<char*>(&buffers)[sizeof(buffers.number) + maxLength - numberLength] = '\0';
    CopyText(buffers.result + std::strlen(buffers.result), buffers.number);
    uint32_t name = 0;
    params->GetChecksum(kResultName, &name, 0);
    script->m_params->AddString(name, buffers.result);
    return true;
}

// 0x0044C120: IsObjectScript: whether the script runs on an object.
bool __cdecl CFunc_IsObjectScript(CStruct*, CScript* script) { return ScriptObject(script) != nullptr; }

} // namespace CFuncs

REPLACE(0x0044B7E0, CFuncs::CFunc_RemoveParameter);
REPLACE(0x0044B820, CFuncs::CFunc_RemoveFlag);
REPLACE(0x0044B850, CFuncs::CFunc_GetRandomValue);
REPLACE(0x0044B9A0, CFuncs::CFunc_SizeOf);
REPLACE(0x0044B9E0, CFuncs::CFunc_GetNextArrayElement);
REPLACE(0x0044BBD0, CFuncs::CFunc_GetRandomArrayElement);
REPLACE(0x0044BD30, CFuncs::CFunc_GotParam);
REPLACE(0x0044BD60, CFuncs::CFunc_Goto);
REPLACE(0x0044BDC0, CFuncs::CFunc_GotoRandomScript);
REPLACE(0x0044BE20, CFuncs::CFunc_FormatText);
REPLACE(0x0044BE40, CFuncs::CFunc_MangleChecksums);
REPLACE(0x0044BEC0, CFuncs::CFunc_AppendSuffixToChecksum);
REPLACE(0x0044BF20, CFuncs::CFunc_GetUpperCaseString);
REPLACE(0x0044BFA0, CFuncs::CFunc_AddFlag);
REPLACE(0x0044BFD0, CFuncs::CFunc_AppendSuffix);
REPLACE(0x0044C120, CFuncs::CFunc_IsObjectScript);
