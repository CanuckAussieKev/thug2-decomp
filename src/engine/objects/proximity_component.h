#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The proximity component (member type 0xE54DB2EE "Proximity"; our name): runs its enter / exit scripts as the
// skater (the global at 0x0069399C) comes within a capsule round it or leaves it. Only its script member functions
// and its update are in the game - their table (0x007CEBF8, made by the dynamic initialiser 0x00644910: Proximity_Init,
// Proximity_SetActive, Proximity_EnterProximityNotHandled) - nothing constructs one: a leftover.
class ProximityComponent : public ObjectModule {
public:
    // 0x0042B620 (RET 8): `script` run on the object with a new structure: 0x0AE89B6C = the skater's id, plus its
    // own parameters (+0x30).
    void launch_script(uint32_t script, Entity* skater);
    // 0x0042B700: the skater's head (its position and the line a radius 0x006939A8 up from it, as a capsule of the
    // inner radius - the outer once inside) holding its position: in or out; a change runs the enter / exit script.
    // Inactive: out.
    void tick_frame();

    // --- script member functions (true) ---
    // 0x0042B850 (Proximity_Init): inactive, updated, then configure_from_data (vtable slot 2), active, not suspended.
    bool proximity_init(script::Struct* params, script::Script* script);
    // 0x0042B590 (Proximity_SetActive): active unless kClockIdle (inactive: +0x1C the game clock plus the unnamed
    // float's seconds, or 0); not suspended.
    bool proximity_set_active(script::Struct* params, script::Script* script);
    // 0x0042B570 (Proximity_EnterProximityNotHandled): counts as out.
    bool proximity_enter_proximity_not_handled(script::Struct* params, script::Script* script);

    bool active_; // +0x18
    uint8_t pad19[3];
    int reactivate_time_;       // +0x1C
    float inner_radius_;        // +0x20
    float outer_radius_;        // +0x24
    uint32_t enter_script_;     // +0x28
    uint32_t dismount_script_;  // +0x2C
    script::Struct* call_args_; // +0x30
    int is_within_;             // +0x34
};
static_assert(offsetof(ProximityComponent, active_) == 0x18 && offsetof(ProximityComponent, is_within_) == 0x34,
              "layout must match the original");

} // namespace objects
