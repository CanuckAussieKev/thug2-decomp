#pragma once

#include <cstdint>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"

// The parts of a script's parameter structure (Scripts::CStruct) script functions (ScriptCommands) use. Parameter names
// are checksums of the names used in the scripts; 0 means the unnamed (first) parameter.
namespace Scripts {

class CScript;

// A script function as the game calls it: its parameters and the calling script; returns true to continue.
using CFunction = bool(__cdecl*)(CStruct* params, CScript* script);

// The string parameter `name`, or false if there isn't one. With `assert`, a missing parameter is an error
// (the game reports it).
inline bool FetchText(CStruct* params, uint32_t name, const char** out, bool assert = false)
{
    return params->FetchText(name, out, assert);
}

// The checksum parameter `name` (a name written without quotes in the script), or false if there isn't one.
inline bool ReadHashValue(CStruct* params, uint32_t name, uint32_t* out, bool assert = false)
{
    return params->ReadHashValue(name, out, assert);
}

// Whether the flag `name` (a bare name among the parameters) is present.
inline bool HasBareFlag(CStruct* params, uint32_t name)
{
    return params->HasBareFlag(name);
}
inline bool HasBareFlag(CStruct* params, const char* name)
{
    return params->HasBareFlag(name);
}

} // namespace Script
