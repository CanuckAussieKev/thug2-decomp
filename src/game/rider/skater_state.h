#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Script;
class Struct;
} // namespace Script

// The skater's state component (SkaterState, type 0x43686585, vtable 0x0064C140, 0x128 bytes): where the skater is
// (on the ground, in the air, on a wall / lip / rail, in a wallplant), whether it skates, walks or drives, its
// terrain, the powerups it picked up and where its sparks come from - read by the physics components and the scripts
// (OnGround ... GetSparksPos), each change of state, physics or terrain sent to the skater as an event. Most of its
// fields are written by the core physics code (not rewritten yet). From the disassembly of 0x00519430-0x005197B4
// (0x00519430-0x00519600, 0x00519650 and 0x005196D0-0x00519720 aren't Ghidra functions). Also here: three
// functions of the skater's state history (0x005197C0-0x005197EF).
//
// Vtable: 0 Destroy, 1 the shared empty 0x0051B970, 2 configure_from_data, 11 the shared 0x0042BB20, 12 the shared
// invoke_method_by_name 0x0051ADC0.
namespace objects {

class RiderPoseState : public ObjectModule {
public:
    // The skater's state (+0x18).
    enum State : int { kGround = 0, kAir = 1, kWall = 2, kLip = 3, kRail = 4, kWallplant = 5 };
    // The physics running it (+0x1C).
    enum PhysicsMode : int { kSkating = 0, kWalking = 1 };

    // 0x00519740 (registered by register_component_types): a new one (0 if out of memory), no powerups.
    static ObjectModule* __cdecl fabricate();
    // 0x00519430 (registered with Create): its script member functions (the table at 0x00684A80) registered.
    static void __cdecl publish_method_table();
    // 0x00519450: the destructor body (the base's).
    void destroy_body();
    // 0x00519620 (vtable slot 0)
    RiderPoseState* teardown(unsigned int flags);
    // 0x00519460 (vtable +0x08): in the air. `params` isn't read.
    void configure_from_data(script::Struct* params);

    // 0x00519660 / 0x00519680 / 0x005196A0: the state / the physics mode / the terrain; when it changes the skater
    // gets OnSkaterStateChange / OnSkaterPhysicsStateChange / OnSkaterTerrainChange.
    void change_phase(int state);
    void set_physics_mode(int mode);
    void set_surface_kind(uint8_t terrain);
    // 0x00519470: the powerups dropped (called by a script command).
    void clear_powerups();
    // 0x00519650: +0xE0 (doingtrick) cleared (called by the skater).
    void clear_doing_trick();

    // --- script member functions (the table at 0x00684A80; params, script) ---
    // (OnGround is 0x00525E90's, not here)
    bool in_air(script::Struct* params, script::Script* script);           // 0x00519480
    bool get_on_wall(script::Struct* params, script::Script* script);          // 0x00519490
    bool on_lip(script::Struct* params, script::Script* script);           // 0x005194A0
    bool get_on_rail(script::Struct* params, script::Script* script);          // 0x005194B0
    bool in_wallplant(script::Struct* params, script::Script* script);     // 0x005194C0
    bool in_bail_state(script::Struct* params, script::Script* script);     // 0x005196D0 (+0x70)
    bool on_foot(script::Struct* params, script::Script* script);         // 0x005194D0 (not driving)
    bool skating(script::Struct* params, script::Script* script);         // 0x005194F0 (not driving)
    bool get_driving(script::Struct* params, script::Script* script);         // 0x00519510
    bool set_driving(script::Struct* params, script::Script* script);      // 0x00519520
    bool unset_driving(script::Struct* params, script::Script* script);    // 0x00519530
    bool in_air_move(script::Struct* params, script::Script* script);      // 0x005196E0 (+0xE0)
    // 0x00519540 / 0x005195B0: the powerup the unnamed checksum names (the one in the table at 0x0067F09C) - picked
    // up or not / picked up. Unknown names: false / nothing.
    bool has_powerup(script::Struct* params, script::Script* script);
    bool picked_up_powerup(script::Struct* params, script::Script* script);
    bool get_surface_kind(script::Struct* params, script::Script* script);      // 0x005196F0 (terrain)
    bool get_sparks_pos(script::Struct* params, script::Script* script);    // 0x00519720 (SparksPos)
    bool set_sparks_pos(script::Struct* params, script::Script* script);    // 0x00519600 (the unnamed checksum)

    int cur_phase_;                // +0x18 State
    int physics_mode_;          // +0x1C PhysicsMode (-1 until the physics control component sets it)
    uint8_t unknown20_[0x50];  // +0x20 (the controls, the flip flag +0x28 and its time +0x2C... - the physics')
    uint8_t bailing_;          // +0x70
    uint8_t unknown71_[0x6F];
    uint8_t in_trick_move_;       // +0xE0
    uint8_t surface_type_;          // +0xE1
    uint8_t unknowne2_[0x36];
    uint8_t in_vehicle_;          // +0x118
    uint8_t unknown119_[7];
    uint8_t boost_items_[1];      // +0x120 (one per name in 0x0067F09C)
    uint8_t unknown121_[3];
    uint32_t sparks_pos_;       // +0x124 a checksum
};
static_assert(offsetof(RiderPoseState, cur_phase_) == 0x18 &&
                  offsetof(RiderPoseState, physics_mode_) == 0x1C &&
                  offsetof(RiderPoseState, bailing_) == 0x70 &&
                  offsetof(RiderPoseState, in_trick_move_) == 0xE0 &&
                  offsetof(RiderPoseState, surface_type_) == 0xE1 &&
                  offsetof(RiderPoseState, in_vehicle_) == 0x118 &&
                  offsetof(RiderPoseState, boost_items_) == 0x120 &&
                  offsetof(RiderPoseState, sparks_pos_) == 0x124 && sizeof(RiderPoseState) == 0x128,
              "layout must match the original (0x00519740 allocates 0x128 bytes)");

// The skater's state history (SkaterStateHistory, state_history.h): its state records (0x84 bytes; 20 of them from
// the component's +0x1C) keep their time as two halves. (Its destructor body 0x005197C0 is here too.)
struct SkaterStateHistoryFrame {
    // 0x005197D0 / 0x005197E0: the frame's time (+0x78 the low 16 bits, +0x7A the high).
    uint32_t get_time();
    void change_moment(uint32_t time);
};

} // namespace Obj
