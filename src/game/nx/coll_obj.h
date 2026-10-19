#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/geometry.h"

namespace Nx {

// The geometry a collision object's faces come from (Nx::CCollObjTriData; its methods are rewritten later).
struct CCollObjTriData;

// The base of the collision objects (0x10 bytes, vtable 0x00645A30). The methods are virtual in the game - each
// kind has its own vtable - so those here that run another slot call it through the vtable. The slots seen so far:
//   0 the scalar deleting destructor, 1 IsTriangleCollision (base: false), 2 GetFaceFlags(face),
//   3 GetFaceTerrain(face), 4 SetGeometry, 5 SetWorldPosition, 6 SetOrientation, 7 GetWorldPosition,
//   8 GetOrientation, 9 GetVertexPos(face vertex), 10 / 11 a value at +0x38 / +0xA0 get and set, 12 Clone,
//   13 WithinBBox (true if it may touch the box), 14 CollideWithLine, 19 a call passed down, 20 GetBBox,
//   21 the geometry a cache keeps for it.
class CCollObj {
public:
    enum Flags : uint16_t {
        kOwnsGeometry = 0x8000,    // the geometry is deleted with the object (unless kSharedGeometry)
        kSharedGeometry = 0x4000,
    };

    // 0x0040B370 (slot 4): the geometry, and its checksum (the sector's name) as the object's.
    void SetGeometry(CCollObjTriData* geometry);
    // 0x00413390: the scalar deleting destructor (the base's body inlined: the geometry deleted if it's owned and
    // not shared).
    CCollObj* ScalarDelete(uint8_t flags);
    // The base's destructor body (inlined in every destructor).
    void DestroyBase();

    template <typename R, typename... A>
    R Call(int slot, A... args)
    {
        return reinterpret_cast<R(__thiscall*)(CCollObj*, A...)>(m_vtable[slot])(this, args...);
    }

    void** m_vtable;
    uint32_t m_checksum;           // +0x04
    uint16_t m_flags;              // +0x08
    uint8_t m_terrain;             // +0x0A
    uint8_t pad0B;
    CCollObjTriData* m_geometry;   // +0x0C
};
static_assert(sizeof(CCollObj) == 0x10, "layout must match the original");

// The node vtable of the lists of collision objects (Lst::Node<CCollObj>, 0x00645AA0) and their heads'
// (Lst::Head<CCollObj>, 0x00645D34).
struct CollObjListNodes {
    // 0x004131D0: unlinked (Lst::Node<CCollObj>'s body). 0x00411880: its scalar deleting destructor.
    void DestroyNodeBody();
    Mem::ListNode* DestroyNode(uint8_t flags);
    // 0x00413180: an empty head (returns it). 0x004131A0: its scalar deleting destructor (the node's body).
    Mem::ListNode* ConstructHead();
    Mem::ListNode* DestroyHead(uint8_t flags);
};

// A collision object made of others (0x3C bytes, vtable 0x00645AA8): its children in a list; most calls are passed
// to each; its box is theirs together (recomputed when a child moves).
class CCollMulti : public CCollObj {
public:
    // 0x004133E0: no children (returns this).
    CCollMulti* Construct();
    // 0x00411D10: a copy (sharing the children's list; returns this).
    CCollMulti* ConstructCopy(const CCollMulti* other);
    // 0x00411B10: the children deleted (each child and its node), the list, out of the caches, the base.
    void Destruct();
    // 0x00413460
    CCollMulti* ScalarDelete(uint8_t flags);
    // 0x00413490 (slot 12): a copy owning its geometry flag, with a clone of each child.
    CCollMulti* Clone(int argument);
    // 0x00411BE0: `child` onto the end of the list.
    void AddChild(CCollObj* child);

    // 0x00411D90 (slot 1): whether every child is (stops asking at the first that isn't).
    bool IsTriangleCollision();
    // 0x00411CC0 / 0x00411C30 (slots 4, 5): passed to each child; the box to be recomputed. 0x00411C80 (slot 6):
    // passed to each child.
    void SetGeometryAll(void* value);
    void SetWorldPosition(const void* position);
    void SetOrientation(const void* orientation);
    // 0x00411F60 / 0x00411F80 (slots 7, 8): the first child's (crashes with none, as in the original).
    void* GetWorldPosition();
    void* GetOrientation();
    // 0x00410960 (slot 9): a zero vector.
    Mth::Vector GetVertexPos(int vertex) const;
    // 0x0040AE20 / 0x0040AE10 (slots 10, 11)
    int GetValue38() const;
    void SetValue38(int value);
    // 0x00411E80 (slot 13): false only if the box is known and doesn't touch `box`.
    int WithinBBox(const Mth::BBox* box);
    // 0x00411EC0 (slot 14): each child asked; whether any hit.
    bool CollideWithLine(void* a, void* b, void* c, void* d);
    // 0x00411F20 (slot 19): passed to each child.
    void PassDown(void* a, void* b);
    // 0x00411E50 (slot 20): the box (recomputed if a child moved; null if a child has none).
    Mth::BBox* GetBBox();
    // 0x00411DD0: the box made from the children's.
    void RecomputeBBox();

    Mem::ListNode* m_children;    // +0x10 the head (Lst::Head<CCollObj>)
    Mth::BBox m_box;              // +0x14
    bool m_dirty;                 // +0x34
    bool m_boxValid;              // +0x35
    uint8_t pad36[2];
    int m_value38;                // +0x38
};
static_assert(sizeof(CCollMulti) == 0x3C && offsetof(CCollMulti, m_box) == 0x14, "layout must match the original");

// 0x0040AE30 (CCollMulti's slot 3, GetFaceTerrain): 0.
struct CollObjStubs {
    uint16_t NoTerrain(int face);
};

} // namespace Nx
