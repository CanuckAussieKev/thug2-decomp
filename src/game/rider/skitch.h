#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace maths {
class Vec4;
}
namespace script {
class Script;
class Struct;
} // namespace script

// The skitch component (Skitch, type 0x3506CE64, vtable 0x0064C218, 0x20 bytes; made by 0x0051BA10): on a car the
// skater can hold on to - whether skitching is allowed (Obj_AllowSkitching), and the car's hook points (its object
// hook manager's nodes, 0x27443708) placed on the car.
//
// Vtable: 0 Destroy, 1 Update 0x0051B970 (shared: suspends itself), 2 configure_from_data, 3 the empty 0x005480A0, 12
// invoke_method_by_name; the rest the base's or empty.
namespace objects {

class TowRideHandler final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x3506CE64;

    // 0x0051BA10 (registered with the composite object manager): a new one, skitching allowed (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // The constructor (inlined in fabricate in the original).
    TowRideHandler();
    // 0x0051B960: the destructor body (the base's).
    ~TowRideHandler() override;
    // 0x0051B9B0 (vtable slot 2): the object's hook manager (`params` isn't read).
    void configure_from_data(script::Struct* params) override;
    // 0x0051B9D0 (vtable slot 12): Obj_AllowSkitching (0xC38CCE3B) - allowed unless the flag "off" is given; true.
    // Other names: 2 (not mine). (Ghidra splits it at 0x0051B9D8, after the first compare.)
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x0051BA70: the hook point nearest `target` (its squared distance), each point on the car: its node through
    // the object's matrix and position, its height above the car dropped (projected onto the car's plane - the
    // matrix's up row). The point goes to `out` and its index is returned; -1 (`out` untouched) without a hook
    // manager, an object or a node list, and -1 with `out` = (0, 0, 0, 1) without nodes.
    int get_nearest_hook_point(maths::Vec4* out, const maths::Vec4* target);
    // 0x0051BC70: hook point `index` on the car, as get_nearest_hook_point places it, into `out`. False without a hook
    // manager, an object or a node list; true otherwise - also when there's no such point (`out` untouched).
    bool get_hook_point(maths::Vec4* out, int index);

    bool allowed_; // +0x18
    uint8_t pad19[3];
    ObjectModule* hooks_; // +0x1C the object's hook manager (0x27443708)
};
static_assert(offsetof(TowRideHandler, allowed_) == 0x18 && offsetof(TowRideHandler, hooks_) == 0x1C &&
                  sizeof(TowRideHandler) == 0x20,
              "layout must match the original");

} // namespace objects
