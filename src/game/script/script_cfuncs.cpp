// Script commands (CFunctions) for the script system itself, 0x00449D00-0x0044B7E0 (see script_cfuncs.h).

#include "game/script/script_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/token_reader.h"
#include "game/script/qb.h"
#include "game/script/param_struct.h"
#include "game/gfx/tod_manager.h"

namespace ScriptCommands {

float g_scriptFrameMilliseconds{16.666666f};
bool g_scriptPrintfEnabled{true};
bool g_firstTimeNotCalled{true};
char g_formatTextBuffer[1004];
Scripts::CComponent g_formatTextValue;
int g_scriptWarningLevel;

namespace {

using Scripts::CComponent;
using Scripts::CSymbolTableEntry;

constexpr uint32_t kGlobalName = 0x60F6F242;   // (a guess: "globalname") Change's name of the global
constexpr uint32_t kNewValue = 0x4B40630D;     // "newValue"
constexpr uint32_t kResolve = 0x991A7BC3;      // "resolve"
constexpr uint32_t kRealTime = 0xC847F2B9;     // (name unknown) Wait: the game clock, not frame time
constexpr uint32_t kSeconds = 0xD029F619;      // "seconds"
constexpr uint32_t kSecond = 0x49E0EE96;       // "second"
constexpr uint32_t kGameFrame = 0xFC37DF05;    // "gameframe"
constexpr uint32_t kGameFrames = 0xB99AE3D6;   // "gameframes"
constexpr uint32_t kGame = 0xDCD4CE73;         // "game"
constexpr uint32_t kFrame = 0x4A07C332;        // "frame"
constexpr uint32_t kFrames = 0x019176C5;       // "frames"
constexpr uint32_t kOnePerFrame = 0xECAA3345;  // "one_per_frame"
constexpr uint32_t kA = 0x174841BC;            // "A"
constexpr uint32_t kB = 0x8E411006;            // "B"
constexpr uint32_t kIndex = 0x7F8C98FE;        // "index"
constexpr uint32_t kString = 0x61414D56;       // "string"
constexpr uint32_t kName = 0xA1DC81F9;         // "name"
constexpr uint32_t kType = 0x7321A8D6;         // "type"
constexpr uint32_t kTextName = 0xFF0DB407;     // "textname"
constexpr uint32_t kChecksumName = 0x76FA7A8F; // "ChecksumName"
constexpr uint32_t kUseCommas = 0xF138085F;    // "UseCommas"
constexpr uint32_t kIntegerWidth = 0xADA5B338; // "integer_width"
constexpr uint32_t kDecimalPlaces = 0xD8E2E09F; // "decimalplaces"
constexpr uint32_t kStructure = 0x90FEC815;    // "Structure"
constexpr uint32_t kStruct = 0x456D28D1;       // "struct"
constexpr uint32_t kUntilEvent = 0x2C3A27A4;   // "UntilEvent"
constexpr uint32_t kEx = 0xF8728BEC;           // "ex"
constexpr uint32_t kScr = 0xA6D2D890;          // "scr"
constexpr uint32_t kGroup = 0x923FBB3A;        // "Group"
constexpr uint32_t kDefaultGroup = 0x1CA1FF20; // "default"
constexpr uint32_t kException = 0x80367192;    // "Exception"
constexpr uint32_t kParams = 0x7031F10C;       // "params"
constexpr uint32_t kLevel = 0x651533EC;        // "level"
constexpr uint32_t kLevelNone = 0x806FFF30;    // "none"
constexpr uint32_t kLevelWarn = 0xD6454EFA;    // (a guess: "warn")
constexpr uint32_t kLevelAssert = 0x4E10B054;  // (a guess: "assert")

// Whether FirstTimeThisIsCalled hasn't been called yet (0x0067D6D5, true at start).
bool& FirstTimeNotCalled() { return g_firstTimeNotCalled; }

// Whether the scripts' printf prints (0x0067D6D4).
bool& ScriptPrintfEnabled() { return g_scriptPrintfEnabled; }

// The value FormatText is writing (0x00696D2C, a component of its own: its flag bit is kept).
CComponent& FormatTextValue() { return g_formatTextValue; }

// IfDebugOn / IfDebugOff's work on the calling script - compiled out of this build (an empty call, 0x005480A0).
inline void SetScriptDebug(Scripts::CScript* /*script*/) {}

// `text` appended at `out`; returns the new end.
char* Append(char* out, const char* text)
{
    size_t length = std::strlen(text);
    std::memcpy(out, text, length + 1);
    return out + length;
}

// _ftol: truncated towards zero, through 64 bits.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// Wait `ms` milliseconds of the game clock or of frame time.
void WaitMs(Scripts::CScript* script, bool realTime, int ms)
{
    if (realTime)
        script->WaitRealTime(ms);
    else
        script->Lull(ms);
}

// GlobalExists' type names: the symbol type each stands for (0 for any other name).
Scripts::EQbValueKind TypeFromName(uint32_t name)
{
    switch (name) {
    case 0x90FEC815: // "Structure"
    case 0x456D28D1: // "struct"
        return Scripts::QBVAL_STRUCT;
    case 0x21902065: // "checksum"
    case 0xA1DC81F9: // "name"
        return Scripts::QBVAL_NAME;
    case 0x365AA16A: // "float"
        return Scripts::QBVAL_FLOAT;
    case 0x5EF31148: // "array"
        return Scripts::QBVAL_ARRAY;
    case 0x61414D56: // "string"
        return Scripts::QBVAL_STRING;
    case 0xE491B7A4: // "vector"
        return Scripts::QBVAL_VECTOR;
    case 0xA5F054F4: // "Integer"
    case 0xEBAE254E: // "int"
        return Scripts::QBVAL_INTEGER;
    case 0xF6A5E196: // "pair"
        return Scripts::QBVAL_PAIR;
    default:
        return Scripts::QBVAL_NONE;
    }
}

} // namespace

char* FormatTextBuffer() { return g_formatTextBuffer; }
int& ScriptWarningLevel() { return g_scriptWarningLevel; }

// 0x0044A480
int FormatText(Scripts::CStruct* results, Scripts::CStruct* params)
{
    uint32_t textName = 0;
    params->ReadHashValue(kTextName, &textName, 0);
    uint32_t nameKeyHash = 0;
    params->ReadHashValue(kChecksumName, &nameKeyHash, 0);
    int result = 1;
    const char* format = "";
    if (!params->FetchText(0u, &format, 0)) {
        DebugOut::Printf("Expected text format parameters to contain a format string\n");
        return 0;
    }
    char* out = FormatTextBuffer();
    const char* p = format;
    while (*p) {
        if (*p == '\\' && p[1] == '%') {
            *out++ = '%';
            p += 2;
            continue;
        }
        if (*p != '%') {
            *out++ = *p++;
            continue;
        }
        // a parameter: "%x" or "%%name"
        char name[0x2C];
        char* n = name;
        char key = p[1];
        p += 2;
        if (key == '%') {
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
                *n++ = *p++;
        } else {
            *n++ = key;
        }
        *n = '\0';
        CComponent* found = params->FindLastComponent(Checksums::HashFromText(name));
        if (!found) {
            DebugOut::Printf("Missing text formatting parameter '%s'\n", name);
            return 0;
        }
        CComponent& value = FormatTextValue();
        value.typeAndFlag = static_cast<uint8_t>((found->typeAndFlag & ~1) | (value.typeAndFlag & 1));
        value.raw = found->raw;
        Scripts::EvaluateNameToken(&value);
        char text[100]; // (the original's sprintf into it is unbounded)
        switch (value.KindTag()) {
        case Scripts::QBVAL_INTEGER:
            if (params->HasBareFlag(kUseCommas)) {
                std::snprintf(text, sizeof(text), "%s", TextUtil::FormatWithSeparators(value.integer));
            } else {
                int width = 0;
                if (params->FetchIntValue(kIntegerWidth, &width, 0))
                    std::snprintf(text, sizeof(text), "%0*d", width, value.integer);
                else
                    std::snprintf(text, sizeof(text), "%d", value.integer);
            }
            out = Append(out, text);
            break;
        case Scripts::QBVAL_FLOAT: {
            int places = 3;
            params->FetchIntValue(kDecimalPlaces, &places, 0);
            char floatFormat[16];
            std::snprintf(floatFormat, sizeof(floatFormat), "%%.%df", places);
            std::snprintf(text, sizeof(text), floatFormat, static_cast<double>(value.floatValue));
            out = Append(out, text);
            break;
        }
        case Scripts::QBVAL_STRING:
        case Scripts::QBVAL_INLINE_TEXT:
            for (const char* c = value.string; *c; ++c)
                *out++ = *c;
            break;
        case Scripts::QBVAL_PAIR:
            std::snprintf(text, sizeof(text), "(%.3f,%.3f)", static_cast<double>(value.pair->x),
                          static_cast<double>(value.pair->y));
            out = Append(out, text);
            break;
        case Scripts::QBVAL_VECTOR:
            std::snprintf(text, sizeof(text), "(%.3f,%.3f,%.3f)", static_cast<double>(value.vector->x),
                         static_cast<double>(value.vector->y), static_cast<double>(value.vector->z));
            out = Append(out, text);
            break;
        case Scripts::QBVAL_NAME:
            result = 2;
            std::snprintf(text, sizeof(text), "%s", Scripts::FindChecksumNameOrUnknown(value.checksum));
            out = Append(out, text);
            break;
        default:
            break;
        }
    }
    *out = '\0';
    if (textName)
        results->AppendWord(textName, FormatTextBuffer());
    if (nameKeyHash)
        results->PutNameKey(nameKeyHash, Checksums::HashFromText(FormatTextBuffer()));
    return result;
}

// 0x00449D00: Change <name>=<value> (or globalname=<name> newValue=<value>) [resolve]: the global is made again
// with the new value (its +0x08 word kept). Integers, floats and names are stored as they are, structures and arrays
// copied; with `resolve`, a name standing for another global takes that global's value. Any other type leaves the
// global made again but empty. False if the name isn't a global, or is a script or a C / member function.
bool __cdecl CFunc_Change(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(kGlobalName, &name, 0);
    CComponent* value;
    if (name) {
        value = params->FindComponent(kNewValue);
        if (!value)
            return false;
    } else {
        value = params->StepToEntry(nullptr);
        if (!value)
            return false;
        name = value->name;
    }
    CSymbolTableEntry* old = Scripts::FindSymbol(name);
    if (!old || old->type == Scripts::QBVAL_SCRIPT_BODY || old->type == Scripts::QBVAL_NATIVE_FUNC ||
        old->type == Scripts::QBVAL_NATIVE_METHOD)
        return false;
    uint32_t kept = old->unknown8;
    Scripts::CleanUpAndRemoveSymbol(old);
    CSymbolTableEntry* entry = Scripts::CreateSymbol(name);
    entry->unknown8 = kept;

    // the value in a component of its own (the new component's flag bit kept)
    CComponent copy;
    Scripts::ConstructComponent(&copy);
    copy.typeAndFlag = static_cast<uint8_t>((value->typeAndFlag & ~1) | (copy.typeAndFlag & 1));
    copy.raw = value->raw;
    if (copy.KindTag() == Scripts::QBVAL_NAME && params->HasBareFlag(kResolve)) {
        if (CSymbolTableEntry* resolved = Scripts::ResolveSymbol(copy.checksum)) {
            copy.typeAndFlag = static_cast<uint8_t>((resolved->type << 1) | (copy.typeAndFlag & 1));
            copy.raw = resolved->raw;
        }
    }
    switch (copy.KindTag()) {
    case Scripts::QBVAL_INTEGER:
    case Scripts::QBVAL_FLOAT:
    case Scripts::QBVAL_NAME:
        entry->type = copy.KindTag();
        entry->raw = copy.raw;
        break;
    case Scripts::QBVAL_STRUCT: {
        entry->type = Scripts::QBVAL_STRUCT;
        Scripts::CStruct* memory = Scripts::NewStruct();
        Scripts::CStruct* structure = memory ? memory->PrepareAll() : nullptr;
        entry->structure = structure;
        structure->AppendStructureIfAny(copy.structure);
        break;
    }
    case Scripts::QBVAL_ARRAY: {
        entry->type = Scripts::QBVAL_ARRAY;
        Scripts::CArray* memory = Scripts::NewArray();
        Scripts::CArray* array = memory ? memory->PrepareAll() : nullptr;
        entry->raw = reinterpret_cast<uint32_t>(array);
        Scripts::DuplicateArray(array, copy.array);
        break;
    }
    default:
        break;
    }
    return true;
}

// 0x0044A0D0: Wait <n> [seconds | second | gameframe(s) | game | frame(s) | one_per_frame] [0xC847F2B9]: seconds
// (times 1000) and frames (times 16.666666) wait as milliseconds, of the game clock with the 0xC847F2B9 flag or of
// frame time otherwise; game frames count Updates; one_per_frame counts Updates one script a frame; a bare number is
// milliseconds. Each count truncated.
bool __cdecl CFunc_Wait(Scripts::CStruct* params, Scripts::CScript* script)
{
    float value = 0.0f;
    params->FetchFloat(0u, &value, 0);
    bool realTime = params->HasBareFlag(kRealTime);
    if (params->HasBareFlag(kSeconds) || params->HasBareFlag(kSecond)) {
        WaitMs(script, realTime, Truncate(static_cast<double>(value) * Gfx2D::g_thousand)); // 1000
    } else if (params->HasBareFlag(kGameFrame) || params->HasBareFlag(kGameFrames) || params->HasBareFlag(kGame)) {
        script->Stall(Truncate(value));
    } else if (params->HasBareFlag(kFrame) || params->HasBareFlag(kFrames)) {
        WaitMs(script, realTime, Truncate(static_cast<double>(value) * g_scriptFrameMilliseconds)); // 16.67
    } else if (params->HasBareFlag(kOnePerFrame)) {
        script->SuspendUntilNextFrame(Truncate(value));
    } else {
        WaitMs(script, realTime, Truncate(value));
    }
    return true;
}

// 0x0044A200: IntegerEquals A=<int> B=<int> (missing: 0).
bool __cdecl CFunc_IntegerEquals(Scripts::CStruct* params, Scripts::CScript*)
{
    int a = 0;
    params->FetchIntValue(kA, &a, 0);
    int b = 0;
    params->FetchIntValue(kB, &b, 0);
    return a == b;
}

// 0x0044A260: ChecksumEquals A=<name> B=<name> (missing: 0).
bool __cdecl CFunc_ChecksumEquals(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t a = 0;
    params->ReadHashValue(kA, &a, 0);
    uint32_t b = 0;
    params->ReadHashValue(kB, &b, 0);
    return a == b;
}

// 0x0044A2C0: StringEquals A="..." B="...": compared by checksum (so case is ignored). Both asserted (missing: the
// original's pointer is undefined - here "").
bool __cdecl CFunc_StringEquals(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* a = "";
    const char* b = "";
    params->FetchText(kA, &a, 1);
    params->FetchText(kB, &b, 1);
    uint32_t checksumB = Checksums::HashFromText(b);
    return Checksums::HashFromText(a) == checksumB;
}

// 0x0044A320: SetScriptString index=<n> (0) string="..." ("").
bool __cdecl CFunc_SetScriptString(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0;
    params->FetchIntValue(kIndex, &index, 0);
    const char* string = "";
    params->FetchText(kString, &string, 0);
    Scripts::SetScriptString(index, string);
    return true;
}

// 0x0044A380: GlobalExists name=<name> [type=<type name>]: whether the global exists with that type (Structure /
// struct, checksum / name, float, array, string, vector, Integer / int, pair; no or another type name: type 0,
// which no global has).
bool __cdecl CFunc_GlobalExists(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, 0);
    uint32_t typeName = 0;
    params->ReadHashValue(kType, &typeName, 0);
    Scripts::EQbValueKind type = TypeFromName(typeName);
    CSymbolTableEntry* entry = Scripts::FindSymbol(name);
    return entry && entry->type == type;
}

