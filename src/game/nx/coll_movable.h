#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_obj.h"

namespace Engine3D {

extern void** const kScaledVtable; // 0x00645B58: CCollScaled (coll_movable.cpp)

struct CollisionHit;
struct CollRect;

// A movable collision object (0xA4 bytes, vtable 0x00645B00; a car, a level object...): its world position and
// orientation, and the orientation's inverse (the transpose).
class CHitMovingMesh : public CHitMesh {
public:
    // 0x00411FA0: at the origin, unrotated (returns this).
    CHitMovingMesh* Construct();
    // 0x00412660: a copy (returns this).
    CHitMovingMesh* ConstructCopy(const CHitMovingMesh* other);
    // 0x004121A0: out of the caches' movables, the base.
    void Destruct();
    // 0x004135B0
    CHitMovingMesh* ScalarDelete(uint8_t flags);
    // 0x00412070 (slot 6; 0x00412FF0 jumps to it): the rotation rows (0-2) taken, the inverse made from the
    // whole matrix.
    void SetOrientation(const Maths::Matrix* orientation);
    // 0x00412050 / 0x00412060 (slots 7, 8)
    Maths::Vector* GetAbsolutePoint();
    Maths::Matrix* GetOrientation();
    // 0x00412230 (slot 9): the geometry's vertex `index` rotated and placed (w = the position's).
    Maths::Vector ReadVertexPoint(int index) const;
    // 0x004122C0: `box` in the object's space (its corners moved back and turned by the inverse).
    void BoxToLocal(const Maths::BBox* box, Maths::BBox* out) const;
    // 0x0040AE50 / 0x0040AE40 (slots 10, 11)
    int GetValueA0() const;
    void SetValueA0(int value);

    Maths::Vector m_anchorPoint;      // +0x10
    Maths::Matrix m_orientation;   // +0x20
    Maths::Matrix m_inverse;       // +0x60 the orientation's transpose
    int m_valueA0;               // +0xA0
};
static_assert(sizeof(CHitMovingMesh) == 0xA4 && offsetof(CHitMovingMesh, m_inverse) == 0x60, "layout must match");

// 0x004120D0: this = `source` transposed (in place when they're the same: 0x00412150). Returns this.
struct MatrixTranspose : Maths::Matrix {
    Maths::Matrix* TransposeFrom(const Maths::Matrix* source);
    void TransposeInPlace();
};

// A movable box (0xD0 bytes, vtable 0x00645BC0): one set of face flags and terrain for every face.
class CHitMovingBox : public CHitMovingMesh {
public:
    // 0x00412550: the default box (-50, -20, -90) .. (30, 36, 90) (returns this).
    CHitMovingBox* Construct();
    // 0x00413700: a copy (returns this).
    CHitMovingBox* ConstructCopy(const CHitMovingBox* other);
    // 0x004125F0 / 0x004136D0
    void Destruct();
    CHitMovingBox* ScalarDelete(uint8_t flags);
    // 0x00412600 (slot 4): the geometry and its box (a null geometry crashes, as in the original).
    void AssignShapeData(CTriMeshSource* geometry);
    // 0x0040AE60 / 0x0040AE70 (slots 2, 3)
    uint32_t GetTriangleAttrs(int face) const;
    uint16_t GetFaceTerrain(int face) const;
    // 0x00414BE0 (slot 12): a copy (no geometry, flag 0x8000; a failed allocation crashes, as in the original).
    CHitMovingBox* Clone(int argument);
    // 0x004137B0 (slot 14): the line (world space) against the box, in the box's space; a hit is offered to
    // `hit` (ReportHit) with its point and normal back in world space. The direction and the line's box unused.
    bool CollideWithLine(const Maths::Line* line, const Maths::Vector* direction, CollisionHit* hit,
                         const Maths::BBox* lineBox);
    // 0x004126E0 (slot 19): the box drawn where the object is (0x0048D090; the arguments unused).
    void DrawDiagnostic(int a, int b);

    Maths::BBox m_box;            // +0xA4
    uint32_t m_faceFlags;       // +0xC4
    uint16_t m_surfaceType;         // +0xC8
    uint8_t m_unknownCA[4];     // +0xCA (copied, not otherwise used here)
    uint8_t padCE[2];
};
static_assert(sizeof(CHitMovingBox) == 0xD0 && offsetof(CHitMovingBox, m_faceFlags) == 0xC4, "layout must match");

// A scaled, rotated and placed collision object (0xA0 bytes, vtable 0x00645B58), with the inverse transform.
class CCollScaled : public CHitMesh {
public:
    // 0x004123C0: unscaled, unrotated, at the origin (returns this).
    CCollScaled* Construct();
    // 0x00413F80: a copy (returns this).
    CCollScaled* ConstructCopy(const CCollScaled* other);
    // 0x00412460 / 0x004135E0
    void Destruct();
    CCollScaled* ScalarDelete(uint8_t flags);
    // 0x00413610 / 0x00413640 / 0x004136A0 (slots 5, 6, 24): the position / rotation rows / scale, then the
    // inverse made again.
    void PlaceAtAbsolutePoint(const Maths::Vector* position);
    void SetOrientation(const Maths::Matrix* orientation);
    void ChangeScale(const Maths::Vector* scale);
    // 0x00412490: the inverse: the rotation's transpose divided by the scale, the position turned back.
    void UpdateInverse();
    // 0x00410A30: `line` (both ends, w included) through the inverse - into the object's space.
    void LineToLocal(const Maths::Line* line, Maths::Line* out) const;
    // 0x00410AA0: `rect` into the object's space: its corner and the far ends of its sides through the inverse,
    // the sides measured from the new corner.
    void RectToLocal(const CollRect* rect, CollRect* out) const;
    // 0x004109C0 / 0x00412060 / 0x00412050 (slots 7, 8, 25)
    Maths::Vector* GetAbsolutePoint();
    // 0x004109D0 (slot 9): vertex `index` scaled (w the scale's w), through the matrix.
    Maths::Vector ReadVertexPoint(int index) const;

    Maths::Vector m_sizeFactor;        // +0x10
    Maths::Matrix m_basisFrame;       // +0x20 (row 3, +0x50, the position)
    Maths::Matrix m_inverse;      // +0x60 (row 3, +0x90, the position turned back)
};
static_assert(sizeof(CCollScaled) == 0xA0 && offsetof(CCollScaled, m_inverse) == 0x60, "layout must match");

} // namespace Nx
