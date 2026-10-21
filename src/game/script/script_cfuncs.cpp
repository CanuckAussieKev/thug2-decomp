// Script commands (CFunctions) for the script system itself, 0x00449D00-0x0044B7E0 (see script_cfuncs.h).

#include "game/script/script_cfuncs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CComponent;
using Script::CSymbolTableEntry;

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
bool& FirstTimeNotCalled() { return GameVariable<bool>(0x0067D6D5); }

// Whether the scripts' printf prints (0x0067D6D4).
bool& ScriptPrintfEnabled() { return GameVariable<bool>(0x0067D6D4); }

// The value FormatText is writing (0x00696D2C, a component of its own: its flag bit is kept).
CComponent& FormatTextValue() { return GameVariable<CComponent>(0x00696D2C); }

// IfDebugOn / IfDebugOff's work on the calling script - compiled out of this build (an empty call, 0x005480A0).
inline void SetScriptDebug(Script::CScript* /*script*/) {}

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
void WaitMs(Script::CScript* script, bool realTime, int ms)
{
    if (realTime)
        script->WaitRealTime(ms);
    else
        script->WaitTime(ms);
}

// GlobalExists' type names: the symbol type each stands for (0 for any other name).
Script::ESymbolType TypeFromName(uint32_t name)
{
    switch (name) {
    case 0x90FEC815: // "Structure"
    case 0x456D28D1: // "struct"
        return Script::ESYMBOLTYPE_STRUCTURE;
    case 0x21902065: // "checksum"
    case 0xA1DC81F9: // "name"
        return Script::ESYMBOLTYPE_NAME;
    case 0x365AA16A: // "float"
        return Script::ESYMBOLTYPE_FLOAT;
    case 0x5EF31148: // "array"
        return Script::ESYMBOLTYPE_ARRAY;
    case 0x61414D56: // "string"
        return Script::ESYMBOLTYPE_STRING;
    case 0xE491B7A4: // "vector"
        return Script::ESYMBOLTYPE_VECTOR;
    case 0xA5F054F4: // "Integer"
    case 0xEBAE254E: // "int"
        return Script::ESYMBOLTYPE_INTEGER;
    case 0xF6A5E196: // "pair"
        return Script::ESYMBOLTYPE_PAIR;
    default:
        return Script::ESYMBOLTYPE_NONE;
    }
}

} // namespace

char* FormatTextBuffer() { return &GameVariable<char>(0x00696940); }
int& ScriptWarningLevel() { return GameVariable<int>(0x006F3534); }