// 0x0044A900: EnableScriptPrintf <0 / 1> (1): the scripts' printf on or off.
bool __cdecl CFunc_EnableScriptPrintf(Scripts::CStruct* params, Scripts::CScript*)
{
    int enable = 1;
    params->FetchIntValue(0u, &enable, 0);
    ScriptPrintfEnabled() = enable != 0;
    return true;
}

// 0x0044A930: IfDebugOn / IfDebugOff (one function): compiled out - true.
bool __cdecl CFunc_IfDebugOn(Scripts::CStruct*, Scripts::CScript* script)
{
    SetScriptDebug(script);
    return true;
}

// 0x0044A940: StructureContains Structure={...} <name>: whether the structure has a component or a flag of that
// name. False without a structure.
bool __cdecl CFunc_StructureContains(Scripts::CStruct* params, Scripts::CScript*)
{
    Scripts::CStruct* structure;
    if (!params->ReadSubStruct(kStructure, &structure, 0))
        return false;
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    if (structure->HasEntryNamed(name))
        return true;
    return structure->HasBareFlag(name);
}

// 0x0044A9B0: ClearStruct struct=<global>: the global structure emptied.
bool __cdecl CFunc_ClearStruct(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(kStruct, &name, 1);
    Scripts::ReadSubStruct(name)->Clear();
    return true;
}

