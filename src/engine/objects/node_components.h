#pragma once

// Components that give an object node arrays of its own (a moving object's model with nodes in it): the node array
// itself, the object hooks among its nodes, and the rails among them.

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/objects/entity.h"
#include "engine/core/mem.h"
#include "engine/core/owner.h"

namespace script {
class Array;
class Struct;
} // namespace script

namespace objects {

class ClassNodeList;
class GrindRailHub;

// The node array component (type 0xC472ECC5 "nodearray", vtable 0x0064628C, 0x1C bytes; made by 0x00428010): the
// node array of the object's model (models\<model>.qb), loaded through the asset manager.
//
// Vtable: 0 Destroy, 1 0x0051B970 (shared), 2 configure_from_data, 3 / 4 / 9 the empty 0x005480A0, 5
// reapply_from_data, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 the base's 0x0045D720,
// 12 0x0051ADC0 (shared), 13 GetNodeArray (the shared getter 0x005480F0: +0x18 - a plain method here, as in the
// hook and rail managers).
class WaypointListModule final : public ObjectModule {
public:
    WaypointListModule(const WaypointListModule&) = delete;
    WaypointListModule& operator=(const WaypointListModule&) = delete;
    static constexpr uint32_t kType = 0xC472ECC5;

    // 0x00428010 (registered with the component types): a new one (memory::alloc_zeroed; the constructor inlined).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    WaypointListModule();
    // 0x00427EA0: the destructor body (the base's - the node array stays the asset manager's).
    ~WaypointListModule() override;
    // 0x00427F20 (slot 2): with a `model` (a string), the node array models\<model> - its extension (from the first
    // dot) made .qb, or .qb added - loaded or found by the asset manager.
    void configure_from_data(script::Struct* params) override;
    // 0x00427EB0 (slot 5): configure_from_data (through the vtable) when `params` has a model.
    void reapply_from_data(script::Struct* params) override;
    // 0x005480F0 (slot 13, GetNodeArray: a getter of +0x18 shared with other classes; only this class's table has
    // it as a slot, so a plain method here): the node array.
    [[nodiscard]] script::Array* get_node_array() const { return nodes_; }

    script::Array* nodes_{nullptr}; // +0x18
};
static_assert(sizeof(WaypointListModule) == 0x1C, "layout must match the original (0x00428010 allocates 0x1C bytes)");

// The object hook manager component (type 0x27443708 "objecthookmanager", vtable 0x006462D4, 0x1C bytes; made by
// 0x00428130): the object hooks (class 0xD18719D5 nodes) of the object's node array.
//
// Vtable: as the node array's, with 0 Destroy, 2 configure_from_data, 5 0x004E3F90.
class HookPointRegistryModule final : public ObjectModule {
public:
    HookPointRegistryModule(const HookPointRegistryModule&) = delete;
    HookPointRegistryModule& operator=(const HookPointRegistryModule&) = delete;
    static constexpr uint32_t kType = 0x27443708;

    // 0x00428130 (registered with the component types): a new one (memory::alloc_zeroed; the constructor inlined).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    HookPointRegistryModule();
    // 0x004281B0: the destructor body: the hook list cleared and freed, then the base's.
    ~HookPointRegistryModule() override;
    // 0x00428090 (slot 2): a new hook list (the old one cleared and freed) from the object's node array component's
    // nodes (no node array component: a crash).
    void configure_from_data(script::Struct* params) override;
    // 0x005480F0 (slot 13, the shared getter of +0x18): the hook list.
    [[nodiscard]] ClassNodeList* get_hooks() const { return hooks_ptr_; }

    ClassNodeList* hooks_ptr_{nullptr}; // +0x18 (0x4C bytes)
};
static_assert(sizeof(HookPointRegistryModule) == 0x1C,
              "layout must match the original (0x00428130 allocates 0x1C bytes)");

// The rail manager component (type 0x1CDFB2A3 "railmanager", vtable 0x0064637C, 0x1C bytes; made by 0x0042B920):
// the grind rails of the object's node array, in the object's own space.
//
// Vtable: as the node array's, with 0 Destroy, 2 configure_from_data, 5 0x004E3F90.
class GrindPathLinkModule final : public ObjectModule {
public:
    GrindPathLinkModule(const GrindPathLinkModule&) = delete;
    GrindPathLinkModule& operator=(const GrindPathLinkModule&) = delete;
    static constexpr uint32_t kType = 0x1CDFB2A3;

    // 0x0042B920 (registered with the component types): a new one (memory::alloc_zeroed; the constructor inlined).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    GrindPathLinkModule();
    // 0x0042B9A0: the destructor body: the rail manager cleaned up and freed, then the base's.
    ~GrindPathLinkModule() override;
    // 0x0042B880 (slot 2): a new rail manager (the old one cleaned up and freed) with the rails of the object's node
    // array component's nodes (none: a crash).
    void configure_from_data(script::Struct* params) override;
    // 0x0042BA10: the object's placement (its orientation without the rows' w, at its position) given to the rail
    // manager and copied to `out`; returns `out`.
    maths::Mat4* update_rail_matrix(maths::Mat4* out);
    // 0x005480F0 (slot 13, the shared getter of +0x18): the rail manager.
    [[nodiscard]] GrindRailHub* get_rail_groups() const { return rail_groups_.get(); }

    memory::HeapUnique<GrindRailHub> rail_groups_; // +0x18 (0x54 bytes)
};
static_assert(sizeof(GrindPathLinkModule) == 0x1C, "layout must match the original (0x0042B920 allocates 0x1C bytes)");

} // namespace objects
