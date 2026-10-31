#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Script;
class Struct;
} // namespace Script

// The special item component (SpecialItem, type 0xF27A294D, vtable 0x0064C250, 0x20 bytes; made by 0x0051BD80): up to
// two items an object carries (CreateSpecialItem: a game object made from a global structure, locked to a bone of
// the object by its lock component, in the object's colour), told to the other players in a network game.
//
// Vtable: 0 Destroy, 1 Update 0x0051B970 (shared: suspends itself), 12 invoke_method_by_name; the rest the base's or
// empty.
namespace objects {

class PickupItemHandler : public ObjectModule {
public:
    static constexpr uint32_t kType = 0xF27A294D;
    static constexpr int kItems = 2;

    // 0x0051BD80 (registered with the composite object manager): a new one, no items (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x0051BF20: the destructor body: both items destroyed, then the base's.
    void destroy_body();
    // 0x0051C130 (vtable slot 0)
    PickupItemHandler* teardown(unsigned int flags);

    // 0x0051BF80: item `slot` (the one there destroyed first) made from `params` (a game object from a node; its id,
    // if given, offset by the object's id - written back into `params`), tagged with the object's id (0x7A9597B3)
    // and the item's script for when it goes (0x40764820), in the object's model colour. Returns it.
    GameEntity* create_item(int slot, script::Struct* params);
    // 0x0051BE00: item `slot` (if any) destroyed: its script for when it goes (its tag 0x40764820) run on it, then
    // it's marked dead.
    void destroy_item(int slot);
    // 0x0051BF00: both items destroyed.
    void destroy_items();

    // 0x0051C160 (vtable slot 12): CreateSpecialItem (index, bone, params), DestroySpecialItem (index) - each sent to
    // the server in a network game (messages 0x77 / 0x78) -, DestroyAllSpecialItems, SpecialItemExists (index, or
    // any). Other names: 2 (not mine).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);

    GameEntity* items_[kItems]; // +0x18
};
static_assert(offsetof(PickupItemHandler, items_) == 0x18 && sizeof(PickupItemHandler) == 0x20,
              "layout must match the original");

} // namespace Obj
