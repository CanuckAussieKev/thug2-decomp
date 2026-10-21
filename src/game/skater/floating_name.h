#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CStruct;
} // namespace Script

// The skater's floating name component (SkaterFloatingName, type 0x125044E2, vtable 0x0064BE54, 0x1C bytes): in a
// network game, each skater's name shown above it - a screen element label (skater_name_0 ... skater_name_7, by the
// skater's object id) made by the script create_object_label, moved each frame, destroyed (destroy_object_label)
// when the skater is hidden or gone. From the disassembly of 0x00511780-0x00511B9E.
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 12 the shared CallMemberFunction 0x0051ADC0.
namespace Obj {

class CSkaterFloatingNameComponent : public CBaseComponent {
public:
    // 0x00511810 (registered by RegisterComponentTypes): a new one (0 if out of memory). The label's id isn't set.
    static CBaseComponent* __cdecl Create();
    // 0x00511AA0: the destructor body: the label destroyed (destroy_object_label, id = its id), then the base's.
    void DestroyBody();
    // 0x00511B80 (vtable slot 0)
    CSkaterFloatingNameComponent* Destroy(unsigned int flags);
    // 0x00511780 (vtable +0x08): the label's id from the skater's object id (0-7; any other id: left as it was).
    // `params` isn't read.
    void InitFromStructure(Script::CStruct* params);
    // 0x00511870 (vtable +0x04): outside a network game where names are shown (the network manager's 0x00533140)
    // the component suspends itself. A skater that is shown (CSkater +0x2C4) and not hidden has its label made or
    // updated: its id, the text "\c<n><name>" (n: its team + 2 in a team game, else its object id + 2 - the colour)
    // and pos3D, 96 above the skater for this machine's player and 120 for the others; otherwise an existing label
    // is destroyed.
    void Update();

    uint32_t m_labelId; // +0x18 skater_name_<n>
};
static_assert(offsetof(CSkaterFloatingNameComponent, m_labelId) == 0x18 && sizeof(CSkaterFloatingNameComponent) == 0x1C,
              "layout must match the original (0x00511810 allocates 0x1C bytes)");

} // namespace Obj
