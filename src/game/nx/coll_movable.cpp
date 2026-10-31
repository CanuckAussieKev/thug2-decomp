// Movable collision objects: the movable base, the movable box and the scaled object.

#include "game/nx/coll_movable.h"

#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_tri.h"
#include "game/core/stubs.h"
#include "game/core/game_vtables.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/nx/coll_obj.h"
#include "hook/game_code.h"
#include "game/nx/feeler.h"

namespace Nx {

namespace {

GAME_VTABLE(kMovableVtable, 0x00645B00, &Nx::CCollMovable::ScalarDelete, Stubs::ReturnFalse,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), &Nx::CCollObj::SetGeometry, &Nx::CFeeler::SetEnd,
    &Nx::CCollMovable::SetOrientation, &Nx::CCollMovable::GetWorldPosition, &Nx::CCollMovable::GetOrientation,
    &Nx::CCollMovable::GetVertexPos, &Nx::CCollMovable::GetValueA0, &Nx::CCollMovable::SetValueA0,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), Stubs::ReturnZero);

void SetIdentity(Mth::Matrix& m)
{
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x0048D090: a box drawn with a transform (debug rendering; not rewritten yet).
void DrawBox(const Mth::BBox* box, const Mth::Matrix* transform, uint32_t colour, int flag)
{
    (void)box;
    Stubs::IgnoreArguments3(reinterpret_cast<int>(transform), static_cast<int>(colour), flag);
}

} // namespace

GAME_VTABLE(kScaledVtable, 0x00645B58, &Nx::CCollScaled::ScalarDelete, Stubs::ReturnFalse,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), &Nx::CCollObj::SetGeometry,
    &Nx::CCollScaled::SetWorldPosition, &Nx::CCollScaled::SetOrientation, &Nx::CCollScaled::GetWorldPosition,
    &Nx::CCollMovable::GetOrientation, &Nx::CCollScaled::GetVertexPos, Stubs::ReturnZero, Stubs::IgnoreArgument,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), Stubs::ReturnZero, GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), &Nx::CCollScaled::SetScale,
    &Nx::CCollMovable::GetWorldPosition);

// 0x00411FA0
CCollMovable* CCollMovable::Construct()
{
    m_vtable = kMovableVtable;
    m_flags = 0;
    m_geometry = nullptr;
    m_position = {0.0f, 0.0f, 0.0f, 1.0f};
    m_valueA0 = 0;
    SetIdentity(m_orientation);
    SetIdentity(m_inverse);
    return this;
}

// 0x00412660
CCollMovable* CCollMovable::ConstructCopy(const CCollMovable* other)
{
    m_vtable = kCollObjVtable;
    m_checksum = other->m_checksum;
    m_flags = other->m_flags;
    m_visitStamp = other->m_visitStamp;
    m_geometry = other->m_geometry;
    m_vtable = kMovableVtable;
    m_position = other->m_position;
    m_orientation = other->m_orientation;
    m_inverse = other->m_inverse;
    m_valueA0 = other->m_valueA0;
    return this;
}

// 0x004121A0
void CCollMovable::Destruct()
{
    m_vtable = kMovableVtable;
    RemoveMovableFromCaches(this);
    DestroyBase();
}

// 0x004135B0
CCollMovable* CCollMovable::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00412070
void CCollMovable::SetOrientation(const Mth::Matrix* orientation)
{
    m_orientation.row[0] = orientation->row[0];
    m_orientation.row[1] = orientation->row[1];
    m_orientation.row[2] = orientation->row[2];
    static_cast<MatrixTranspose*>(&m_inverse)->TransposeFrom(&m_orientation);
}

// 0x00412050
Mth::Vector* CCollMovable::GetWorldPosition()
{
    return &m_position;
}

// 0x00412060
Mth::Matrix* CCollMovable::GetOrientation()
{
    return &m_orientation;
}

// 0x00412230
Mth::Vector CCollMovable::GetVertexPos(int index) const
{
    Mth::Vector raw = m_geometry->GetRawVertexPos(index);
    raw.w = 0.0f;
    Mth::Vector turned;
    m_orientation.RotateVector(turned, raw);
    return {turned.x + m_position.x, turned.y + m_position.y, turned.z + m_position.z, turned.w + m_position.w};
}

// 0x004122C0
void CCollMovable::BoxToLocal(const Mth::BBox* box, Mth::BBox* out) const
{
    *out = *box;
    out->min.x -= m_position.x;
    out->min.y -= m_position.y;
    out->min.z -= m_position.z;
    out->min.w -= m_position.w;
    out->max.x -= m_position.x;
    out->max.y -= m_position.y;
    out->max.z -= m_position.z;
    out->max.w -= m_position.w;
    Mth::Vector turned;
    out->min = m_inverse.RotateVector(turned, out->min);
    out->max = m_inverse.RotateVector(turned, out->max);
}

