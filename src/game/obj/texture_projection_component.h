#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/obj/object.h"

namespace Script {
class CStruct;
} // namespace Script

namespace Obj {

// The texture projection component (type 0x7505D7CC - its name isn't known; vtable 0x00646604, 0x38 bytes; made by
// 0x00434870): textures projected from its object (texture_projections=: the renderer's texture projections, 0xC8
// bytes each - made by 0x004C7E80, set up from their structures by 0x004A04C0: texture, fov / aspect or size,
// offset, angle...), kept in a list and updated from the object each frame (0x0049FFE0).
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 InitFromStructure; 3 Finalize; 4 / 9 the empty 0x005480A0;
// 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11 the base's member
// function table entry 0x0045D720; 12 CallMemberFunction 0x0051ADC0 (none by name).
class CTextureProjectionComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x7505D7CC;

    // 0x00434870 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory): the
    // base's constructor, its vtable and type, an empty list (its head: vtable 0x00646638, data -1), no components.
    static CBaseComponent* __cdecl Create();
    // 0x004346B0: every projection deleted (0x0057A2F0), the head unlinked (its vtable 0x00646600); the base's body.
    void DestroyBody();
    // 0x00434840 (vtable slot 0)
    CTextureProjectionComponent* Destroy(unsigned int flags);
    // 0x00434730 (vtable slot 2): a projection for each structure of texture_projections= (asserted): its
    // 0x1EE8B7B9 checksum (asserted) at its +0x14, set up from the structure, put at the front of the list.
    void InitFromStructure(Script::CStruct* params);
    // 0x00434630 (vtable slot 3): its object's suspend, skeleton and model components found and kept.
    void Finalize();
    // 0x00434810 (vtable slot 1): each projection updated from the object (from the first to the head).
    void Update();

    // The list head's scalar deleting destructor (vtable 0x00646600 slot 0).
    struct ProjectionList {
        // 0x00434670: unlinked (vtable 0x00646600) and linked to itself; freed with flag 1. Returns this.
        ProjectionList* Destroy(unsigned int flags);

        Mem::ListNode m_node;
    };

    ProjectionList m_projections;         // +0x18 (a Lst::Head of the projections)
    CBaseComponent* mp_suspend;      // +0x2C
    CBaseComponent* mp_skeleton;     // +0x30
    CBaseComponent* mp_model;        // +0x34
};
static_assert(offsetof(CTextureProjectionComponent, m_projections) == 0x18 &&
                  offsetof(CTextureProjectionComponent, mp_suspend) == 0x2C &&
                  sizeof(CTextureProjectionComponent) == 0x38,
              "layout must match the original");

} // namespace Obj
