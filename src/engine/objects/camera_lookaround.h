#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class PadInputModule;

// The CameraLookAround component (type 0x81C7122D, vtable 0x0064B28C, 0x44 bytes; made by 0x004E42E0): the
// camera's look-around angles from the pad's right stick (heading and tilt eased toward the stick each frame), or
// turned over a time to a heading and tilt a script asks for.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize, 5 0x004E3F90 (the base
// classes' "refresh from a structure": configure_from_data again), the base's Suspend and invoke_method_by_name by
// entry, 12 invoke_method_by_name.
class FreeLookModule final : public ObjectModule {
public:
    FreeLookModule(const FreeLookModule&) = delete;
    FreeLookModule& operator=(const FreeLookModule&) = delete;
    // 0x004E42E0: a new one (0 if out of memory): the base's constructor, its vtable and type.
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    FreeLookModule();
    // 0x004E3F60: the destructor body (the base's).
    ~FreeLookModule() override;
    // 0x004E3F70 (slot 2): looking ahead, not locked, nothing in progress, the factor 1. `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x004E4040 (slot 3): the object's Input component.
    void seal() override;
    // 0x004E4060 (slot 1): the pad (the second one's state when the global 0x702247A5 is set): unless the
    // global 0xF3E055E1 is set, its button at +0x1F0 runs 0x60871393 (for the object 0x967C138C) or 0xA1B1146D;
    // the button at +0x130 locks / unlocks the angles. Unlocked and not turning: the tilt eased toward the stick's
    // +0x214 (x 0.7 below 0, else x 0.5; then x -1.4) and the heading toward its +0x210 x 3, at 3.75 x their
    // distance a second (set outright within 0.001). Turning: by the rates until the time is up, then exactly to
    // the targets.
    void tick_frame() override;
    // 0x004E4220 (slot 12): 0xB8F23899 turns to heading=, tilt= over time= (floats, asserted) with the factor
    // 0x48D4868B (1 by default); 0xE3B327C0 stops turning. 2 for anything else. (NOT ORIGINAL, the feature on:
    // THAW's LookaroundOnDPad_On / _Off too.)
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004E3FA0 (not a Ghidra function): turn to `heading` and `tilt` over `time` (straight away without one).
    void turn_to(float heading, float tilt, float time, float factor);
    // 0x004E4000: lock (or unlock) the angles where they are.
    void set_locked(bool locked);

    bool locked_{};  // +0x18
    bool turning_{}; // +0x19
    // NOT ORIGINAL - THAW's BMX (game/rider/thaw_bmx.h): THAW's look-around has this flag (its +0x40, set by its
    // members LookaroundOnDPad_On / _Off): the d-pad turns the camera, the right stick isn't read. Only while the
    // feature is on; THUG2's component never sets nor reads these bytes.
    bool dpad_look_{}; // +0x1A
    uint8_t pad1b_{};
    float factor_{};            // +0x1C
    float lean_angle_{};        // +0x20
    float heading_{};           // +0x24
    float mode_heading_{};      // +0x28 the skater camera's mode's heading (its change_regime; its frame adds heading_)
    float target_tilt_{};       // +0x2C
    float target_heading_{};    // +0x30
    float tilt_rate_{};         // +0x34 per second
    float heading_rate_{};      // +0x38
    float seconds_remaining_{}; // +0x3C
    PadInputModule* input_ptr_{}; // +0x40 the Input component
};
static_assert(offsetof(FreeLookModule, locked_) == 0x18 && offsetof(FreeLookModule, factor_) == 0x1C &&
                  offsetof(FreeLookModule, target_tilt_) == 0x2C && offsetof(FreeLookModule, input_ptr_) == 0x40 &&
                  sizeof(FreeLookModule) == 0x44,
              "layout must match the original");

} // namespace objects