// 0x0040AE50
int CCollMovable::GetValueA0() const
{
    return m_valueA0;
}

// 0x0040AE40
void CCollMovable::SetValueA0(int value)
{
    m_valueA0 = value;
}

// 0x004120D0
Mth::Matrix* MatrixTranspose::TransposeFrom(const Mth::Matrix* source)
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
CCollMovBBox* CCollMovBBox::Construct()
{
    CCollMovable::Construct();
    m_vtable = Vt::kCollMovBBox;
    new (&m_box) Mth::BBox();
    m_faceFlags = 0;
    m_terrain = 0;
    m_box.min = {-50.0f, -20.0f, -90.0f, 1.0f};
    m_box.max = {30.0f, 36.0f, 90.0f, 1.0f};
    return this;
}

// 0x00413700
CCollMovBBox* CCollMovBBox::ConstructCopy(const CCollMovBBox* other)
{
    CCollMovable::ConstructCopy(other);
    m_vtable = Vt::kCollMovBBox;
    m_box = other->m_box;
    m_faceFlags = other->m_faceFlags;
    m_terrain = other->m_terrain;
    for (int i = 0; i < 4; ++i)
        m_unknownCA[i] = other->m_unknownCA[i];
    return this;
}

// 0x004125F0
void CCollMovBBox::Destruct()
{
    m_vtable = Vt::kCollMovBBox;
    CCollMovable::Destruct();
}

// 0x004136D0
CCollMovBBox* CCollMovBBox::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00412600
void CCollMovBBox::SetGeometry(CCollObjTriData* geometry)
{
    m_geometry = geometry;
    if (geometry)
        m_checksum = geometry->m_checksum;
    m_box.min = geometry->m_min;
    m_box.max = geometry->m_max;
}

// 0x0040AE60
uint32_t CCollMovBBox::GetFaceFlags(int /*face*/) const
{
    return m_faceFlags;
}

// 0x0040AE70
uint16_t CCollMovBBox::GetFaceTerrain(int /*face*/) const
{
    return m_terrain;
}

// 0x00414BE0
CCollMovBBox* CCollMovBBox::Clone(int)
{
    void* memory = Mem::New(sizeof(CCollMovBBox));
    CCollMovBBox* copy = memory ? static_cast<CCollMovBBox*>(memory)->ConstructCopy(this) : nullptr;
    copy->m_flags |= 0x8000;
    copy->m_geometry = nullptr;
    return copy;
}

// 0x004126E0
void CCollMovBBox::DebugRender(int /*a*/, int /*b*/)
{
    Mth::Matrix transform = m_orientation;
    transform.row[3] = m_position;
    DrawBox(&m_box, &transform, 0x80808080, 1);
}

// 0x004123C0
CCollScaled* CCollScaled::Construct()
{
    m_vtable = kScaledVtable;
    m_flags = 0;
    m_geometry = nullptr;
    m_scale = {1.0f, 1.0f, 1.0f, 0.0f};
    SetIdentity(m_matrix);
    SetIdentity(m_inverse);
    return this;
}

