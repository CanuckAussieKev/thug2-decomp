#pragma once

#include <cstdint>

#include "hook/game.h"

// Script::CStruct - a script's parameter structure - and the parts of it script functions (CFuncs) use. Not
// decompiled yet: these call the game's code. Parameter names are checksums of the names used in the scripts;
// 0 means the unnamed (first) parameter.
namespace Script {

class CStruct;
class CScript;

// A script function as the game calls it: its parameters and the calling script; returns true to continue.
using CFunction = bool(__cdecl*)(CStruct* params, CScript* script);

// The string parameter `name`, or false if there isn't one. With `assert`, a missing parameter is an error
// (the game reports it).
inline bool GetString(CStruct* params, uint32_t name, const char** out, bool assert = false)
{
    return GameFunction<bool(__thiscall*)(CStruct*, uint32_t, const char**, bool)>(0x00476C60)(params, name, out,
                                                                                            assert);
}

// The checksum parameter `name` (a name written without quotes in the script), or false if there isn't one.
inline bool GetChecksum(CStruct* params, uint32_t name, uint32_t* out, bool assert = false)
{
    return GameFunction<bool(__thiscall*)(CStruct*, uint32_t, uint32_t*, bool)>(0x00476950)(params, name, out,
                                                                                         assert);
}

// The name a checksum was made from, if the game knows it (for debug output).
inline const char* FindChecksumName(uint32_t checksum)
{
    return GameFunction<const char*(__cdecl*)(uint32_t)>(0x0046D110)(checksum);
}

// Whether the flag `name` (a bare name among the parameters) is present.
inline bool ContainsFlag(CStruct* params, uint32_t name)
{
    return GameFunction<bool(__thiscall*)(CStruct*, uint32_t)>(0x00476B40)(params, name);
}
inline bool ContainsFlag(CStruct* params, const char* name)
{
    return GameFunction<bool(__thiscall*)(CStruct*, const char*)>(0x00476BA0)(params, name);
}

} // namespace Script
