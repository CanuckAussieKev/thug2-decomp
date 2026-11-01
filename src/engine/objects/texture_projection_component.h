#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Struct;
} // namespace script

namespace objects {

// The texture projection component (type 0x7505D7CC - its name isn't known; vtable 0x00646604, 0x38 bytes; made by
// 0x00434870): textures projected from its object (texture_projections=: the renderer's texture projections, 0xC8
// bytes each - made by 0x004C7E80, set up from their structures by 0x004A04C0: texture, fov / aspect or size,
// offset, angle...), kept in a list and updated from the object each frame (0x0049FFE0).
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 configure_from_data; 3 Finalize; 4 / 9 the empty 0x005480A0;
// 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11 the base's member
// function table entry 0x0045D720; 12 invoke_method_by_name 0x0051ADC0 (none by name).
class TextureProjectionComponent final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x7505D7CC;

    // 0x00434870 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory): the
    // base's constructor, its type, an empty list (its head's data -1), no components.
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    TextureProjectionComponent();
    // 0x004346B0: every projection deleted (0x0057A2F0), the head unlinked; the base's body.
    ~TextureProjectionComponent() override;
    // 0x00434730 (vtable slot 2): a projection for each structure of texture_projections= (asserted): its
    // 0x1EE8B7B9 checksum (asserted) at its +0x14, set up from the structure, put at the front of the list.
    void configure_from_data(script::Struct* params) override;
    // 0x00434630 (vtable slot 3): its object's suspend, skeleton and model components found and kept.
    void seal() override;
    // 0x00434810 (vtable slot 1): each projection updated from the object (from the first to the head).
    void tick_frame() override;

    // +0x18 (a lists::Foremost of the projections; the head's scalar deleting destructor 0x00434670 is
    // memory::ListNode's)
    memory::ListNode projections_;
    ObjectModule* suspend_;       // +0x2C
    ObjectModule* bone_rig_;      // +0x30
    ObjectModule* mesh_instance_; // +0x34
};

} // namespace objects
