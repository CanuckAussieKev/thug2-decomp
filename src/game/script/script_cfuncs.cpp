// Script commands (CFunctions) for the script system itself, 0x00449D00-0x0044B7E0 (see script_cfuncs.h).

#include "game/script/script_cfuncs.h"

#include <cstdint>

#include "game/core/crc.h"
#include "game/script/cscript.h"
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

} // namespace CFuncs

REPLACE(0x00449D00, CFuncs::CFunc_Change);
REPLACE(0x0044A0D0, CFuncs::CFunc_Wait);
REPLACE(0x0044A200, CFuncs::CFunc_IntegerEquals);
REPLACE(0x0044A260, CFuncs::CFunc_ChecksumEquals);
REPLACE(0x0044A2C0, CFuncs::CFunc_StringEquals);
REPLACE(0x0044A320, CFuncs::CFunc_SetScriptString);
REPLACE(0x0044A380, CFuncs::CFunc_GlobalExists);
