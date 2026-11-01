// Script functions about the platform.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/platform_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/script/script_tables.h"
#include "engine/pad_input/sdl_input.h"
#include "engine/script/cscript.h"
#include "platform/code_address.h"

namespace sys {

namespace {

constexpr uint32_t kVehicleGrindScript = 0x47F15883;

// The script's name (the one it's running now: CScript +0xC8).
uint32_t get_script_name(const script::Script* script) { return script->script_title_; }

} // namespace

bool cfunc_is_ps2(script::Struct* /*params*/, script::Script* script)
{
    if (!sio::the_input_settings().ps2_controls_)
        return false; // (the original: 0x005B9E50)
    return script && get_script_name(script) == kVehicleGrindScript;
}

void install_is_ps2()
{
    if (script::NativeCommand* entry = script::find_c_function("IsPS2"))
        script::bind_native_command(*entry, code_address(&cfunc_is_ps2));
}

} // namespace sys
