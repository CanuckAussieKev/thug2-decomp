// Script functions about the input (PartyMod's PS2 controls).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/pad_input/pad_cfuncs.h"

#include <cstdint>
#include "engine/script/names.h"
#include "engine/script/script_tables.h"
#include "engine/pad_input/sdl_input.h"
#include "engine/script/cscript.h"

namespace sys {

namespace {

constexpr uint32_t kVehicleGrindScript = 0x47F15883;


} // namespace

bool cfunc_is_ps2(script::Struct* /*params*/, script::Script* script)
{
    if (sio::the_input_settings().ps2_controls_ == 0U) {
        return false; // (the original: 0x005B9E50)
    }
    return (script != nullptr) && script->script_title() == kVehicleGrindScript;
}

void install_is_ps2()
{
    if (script::NativeCommand* entry = script::find_c_function("IsPS2")) {
        script::bind_native_command(*entry, &cfunc_is_ps2);
    }
}

} // namespace sys