// 0x00413F80
CCollScaled* CCollScaled::ConstructCopy(const CCollScaled* other)
{
    m_vtable = kCollObjVtable;
    m_checksum = other->m_checksum;
    m_flags = other->m_flags;
    m_visitStamp = other->m_visitStamp;
    m_geometry = other->m_geometry;
    m_vtable = kScaledVtable;
    m_scale = other->m_scale;
    m_matrix = other->m_matrix;
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
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00413610
void CCollScaled::SetWorldPosition(const Mth::Vector* position)
{
    m_matrix.row[3] = *position;
    UpdateInverse();
}

// 0x00413640
void CCollScaled::SetOrientation(const Mth::Matrix* orientation)
{
    m_matrix.row[0] = orientation->row[0];
    m_matrix.row[1] = orientation->row[1];
    m_matrix.row[2] = orientation->row[2];
    UpdateInverse();
}

// 0x004136A0
void CCollScaled::SetScale(const Mth::Vector* scale)
{
    m_scale = *scale;
    UpdateInverse();
}

// 0x00410A30
void CCollScaled::LineToLocal(const Mth::Line* line, Mth::Line* out) const
{
    Mth::Vector moved;
    m_inverse.Transform(moved, line->start);
    out->start = moved;
    m_inverse.Transform(moved, line->end);
    out->end = moved;
}

// 0x00412490 (each 1 / scale kept unrounded for its row's three products)
void CCollScaled::UpdateInverse()
{
    for (int r = 0; r < 3; ++r) {
        double inverse = 1.0 / (&m_scale.x)[r];
        (&m_inverse.row[0].x)[r] = static_cast<float>(inverse * m_matrix.row[r].x);
        (&m_inverse.row[1].x)[r] = static_cast<float>(inverse * m_matrix.row[r].y);
        (&m_inverse.row[2].x)[r] = static_cast<float>(inverse * m_matrix.row[r].z);
    }
    Mth::Vector& back = m_inverse.row[3];
    back.x = -m_matrix.row[3].x;
    back.y = -m_matrix.row[3].y;
    back.z = -m_matrix.row[3].z;
    Mth::Vector turned;
    back = m_inverse.RotateVector(turned, back);
}

// 0x004109C0
Mth::Vector* CCollScaled::GetWorldPosition()
{
    return &m_matrix.row[3];
}

// 0x004109D0
Mth::Vector CCollScaled::GetVertexPos(int index) const
{
    Mth::Vector raw = m_geometry->GetRawVertexPos(index);
    Mth::Vector scaled{raw.x * m_scale.x, raw.y * m_scale.y, raw.z * m_scale.z, m_scale.w};
    Mth::Vector out;
    m_matrix.Transform(out, scaled);
    return out;
}

} // namespace Nx

REPLACE(0x00411FA0, &Nx::CCollMovable::Construct);
REPLACE(0x00412660, &Nx::CCollMovable::ConstructCopy);
REPLACE(0x004121A0, &Nx::CCollMovable::Destruct);
REPLACE(0x004135B0, &Nx::CCollMovable::ScalarDelete);
REPLACE(0x00412070, &Nx::CCollMovable::SetOrientation);
REPLACE_SOURCE_ONLY(0x00412FF0, &Nx::CCollMovable::SetOrientation); // a jump to 0x00412070
REPLACE_SOURCE_ONLY(0x00412050, &Nx::CCollMovable::GetWorldPosition); // 4 bytes (also the scaled object's GetScale)
REPLACE_SOURCE_ONLY(0x00412060, &Nx::CCollMovable::GetOrientation);   // 4 bytes (also the scaled object's)
REPLACE(0x00412230, &Nx::CCollMovable::GetVertexPos);
REPLACE(0x004122C0, &Nx::CCollMovable::BoxToLocal);
REPLACE(0x0040AE50, &Nx::CCollMovable::GetValueA0);
REPLACE(0x0040AE40, &Nx::CCollMovable::SetValueA0);
REPLACE(0x004120D0, &Nx::MatrixTranspose::TransposeFrom);
REPLACE(0x00412150, &Nx::MatrixTranspose::TransposeInPlace);
REPLACE(0x00412550, &Nx::CCollMovBBox::Construct);
REPLACE(0x00413700, &Nx::CCollMovBBox::ConstructCopy);
REPLACE(0x004125F0, &Nx::CCollMovBBox::Destruct);
REPLACE(0x004136D0, &Nx::CCollMovBBox::ScalarDelete);
REPLACE(0x00412600, &Nx::CCollMovBBox::SetGeometry);
REPLACE(0x0040AE60, &Nx::CCollMovBBox::GetFaceFlags);
REPLACE(0x0040AE70, &Nx::CCollMovBBox::GetFaceTerrain);
REPLACE(0x004126E0, &Nx::CCollMovBBox::DebugRender);
REPLACE(0x00414BE0, &Nx::CCollMovBBox::Clone);
REPLACE(0x00410A30, &Nx::CCollScaled::LineToLocal);
REPLACE(0x004123C0, &Nx::CCollScaled::Construct);
REPLACE(0x00413F80, &Nx::CCollScaled::ConstructCopy);
REPLACE(0x00412460, &Nx::CCollScaled::Destruct);
REPLACE(0x004135E0, &Nx::CCollScaled::ScalarDelete);
REPLACE(0x00413610, &Nx::CCollScaled::SetWorldPosition);
REPLACE(0x00413640, &Nx::CCollScaled::SetOrientation);
REPLACE(0x004136A0, &Nx::CCollScaled::SetScale);
REPLACE(0x00412490, &Nx::CCollScaled::UpdateInverse);
REPLACE_SOURCE_ONLY(0x004109C0, &Nx::CCollScaled::GetWorldPosition); // 4 bytes
REPLACE(0x004109D0, &Nx::CCollScaled::GetVertexPos);