// 0x0044A9E0: ScriptExists <name>: whether the name (following names of names) is a script.
bool __cdecl CFunc_ScriptExists(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    CSymbolTableEntry* entry = Scripts::ResolveSymbol(name);
    return entry && entry->type == Scripts::QBVAL_SCRIPT_BODY;
}

// 0x0044AA20: IsArray <value>: whether the unnamed value is an array.
bool __cdecl CFunc_IsArray(Scripts::CStruct* params, Scripts::CScript*)
{
    Scripts::CArray* array = nullptr;
    return params->FetchList(0u, &array, 0);
}

// 0x0044AA50: Block [type=<event> | UntilEvent=<event>]: the script waits until an event of that type (none: until
// unblocked).
bool __cdecl CFunc_Block(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t eventType = 0;
    if (!params->IsVacant()) {
        if (!params->ReadHashValue(kType, &eventType, 0))
            params->ReadHashValue(kUntilEvent, &eventType, 0);
    }
    script->GateOff(eventType);
    return true;
}

// 0x0044AAB0: IsTrue <integer>: whether it is non-zero (missing: false).
bool __cdecl CFunc_IsTrue(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = 0;
    params->FetchIntValue(0u, &value, 0);
    return value != 0;
}

// 0x0044AAE0: CleanUpAndRemoveSymbol <name>: the global freed and taken out of the table. False if there is none.
bool __cdecl CFunc_CleanUpAndRemoveSymbol(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 1);
    CSymbolTableEntry* entry = Scripts::FindSymbol(name);
    if (!entry)
        return false;
    Scripts::CleanUpAndRemoveSymbol(entry);
    return true;
}

