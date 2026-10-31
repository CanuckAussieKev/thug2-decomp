// Movable collision objects: the movable base, the movable box and the scaled object.

#include "game/engine3d/coll_movable.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/engine3d/coll_cache.h"
#include "game/engine3d/coll_tri.h"
#include "game/core/stubs.h"
#include "game/core/game_vtables.h"
#include "game/core/code_address.h"
#include "game/engine3d/coll_obj.h"
#include "game/engine3d/probe_ray.h"

namespace Engine3D {

namespace {

static VtableSlot kMovableVtable_slots[] = {&Engine3D::CHitMovingMesh::ScalarDelete, Stubs::ReturnFalse,
        &_purecall, &_purecall, &Engine3D::CHitMesh::AssignShapeData, &Engine3D::CProbeRay::ChangeFinish,
        &Engine3D::CHitMovingMesh::ChangeFacing, &Engine3D::CHitMovingMesh::GetAbsolutePoint, &Engine3D::CHitMovingMesh::FetchFacing,
        &Engine3D::CHitMovingMesh::ReadVertexPoint, &Engine3D::CHitMovingMesh::GetValueA0, &Engine3D::CHitMovingMesh::SetValueA0,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, Stubs::ReturnZero};
void** const kMovableVtable = reinterpret_cast<void**>(kMovableVtable_slots);

void SetIdentity(Maths::Mat4& m)
{
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x0048D090: a box drawn with a transform (debug rendering; not rewritten yet).
void DrawBox(const Maths::BBox* box, const Maths::Mat4* transform, uint32_t colour, int flag)
{
    (void)box;
    Stubs::IgnoreArguments3(reinterpret_cast<int>(transform), static_cast<int>(colour), flag);
}

} // namespace

static VtableSlot kScaledVtable_slots[] = {&Engine3D::CCollScaled::ScalarDelete, Stubs::ReturnFalse,
        &_purecall, &_purecall, &Engine3D::CHitMesh::AssignShapeData,
        &Engine3D::CCollScaled::PlaceAtAbsolutePoint, &Engine3D::CCollScaled::ChangeFacing, &Engine3D::CCollScaled::GetAbsolutePoint,
        &Engine3D::CHitMovingMesh::FetchFacing, &Engine3D::CCollScaled::ReadVertexPoint, Stubs::ReturnZero, Stubs::IgnoreArgument,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, Stubs::ReturnZero, &_purecall,
        &_purecall, &_purecall, &Engine3D::CCollScaled::ChangeScale,
        &Engine3D::CHitMovingMesh::GetAbsolutePoint};
void** const kScaledVtable = reinterpret_cast<void**>(kScaledVtable_slots);

// 0x00411FA0
CHitMovingMesh* CHitMovingMesh::MakeNew()
{
    m_vtable = kMovableVtable;
    m_flagBits = 0;
    m_geometry = nullptr;
    m_anchorPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_valueA0 = 0;
    SetIdentity(m_orientation);
    SetIdentity(m_inverse);
    return this;
}

// 0x00412660
CHitMovingMesh* CHitMovingMesh::ConstructCopy(const CHitMovingMesh* other)
{
    m_vtable = kCollObjVtable;
    m_checksum = other->m_checksum;
    m_flagBits = other->m_flagBits;
    m_visitStamp = other->m_visitStamp;
    m_geometry = other->m_geometry;
    m_vtable = kMovableVtable;
    m_anchorPoint = other->m_anchorPoint;
    m_orientation = other->m_orientation;
    m_inverse = other->m_inverse;
    m_valueA0 = other->m_valueA0;
    return this;
}

// 0x004121A0
void CHitMovingMesh::Destruct()
{
    m_vtable = kMovableVtable;
    RemoveMovableFromCaches(this);
    DestroyBase();
}

// 0x004135B0
CHitMovingMesh* CHitMovingMesh::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00412070
void CHitMovingMesh::ChangeFacing(const Maths::Mat4* orientation)
{
    m_orientation.row[0] = orientation->row[0];
    m_orientation.row[1] = orientation->row[1];
    m_orientation.row[2] = orientation->row[2];
    static_cast<MatrixTranspose*>(&m_inverse)->TransposeFrom(&m_orientation);
}

// 0x00412050
Maths::Vec4* CHitMovingMesh::GetAbsolutePoint()
{
    return &m_anchorPoint;
}

// 0x00412060
Maths::Mat4* CHitMovingMesh::FetchFacing()
{
    return &m_orientation;
}

// 0x00412230
Maths::Vec4 CHitMovingMesh::ReadVertexPoint(int index) const
{
    Maths::Vec4 raw = m_geometry->ReadStoredVertex(index);
    raw.w = 0.0f;
    Maths::Vec4 turned;
    m_orientation.RotateVector(turned, raw);
    return {turned.x + m_anchorPoint.x, turned.y + m_anchorPoint.y, turned.z + m_anchorPoint.z, turned.w + m_anchorPoint.w};
}

// 0x004122C0
void CHitMovingMesh::BoxToLocal(const Maths::BBox* box, Maths::BBox* out) const
{
    *out = *box;
    out->min.x -= m_anchorPoint.x;
    out->min.y -= m_anchorPoint.y;
    out->min.z -= m_anchorPoint.z;
    out->min.w -= m_anchorPoint.w;
    out->max.x -= m_anchorPoint.x;
    out->max.y -= m_anchorPoint.y;
    out->max.z -= m_anchorPoint.z;
    out->max.w -= m_anchorPoint.w;
    Maths::Vec4 turned;
    out->min = m_inverse.RotateVector(turned, out->min);
    out->max = m_inverse.RotateVector(turned, out->max);
}

// 0x0040AE50
int CHitMovingMesh::GetValueA0() const
{
    return m_valueA0;
}

// 0x0040AE40
void CHitMovingMesh::SetValueA0(int value)
{
    m_valueA0 = value;
}

// 0x004120D0
Maths::Mat4* MatrixTranspose::TransposeFrom(const Maths::Mat4* source)
{
    if (source == this) {
        TransposeInPlace();
        return this;
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c)
            (&row[r].x)[c] = (&source->row[c].x)[r];
    }
    return this;
}

// 0x00412150
void MatrixTranspose::TransposeInPlace()
{
    for (int r = 0; r < 4; ++r) {
        for (int c = r + 1; c < 4; ++c) {
            float swap = (&row[r].x)[c];
            (&row[r].x)[c] = (&row[c].x)[r];
            (&row[c].x)[r] = swap;
        }
    }
}

// 0x00412550
CHitMovingBox* CHitMovingBox::MakeNew()
{
    CHitMovingMesh::MakeNew();
    m_vtable = Vt::kCollMovBBox;
    new (&m_box) Maths::BBox();
    m_faceFlags = 0;
    m_surfaceType = 0;
    m_box.min = {-50.0f, -20.0f, -90.0f, 1.0f};
    m_box.max = {30.0f, 36.0f, 90.0f, 1.0f};
    return this;
}

// 0x00413700
CHitMovingBox* CHitMovingBox::ConstructCopy(const CHitMovingBox* other)
{
    CHitMovingMesh::ConstructCopy(other);
    m_vtable = Vt::kCollMovBBox;
    m_box = other->m_box;
    m_faceFlags = other->m_faceFlags;
    m_surfaceType = other->m_surfaceType;
    for (int i = 0; i < 4; ++i)
        m_unknownCA[i] = other->m_unknownCA[i];
    return this;
}

// 0x004125F0
void CHitMovingBox::Destruct()
{
    m_vtable = Vt::kCollMovBBox;
    CHitMovingMesh::Destruct();
}

// 0x004136D0
CHitMovingBox* CHitMovingBox::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00412600
void CHitMovingBox::AssignShapeData(CTriMeshSource* geometry)
{
    m_geometry = geometry;
    if (geometry)
        m_checksum = geometry->m_checksum;
    m_box.min = geometry->m_lowerLimit;
    m_box.max = geometry->m_upperLimit;
}

// 0x0040AE60
uint32_t CHitMovingBox::GetTriangleAttrs(int /*face*/) const
{
    return m_faceFlags;
}

// 0x0040AE70
uint16_t CHitMovingBox::GetFaceTerrain(int /*face*/) const
{
    return m_surfaceType;
}

// 0x00414BE0
CHitMovingBox* CHitMovingBox::Replicate(int)
{
    void* memory = MemSys::Alloc(sizeof(CHitMovingBox));
    CHitMovingBox* copy = memory ? static_cast<CHitMovingBox*>(memory)->ConstructCopy(this) : nullptr;
    copy->m_flagBits |= 0x8000;
    copy->m_geometry = nullptr;
    return copy;
}

// 0x004126E0
void CHitMovingBox::DrawDiagnostic(int /*a*/, int /*b*/)
{
    Maths::Mat4 transform = m_orientation;
    transform.row[3] = m_anchorPoint;
    DrawBox(&m_box, &transform, 0x80808080, 1);
}

// 0x004123C0
CCollScaled* CCollScaled::MakeNew()
{
    m_vtable = kScaledVtable;
    m_flagBits = 0;
    m_geometry = nullptr;
    m_sizeFactor = {1.0f, 1.0f, 1.0f, 0.0f};
    SetIdentity(m_basisFrame);
    SetIdentity(m_inverse);
    return this;
}

// 0x00413F80
CCollScaled* CCollScaled::ConstructCopy(const CCollScaled* other)
{
    m_vtable = kCollObjVtable;
    m_checksum = other->m_checksum;
    m_flagBits = other->m_flagBits;
    m_visitStamp = other->m_visitStamp;
    m_geometry = other->m_geometry;
    m_vtable = kScaledVtable;
    m_sizeFactor = other->m_sizeFactor;
    m_basisFrame = other->m_basisFrame;
    m_inverse = other->m_inverse;
    return this;
}

// 0x00412460 (only the base's body)
void CCollScaled::Destruct()
{
    DestroyBase();
}

// 0x004135E0
CCollScaled* CCollScaled::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00413610
void CCollScaled::PlaceAtAbsolutePoint(const Maths::Vec4* position)
{
    m_basisFrame.row[3] = *position;
    UpdateInverse();
}

// 0x00413640
void CCollScaled::ChangeFacing(const Maths::Mat4* orientation)
{
    m_basisFrame.row[0] = orientation->row[0];
    m_basisFrame.row[1] = orientation->row[1];
    m_basisFrame.row[2] = orientation->row[2];
    UpdateInverse();
}

// 0x004136A0
void CCollScaled::ChangeScale(const Maths::Vec4* scale)
{
    m_sizeFactor = *scale;
    UpdateInverse();
}

// 0x00410A30
void CCollScaled::LineToLocal(const Maths::Seg* line, Maths::Seg* out) const
{
    Maths::Vec4 moved;
    m_inverse.Xform(moved, line->start);
    out->start = moved;
    m_inverse.Xform(moved, line->end);
    out->end = moved;
}

// 0x00412490 (each 1 / scale kept unrounded for its row's three products)
void CCollScaled::UpdateInverse()
{
    for (int r = 0; r < 3; ++r) {
        double inverse = 1.0 / (&m_sizeFactor.x)[r];
        (&m_inverse.row[0].x)[r] = static_cast<float>(inverse * m_basisFrame.row[r].x);
        (&m_inverse.row[1].x)[r] = static_cast<float>(inverse * m_basisFrame.row[r].y);
        (&m_inverse.row[2].x)[r] = static_cast<float>(inverse * m_basisFrame.row[r].z);
    }
    Maths::Vec4& back = m_inverse.row[3];
    back.x = -m_basisFrame.row[3].x;
    back.y = -m_basisFrame.row[3].y;
    back.z = -m_basisFrame.row[3].z;
    Maths::Vec4 turned;
    back = m_inverse.RotateVector(turned, back);
}

// 0x004109C0
Maths::Vec4* CCollScaled::GetAbsolutePoint()
{
    return &m_basisFrame.row[3];
}

// 0x004109D0
Maths::Vec4 CCollScaled::ReadVertexPoint(int index) const
{
    Maths::Vec4 raw = m_geometry->ReadStoredVertex(index);
    Maths::Vec4 scaled{raw.x * m_sizeFactor.x, raw.y * m_sizeFactor.y, raw.z * m_sizeFactor.z, m_sizeFactor.w};
    Maths::Vec4 out;
    m_basisFrame.Xform(out, scaled);
    return out;
}

} // namespace Nx
