// Script functions about the platform.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/sys/platform_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/script/script_tables.h"
#include "game/sio/sdl_input.h"
#include "hook/hook.h"

namespace Sys {

namespace {

constexpr uint32_t kVehicleGrindScript = 0x47F15883;

// The script's name (the one it's running now: CScript +0xC8).
uint32_t ScriptName(const Script::CScript* script)
{
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(script) + 0xC8);
}

} // namespace

bool __cdecl CFunc_IsPS2(Script::CStruct* /*params*/, Script::CScript* script)
{
    if (!Sio::TheInputSettings().ps2Controls)
        return false; // (the original: 0x005B9E50)
    return script && ScriptName(script) == kVehicleGrindScript;
}

void InstallIsPS2()
{
    if (Script::CFunctionEntry* entry = Script::FindCFunction("IsPS2"))
        Script::SetCFunction(*entry, reinterpret_cast<void*>(&CFunc_IsPS2));
}

} // namespace Sys