// 0x0044AB20: SetEventHandler ex=<event> scr=<script> [Group=<name> (default)] [Exception] [params={...}]: an
// event handler of the calling script.
bool __cdecl CFunc_SetEventHandler(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t eventType = 0;
    params->ReadHashValue(kEx, &eventType, 1);
    uint32_t handler = 0;
    params->ReadHashValue(kScr, &handler, 1);
    uint32_t group = kDefaultGroup;
    params->ReadHashValue(kGroup, &group, 0);
    bool exception = params->HasBareFlag(kException);
    Scripts::CStruct* handlerParams = nullptr;
    params->ReadSubStruct(kParams, &handlerParams, 0);
    script->SetEventHandler(eventType, handler, group, exception, handlerParams);
    return true;
}

// 0x0044ABD0: SetExceptionHandler / SetException (one function): SetEventHandler, always as an exception.
bool __cdecl CFunc_SetExceptionHandler(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t eventType = 0;
    params->ReadHashValue(kEx, &eventType, 1);
    uint32_t handler = 0;
    params->ReadHashValue(kScr, &handler, 1);
    uint32_t group = kDefaultGroup;
    params->ReadHashValue(kGroup, &group, 0);
    Scripts::CStruct* handlerParams = nullptr;
    params->ReadSubStruct(kParams, &handlerParams, 0);
    script->SetEventHandler(eventType, handler, group, true, handlerParams);
    return true;
}

