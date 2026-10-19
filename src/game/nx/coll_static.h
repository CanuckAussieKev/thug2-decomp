#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_obj.h"

namespace Nx {

struct CollisionHit;

// The triangle collision objects: a level sector's (in world space), an instance of one (scaled and placed) and a
// movable one. Their faces come from the geometry: face flags (u16 at face +0) and terrain (u16 at +2), faces of
// 8 bytes when the geometry's "small faces" byte (+0x0A) is set, else 10.

// The face flags / terrain of a triangle object's geometry (slots 2, 3 of all three): 0x00410BF0 / 0x00411750.
struct CollFaceAccess : CCollObj {
    uint32_t GetFaceFlags(int face) const;
    uint16_t GetFaceTerrain(int face) const;
};

// A level sector's collision (0x18 bytes, vtable 0x00645C18): the geometry in world space; +0x10 its "face-
// rejection optimization" buffer (as many bytes as the geometry has faces), copied by Clone.
class CCollStatic : public CCollObj {
public:
    // 0x00412760: the buffer freed if this object owns it, the base. (The deleting destructor is 0x0049CE00.)
    void Destruct();
    // 0x004139A0 (slot 12; not a function in Ghidra): an instance (a CCollStaticInstance sharing the geometry) or
    // a copy (with a copy of the geometry - 0x00416BF0 - and of the buffer, on the bottom-up heap). Null if the
    // geometry couldn't be copied.
    CCollObj* Clone(bool instance);
    // 0x00410C20 (slot 5): the geometry moved by the difference (0x00416400).
    void SetWorldPosition(const Mth::Vector* position);
    // 0x004123A0 / 0x004123B0 (slots 7, 8): the shared origin (0x00691914) and orientation (0x00691928).
    const Mth::Vector* GetWorldPosition() const;
    const Mth::Matrix* GetOrientation() const;
    // 0x00410980 (slot 9): the vertex, w = 1.
    Mth::Vector GetVertexPos(int index) const;
    // 0x00413B50 (slot 14): the line against each face of the geometry in the line's box (GetFacesInBox) that
    // isn't rejected - its rejection byte (the buffer's for 10-byte faces, the face's last byte for 8-byte ones)
    // below the line's distance from the geometry's minimum corner times the rejection scale (none at all past
    // 255) -, each hit offered to `hit` (ReportHit). Whether any was taken.
    bool CollideWithLine(const Mth::Line* line, const Mth::Vector* direction, CollisionHit* hit,
                         const Mth::BBox* lineBox);
    // 0x00410C70 (slot 13) / 0x00412750 (slot 21): the geometry's box.
    bool WithinBBox(const Mth::BBox* box) const;
    const Mth::BBox* GetGeometryBBox() const;

    uint8_t* m_buffer;   // +0x10
    int m_unknown14;     // +0x14
};
static_assert(sizeof(CCollStatic) == 0x18, "layout must match the original");

// An instance of a sector's collision (0xC8 bytes, vtable 0x00645C78): scaled, rotated and placed; its world box.
class CCollStaticInstance : public CCollScaled {
public:
    // 0x004127C0 (returns this)
    CCollStaticInstance* Construct();
    // 0x00414C30: a copy (returns this).
    CCollStaticInstance* ConstructCopy(const CCollStaticInstance* other);
    // 0x00412840 / 0x00413ED0
    void Destruct();
    CCollStaticInstance* ScalarDelete(uint8_t flags);
    // 0x00414CD0 (slot 12): a copy sharing the geometry.
    CCollStaticInstance* Clone(int argument);
    // 0x00411480: the geometry shared (flags 0xC000) and its box taken.
    void SetSharedGeometry(CCollObjTriData* geometry);
    // 0x00413F00 / 0x00413F30 (not a function in Ghidra) / 0x00413F50 (slots 5, 6, 24): set, the inverse and the
    // world box updated.
    void SetWorldPosition(const Mth::Vector* position);
    void SetOrientation(const Mth::Matrix* orientation);
    void SetScale(const Mth::Vector* scale);
    // 0x004113B0: the world box from every vertex scaled and placed.
    void UpdateWorldBBox();
    // 0x00413FF0 (slot 14): as the sector's, in the instance's space (no rejection); the point (scaled, placed)
    // and normal (turned) back in world space.
    bool CollideWithLine(const Mth::Line* line, const Mth::Vector* direction, CollisionHit* hit,
                         const Mth::BBox* lineBox);
    // 0x00411460 (slot 13) / 0x00412830 (slot 21)
    bool WithinBBox(const Mth::BBox* box) const;
    Mth::BBox* GetWorldBBox();
    // 0x00411330 (slot 23): rescaled by `scale` times the current scale, if `position` is where it is already.
    void RescaleIfAt(const Mth::Vector* position, const Mth::Vector* scale);
    // 0x00412F50 (slot 19): the geometry drawn with the scaled matrix (0x00414D60).
    void DebugRender(int a, int b);