// 0x0044A480
int FormatText(Script::CStruct* results, Script::CStruct* params)
{
    uint32_t textName = 0;
    params->GetChecksum(kTextName, &textName, 0);
    uint32_t checksumName = 0;
    params->GetChecksum(kChecksumName, &checksumName, 0);
    int result = 1;
    const char* format = "";
    if (!params->GetString(0u, &format, 0)) {
        Dbg::Printf("Expected text format parameters to contain a format string\n");
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
        CComponent* found = params->FindLastComponent(Crc::GenerateCRCFromString(name));
        if (!found) {
            Dbg::Printf("Missing text formatting parameter '%s'\n", name);
            return 0;
        }
        CComponent& value = FormatTextValue();
        value.typeAndFlag = static_cast<uint8_t>((found->typeAndFlag & ~1) | (value.typeAndFlag & 1));
        value.raw = found->raw;
        Script::ResolveNameComponent(&value);
        char text[100]; // (the original's sprintf into it is unbounded)
        switch (value.Type()) {
        case Script::ESYMBOLTYPE_INTEGER:
            if (params->ContainsFlag(kUseCommas)) {
                std::snprintf(text, sizeof(text), "%s", Str::FormatWithSeparators(value.integer));
            } else {
                int width = 0;
                if (params->GetInteger(kIntegerWidth, &width, 0))
                    std::snprintf(text, sizeof(text), "%0*d", width, value.integer);
                else
                    std::snprintf(text, sizeof(text), "%d", value.integer);
            }
            out = Append(out, text);
            break;
        case Script::ESYMBOLTYPE_FLOAT: {
            int places = 3;
            params->GetInteger(kDecimalPlaces, &places, 0);
            char floatFormat[16];
            std::snprintf(floatFormat, sizeof(floatFormat), "%%.%df", places);
            std::snprintf(text, sizeof(text), floatFormat, static_cast<double>(value.floatValue));
            out = Append(out, text);
            break;
        }
        case Script::ESYMBOLTYPE_STRING:
        case Script::ESYMBOLTYPE_LOCALSTRING:
            for (const char* c = value.string; *c; ++c)
                *out++ = *c;
            break;
        case Script::ESYMBOLTYPE_PAIR:
            std::snprintf(text, sizeof(text), "(%.3f,%.3f)", static_cast<double>(value.pair->x),
                          static_cast<double>(value.pair->y));
            out = Append(out, text);
            break;
        case Script::ESYMBOLTYPE_VECTOR:
            std::snprintf(text, sizeof(text), "(%.3f,%.3f,%.3f)", static_cast<double>(value.vector->x),
                         static_cast<double>(value.vector->y), static_cast<double>(value.vector->z));
            out = Append(out, text);
            break;
        case Script::ESYMBOLTYPE_NAME:
            result = 2;
            std::snprintf(text, sizeof(text), "%s", Script::FindChecksumNameOrUnknown(value.checksum));
            out = Append(out, text);
            break;
        default:
            break;
        }
    }
    *out = '\0';
    if (textName)
        results->AddString(textName, FormatTextBuffer());
    if (checksumName)
        results->AddChecksum(checksumName, Crc::GenerateCRCFromString(FormatTextBuffer()));
    return result;
}

