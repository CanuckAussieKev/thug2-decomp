// Script functions about the platform.
// Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt

#include "game/sys/platform_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/sio/sdl_input.h"
#include "hook/hook.h"

namespace Sys {

namespace {

constexpr uint32_t kVehicleGrindScript = 0x47F15883;
// The table's IsPS2 entry: {name 0x006524FC "IsPS2", function}.
constexpr uint32_t kIsPS2Entry = 0x00681468;

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
    if (std::strcmp(*reinterpret_cast<const char* const*>(kIsPS2Entry), "IsPS2") != 0)
        return; // (not this exe's table)
    auto function = reinterpret_cast<uint32_t>(&CFunc_IsPS2);
    WriteGameCode(kIsPS2Entry + 4, &function, sizeof(function));
}

} // namespace Sys
