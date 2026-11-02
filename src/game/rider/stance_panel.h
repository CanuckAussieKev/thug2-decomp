#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace script {
class Script;
class Struct;
} // namespace script

// The skater's stance panel component (SkaterStancePanel, type 0x21FC3301, vtable 0x0064C0F8, 0x24 bytes): the HUD
// sprite showing the skater's stance - regular / goofy, switch, nollie, pressure - and the nollie / pressure flags
// the scripts set (NollieOn / NollieOff / InNollie, PressureOn / PressureOff / InPressure; each change broadcast).
// From the disassembly of 0x00519130-0x00519421 (0x00519150 isn't a Ghidra function).
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize, 12 invoke_method_by_name.
namespace objects {

class RiderStanceDisplay final : public ObjectModule {
public:
    RiderStanceDisplay(const RiderStanceDisplay&) = delete;
    RiderStanceDisplay& operator=(const RiderStanceDisplay&) = delete;
    // 0x005193B0 (registered by register_component_types): a new one (0 if out of memory), no core physics yet.
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderStanceDisplay();
    // 0x00519130: the destructor body (the base's).
    ~RiderStanceDisplay() override;
    // 0x00519140 (vtable +0x08): no stance shown yet. `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x00519210 (vtable +0x0C): the core physics component found.
    void seal() override;
    // 0x00519230 (vtable +0x04): the skater's panel element (0x69751A23 below its skater number's id) shown the
    // stance when it changes: hidden for 5, else the stance's texture (the table at 0x0064C12C) at full alpha.
    void tick_frame() override;
    // 0x005192C0 (vtable +0x30): NollieOn / NollieOff / InNollie, PressureOn / PressureOff / InPressure; 2 for any
    // other name.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x00519150: the stance: with pressure 3 or 4 (flipped or not), else nollie 1 / 2 when flipped, 0 / 5 when not
    // - flipped being the state component's +0x28 (the other way round when the skater's +0x110 is set).
    int get_stance();

    // The fields other code reads and sets.
    void set_shown(int value) { shown_ = value; }

private:
    int shown_{}; // +0x18 the stance shown (-1: none yet)
    uint8_t pad1c_{};
    uint8_t nollie_{};       // +0x1D
    uint8_t press_amount_{}; // +0x1E
    uint8_t pad1f_{};
    skater::SkaterPhysicsModule* core_physics_{nullptr}; // +0x20 SkaterCorePhysics (0x5BD63B29)
};

} // namespace objects
