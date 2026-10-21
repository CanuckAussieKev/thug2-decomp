#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The special item component (SpecialItem, type 0xF27A294D, vtable 0x0064C250, 0x20 bytes; made by 0x0051BD80): up to
// two items an object carries (CreateSpecialItem: a game object made from a global structure, locked to a bone of
// the object by its lock component, in the object's colour), told to the other players in a network game.
//
// Vtable: 0 Destroy, 1 Update 0x0051B970 (shared: suspends itself), 12 CallMemberFunction; the rest the base's or
// empty.
namespace Obj {

class CSpecialItemComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xF27A294D;
    static constexpr int kItems = 2;

    // 0x0051BD80 (registered with the composite object manager): a new one, no items (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0051BF20: the destructor body: both items destroyed, then the base's.
    void DestroyBody();
    // 0x0051C130 (vtable slot 0)
    CSpecialItemComponent* Destroy(unsigned int flags);

    // 0x0051BF80: item `slot` (the one there destroyed first) made from `params` (a game object from a node; its id,
    // if given, offset by the object's id - written back into `params`), tagged with the object's id (0x7A9597B3)
    // and the item's script for when it goes (0x40764820), in the object's model colour. Returns it.
    CCompositeObject* CreateItem(int slot, Script::CStruct* params);
    // 0x0051BE00: item `slot` (if any) destroyed: its script for when it goes (its tag 0x40764820) run on it, then
    // it's marked dead.
    void DestroyItem(int slot);
    // 0x0051BF00: both items destroyed.
    void DestroyItems();

    // 0x0051C160 (vtable slot 12): CreateSpecialItem (index, bone, params), DestroySpecialItem (index) - each sent to
    // the server in a network game (messages 0x77 / 0x78) -, DestroyAllSpecialItems, SpecialItemExists (index, or
    // any). Other names: 2 (not mine).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    CCompositeObject* m_items[kItems]; // +0x18
};
static_assert(offsetof(CSpecialItemComponent, m_items) == 0x18 && sizeof(CSpecialItemComponent) == 0x20,
              "layout must match the original");

} // namespace Obj
