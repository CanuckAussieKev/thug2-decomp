#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Script;
class Struct;
} // namespace Script

// The skater's rotate component (SkaterRotate, type 0x52D61A0D, vtable 0x0064C018, 0x58 bytes): the script member
// function Rotate - the skater turned at once about x, y or z (in degrees), or turned round (no angle), or turned
// over a duration (seconds, or ms by default, or frames), by an angle or to face a Node, one step each frame. From
// the disassembly of 0x00517CC0-0x0051823C.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize, 12 invoke_method_by_name.
namespace objects {

class RiderTurning : public ObjectModule {
public:
    // A turn about one axis over time.
    struct Turn {
        float remaining; // +0x00 the angle to turn (radians, positive)
        float duration;  // +0x04 (seconds)
        float rate;      // +0x08 radians a second (signed)
        float turned;    // +0x0C so far
        bool active;     // +0x10
        uint8_t pad11[3];
    };

    // 0x00517D40 (registered by register_component_types): a new one (0 if out of memory), no core physics yet.
    static ObjectModule* __cdecl fabricate();
    // 0x00517CC0: the destructor body (the base's).
    void destroy_body();
    // 0x00517CF0 (vtable slot 0)
    RiderTurning* teardown(unsigned int flags);
    // 0x00517CD0 (vtable +0x08): no turns. `params` isn't read.
    void configure_from_data(script::Struct* params);
    // 0x00517D20 (vtable +0x0C): the core physics component found.
    void seal();
    // 0x00517DC0 (vtable +0x04): each active turn steps by the frame's time (the last step cut to end exactly), the
    // object's and the core physics' matrices turned by it about the axis; with none left the component suspends
    // itself.
    void tick_frame();
    // 0x00517EA0 (vtable +0x30): Rotate (1); 2 for any other name.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);

    Turn turns_[3];        // +0x18 about x, y, z
    uint8_t* core_physics_; // +0x54 SkaterCorePhysics (0x5BD63B29)
};
static_assert(sizeof(RiderTurning::Turn) == 0x14 && offsetof(RiderTurning, turns_) == 0x18 &&
                  offsetof(RiderTurning, core_physics_) == 0x54 && sizeof(RiderTurning) == 0x58,
              "layout must match the original (0x00517D40 allocates 0x58 bytes)");

} // namespace Obj
