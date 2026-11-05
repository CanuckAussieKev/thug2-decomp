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

// The skater's flip-and-rotate component (SkaterFlipAndRotate, type 0x488AF07A, vtable 0x0064BE20, 0x28 bytes): the
// skater flipped (facing the other way: the state component's flag, the animations mirrored) and turned around, the
// board rotated (shown back to front, told to the server) - at once or after the current animation (FlipAfter,
// RotateAfter, BoardRotateAfter: done when the next PlayAnim comes). From the disassembly of 0x00511300-0x00511778.
//
// Vtable: 0 Destroy, 3 Finalize, 12 invoke_method_by_name; Update is the shared empty 0x0051B970.
namespace objects {

class MeshInstanceModule;

class RiderSpinAndFlip final : public ObjectModule {
public:
    RiderSpinAndFlip(const RiderSpinAndFlip&) = delete;
    RiderSpinAndFlip& operator=(const RiderSpinAndFlip&) = delete;
    // 0x00511470 (registered by register_component_types): a new one (0 if out of memory): the board not rotated, no
    // components yet. The "after" flags aren't initialised.
    static gsl::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderSpinAndFlip();
    // 0x00511300: the destructor body (the base's).
    ~RiderSpinAndFlip() override;
    // 0x00511350 (vtable +0x0C): the core physics, model and animation blender components found.
    void seal() override;
    // 0x00511600 (vtable +0x30): BoardIsRotated, BoardRotate (to `Normal` - not rotated - or the other way; told to
    // the server), BoardRotateAfter, flip, FlipAndRotate, RotateNoFlip, FlipAfter, IsFlipAfterSet, RotateAfter, the
    // flip only if the core physics says so (0x72289A28, name unknown) - 1, or the flag; PlayAnim does what was
    // left for after the animation and returns 2 (not handled: the animation components play it).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x00511390: the board rotated or not (returns what it was); with `send`, if `objectId`'s player is local, the
    // server is sent message 0x2A (the id and the value, a byte each). `unused` isn't read.
    bool apply_board_turned(uint32_t object_id, bool rotated, uint32_t unused, bool send);
    // 0x00511310: the flip and rotate left for after the animation dropped (the board's isn't).
    void clear_after_flags();
    // 0x00511420: the animation blender mirrored as the state component's flip says (0x00419C10, with the skater's
    // client connection).
    void apply_flip_to_anims();
    // 0x005114F0: flipped the other way (the state component's +0x28, the time it changed at +0x2C), then the
    // animations as apply_flip_to_anims.
    void get_flip();
    // 0x00511570: what was left for after the animation: the flip, the turn around (the core physics' 0x004FE120),
    // the board's rotation (the other way, told to the server); the model's +0x1F4 cleared.
    void do_after_anim();

    // The fields other code reads and sets.
    [[nodiscard]] bool deck_turned() const { return deck_turned_; }

private:
    bool deck_turned_{false};                   // +0x18
    bool flip_after_{};                         // +0x19
    bool rotate_after_{};                       // +0x1A
    bool board_rotate_after_{};                 // +0x1B
    ObjectModule* anim_blender_{nullptr};       // +0x1C (0x7149B753)
    MeshInstanceModule* mesh_set_{nullptr};     // +0x20 Model (0x286A8D26)
    skater::SkaterPhysicsModule* core_physics_{}; // +0x24 SkaterCorePhysics (0x5BD63B29)
};

} // namespace objects
