// The engine's general script commands, 0x0044B7E0-0x0044CFFF (see script_util_cfuncs.h).

#include "game/script/script_util_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/maths/random.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/token_reader.h"
#include "game/script/qb.h"
#include "game/script/script_cfuncs.h"
#include "game/script/param_struct.h"
#include "game/sys/crt.h"

namespace ScriptCommands {

namespace {

using Scripts::CArray;
using Scripts::CComponent;
using Scripts::CScript;
using Scripts::CStruct;

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
constexpr uint32_t kDo = 0x62BA3F6A;             // "do"
constexpr uint32_t kTextName = 0xFF0DB407;       // "TextName"
constexpr uint32_t kPrintfText = 0xBC4AC08B;     // "PrintfText"
constexpr uint32_t kIndexArray = 0xAC3A91DE;     // "index_array"
constexpr uint32_t kStruct = 0x456D28D1;         // "struct"
constexpr uint32_t kField = 0xA40ABAA7;          // "field"
constexpr uint32_t kString = 0x61414D56;         // "string"
constexpr uint32_t kCharArray = 0x3C5EC9E9;      // "char_array"
constexpr uint32_t kArrayName = 0x8F61D602;      // "ArrayName"
constexpr uint32_t kNewValue = 0x4B40630D;       // "NewValue"
// AddComponentOfType's type for a structure handed over by pointer.
constexpr uint32_t kTypeStructurePointer = 11;

// The calling script's object (null: not an object's script).
GameObjects::CEntity* ScriptObject(CScript* script) { return script->m_entity.Pull(); }

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

// strcpy, unbounded as the original's inlined copies are.
void CopyText(char* out, const char* text)
{
    while ((*out++ = *text++) != '\0') {
    }
}

// 0x00479190: the type-name lookup, compiled out (its "(Hey, don't use this...)" text; the type is ignored).
const char* TypeName(Scripts::EQbValueKind /*type*/) { return "(Hey, don't use GetTypeName on the CD!)"; }

// 0x0044A480 (script_cfuncs.cpp): FormatText's work - the text put together from `params` into `out`.
int FormatTextInto(CStruct* out, CStruct* params) { return FormatText(out, params); }

} // namespace

// 0x0044B7E0: RemoveParameter <name>: the script's parameter (every component of that name) and the flag of
// that name removed. Always true.
bool __cdecl CFunc_RemoveParameter(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    CStruct* own = script->m_args;
    own->EraseModule(name);
    own->RemoveFlag(name);
    return true;
}

// 0x0044B820: RemoveFlag <flag>: the flag removed from the script's parameters. Always true.
bool __cdecl CFunc_RemoveFlag(CStruct* params, CScript* script)
{
    uint32_t flag = 0;
    params->ReadHashValue(0u, &flag, 0);
    script->m_args->RemoveFlag(flag);
    return true;
}

// 0x0044B850: GetRandomValue name=<result> a=<low> b=<high> [integer] [resolution=n (1000)]: with `integer`, a
// random integer from a to b (either order) inclusive; else a random float from a to b in `resolution` steps
// (a + Rnd(resolution) * (b - a) / (resolution - 1)). Put in the script's parameters under `name`. Always true.
bool __cdecl CFunc_GetRandomValue(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, 0);
    if (params->HasBareFlag(kInteger)) {
        int a = 0;
        int b = 0;
        params->FetchIntValue(kA, &a, 0);
        params->FetchIntValue(kB, &b, 0);
        if (a > b) {
            int swap = a;
            a = b;
            b = swap;
        }
        script->m_args->PutInt(name, Maths::Rnd(b - a + 1) + a);
        return true;
    }
    int resolution = 1000;
    params->FetchIntValue(kResolution, &resolution, 0);
    float a = 0.0f;
    float b = 0.0f;
    params->FetchFloat(kA, &a, 0);
    params->FetchFloat(kB, &b, 0);
    int step = Maths::Rnd(resolution);
    // x87 at 53 bits: step * (b - a), / (resolution - 1), + a, each rounded to double; then to float
    double value = static_cast<double>(step) * (static_cast<double>(b) - static_cast<double>(a));
    value /= static_cast<double>(resolution - 1);
    value += static_cast<double>(a);
    script->m_args->PushFloat(name, static_cast<float>(value));
    return true;
}