// 0x0044AC70: ResetEventHandlersFromTable <global array> [Group=<name> (default)].
bool __cdecl CFunc_ResetEventHandlersFromTable(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 1);
    Scripts::CArray* table = Scripts::FetchList(name);
    uint32_t group = kDefaultGroup;
    params->ReadHashValue(kGroup, &group, 0);
    script->ResetEventHandlersFromTable(table, group);
    return true;
}

// 0x0044ACD0: ClearEventHandler <event>.
bool __cdecl CFunc_ClearEventHandler(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t eventType = 0;
    params->ReadHashValue(0u, &eventType, 1);
    script->ClearEventHandler(eventType);
    return true;
}

// 0x0044AD00: ClearEventHandlerGroup [<group> (default)].
bool __cdecl CFunc_ClearEventHandlerGroup(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t group = kDefaultGroup;
    params->ReadHashValue(0u, &group, 0);
    script->ClearEventHandlerGroup(group);
    return true;
}

// 0x0044AD30: OnExceptionRun [<script>] (none: 0 - cleared).
bool __cdecl CFunc_OnExceptionRun(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    script->SetOnExceptionScript(name);
    return true;
}

// 0x0044AD60: OnExitRun [<script>] (none: 0 - cleared).
bool __cdecl CFunc_OnExitRun(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, 0);
    script->SetOnExitScript(name);
    return true;
}

