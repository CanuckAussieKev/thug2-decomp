#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's physics control component (SkaterPhysicsControl, type 0xD9151D4D, vtable 0x0064BFE0, 0x30 bytes):
// which physics runs the skater - skating (the core physics, rotate, adjust and finalize physics components) or
// walking (the walk component) - the switches between them (and their cameras), pausing the physics, and the board
// missing / force walk flags. From the disassembly of 0x005176A0-0x00517CB1 (0x005176A0, 0x005176E0-0x00517730,
// 0x005177A0, 0x005177F0, 0x00517A80 and 0x00517AA0 aren't Ghidra functions).
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize, 11 the shared 0x0042BB20, 12 invoke_method_by_name.
namespace objects {

class RiderBoardPhysics final : public ObjectModule {
public:
    // 0x00517830 (registered by register_component_types): a new one (0 if out of memory), no core physics yet.
    static ObjectModule* __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderBoardPhysics();
    // 0x005176A0 (registered with Create): its script member functions (the table at 0x006846F0) registered.
    static void __cdecl publish_method_table();
    // 0x005176C0: the destructor body (the base's).
    ~RiderBoardPhysics() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x005176D0 (vtable +0x08): not paused. `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x00517770 (vtable +0x0C): the core physics and state components found; the state's physics mode (+0x1C) -1.
    void seal() override;
    // 0x00517AC0 (vtable +0x04): while paused the display matrix follows the physics' matrix (skating) or the object's
    // (walking), and the physics is paused again if something resumed it.
    void tick_frame() override;
    // 0x00517C80 (vtable +0x30): SkaterPhysicsControl_SwitchWalkingToSkating / _SwitchSkatingToWalking - 1; 2 for
    // any other name.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x005178B0: the physics paused or not: the core physics, rotate, adjust and finalize physics components
    // (skating) or the walk component (walking) suspended or resumed - not resumed while the object itself is
    // suspended.
    void set_frozen(bool paused);
    // 0x00517940: to walking (nothing if walking): the time switched noted; the skating components and the skater
    // camera suspended (the core physics told first - 0x004FDD50), the state walking (0x00519680), the walk
    // component resumed (told whether the skater was on the ground) and the walk camera, the camera's state handed
    // over; paused again if it was.
    void dismount_to_walk();
    // 0x00517B20: to skating (nothing if skating): the same the other way (the core physics given the walk's
    // ground state and the ladder or bar it's on - 0x0050C670).
    void remount_board();

    // --- script member functions (the table at 0x006846F0; params, script; all true) ---
    // 0x004FC420 (with the skater's core physics): walking - the state's physics mode 1 and not driving.
    bool is_on_foot();
    bool pause_physics(script::Struct* params, script::Script* script);                       // 0x00517A80
    bool un_pause_physics(script::Struct* params, script::Script* script);                    // 0x00517AA0
    bool get_time_since_physics_switch(script::Struct* params, script::Script* script);       // 0x005177A0
    bool get_previous_physics_state_duration(script::Struct* params, script::Script* script); // 0x005177F0
    bool set_board_missing(script::Struct* params, script::Script* script);                   // 0x005176E0
    bool unset_board_missing(script::Struct* params, script::Script* script);                 // 0x005176F0
    bool is_board_missing(script::Struct* params, script::Script* script);                    // 0x00517700
    bool set_force_walk(script::Struct* params, script::Script* script);                      // 0x00517710
    bool un_set_force_walk(script::Struct* params, script::Script* script);                   // 0x00517720
    bool is_force_walk_on(script::Struct* params, script::Script* script);                    // 0x00517730

    bool is_halted_; // +0x18
    uint8_t pad19[3];
    int switch_time_;       // +0x1C the game clock when the physics last switched
    int previous_duration_; // +0x20 how long the physics before it ran (ms)
    bool board_missing_;    // +0x24
    bool force_walk_;       // +0x25
    uint8_t flag26_;        // +0x26 (cleared each update)
    uint8_t pad27;
    uint8_t* core_physics_; // +0x28 SkaterCorePhysics (0x5BD63B29)
    uint8_t* cur_phase_;    // +0x2C SkaterState (0x43686585)
};
static_assert(offsetof(RiderBoardPhysics, is_halted_) == 0x18 && offsetof(RiderBoardPhysics, switch_time_) == 0x1C &&
                  offsetof(RiderBoardPhysics, board_missing_) == 0x24 &&
                  offsetof(RiderBoardPhysics, core_physics_) == 0x28 &&
                  offsetof(RiderBoardPhysics, cur_phase_) == 0x2C && sizeof(RiderBoardPhysics) == 0x30,
              "layout must match the original (0x00517830 allocates 0x30 bytes)");

} // namespace objects
