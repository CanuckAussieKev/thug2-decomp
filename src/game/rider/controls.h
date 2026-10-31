#pragma once

#include <cstdint>

namespace script {
class Struct;
}

// The skater's control code: pieces PartyMod changes for its PS2-style controls (thug2-decomp.ini
// [Miscellaneous] UsePS2Controls). The skater component's layout is only partly known: its fields are reached by offset.
namespace skater {

// The UsePS2Controls setting (sio::the_input_settings, engine/pad_input/sdl_input.h).
bool ps2_controls();

class Component {
public:
    // 0x0051F4C0: from a control script's parameters, the structure for this platform: "xbox" (0xCE42EE1A) on
    // platform 7, 0xCB0E600C on platform 5, else the default (0xE594F0A2, asserted). PC is platform 7; with
    // PartyMod's PS2 controls the Xbox structure is skipped (the 7 becomes a 5). `this` isn't used.
    script::Struct* platform_structure(script::Struct* params);
    // 0x00527510 / 0x00527600: start an acid drop from walking (when the physics finds one,
    // 0x00502240): two buttons held (+0xC0, and +0x120 of the input). PartyMod's PS2 controls drop the first
    // (that's nollie, which centres the camera).
    bool try_walk_acid_drop();
    bool try_acid_drop();
};

} // namespace Skater