// 0x0044AD90: PrintEventHandlerTable.
bool __cdecl CFunc_PrintEventHandlerTable(Scripts::CStruct*, Scripts::CScript* script)
{
    script->PrintEventHandlerTable();
    return true;
}

// 0x0044ADA0: StopAllScripts.
bool __cdecl CFunc_StopAllScripts(Scripts::CStruct*, Scripts::CScript*)
{
    Scripts::StopAllScripts();
    return true;
}

// 0x0044ADB0: FirstTimeThisIsCalled: true the first time only (for the whole game).
bool __cdecl CFunc_FirstTimeThisIsCalled(Scripts::CStruct*, Scripts::CScript*)
{
    if (!FirstTimeNotCalled())
        return false;
    FirstTimeNotCalled() = false;
    return true;
}

// 0x0044ADD0: SetScriptWarningLevel [level=none (default) | warn | assert]: 0 / 1 / 2; another name changes nothing.
bool __cdecl CFunc_SetScriptWarningLevel(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t level = kLevelNone;
    params->ReadHashValue(kLevel, &level, 0);
    if (level == kLevelAssert)
        ScriptWarningLevel() = 2;
    else if (level == kLevelNone)
        ScriptWarningLevel() = 0;
    else if (level == kLevelWarn)
        ScriptWarningLevel() = 1;
    return true;
}

// 0x0044AE30: CastToInteger <name>: the calling script's float parameter of that name made an integer (truncated).
// False if there's no name or no such float.
bool __cdecl CFunc_CastToInteger(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    if (!params->ReadHashValue(0u, &name, 0))
        return false;
    float value = 0.0f;
    if (!script->m_args->FetchFloat(name, &value, 0))
        return false;
    script->m_args->EraseModule(name);
    script->m_args->PutInt(name, Truncate(value));
    return true;
}

// 0x0044AEB0: StringToInteger <name>: the calling script's string parameter of that name made an integer (each
// character taken as a digit: c - '0', no checks). False if there's no name or no such string. (The string is read
// after its component was removed - freed memory, as in the original.)
bool __cdecl CFunc_StringToInteger(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    if (!params->ReadHashValue(0u, &name, 0))
        return false;
    const char* string = "";
    if (!script->m_args->FetchText(name, &string, 0))
        return false;
    script->m_args->EraseModule(name);
    int value = 0;
    for (int length = static_cast<int>(std::strlen(string)); length > 0; --length)
        value = value * 10 + (*string++ - '0');
    script->m_args->PutInt(name, value);
    return true;
}

// 0x0044B720: AddParams {...}: the command's parameters added to the calling script's.
bool __cdecl CFunc_AddParams(Scripts::CStruct* params, Scripts::CScript* script)
{
    script->m_args->AppendStructureIfAny(params);
    return true;
}

// 0x0044B740: EraseModule name=<name> (or the unnamed name): taken out of the calling script's parameters.
bool __cdecl CFunc_RemoveComponent(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0;
    if (!params->ReadHashValue(kName, &name, 0))
        params->ReadHashValue(0u, &name, 1);
    script->m_args->EraseModule(name);
    return true;
}

// 0x0044B790: GetScriptString index=<n> (0): string = the script string.
bool __cdecl CFunc_GetScriptString(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index = 0;
    params->FetchIntValue(kIndex, &index, 0);
    script->m_args->AppendWord(kString, Scripts::GetScriptString(index));
    return true;
}

} // namespace ScriptCommands
