#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/entity.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The skater's rotate component (SkaterRotate, type 0x52D61A0D, vtable 0x0064C018, 0x58 bytes): the script member
// function Rotate - the skater turned at once about x, y or z (in degrees), or turned round (no angle), or turned
// over a duration (seconds, or ms by default, or frames), by an angle or to face a Node, one step each frame. From
// the disassembly of 0x00517CC0-0x0051823C.
//
// Vtable: 0 Destroy, 1 Update, 2 ConfigureFromData, 3 Finalize, 12 InvokeMethodByName.
namespace GameObjects {

class CRiderTurning : public CObjectModule {
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

    // 0x00517D40 (registered by RegisterComponentTypes): a new one (0 if out of memory), no core physics yet.
    static CObjectModule* __cdecl Fabricate();
    // 0x00517CC0: the destructor body (the base's).
    void DestroyBody();
    // 0x00517CF0 (vtable slot 0)
    CRiderTurning* Teardown(unsigned int flags);
    // 0x00517CD0 (vtable +0x08): no turns. `params` isn't read.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x00517D20 (vtable +0x0C): the core physics component found.
    void Seal();
    // 0x00517DC0 (vtable +0x04): each active turn steps by the frame's time (the last step cut to end exactly), the
    // object's and the core physics' matrices turned by it about the axis; with none left the component suspends
    // itself.
    void TickFrame();
    // 0x00517EA0 (vtable +0x30): Rotate (1); 2 for any other name.
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    Turn m_turns[3];        // +0x18 about x, y, z
    uint8_t* m_corePhysics; // +0x54 SkaterCorePhysics (0x5BD63B29)
};
static_assert(sizeof(CRiderTurning::Turn) == 0x14 && offsetof(CRiderTurning, m_turns) == 0x18 &&
                  offsetof(CRiderTurning, m_corePhysics) == 0x54 && sizeof(CRiderTurning) == 0x58,
              "layout must match the original (0x00517D40 allocates 0x58 bytes)");

} // namespace Obj
