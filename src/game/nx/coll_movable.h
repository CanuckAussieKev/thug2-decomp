#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_obj.h"

namespace Nx {

struct CollisionHit;
struct CollRect;

// A movable collision object (0xA4 bytes, vtable 0x00645B00; a car, a level object...): its world position and
// orientation, and the orientation's inverse (the transpose).
class CCollMovable : public CCollObj {
public:
    // 0x00411FA0: at the origin, unrotated (returns this).
    CCollMovable* Construct();
    // 0x00412660: a copy (returns this).
    CCollMovable* ConstructCopy(const CCollMovable* other);
    // 0x004121A0: out of the caches' movables, the base.
    void Destruct();
    // 0x004135B0
    CCollMovable* ScalarDelete(uint8_t flags);
    // 0x00412070 (slot 6; 0x00412FF0 jumps to it): the rotation rows (0-2) taken, the inverse made from the
    // whole matrix.
    void SetOrientation(const Mth::Matrix* orientation);
    // 0x00412050 / 0x00412060 (slots 7, 8)
    Mth::Vector* GetWorldPosition();
    Mth::Matrix* GetOrientation();
    // 0x00412230 (slot 9): the geometry's vertex `index` rotated and placed (w = the position's).
    Mth::Vector GetVertexPos(int index) const;
    // 0x004122C0: `box` in the object's space (its corners moved back and turned by the inverse).
    void BoxToLocal(const Mth::BBox* box, Mth::BBox* out) const;
    // 0x0040AE50 / 0x0040AE40 (slots 10, 11)
    int GetValueA0() const;
    void SetValueA0(int value);

    Mth::Vector m_position;      // +0x10
    Mth::Matrix m_orientation;   // +0x20
    Mth::Matrix m_inverse;       // +0x60 the orientation's transpose
    int m_valueA0;               // +0xA0
};
static_assert(sizeof(CCollMovable) == 0xA4 && offsetof(CCollMovable, m_inverse) == 0x60, "layout must match");

// 0x004120D0: this = `source` transposed (in place when they're the same: 0x00412150). Returns this.
struct MatrixTranspose : Mth::Matrix {
    Mth::Matrix* TransposeFrom(const Mth::Matrix* source);
    void TransposeInPlace();
};

// A movable box (0xD0 bytes, vtable 0x00645BC0): one set of face flags and terrain for every face.
class CCollMovBBox : public CCollMovable {
public:
    // 0x00412550: the default box (-50, -20, -90) .. (30, 36, 90) (returns this).
    CCollMovBBox* Construct();
    // 0x00413700: a copy (returns this).
    CCollMovBBox* ConstructCopy(const CCollMovBBox* other);
    // 0x004125F0 / 0x004136D0
    void Destruct();
    CCollMovBBox* ScalarDelete(uint8_t flags);
    // 0x00412600 (slot 4): the geometry and its box (a null geometry crashes, as in the original).
    void SetGeometry(CCollObjTriData* geometry);
    // 0x0040AE60 / 0x0040AE70 (slots 2, 3)
    uint32_t GetFaceFlags(int face) const;
    uint16_t GetFaceTerrain(int face) const;
    // 0x00414BE0 (slot 12): a copy (no geometry, flag 0x8000; a failed allocation crashes, as in the original).
    CCollMovBBox* Clone(int argument);
    // 0x004137B0 (slot 14): the line (world space) against the box, in the box's space; a hit is offered to
    // `hit` (ReportHit) with its point and normal back in world space. The direction and the line's box unused.
    bool CollideWithLine(const Mth::Line* line, const Mth::Vector* direction, CollisionHit* hit,
                         const Mth::BBox* lineBox);
    // 0x004126E0 (slot 19): the box drawn where the object is (0x0048D090; the arguments unused).
    void DebugRender(int a, int b);

    Mth::BBox m_box;            // +0xA4
    uint32_t m_faceFlags;       // +0xC4
    uint16_t m_terrain;         // +0xC8
    uint8_t m_unknownCA[4];     // +0xCA (copied, not otherwise used here)
    uint8_t padCE[2];
};
static_assert(sizeof(CCollMovBBox) == 0xD0 && offsetof(CCollMovBBox, m_faceFlags) == 0xC4, "layout must match");

// A scaled, rotated and placed collision object (0xA0 bytes, vtable 0x00645B58), with the inverse transform.
class CCollScaled : public CCollObj {
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
    void SetWorldPosition(const Mth::Vector* position);
    void SetOrientation(const Mth::Matrix* orientation);
    void SetScale(const Mth::Vector* scale);
    // 0x00412490: the inverse: the rotation's transpose divided by the scale, the position turned back.
    void UpdateInverse();
    // 0x00410A30: `line` (both ends, w included) through the inverse - into the object's space.
    void LineToLocal(const Mth::Line* line, Mth::Line* out) const;
    // 0x00410AA0: `rect` into the object's space: its corner and the far ends of its sides through the inverse,
    // the sides measured from the new corner.
    void RectToLocal(const CollRect* rect, CollRect* out) const;
    // 0x004109C0 / 0x00412060 / 0x00412050 (slots 7, 8, 25)
    Mth::Vector* GetWorldPosition();
    // 0x004109D0 (slot 9): vertex `index` scaled (w the scale's w), through the matrix.
    Mth::Vector GetVertexPos(int index) const;

    Mth::Vector m_scale;        // +0x10
    Mth::Matrix m_matrix;       // +0x20 (row 3, +0x50, the position)
    Mth::Matrix m_inverse;      // +0x60 (row 3, +0x90, the position turned back)
};
static_assert(sizeof(CCollScaled) == 0xA0 && offsetof(CCollScaled, m_inverse) == 0x60, "layout must match");

} // namespace Nx