// 0x00449D00: Change <name>=<value> (or globalname=<name> newValue=<value>) [resolve]: the global is made again
// with the new value (its +0x08 word kept). Integers, floats and names are stored as they are, structures and arrays
// copied; with `resolve`, a name standing for another global takes that global's value. Any other type leaves the
// global made again but empty. False if the name isn't a global, or is a script or a C / member function.
bool __cdecl CFunc_Change(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(kGlobalName, &name, 0);
    CComponent* value;
    if (name) {
        value = params->FindComponent(kNewValue);
        if (!value)
            return false;
    } else {
        value = params->GetNextComponent(nullptr);
        if (!value)
            return false;
        name = value->name;
    }
    CSymbolTableEntry* old = Script::FindSymbol(name);
    if (!old || old->type == Script::ESYMBOLTYPE_QSCRIPT || old->type == Script::ESYMBOLTYPE_CFUNCTION ||
        old->type == Script::ESYMBOLTYPE_MEMBERFUNCTION)
        return false;
    uint32_t kept = old->unknown8;
    Script::CleanUpAndRemoveSymbol(old);
    CSymbolTableEntry* entry = Script::CreateSymbol(name);
    entry->unknown8 = kept;

    // the value in a component of its own (the new component's flag bit kept)
    CComponent copy;
    Script::ConstructComponent(&copy);
    copy.typeAndFlag = static_cast<uint8_t>((value->typeAndFlag & ~1) | (copy.typeAndFlag & 1));
    copy.raw = value->raw;
    if (copy.Type() == Script::ESYMBOLTYPE_NAME && params->ContainsFlag(kResolve)) {
        if (CSymbolTableEntry* resolved = Script::LookUpSymbol(copy.checksum)) {
            copy.typeAndFlag = static_cast<uint8_t>((resolved->type << 1) | (copy.typeAndFlag & 1));
            copy.raw = resolved->raw;
        }
    }
    switch (copy.Type()) {
    case Script::ESYMBOLTYPE_INTEGER:
    case Script::ESYMBOLTYPE_FLOAT:
    case Script::ESYMBOLTYPE_NAME:
        entry->type = copy.Type();
        entry->raw = copy.raw;
        break;
    case Script::ESYMBOLTYPE_STRUCTURE: {
        entry->type = Script::ESYMBOLTYPE_STRUCTURE;
        Script::CStruct* memory = Script::NewStruct();
        Script::CStruct* structure = memory ? memory->Init() : nullptr;
        entry->structure = structure;
        structure->AppendStructureIfAny(copy.structure);
        break;
    }
    case Script::ESYMBOLTYPE_ARRAY: {
        entry->type = Script::ESYMBOLTYPE_ARRAY;
        Script::CArray* memory = Script::NewArray();
        Script::CArray* array = memory ? memory->Init() : nullptr;
        entry->raw = reinterpret_cast<uint32_t>(array);
        Script::CopyArray(array, copy.array);
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
bool __cdecl CFunc_Wait(Script::CStruct* params, Script::CScript* script)
{
    float value = 0.0f;
    params->GetFloat(0u, &value, 0);
    bool realTime = params->ContainsFlag(kRealTime);
    if (params->ContainsFlag(kSeconds) || params->ContainsFlag(kSecond)) {
        WaitMs(script, realTime, Truncate(static_cast<double>(value) * GameVariable<const float>(0x006453C0))); // 1000
    } else if (params->ContainsFlag(kGameFrame) || params->ContainsFlag(kGameFrames) || params->ContainsFlag(kGame)) {
        script->Wait(Truncate(value));
    } else if (params->ContainsFlag(kFrame) || params->ContainsFlag(kFrames)) {
        WaitMs(script, realTime, Truncate(static_cast<double>(value) * GameVariable<const float>(0x006460DC))); // 16.67
    } else if (params->ContainsFlag(kOnePerFrame)) {
        script->WaitOnePerFrame(Truncate(value));
    } else {
        WaitMs(script, realTime, Truncate(value));
    }
    return true;
}

// 0x0044A200: IntegerEquals A=<int> B=<int> (missing: 0).
bool __cdecl CFunc_IntegerEquals(Script::CStruct* params, Script::CScript*)
{
    int a = 0;
    params->GetInteger(kA, &a, 0);
    int b = 0;
    params->GetInteger(kB, &b, 0);
    return a == b;
}

// 0x0044A260: ChecksumEquals A=<name> B=<name> (missing: 0).
bool __cdecl CFunc_ChecksumEquals(Script::CStruct* params, Script::CScript*)
{
    uint32_t a = 0;
    params->GetChecksum(kA, &a, 0);
    uint32_t b = 0;
    params->GetChecksum(kB, &b, 0);
    return a == b;
}

// 0x0044A2C0: StringEquals A="..." B="...": compared by checksum (so case is ignored). Both asserted (missing: the
// original's pointer is undefined - here "").
bool __cdecl CFunc_StringEquals(Script::CStruct* params, Script::CScript*)
{
    const char* a = "";
    const char* b = "";
    params->GetString(kA, &a, 1);
    params->GetString(kB, &b, 1);
    uint32_t checksumB = Crc::GenerateCRCFromString(b);
    return Crc::GenerateCRCFromString(a) == checksumB;
}

// 0x0044A320: SetScriptString index=<n> (0) string="..." ("").
bool __cdecl CFunc_SetScriptString(Script::CStruct* params, Script::CScript*)
{
    int index = 0;
    params->GetInteger(kIndex, &index, 0);
    const char* string = "";
    params->GetString(kString, &string, 0);
    Script::SetScriptString(index, string);
    return true;
}

// 0x0044A380: GlobalExists name=<name> [type=<type name>]: whether the global exists with that type (Structure /
// struct, checksum / name, float, array, string, vector, Integer / int, pair; no or another type name: type 0,
// which no global has).
bool __cdecl CFunc_GlobalExists(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(kName, &name, 0);
    uint32_t typeName = 0;
    params->GetChecksum(kType, &typeName, 0);
    Script::ESymbolType type = TypeFromName(typeName);
    CSymbolTableEntry* entry = Script::FindSymbol(name);
    return entry && entry->type == type;
}

// 0x0044A900: EnableScriptPrintf <0 / 1> (1): the scripts' printf on or off.
bool __cdecl CFunc_EnableScriptPrintf(Script::CStruct* params, Script::CScript*)
{
    int enable = 1;
    params->GetInteger(0u, &enable, 0);
    ScriptPrintfEnabled() = enable != 0;
    return true;
}

// 0x0044A930: IfDebugOn / IfDebugOff (one function): compiled out - true.
bool __cdecl CFunc_IfDebugOn(Script::CStruct*, Script::CScript* script)
{
    SetScriptDebug(script);
    return true;
}

// 0x0044A940: StructureContains Structure={...} <name>: whether the structure has a component or a flag of that
// name. False without a structure.
bool __cdecl CFunc_StructureContains(Script::CStruct* params, Script::CScript*)
{
    Script::CStruct* structure;
    if (!params->GetStructure(kStructure, &structure, 0))
        return false;
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    if (structure->ContainsComponentNamed(name))
        return true;
    return structure->ContainsFlag(name);
}

// 0x0044A9B0: ClearStruct struct=<global>: the global structure emptied.
bool __cdecl CFunc_ClearStruct(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(kStruct, &name, 1);
    Script::GetStructure(name)->Clear();
    return true;
}

// 0x0044A9E0: ScriptExists <name>: whether the name (following names of names) is a script.
bool __cdecl CFunc_ScriptExists(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    CSymbolTableEntry* entry = Script::LookUpSymbol(name);
    return entry && entry->type == Script::ESYMBOLTYPE_QSCRIPT;
}

// 0x0044AA20: IsArray <value>: whether the unnamed value is an array.
bool __cdecl CFunc_IsArray(Script::CStruct* params, Script::CScript*)
{
    Script::CArray* array = nullptr;
    return params->GetArray(0u, &array, 0);
}

// 0x0044AA50: Block [type=<event> | UntilEvent=<event>]: the script waits until an event of that type (none: until
// unblocked).
bool __cdecl CFunc_Block(Script::CStruct* params, Script::CScript* script)
{
    uint32_t eventType = 0;
    if (!params->IsEmpty()) {
        if (!params->GetChecksum(kType, &eventType, 0))
            params->GetChecksum(kUntilEvent, &eventType, 0);
    }
    script->Block(eventType);
    return true;
}

// 0x0044AAB0: IsTrue <integer>: whether it is non-zero (missing: false).
bool __cdecl CFunc_IsTrue(Script::CStruct* params, Script::CScript*)
{
    int value = 0;
    params->GetInteger(0u, &value, 0);
    return value != 0;
}

// 0x0044AAE0: CleanUpAndRemoveSymbol <name>: the global freed and taken out of the table. False if there is none.
bool __cdecl CFunc_CleanUpAndRemoveSymbol(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 1);
    CSymbolTableEntry* entry = Script::FindSymbol(name);
    if (!entry)
        return false;
    Script::CleanUpAndRemoveSymbol(entry);
    return true;
}

// 0x0044AB20: SetEventHandler ex=<event> scr=<script> [Group=<name> (default)] [Exception] [params={...}]: an
// event handler of the calling script.
bool __cdecl CFunc_SetEventHandler(Script::CStruct* params, Script::CScript* script)
{
    uint32_t eventType = 0;
    params->GetChecksum(kEx, &eventType, 1);
    uint32_t handler = 0;
    params->GetChecksum(kScr, &handler, 1);
    uint32_t group = kDefaultGroup;
    params->GetChecksum(kGroup, &group, 0);
    bool exception = params->ContainsFlag(kException);
    Script::CStruct* handlerParams = nullptr;
    params->GetStructure(kParams, &handlerParams, 0);
    script->SetEventHandler(eventType, handler, group, exception, handlerParams);
    return true;
}

// 0x0044ABD0: SetExceptionHandler / SetException (one function): SetEventHandler, always as an exception.
bool __cdecl CFunc_SetExceptionHandler(Script::CStruct* params, Script::CScript* script)
{
    uint32_t eventType = 0;
    params->GetChecksum(kEx, &eventType, 1);
    uint32_t handler = 0;
    params->GetChecksum(kScr, &handler, 1);
    uint32_t group = kDefaultGroup;
    params->GetChecksum(kGroup, &group, 0);
    Script::CStruct* handlerParams = nullptr;
    params->GetStructure(kParams, &handlerParams, 0);
    script->SetEventHandler(eventType, handler, group, true, handlerParams);
    return true;
}

// 0x0044AC70: ResetEventHandlersFromTable <global array> [Group=<name> (default)].
bool __cdecl CFunc_ResetEventHandlersFromTable(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 1);
    Script::CArray* table = Script::GetArray(name);
    uint32_t group = kDefaultGroup;
    params->GetChecksum(kGroup, &group, 0);
    script->ResetEventHandlersFromTable(table, group);
    return true;
}

// 0x0044ACD0: ClearEventHandler <event>.
bool __cdecl CFunc_ClearEventHandler(Script::CStruct* params, Script::CScript* script)
{
    uint32_t eventType = 0;
    params->GetChecksum(0u, &eventType, 1);
    script->ClearEventHandler(eventType);
    return true;
}

// 0x0044AD00: ClearEventHandlerGroup [<group> (default)].
bool __cdecl CFunc_ClearEventHandlerGroup(Script::CStruct* params, Script::CScript* script)
{
    uint32_t group = kDefaultGroup;
    params->GetChecksum(0u, &group, 0);
    script->ClearEventHandlerGroup(group);
    return true;
}

// 0x0044AD30: OnExceptionRun [<script>] (none: 0 - cleared).
bool __cdecl CFunc_OnExceptionRun(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    script->SetOnExceptionScript(name);
    return true;
}

// 0x0044AD60: OnExitRun [<script>] (none: 0 - cleared).
bool __cdecl CFunc_OnExitRun(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    script->SetOnExitScript(name);
    return true;
}

// 0x0044AD90: PrintEventHandlerTable.
bool __cdecl CFunc_PrintEventHandlerTable(Script::CStruct*, Script::CScript* script)
{
    script->PrintEventHandlerTable();
    return true;
}

// 0x0044ADA0: StopAllScripts.
bool __cdecl CFunc_StopAllScripts(Script::CStruct*, Script::CScript*)
{
    Script::StopAllScripts();
    return true;
}

// 0x0044ADB0: FirstTimeThisIsCalled: true the first time only (for the whole game).
bool __cdecl CFunc_FirstTimeThisIsCalled(Script::CStruct*, Script::CScript*)
{
    if (!FirstTimeNotCalled())
        return false;
    FirstTimeNotCalled() = false;
    return true;
}

// 0x0044ADD0: SetScriptWarningLevel [level=none (default) | warn | assert]: 0 / 1 / 2; another name changes nothing.
bool __cdecl CFunc_SetScriptWarningLevel(Script::CStruct* params, Script::CScript*)
{
    uint32_t level = kLevelNone;
    params->GetChecksum(kLevel, &level, 0);
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
bool __cdecl CFunc_CastToInteger(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    if (!params->GetChecksum(0u, &name, 0))
        return false;
    float value = 0.0f;
    if (!script->m_params->GetFloat(name, &value, 0))
        return false;
    script->m_params->RemoveComponent(name);
    script->m_params->AddInteger(name, Truncate(value));
    return true;
}

// 0x0044AEB0: StringToInteger <name>: the calling script's string parameter of that name made an integer (each
// character taken as a digit: c - '0', no checks). False if there's no name or no such string. (The string is read
// after its component was removed - freed memory, as in the original.)
bool __cdecl CFunc_StringToInteger(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    if (!params->GetChecksum(0u, &name, 0))
        return false;
    const char* string = "";
    if (!script->m_params->GetString(name, &string, 0))
        return false;
    script->m_params->RemoveComponent(name);
    int value = 0;
    for (int length = static_cast<int>(std::strlen(string)); length > 0; --length)
        value = value * 10 + (*string++ - '0');
    script->m_params->AddInteger(name, value);
    return true;
}

// 0x0044B720: AddParams {...}: the command's parameters added to the calling script's.
bool __cdecl CFunc_AddParams(Script::CStruct* params, Script::CScript* script)
{
    script->m_params->AppendStructureIfAny(params);
    return true;
}

// 0x0044B740: RemoveComponent name=<name> (or the unnamed name): taken out of the calling script's parameters.
bool __cdecl CFunc_RemoveComponent(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    if (!params->GetChecksum(kName, &name, 0))
        params->GetChecksum(0u, &name, 1);
    script->m_params->RemoveComponent(name);
    return true;
}

// 0x0044B790: GetScriptString index=<n> (0): string = the script string.
bool __cdecl CFunc_GetScriptString(Script::CStruct* params, Script::CScript* script)
{
    int index = 0;
    params->GetInteger(kIndex, &index, 0);
    script->m_params->AddString(kString, Script::GetScriptString(index));
    return true;
}

} // namespace CFuncs

REPLACE(0x00449D00, CFuncs::CFunc_Change);
REPLACE(0x0044A0D0, CFuncs::CFunc_Wait);
REPLACE(0x0044A200, CFuncs::CFunc_IntegerEquals);
REPLACE(0x0044A260, CFuncs::CFunc_ChecksumEquals);
REPLACE(0x0044A2C0, CFuncs::CFunc_StringEquals);
REPLACE(0x0044A320, CFuncs::CFunc_SetScriptString);
REPLACE(0x0044A380, CFuncs::CFunc_GlobalExists);
REPLACE(0x0044A480, CFuncs::FormatText);
REPLACE(0x0044A900, CFuncs::CFunc_EnableScriptPrintf);
REPLACE(0x0044A930, CFuncs::CFunc_IfDebugOn);
REPLACE(0x0044A940, CFuncs::CFunc_StructureContains);
REPLACE(0x0044A9B0, CFuncs::CFunc_ClearStruct);
REPLACE(0x0044A9E0, CFuncs::CFunc_ScriptExists);
REPLACE(0x0044AA20, CFuncs::CFunc_IsArray);
REPLACE(0x0044AA50, CFuncs::CFunc_Block);
REPLACE(0x0044AAB0, CFuncs::CFunc_IsTrue);
REPLACE(0x0044AAE0, CFuncs::CFunc_CleanUpAndRemoveSymbol);
REPLACE(0x0044AB20, CFuncs::CFunc_SetEventHandler);
REPLACE(0x0044ABD0, CFuncs::CFunc_SetExceptionHandler);
REPLACE(0x0044AC70, CFuncs::CFunc_ResetEventHandlersFromTable);
REPLACE(0x0044ACD0, CFuncs::CFunc_ClearEventHandler);
REPLACE(0x0044AD00, CFuncs::CFunc_ClearEventHandlerGroup);
REPLACE(0x0044AD30, CFuncs::CFunc_OnExceptionRun);
REPLACE(0x0044AD60, CFuncs::CFunc_OnExitRun);
REPLACE(0x0044AD90, CFuncs::CFunc_PrintEventHandlerTable);
REPLACE(0x0044ADA0, CFuncs::CFunc_StopAllScripts);
REPLACE(0x0044ADB0, CFuncs::CFunc_FirstTimeThisIsCalled);
REPLACE(0x0044ADD0, CFuncs::CFunc_SetScriptWarningLevel);
REPLACE(0x0044AE30, CFuncs::CFunc_CastToInteger);
REPLACE(0x0044AEB0, CFuncs::CFunc_StringToInteger);
REPLACE(0x0044B720, CFuncs::CFunc_AddParams);
REPLACE(0x0044B740, CFuncs::CFunc_RemoveComponent);
REPLACE(0x0044B790, CFuncs::CFunc_GetScriptString);