// 0x0044B9A0: SizeOf <array>: the array's size (0 without one) as the script's ArraySize. Always true.
bool __cdecl CFunc_SizeOf(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    uint32_t size = array ? array->m_extent : 0;
    script->m_args->AddComponentOfType(kArraySize, Scripts::QBVAL_INTEGER, size);
    return true;
}

// 0x0044B9E0: GetNextArrayElement <array> [index=n]: the element after `index` (the first without one, or for a
// negative one) as the script's element, and its index as index (both taken out of the script's parameters
// first). False at the end of the array (or without one). Integers, floats, strings, local strings, structures,
// arrays and names; another type prints an error (and is still true, with only the index set).
bool __cdecl CFunc_GetNextArrayElement(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    int index = -1;
    script->m_args->FetchIntValue(kIndex, &index, 0);
    script->m_args->EraseModule(kIndex);
    script->m_args->EraseModule(kElement);
    if (!array)
        return false;
    uint32_t size = array->m_extent;
    if (index < 0) {
        index = 0;
        if (size == 0)
            return false;
    } else {
        ++index;
        if (index >= static_cast<int>(size))
            return false;
    }
    script->m_args->PutInt(kIndex, index);
    CStruct* own = script->m_args;
    switch (array->m_kindTag) {
    case Scripts::QBVAL_INTEGER:
        own->PutInt(kElement, static_cast<int>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_FLOAT:
        own->PushFloat(kElement, array->FetchFloat(index));
        break;
    case Scripts::QBVAL_STRING:
        own->AppendWord(kElement, array->FindInlineText(index)); // (0x0046CB70: the same element read)
        break;
    case Scripts::QBVAL_INLINE_TEXT:
        own->AppendInlineText(kElement, array->FetchText(index));
        break;
    case Scripts::QBVAL_STRUCT:
        own->PutSubStruct(kElement, reinterpret_cast<CStruct*>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_ARRAY:
        own->AppendArrayValue(kElement, reinterpret_cast<CArray*>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_NAME:
        own->PutNameKey(kElement, array->FetchPart(index));
        break;
    default:
        DebugOut::Printf("GetNextArrayElement does not support arrays of type '%s'\n", TypeName(array->m_kindTag)); // "... does not support ..."
        break;
    }
    return true;
}

// 0x0044BBD0: GetRandomArrayElement <array>: a random element as the script's element and its index as index
// (both taken out of the script's parameters first). False for no array or an empty one. Integers, floats,
// structures, arrays and names; another type (strings too) prints an error (still true, only the index set).
bool __cdecl CFunc_GetRandomArrayElement(CStruct* params, CScript* script)
{
    script->m_args->EraseModule(kElement);
    script->m_args->EraseModule(kIndex);
    CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    if (!array || array->m_extent == 0)
        return false;
    int index = Maths::Rnd(static_cast<int>(array->m_extent));
    script->m_args->PutInt(kIndex, index);
    CStruct* own = script->m_args;
    switch (array->m_kindTag) {
    case Scripts::QBVAL_INTEGER:
        own->PutInt(kElement, static_cast<int>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_FLOAT:
        own->PushFloat(kElement, array->FetchFloat(index));
        break;
    case Scripts::QBVAL_STRUCT:
        own->PutSubStruct(kElement, reinterpret_cast<CStruct*>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_ARRAY:
        own->AppendArrayValue(kElement, reinterpret_cast<CArray*>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_NAME:
        own->PutNameKey(kElement, array->FetchPart(index));
        break;
    default:
        DebugOut::Printf("GetRandomArrayElement does not support arrays of type '%s'\n", TypeName(array->m_kindTag)); // "... does not support ..."
        break;
    }
    return true;
}

// 0x0044BD30: GotParam <name>: whether the script's parameters have a component (or flag) of that name.
bool __cdecl CFunc_GotParam(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    return script->m_args->HasEntryNamed(name);
}

// 0x0044BD60: Goto <script> [params={...}]: the script continues as that script (on the same object). Always
// true.
bool __cdecl CFunc_Goto(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    script->BindScript(name, scriptParams, ScriptObject(script));
    return true;
}

// 0x0044BDC0: GotoRandomScript [<names>]: the script continues as one of the scripts named in the array (picked at
// random; no parameters). Nothing without a non-empty array of names. Always true.
bool __cdecl CFunc_GotoRandomScript(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    if (array && array->m_extent && array->m_kindTag == Scripts::QBVAL_NAME) {
        GameObjects::CEntity* object = ScriptObject(script);
        uint32_t name = array->FetchPart(Maths::Rnd(static_cast<int>(array->m_extent)));
        script->BindScript(name, nullptr, object);
    }
    return true;
}

// 0x0044BE20: FormatText: the text put together into the script's parameters (0x0044A480). Always true.
bool __cdecl CFunc_FormatText(CStruct* params, CScript* script)
{
    FormatTextInto(script->m_args, params);
    return true;
}

// 0x0044BE40: MangleChecksums a=<checksum or integer> b=<...>: the sum a + b as the script's mangled_id. Always
// true. (Original quirk: a value given as neither is left uninitialised; here 0.)
bool __cdecl CFunc_MangleChecksums(CStruct* params, CScript* script)
{
    uint32_t a = 0;
    uint32_t b = 0;
    if (!params->ReadHashValue(kA, &a, 0))
        params->FetchIntValue(kA, reinterpret_cast<int*>(&a), 1);
    if (!params->ReadHashValue(kB, &b, 0))
        params->FetchIntValue(kB, reinterpret_cast<int*>(&b), 1);
    script->m_args->PutNameKey(kMangledId, a + b);
    return true;
}

// 0x0044BEC0: AppendSuffixToChecksum base=<checksum> SuffixString=<text>: the checksum continued with the text
// (as if the name had the text appended) as the script's appended_id. Always true. (Original quirk: missing ones
// are left uninitialised; here 0 / null.)
bool __cdecl CFunc_AppendSuffixToChecksum(CStruct* params, CScript* script)
{
    uint32_t base = 0;
    const char* suffix = nullptr;
    params->ReadHashValue(kBase, &base, 1);
    params->FetchText(kSuffixString, &suffix, 1);
    script->m_args->PutNameKey(kAppendedId, Checksums::FoldTextIntoChecksum(base, suffix));
    return true;
}

// 0x0044BF20: GetUpperCaseString <text>: the text in capitals as the script's UpperCaseString. Always true.
// (Original quirks: text of 128 characters or more overruns the buffer; no text is read through an
// uninitialised pointer - here null.)
bool __cdecl CFunc_GetUpperCaseString(CStruct* params, CScript* script)
{
    const char* text = nullptr;
    params->FetchText(0u, &text, 1);
    char upper[0x80];
    CopyText(upper, text);
    TextUtil::ToCapitals(upper);
    script->m_args->AppendWord(kUpperCaseString, upper);
    return true;
}

// 0x0044BFA0: AddFlag <flag>: the flag added to the script's parameters. Always true. (Original quirk: no flag
// leaves the name uninitialised; here 0.)
bool __cdecl CFunc_AddFlag(CStruct* params, CScript* script)
{
    uint32_t flag = 0;
    params->ReadHashValue(0u, &flag, 0);
    script->m_args->PutNameKey(0u, flag);
    return true;
}

// 0x0044BFD0: AppendSuffix text=<text> suffix=<n (1)> [0xA6931595=<longest (15)>] [0x50060694=<result name>]: the
// text with a space and the number appended (sprintf " %d"), the text cut so the result is at most the longest,
// put in the script's parameters under the result name. Always true. (Original quirks: a text of 100 characters
// or more overruns the buffer; a number longer than the limit cuts before the result - into the number's buffer,
// which lies just before it, as here.)
bool __cdecl CFunc_AppendSuffix(CStruct* params, CScript* script)
{
    const char* text = ""; // ""
    params->FetchText(kText, &text, 0);
    size_t textLength = std::strlen(text);
    struct {
        char number[20];
        char result[100];
    } buffers;
    CopyText(buffers.result, text);
    int maxLength = 15;
    params->FetchIntValue(kMaxLength, &maxLength, 0);
    int suffix = 1;
    params->FetchIntValue(kSuffix, &suffix, 0);
    Sprintf(buffers.number, " %d", suffix); // " %d"
    int numberLength = static_cast<int>(std::strlen(buffers.number));
    if (numberLength + static_cast<int>(textLength) > maxLength)
        reinterpret_cast<char*>(&buffers)[sizeof(buffers.number) + maxLength - numberLength] = '\0';
    CopyText(buffers.result + std::strlen(buffers.result), buffers.number);
    uint32_t name = 0;
    params->ReadHashValue(kResultName, &name, 0);
    script->m_args->AppendWord(name, buffers.result);
    return true;
}

// 0x0044C120: IsObjectScript: whether the script runs on an object.
bool __cdecl CFunc_IsObjectScript(CStruct*, CScript* script) { return ScriptObject(script) != nullptr; }

// 0x0044C290: ForEachIn <array> do=<script> [params={...}]: the script run (now, on this script's object) for each
// element of the array - with params, on a structure made of the element (a structure; none for null) and those
// params, else on the element itself as its parameters. Always true. (Original quirk: no array crashes.)
bool __cdecl CFunc_ForEachIn(CStruct* params, CScript* script)
{
    uint32_t each = 0;
    params->ReadHashValue(kDo, &each, 0);
    CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    CStruct* extra = nullptr;
    params->ReadSubStruct(kParams, &extra, 0);
    if (extra) {
        CStruct* elementParams = Scripts::NewStruct();
        if (elementParams)
            elementParams = elementParams->PrepareAll();
        for (uint32_t i = 0; i < array->m_extent; ++i) {
            elementParams->Clear();
            if (auto* element = reinterpret_cast<CStruct*>(array->FetchPart(i)))
                elementParams->MergeStructure(element);
            if (extra)
                elementParams->MergeStructure(extra);
            Scripts::LaunchScript(each, elementParams, ScriptObject(script), false, nullptr);
        }
        if (elementParams) {
            elementParams->Clear();
            Scripts::DeleteStruct(elementParams);
        }
        return true;
    }
    for (uint32_t i = 0; i < array->m_extent; ++i) {
        GameObjects::CEntity* object = ScriptObject(script);
        Scripts::LaunchScript(each, reinterpret_cast<CStruct*>(array->FetchPart(i)), object, false, nullptr);
    }
    return true;
}

// 0x0044C430: FetchPart <array> Index=n: element n as the script's element (taken out first): integers, a copy of
// a structure (handed over), arrays (copied); another type prints 0x00647110 (still true). False if the index is
// outside the array; true without an array.
bool __cdecl CFunc_GetElement(CStruct* params, CScript* script)
{
    CArray* array = nullptr;
    params->FetchList(0u, &array, 0);
    int index = 0;
    params->FetchIntValue("Index", &index, 0); // "Index"
    script->m_args->EraseModule(kElement);
    if (!array)
        return true;
    if (index < 0 || index >= static_cast<int>(array->m_extent))
        return false;
    switch (array->m_kindTag) {
    case Scripts::QBVAL_INTEGER:
        script->m_args->AddComponentOfType(kElement, Scripts::QBVAL_INTEGER, array->FetchPart(index));
        break;
    case Scripts::QBVAL_STRUCT: {
        CStruct* copy = Scripts::NewStruct();
        if (copy)
            copy = copy->PrepareAll();
        copy->AppendStructureIfAny(reinterpret_cast<CStruct*>(array->FetchPart(index)));
        script->m_args->AddComponentOfType(kElement, kTypeStructurePointer, reinterpret_cast<uint32_t>(copy));
        break;
    }
    case Scripts::QBVAL_ARRAY:
        script->m_args->AppendArrayValue(kElement, reinterpret_cast<CArray*>(array->FetchPart(index)));
        break;
    default:
        DebugOut::Printf("GetElement currently only supports arrays of structures, arrays or integers ... \n(Requested type=%d)\n", array->m_kindTag); // "... only supports ..."
        break;
    }
    return true;
}

// 0x0044C5A0: PermuteArray Array=<array> (NewArrayName=<name> | PermuteSource) [MakeNewFirstDifferFromOldLast]:
// the elements shuffled (size * 10 random swaps) - in a copy put in the script's parameters under the new name, or
// with PermuteSource in the array itself. With MakeNewFirstDifferFromOldLast, a first element equal to the old last
// one is swapped with a random other. False without the array or (making a copy) a name.
bool __cdecl CFunc_PermuteArray(CStruct* params, CScript* script)
{
    CArray* source = nullptr;
    params->FetchList("Array", &source, 0); // "Array"
    if (!source)
        return false;
    bool makeCopy = !params->HasBareFlag("PermuteSource"); // "PermuteSource"
    uint32_t newName = 0;
    CArray* copy = nullptr;
    CArray* array = source;
    if (makeCopy) {
        params->ReadHashValue("NewArrayName", &newName, 0); // "NewArrayName"
        if (!newName)
            return false;
        array = Scripts::NewArray();
        if (array)
            array = array->PrepareAll();
        copy = array;
        Scripts::DuplicateArray(array, source);
    }
    int size = static_cast<int>(array->m_extent);
    if (size) {
        uint32_t* elements = array->FindArrayValue();
        uint32_t oldLast = elements[size - 1];
        for (int swaps = size * 10; swaps > 0; --swaps) {
            int a = Maths::Rnd(size);
            int b = Maths::Rnd(size);
            uint32_t swap = elements[a];
            elements[a] = elements[b];
            elements[b] = swap;
        }
        if (params->HasBareFlag("MakeNewFirstDifferFromOldLast") && size > 1 && elements[0] == oldLast) {
            // "MakeNewFirstDifferFromOldLast"
            int other = Maths::Rnd(size - 1) + 1;
            uint32_t swap = elements[0];
            elements[0] = elements[other];
            elements[other] = swap;
        }
    }
    if (makeCopy)
        script->m_args->AppendArrayRef(newName, copy);
    return true;
}

// 0x0044C730: GotoPreserveParams <script> [params={...}]: Goto, the script's own parameters (and the params) going
// with it. Always true.
bool __cdecl CFunc_GotoPreserveParams(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    CStruct* scriptParams = Scripts::NewStruct();
    if (scriptParams)
        scriptParams = scriptParams->PrepareAll();
    scriptParams->AppendStructureIfAny(script->m_args);
    CStruct* extra = nullptr;
    params->ReadSubStruct(kParams, &extra, 0);
    scriptParams->AppendStructureIfAny(extra);
    script->BindScript(name, scriptParams, ScriptObject(script));
    if (scriptParams) {
        scriptParams->Clear();
        Scripts::DeleteStruct(scriptParams);
    }
    return true;
}

// 0x0044C840
bool __cdecl FormatPrintfText(char* out, CStruct* params, CScript*)
{
    const char* text = "Default printf text"; // "Default printf text"
    if (params->FetchText(0u, &text, 0)) {
        CStruct* format = Scripts::NewStruct();
        if (format)
            format = format->PrepareAll();
        format->PutNameKey(kTextName, kPrintfText);
        format->AppendStructureIfAny(params);
        if (FormatTextInto(format, format))
            format->FetchText("PrintfText", &text, 0); // "PrintfText"
        else
            text = ""; // ""
        Sprintf(out, "%s\n", text); // "%s\n"
        if (format) {
            format->Clear();
            Scripts::DeleteStruct(format);
        }
        return true;
    }
    int value = 0;
    if (params->FetchIntValue(0u, &value, 0))
        Sprintf(out, "%d\n", value); // "%d\n"
    return true;
}

// 0x0044C9B0: Dump <name>: the script's parameter of that name written up ("name=%a" with a copy of it as a, or
// "name=[!! DOES NOT EXIST !!]") for printing - the printing is compiled out, so it's only made and freed. Always
// true. (Original quirk: no name leaves it uninitialised; here 0.)
bool __cdecl CFunc_Dump(CStruct* params, CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 1);
    CStruct* dump = Scripts::NewStruct();
    if (dump)
        dump = dump->PrepareAll();
    char text[0x400];
    CComponent* component = script->m_args->FindComponent(name);
    if (!component) {
        Sprintf(text, "%s=[!! DOES NOT EXIST !!]", Scripts::FindChecksumNameOrUnknown(name));
    } else {
        Sprintf(text, "%s=%%a", Scripts::FindChecksumNameOrUnknown(name)); // "%s=%%a"
        CComponent* value = Scripts::NewComponent();
        Scripts::DuplicateComponent(value, component, 0);
        value->name = kA;
        dump->AttachModule(value);
    }
    dump->AppendWord(0u, text);
    if (dump) {
        dump->Clear();
        Scripts::DeleteStruct(dump);
    }
    return true;
}

// 0x0044CB70: ScriptAssert <text or integer>: the message made (FormatPrintfText); the assert itself is compiled
// out. Always true.
bool __cdecl CFunc_ScriptAssert(CStruct* params, CScript* script)
{
    char message[0x400];
    FormatPrintfText(message, params, script);
    return true;
}

// 0x0044CBC0: CreateIndexArray <n>: the array 0, 1 .. n-1 as the script's index_array. Always true. (Original
// quirk: no n leaves it uninitialised; here 0.)
bool __cdecl CFunc_CreateIndexArray(CStruct* params, CScript* script)
{
    int size = 0;
    params->FetchIntValue(0u, &size, 1);
    CArray* indexes = Scripts::NewArray();
    if (indexes)
        indexes = indexes->PrepareAll();
    indexes->ConfigureSizeKind(static_cast<uint32_t>(size), Scripts::QBVAL_INTEGER);
    for (int i = 0; i < size; ++i)
        indexes->SetElement(i, static_cast<uint32_t>(i));
    script->m_args->AppendArrayValue(kIndexArray, indexes);
    Scripts::ReleaseArrayStorage(indexes);
    if (indexes) {
        indexes->Clear();
        Scripts::DeleteArray(indexes);
    }
    return true;
}

// 0x0044CCC0: AppendStruct struct=<global structure> field=<name> params={...}: a copy of params added to the
// global structure as `field`. Always true. (Original quirks: missing names are uninitialised - here 0 - and a
// missing global structure crashes.)
bool __cdecl CFunc_AppendStruct(CStruct* params, CScript*)
{
    uint32_t blockName = 0;
    uint32_t field = 0;
    CStruct* fieldParams = nullptr;
    params->ReadHashValue(kStruct, &blockName, 1);
    params->ReadHashValue(kField, &field, 1);
    params->ReadSubStruct(kParams, &fieldParams, 1);
    CStruct* copy = Scripts::NewStruct();
    if (copy)
        copy = copy->PrepareAll();
    copy->AppendStructureIfAny(fieldParams);
    Scripts::ReadSubStruct(blockName)->AppendStructRef(field, copy);
    return true;
}

// 0x0044CDB0: StringToCharArray string=<text>: an array of one-character strings as the script's char_array.
// Always true.
bool __cdecl CFunc_StringToCharArray(CStruct* params, CScript* script)
{
    const char* string = nullptr;
    params->FetchText(kString, &string, 1);
    uint32_t length = static_cast<uint32_t>(std::strlen(string));
    CArray* characters = Scripts::NewArray();
    if (characters)
        characters = characters->PrepareAll();
    characters->ConfigureSizeKind(length, Scripts::QBVAL_STRING);
    char one[2];
    one[1] = '\0';
    for (uint32_t i = 0; i < length; ++i) {
        one[0] = string[i];
        characters->SetElement(i, reinterpret_cast<uint32_t>(Scripts::SpawnText(one)));
    }
    script->m_args->AppendArrayRef(kCharArray, characters);
    return true;
}

// 0x0044CEC0: SetArrayElement ArrayName=<the script's array> index=n NewValue=<value>: element n of the script's
// array set to a copy of the value (a name naming a global resolved first); what the element owned before
// (string, pair, vector, structure, array) is freed. Float arrays take integers too; other array types are left
// alone. Always true. (Original quirks: missing parameters crash or use uninitialised values; the value isn't
// checked against the array's type.)
bool __cdecl CFunc_SetArrayElement(CStruct* params, CScript* script)
{
    uint32_t arrayName = 0;
    params->ReadHashValue(kArrayName, &arrayName, 1);
    CArray* array = nullptr;
    script->m_args->FetchList(arrayName, &array, 1);
    int index = 0;
    params->FetchIntValue(kIndex, &index, 1);
    CComponent* newValue = params->FindLastComponent(kNewValue);
    Scripts::EvaluateNameToken(newValue);
    CComponent* value = Scripts::NewComponent();
    Scripts::DuplicateComponent(value, newValue, 0);
    bool store = true; // the element becomes the value (whose own copy is handed over)
    switch (array->m_kindTag) {
    case Scripts::QBVAL_INTEGER:
    case Scripts::QBVAL_NAME:
        array->SetElement(index, value->raw);
        store = false;
        break;
    case Scripts::QBVAL_FLOAT:
        if (value->KindTag() == Scripts::QBVAL_FLOAT)
            array->ChangeFloat(index, value->floatValue);
        else
            array->ChangeFloat(index, static_cast<float>(value->integer));
        store = false;
        break;
    case Scripts::QBVAL_STRING:
    case Scripts::QBVAL_INLINE_TEXT:
        Scripts::FreeString(array->FindInlineText(index));
        break;
    case Scripts::QBVAL_PAIR:
        Scripts::FreePair(reinterpret_cast<void*>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_VECTOR:
        Scripts::FreeVector(reinterpret_cast<void*>(array->FetchPart(index)));
        break;
    case Scripts::QBVAL_STRUCT:
        if (auto* old = reinterpret_cast<CStruct*>(array->FetchPart(index))) {
            old->Clear();
            Scripts::DeleteStruct(old);
        }
        break;
    case Scripts::QBVAL_ARRAY: {
        auto* old = reinterpret_cast<CArray*>(array->FetchPart(index));
        Scripts::ReleaseArrayStorage(old);
        if (old) {
            old->Clear();
            Scripts::DeleteArray(old);
        }
        break;
    }
    default:
        store = false;
        break;
    }
    if (store)
        array->FindArrayValue()[index] = value->raw;
    value->raw = 0;
    Scripts::FreeComponent(value);
    return true;
}

} // namespace ScriptCommands