    Mth::BBox m_worldBox;   // +0xA0
    uint8_t* m_buffer;      // +0xC0
    int m_unknownC4;        // +0xC4
};
static_assert(sizeof(CCollStaticInstance) == 0xC8 && offsetof(CCollStaticInstance, m_worldBox) == 0xA0, "layout");

// A movable triangle object (0xD4 bytes, vtable 0x00645CE0): its bounding sphere's radius and the box around it.
class CCollMovTri : public CCollMovable {
public:
    // 0x00413000 (returns this)
    CCollMovTri* Construct(CCollObjTriData* geometry);
    // 0x00413080 (returns this)
    CCollMovTri* ConstructCopy(const CCollMovTri* other);
    // 0x00413070 / 0x004143D0
    void Destruct();
    CCollMovTri* ScalarDelete(uint8_t flags);
    // 0x00414400 (slot 12): a copy with a copy of the geometry.
    CCollMovTri* Clone(int argument);
    // 0x00411780 (slot 4): the geometry and the radius from its vertices.
    void SetGeometry(CCollObjTriData* geometry);
    // 0x00412F90 (slot 5; not a function in Ghidra)
    void SetWorldPosition(const Mth::Vector* position);
    // 0x00414460 (slot 14): as the sector's, in the object's space (rejection only for 8-byte faces); the point
    // and normal turned and placed back.
    bool CollideWithLine(const Mth::Line* line, const Mth::Vector* direction, CollisionHit* hit,
                         const Mth::BBox* lineBox);
    // 0x00411810 (slot 13) / 0x00412FC0 (slot 20): the box (made again if the object moved).
    bool WithinBBox(const Mth::BBox* box);
    Mth::BBox* GetBBox();
    // 0x0040B390: the box around the sphere.
    void UpdateBBox();
    // 0x00413130 (slot 19)
    void DebugRender(int a, int b);

    float m_radius;          // +0xA4
    bool m_boxDirty;         // +0xA8
    uint8_t padA9[3];
    Mth::BBox m_box;         // +0xAC
    uint32_t m_unknownCC;    // +0xCC
    uint32_t m_unknownD0;    // +0xD0 the geometry's +0x3C
};
static_assert(sizeof(CCollMovTri) == 0xD4 && offsetof(CCollMovTri, m_box) == 0xAC, "layout must match the original");

// Slots every collision object shares that pass straight on to its geometry (m_geometry; a multi-object has none).
struct CollGeometryForwarders : CCollObj {
    // 0x0040AE80 / 0x0040AE90 (a sector's slots 22, 23): the geometry turned / scaled.
    void RotateY(const Mth::Vector* centre, int quarterTurns);
    void Scale(const Mth::Vector* centre, const Mth::Vector* scale);
    // 0x0040AEA0 (a sector's slot 19): the geometry's debug rendering (0x0048DCD0, compiled out).
    void DebugRender(int a, int b);
    // 0x0040AEB0: the geometry's occlusion polygons (CCollObjTriData::AddOcclusionPolys).
    void AddOcclusionPolys();
    // 0x0040AEC0 (slot 18 of every collision class): CCollObjTriData::IsPointBehindFaces.
    bool IsPointBehindFaces(const Mth::Vector* point, int unused);
};

// 0x00414990 (cdecl): a movable collision object made from `count` geometries (consecutive in memory): type 1 a box
// around them all, type 4 a movable triangle object (several: a multi-object of them); null for other types or
// no geometry. `value` is set through slot 11 and the object registered with it (0x004177B0).
CCollObj* CreateMovableCollision(int type, CCollObjTriData* geometries, int count, int value);

// 0x00414D60: the geometry's debug rendering (compiled out: returns).
struct CollGeometryStubs {
    void DebugRender(const Mth::Matrix* transform, int a, int b, int c);
};

} // namespace Nx
