#pragma once

#include <cstdint>

#include "game/script/struct.h"
#include "hook/game.h"

// The parts of a script's parameter structure (Script::CStruct) script functions (CFuncs) use. Parameter names
// are checksums of the names used in the scripts; 0 means the unnamed (first) parameter.
namespace Script {

class CScript;

// A script function as the game calls it: its parameters and the calling script; returns true to continue.
using CFunction = bool(__cdecl*)(CStruct* params, CScript* script);

// The string parameter `name`, or false if there isn't one. With `assert`, a missing parameter is an error
// (the game reports it).
inline bool GetString(CStruct* params, uint32_t name, const char** out, bool assert = false)
{
    return params->GetString(name, out, assert);
}

// The checksum parameter `name` (a name written without quotes in the script), or false if there isn't one.
inline bool GetChecksum(CStruct* params, uint32_t name, uint32_t* out, bool assert = false)
{
    return params->GetChecksum(name, out, assert);
}

// The name a checksum was made from, if the game knows it (for debug output).
inline const char* FindChecksumName(uint32_t checksum)
{
    return GameFunction<const char*(__cdecl*)(uint32_t)>(0x0046D110)(checksum);
}

// Whether the flag `name` (a bare name among the parameters) is present.
inline bool ContainsFlag(CStruct* params, uint32_t name)
{
    return params->ContainsFlag(name);
}
inline bool ContainsFlag(CStruct* params, const char* name)
{
    return params->ContainsFlag(name);
}

} // namespace Script
