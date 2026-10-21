#pragma once

// Components that give an object node arrays of its own (a moving object's model with nodes in it): the node array
// itself, the object hooks among its nodes, and the rails among them.

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/obj/object.h"

namespace Script {
class CArray;
class CStruct;
} // namespace Script

namespace Obj {

class CClassNodeList;
class CRailManager;

// The node array component (type 0xC472ECC5 "nodearray", vtable 0x0064628C, 0x1C bytes; made by 0x00428010): the
// node array of the object's model (models\<model>.qb), loaded through the asset manager.
//
// Vtable: 0 Destroy, 1 0x0051B970 (shared), 2 InitFromStructure, 3 / 4 / 9 the empty 0x005480A0, 5
// RefreshFromStructure, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 the base's 0x0045D720,
// 12 0x0051ADC0 (shared), 13 GetNodeArray (the shared getter 0x005480F0: +0x18).
class CNodeArrayComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xC472ECC5;

    // 0x00428010 (registered with the component types): a new one (Mem::Calloc; the constructor inlined).
    static CBaseComponent* __cdecl Create();
    // 0x00427EA0: the destructor body (its vtable, then the base's - the node array stays the asset manager's).
    void DestroyBody();
    // 0x00427EF0 (vtable slot 0): the scalar deleting destructor.
    CNodeArrayComponent* Destroy(unsigned int flags);
    // 0x00427F20 (slot 2): with a `model` (a string), the node array models\<model> - its extension (from the first
    // dot) made .qb, or .qb added - loaded or found by the asset manager.
    void InitFromStructure(Script::CStruct* params);
    // 0x00427EB0 (slot 5): InitFromStructure (through the vtable) when `params` has a model.
    void RefreshFromStructure(Script::CStruct* params);

    Script::CArray* m_nodes; // +0x18
};
static_assert(sizeof(CNodeArrayComponent) == 0x1C, "layout must match the original (0x00428010 allocates 0x1C bytes)");

// The object hook manager component (type 0x27443708 "objecthookmanager", vtable 0x006462D4, 0x1C bytes; made by
// 0x00428130): the object hooks (class 0xD18719D5 nodes) of the object's node array.
//
// Vtable: as the node array's, with 0 Destroy, 2 InitFromStructure, 5 0x004E3F90.
class CObjectHookManagerComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x27443708;

    // 0x00428130 (registered with the component types): a new one (Mem::Calloc; the constructor inlined).
    static CBaseComponent* __cdecl Create();
    // 0x004281B0: the destructor body: the hook list cleared and freed, then the base's.
    void DestroyBody();
    // 0x00428220 (vtable slot 0): the scalar deleting destructor.
    CObjectHookManagerComponent* Destroy(unsigned int flags);
    // 0x00428090 (slot 2): a new hook list (the old one cleared and freed) from the object's node array component's
    // nodes (no node array component: a crash).
    void InitFromStructure(Script::CStruct* params);

    CClassNodeList* mp_hooks; // +0x18 (0x4C bytes)
};
static_assert(sizeof(CObjectHookManagerComponent) == 0x1C,
              "layout must match the original (0x00428130 allocates 0x1C bytes)");

// The rail manager component (type 0x1CDFB2A3 "railmanager", vtable 0x0064637C, 0x1C bytes; made by 0x0042B920):
// the grind rails of the object's node array, in the object's own space.
//
// Vtable: as the node array's, with 0 Destroy, 2 InitFromStructure, 5 0x004E3F90.
class CRailManagerComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x1CDFB2A3;

    // 0x0042B920 (registered with the component types): a new one (Mem::Calloc; the constructor inlined).
    static CBaseComponent* __cdecl Create();
    // 0x0042B9A0: the destructor body: the rail manager cleaned up and freed, then the base's.
    void DestroyBody();
    // 0x0042BA90 (vtable slot 0): the scalar deleting destructor.
    CRailManagerComponent* Destroy(unsigned int flags);
    // 0x0042B880 (slot 2): a new rail manager (the old one cleaned up and freed) with the rails of the object's node
    // array component's nodes (none: a crash).
    void InitFromStructure(Script::CStruct* params);
    // 0x0042BA10: the object's placement (its orientation without the rows' w, at its position) given to the rail
    // manager and copied to `out`; returns `out`.
    Mth::Matrix* UpdateRailMatrix(Mth::Matrix* out);

    CRailManager* mp_rails; // +0x18 (0x54 bytes)
};
static_assert(sizeof(CRailManagerComponent) == 0x1C, "layout must match the original (0x0042B920 allocates 0x1C bytes)");

} // namespace Obj
