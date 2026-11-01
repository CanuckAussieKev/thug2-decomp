#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's sound component (SkaterSound, type 0x187574FA, vtable 0x0064C0C4, 0x28 bytes): the one-shot sounds of
// the skater's terrain - jump, land, bonk, cess (and on a rail grind / slide jump and land) - played at the skater
// with a volume by the speed. The finalize physics component keeps its terrain and slide flag up to date. From the
// disassembly of 0x00518C80-0x00519129 (0x00518EA0-0x00518FA0 aren't Ghidra functions).
//
// Vtable: 0 Destroy, 1 the shared empty 0x0051B970, 2 configure_from_data, 12 invoke_method_by_name.
namespace objects {

class RiderSfxPlayer final : public ObjectModule {
public:
    // 0x00518E20 (registered by register_component_types): a new one (0 if out of memory): full speed 1, volume_mult 1,
    // not sliding.
    static memory::owner<ObjectModule*> __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderSfxPlayer();
    // 0x00518C80: the destructor body (the base's).
    ~RiderSfxPlayer() override;
    // 0x00518CC0 (vtable +0x08): volume_mult.
    void configure_from_data(script::Struct* params) override;
    // 0x00518FD0 (vtable +0x30): PlayJumpSound / PlayLandSound (on the ground or in the air, or on a rail: the speed
    // - |vy| for a landing on the ground -, else nothing), PlayBonkSound, PlayCessSound (the speed) - 1; 2 for any
    // other name.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // `speed` (a fraction of full speed) as a percentage: the volume times volume_mult; the sound of `terrain` in
    // the state's table played at the skater:
    // 0x00518CF0: a landing (TABLE_LAND; on a rail TABLE_GRINDLAND / TABLE_SLIDELAND).
    void play_land(float speed, int terrain);
    // 0x00518D60: a jump (TABLE_JUMP; in `state` 4, a rail, TABLE_GRINDJUMP / TABLE_SLIDEJUMP).
    void play_jump(float speed, int terrain, int state);
    // 0x00518DD0: a bonk (TABLE_BONK).
    void play_bonk(float speed, int terrain);
    // 0x00518EA0 / 0x00518FA0: a landing / jump on the terrain now (on the ground or in the air last_ground_).
    void play_land_here(float speed);
    void play_jump_here(float speed);
    // 0x00518EE0: a jump in this state.
    void play_jump_in_state(float speed, int terrain);
    // 0x00518F00 / 0x00518F50: a bonk / a cess (TABLE_CESS) on the terrain now.
    void play_bonk_here(float speed);
    void play_cess_here(float speed);

    int cur_phase_;        // +0x18 the skater's state (0 ground, 1 air, 4 rail)
    uint8_t surface_type_; // +0x1C
    uint8_t last_ground_;  // +0x1D the terrain the ground and air sounds use
    uint8_t sliding_;      // +0x1E on a rail: a slide, not a grind
    uint8_t pad1f_;
    float full_speed_;  // +0x20 the speed that counts as 1
    float volume_mult_; // +0x24 volume_mult
};

} // namespace objects
