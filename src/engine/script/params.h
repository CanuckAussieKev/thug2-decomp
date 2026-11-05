#pragma once

#include <cstdint>

#include "engine/script/names.h"
#include "engine/script/param_struct.h"

// The parts of a script's parameter structure (Scripts::CStruct) script functions (commands) use. Parameter names
// are checksums of the names used in the scripts; 0 means the unnamed (first) parameter.
namespace script {

class Script;

// A script function as the game calls it: its parameters and the calling script; returns true to continue.
using Function = bool (*)(Struct* params, Script* script);

// The string parameter `name`, or false if there isn't one. With `assert`, a missing parameter is an error
// (the game reports it).
inline bool fetch_text(Struct* params, uint32_t name, const char** out, bool assert = false)
{
    return params->fetch_text(name, out, static_cast<int>(assert));
}

// The checksum parameter `name` (a name written without quotes in the script), or false if there isn't one.
inline bool read_hash_value(Struct* params, uint32_t name, uint32_t* out, bool assert = false)
{
    return params->read_hash_value(name, out, static_cast<int>(assert));
}

// Whether the flag `name` (a bare name among the parameters) is present.
inline bool has_bare_flag(Struct* params, uint32_t name) { return params->has_bare_flag(name); }
inline bool has_bare_flag(Struct* params, const char* name) { return params->has_bare_flag(name); }

} // namespace script
