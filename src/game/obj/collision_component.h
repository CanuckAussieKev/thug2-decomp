#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Nx {
class CCollObj;
struct CCollObjTriData;
} // namespace Nx
namespace Script {
class CStruct;
}

namespace Obj {

// The collision component (type 0xA432DC41 "Collision", vtable 0x00645EEC, 0x1C bytes; made by 0x0041C920): an
// object's own collision - a movable collision object (Nx::CreateMovableCollision 0x00414990: a box, a movable
// triangle object or several under a multi-object) that its object registered with the movables' list, placed where
// the object is after each of its updates. Level objects, cars and the other objects with a model get one from their
// node's CollisionMode (0x2D7E583B): its triangles are the model's collision (the asset manager's data for the model
// - the .col of the .mdl, shared by every object of that model) or, for a LevelObject, the level sector of the same
// name. The geometry stays in the object's own space: the collision object's position and orientation say where.
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 InitFromStructure; 3 / 4 the empty 0x005480A0; 5 the shared
// 0x004E3F90 (slot 2 again); 7 the base's Suspend; 9 0x004A1090 (Teleport: Update); 12 CallMemberFunction 0x0051ADC0;
// 13 InitCollision.
class CCollisionComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xA432DC41;

    // 0x0041C920 (registered with the composite object manager by 0x004E4860): a new one, without collision (0 if
    // out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041C710: the collision object (if any) out of the movables' list and deleted, then the base's body.
    void DestroyBody();
    // 0x0041C780 (vtable slot 0)
    CCollisionComponent* Destroy(unsigned int flags);
    // 0x0041C7B0 (vtable slot 2): CollisionMode (0x2D7E583B) to a shape type (Nx::ShapeTypeFromChecksum; nothing
    // more when 0); a LevelObject (Class 0xB7B3BD86) takes the geometry of the level sector named as it (Name, which
    // must be there) when that sector has collision; then InitCollision.
    void InitFromStructure(Script::CStruct* params);
    // 0x0041C890 (vtable slot 13): the collision object made from `geometry` (one) or, without one, from the asset
    // manager's data for the model component's model (+0x208, its +0x30: its count at +0x88, its geometries at +0x8C;
    // none found: no geometry). A component without a model crashes there, as in the original.
    void InitCollision(int type, Nx::CCollObjTriData* geometry);
    // 0x0041C980 (vtable slot 1): the collision object placed where the object is: with a model whose bone matrix
    // (0x00493040) there is, at the object's position plus that matrix's translation (not turned) and turned by the
    // matrix times the object's display matrix; else at the object's position, turned by its display matrix.
    void Update();

    Nx::CCollObj* mp_collision; // +0x18
};
static_assert(offsetof(CCollisionComponent, mp_collision) == 0x18 && sizeof(CCollisionComponent) == 0x1C,
              "layout must match the original");

} // namespace Obj
