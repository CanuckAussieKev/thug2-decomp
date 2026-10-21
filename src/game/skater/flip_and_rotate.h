#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's flip-and-rotate component (SkaterFlipAndRotate, type 0x488AF07A, vtable 0x0064BE20, 0x28 bytes): the
// skater flipped (facing the other way: the state component's flag, the animations mirrored) and turned around, the
// board rotated (shown back to front, told to the server) - at once or after the current animation (FlipAfter,
// RotateAfter, BoardRotateAfter: done when the next PlayAnim comes). From the disassembly of 0x00511300-0x00511778.
//
// Vtable: 0 Destroy, 3 Finalize, 12 CallMemberFunction; Update is the shared empty 0x0051B970.
namespace Obj {

class CSkaterFlipAndRotateComponent : public CBaseComponent {
public:
    // 0x00511470 (registered by RegisterComponentTypes): a new one (0 if out of memory): the board not rotated, no
    // components yet. The "after" flags aren't initialised.
    static CBaseComponent* __cdecl Create();
    // 0x00511300: the destructor body (the base's).
    void DestroyBody();
    // 0x00511320 (vtable slot 0)
    CSkaterFlipAndRotateComponent* Destroy(unsigned int flags);
    // 0x00511350 (vtable +0x0C): the core physics, model and animation blender components found.
    void Finalize();
    // 0x00511600 (vtable +0x30): BoardIsRotated, BoardRotate (to `Normal` - not rotated - or the other way; told to
    // the server), BoardRotateAfter, flip, FlipAndRotate, RotateNoFlip, FlipAfter, IsFlipAfterSet, RotateAfter, the
    // flip only if the core physics says so (0x72289A28, name unknown) - 1, or the flag; PlayAnim does what was
    // left for after the animation and returns 2 (not handled: the animation components play it).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x00511390: the board rotated or not (returns what it was); with `send`, if `objectId`'s player is local, the
    // server is sent message 0x2A (the id and the value, a byte each). `unused` isn't read.
    bool SetBoardRotated(uint32_t objectId, bool rotated, uint32_t unused, bool send);
    // 0x00511310: the flip and rotate left for after the animation dropped (the board's isn't).
    void ClearAfterFlags();
    // 0x00511420: the animation blender mirrored as the state component's flip says (0x00419C10, with the skater's
    // client connection).
    void ApplyFlipToAnims();
    // 0x005114F0: flipped the other way (the state component's +0x28, the time it changed at +0x2C), then the
    // animations as ApplyFlipToAnims.
    void Flip();
    // 0x00511570: what was left for after the animation: the flip, the turn around (the core physics' 0x004FE120),
    // the board's rotation (the other way, told to the server); the model's +0x1F4 cleared.
    void DoAfterAnim();

    bool m_boardRotated;     // +0x18
    bool m_flipAfter;        // +0x19
    bool m_rotateAfter;      // +0x1A
    bool m_boardRotateAfter; // +0x1B
    CBaseComponent* m_animBlender; // +0x1C (0x7149B753)
    uint8_t* m_model;        // +0x20 Model (0x286A8D26)
    uint8_t* m_corePhysics;  // +0x24 SkaterCorePhysics (0x5BD63B29)
};
static_assert(offsetof(CSkaterFlipAndRotateComponent, m_boardRotated) == 0x18 &&
                  offsetof(CSkaterFlipAndRotateComponent, m_boardRotateAfter) == 0x1B &&
                  offsetof(CSkaterFlipAndRotateComponent, m_animBlender) == 0x1C &&
                  offsetof(CSkaterFlipAndRotateComponent, m_model) == 0x20 &&
                  offsetof(CSkaterFlipAndRotateComponent, m_corePhysics) == 0x24 &&
                  sizeof(CSkaterFlipAndRotateComponent) == 0x28,
              "layout must match the original (0x00511470 allocates 0x28 bytes)");

} // namespace Obj
