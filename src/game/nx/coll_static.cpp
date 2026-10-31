// Triangle collision objects: a level sector's, an instance of one, a movable one.

#include "game/nx/coll_static.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/coll_tri.h"
#include "game/nx/coll_world.h"
#include "game/core/stubs.h"
#include "game/core/game_vtables.h"
#include "game/core/code_address.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_movable.h"
#include "game/sys/startup_init.h"

namespace Nx {

Mth::Vector g_sharedOrigin;        // the origin and orientation every static object reports
Mth::Matrix g_sharedOrientation;

namespace {

static VtableSlot kInstanceVtable_slots[] = {&Nx::CCollStaticInstance::ScalarDelete, Stubs::ReturnTrue,
        &Nx::CollFaceAccess::GetFaceFlags, &Nx::CollFaceAccess::GetFaceTerrain, &Nx::CCollObj::SetGeometry,
        &Nx::CCollStaticInstance::SetWorldPosition, &Nx::CCollStaticInstance::SetOrientation,
        &Nx::CCollScaled::GetWorldPosition, &Nx::CCollMovable::GetOrientation, &Nx::CCollScaled::GetVertexPos,
        Stubs::ReturnZero, Stubs::IgnoreArgument, &Nx::CCollStaticInstance::Clone, &Nx::CCollStaticInstance::WithinBBox,
        &Nx::CCollStaticInstance::CollideWithLine, &Nx::CCollStaticInstance::CollideWithRect,
        &Nx::CCollStatic::CollideWithSphere, &Nx::CCollStatic::CollideWithCapsule,
        &Nx::CollGeometryForwarders::IsPointBehindFaces, &Nx::CCollStaticInstance::DebugRender, Stubs::ReturnZero,
        &Nx::CCollStaticInstance::GetWorldBBox, Stubs::IgnoreArguments2, &Nx::CCollStaticInstance::RescaleIfAt,
        &Nx::CCollStaticInstance::SetScale, &Nx::CCollMovable::GetWorldPosition};
void** const kInstanceVtable = reinterpret_cast<void**>(kInstanceVtable_slots);

// The geometry's own methods: 0x00416BF0 a copy, 0x00416400 moved.
CCollObjTriData* CloneGeometry(CCollObjTriData* geometry, int a, int b) { return geometry->Clone(a, b != 0); }
void TranslateGeometry(CCollObjTriData* geometry, const Mth::Vector* delta) { geometry->Translate(delta); }
// 0x00414D60 through our stub (the original's is the same empty function).
void DrawGeometry(CCollObjTriData* geometry, const Mth::Matrix* transform, int a, int b)
{
    reinterpret_cast<CollGeometryStubs*>(geometry)->DebugRender(transform, a, b, 1);
}
const uint8_t* Face(const CCollObjTriData* geometry, int face)
{
    return geometry->m_faces + face * (geometry->m_smallFaces ? 8 : 10);
}

} // namespace

// 0x00410BF0
uint32_t CollFaceAccess::GetFaceFlags(int face) const
{
    return *reinterpret_cast<const uint16_t*>(Face(m_geometry, face));
}

// 0x00411750
uint16_t CollFaceAccess::GetFaceTerrain(int face) const
{
    return *reinterpret_cast<const uint16_t*>(Face(m_geometry, face) + 2);
}

// 0x00412730 (not a function in Ghidra: the array constructor's element constructor)
CCollStatic* CCollStatic::Construct()
{
    m_flags = 0;
    m_geometry = nullptr;
    m_vtable = Vt::kCollStatic;
    m_buffer = nullptr;
    m_unknown14 = 0;
    return this;
}

// 0x00412760
void CCollStatic::Destruct()
{
    m_vtable = Vt::kCollStatic;
    if ((m_flags & kOwnsGeometry) && m_buffer) {
        Mem::FreeMalloc(m_buffer);
        m_buffer = nullptr;
    }
    DestroyBase();
}

// 0x004139A0
CCollObj* CCollStatic::Clone(bool instance)
{
    CCollObjTriData* geometry = instance ? m_geometry : CloneGeometry(m_geometry, 0, 1);
    if (!geometry)
        return nullptr;
    CCollStaticInstance* copyInstance = nullptr;
    CCollStatic* copy = nullptr;
    CCollObj* made;
    if (instance) {
        void* memory = Mem::New(sizeof(CCollStaticInstance));
        copyInstance = memory ? static_cast<CCollStaticInstance*>(memory)->Construct() : nullptr;
        made = copyInstance;
    } else {
        auto* memory = static_cast<CCollStatic*>(Mem::New(sizeof(CCollStatic)));
        if (memory) {
            memory->m_flags = 0;
            memory->m_geometry = nullptr;
            memory->m_vtable = Vt::kCollStatic;
            memory->m_buffer = nullptr;
            memory->m_unknown14 = 0;
        }
        copy = memory;
        made = copy;
    }
    // (a failed allocation crashes here, as in the original)
    made->m_checksum = m_checksum;
    made->m_flags = m_flags;
    made->m_visitStamp = m_visitStamp;
    made->m_geometry = m_geometry;
    if (copyInstance) {
        copyInstance->SetSharedGeometry(geometry);
    } else if (copy) {
        copy->m_flags |= kOwnsGeometry;
        copy->m_geometry = geometry;
        if (!instance && m_buffer) {
            uint32_t size = m_geometry->m_numFaces;
            Mem::PushMemProfile("Collision face-rejection optimization");
            Mem::PushContext(Mem::BottomUpHeap());
            copy->m_buffer = static_cast<uint8_t*>(Mem::Malloc(size));
            std::memcpy(copy->m_buffer, m_buffer, size);
            Mem::PopContext();
            Mem::PopMemProfile();
        } else {
            copy->m_buffer = nullptr;
        }
    }
    return made;
}

// 0x00410C20
void CCollStatic::SetWorldPosition(const Mth::Vector* position)
{
    const auto* current = Call<const Mth::Vector*>(7);
    Mth::Vector delta{position->x - current->x, position->y - current->y, position->z - current->z,
                      position->w - current->w};
    TranslateGeometry(m_geometry, &delta);
}

// 0x004123A0
const Mth::Vector* CCollStatic::GetWorldPosition() const
{
    return &g_sharedOrigin;
}

// 0x004123B0
const Mth::Matrix* CCollStatic::GetOrientation() const
{
    return &g_sharedOrientation;
}

// 0x00410980
Mth::Vector CCollStatic::GetVertexPos(int index) const
{
    Mth::Vector raw = m_geometry->GetRawVertexPos(index);
    return {raw.x, raw.y, raw.z, 1.0f};
}

// 0x00410C70
bool CCollStatic::WithinBBox(const Mth::BBox* box) const
{
    return box->Intersects(*reinterpret_cast<const Mth::BBox*>(&m_geometry->m_min));
}

// 0x00412750
const Mth::BBox* CCollStatic::GetGeometryBBox() const
{
    return reinterpret_cast<const Mth::BBox*>(&m_geometry->m_min);
}

// 0x004127C0
CCollStaticInstance* CCollStaticInstance::Construct()
{
    CCollScaled::Construct();
    m_vtable = kInstanceVtable;
    new (&m_worldBox) Mth::BBox();
    m_buffer = nullptr;
    m_unknownC4 = 0;
    return this;
}

// 0x00414C30
CCollStaticInstance* CCollStaticInstance::ConstructCopy(const CCollStaticInstance* other)
{
    static_cast<CCollScaled*>(this)->ConstructCopy(other);
    m_vtable = kInstanceVtable;
    m_worldBox = other->m_worldBox;
    m_buffer = other->m_buffer;
    m_unknownC4 = other->m_unknownC4;
    return this;
}

// 0x00412840
void CCollStaticInstance::Destruct()
{
    m_vtable = kInstanceVtable;
    if ((m_flags & kOwnsGeometry) && m_buffer) {
        Mem::FreeMalloc(m_buffer);
        m_buffer = nullptr;
    }
    DestroyBase();
}

// 0x00413ED0
CCollStaticInstance* CCollStaticInstance::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00414CD0
CCollStaticInstance* CCollStaticInstance::Clone(int /*argument*/)
{
    void* memory = Mem::New(sizeof(CCollStaticInstance));
    CCollStaticInstance* copy = memory ? static_cast<CCollStaticInstance*>(memory)->ConstructCopy(this) : nullptr;
    copy->SetSharedGeometry(m_geometry); // (on a null copy too: a crash, as in the original)
    return copy;
}

// 0x00411480
void CCollStaticInstance::SetSharedGeometry(CCollObjTriData* geometry)
{
    m_flags |= kOwnsGeometry | kSharedGeometry;
    m_geometry = geometry;
    m_worldBox.min = geometry->m_min;
    m_worldBox.max = geometry->m_max;
    m_buffer = nullptr;
}

// 0x00413F00
void CCollStaticInstance::SetWorldPosition(const Mth::Vector* position)
{
    m_matrix.row[3] = *position;
    UpdateInverse();
    UpdateWorldBBox();
}

// 0x00413F30
void CCollStaticInstance::SetOrientation(const Mth::Matrix* orientation)
{
    CCollScaled::SetOrientation(orientation);
    UpdateWorldBBox();
}

// 0x00413F50
void CCollStaticInstance::SetScale(const Mth::Vector* scale)
{
    m_scale = *scale;
    UpdateInverse();
    UpdateWorldBBox();
}

// 0x004113B0
void CCollStaticInstance::UpdateWorldBBox()
{
    m_worldBox.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    m_worldBox.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    int count = m_geometry->m_numVertices;
    for (int i = 0; i < count; ++i) {
        Mth::Vector raw;
        m_geometry->GetRawVertexPos(i, &raw);
        Mth::Vector scaled{raw.x * m_scale.x, raw.y * m_scale.y, raw.z * m_scale.z, raw.w * m_scale.w};
        Mth::Vector placed;
        m_matrix.TransformAsPos(placed, scaled);
        m_worldBox.AddPoint(placed);
    }
}

// 0x00411460
bool CCollStaticInstance::WithinBBox(const Mth::BBox* box) const
{
    return box->Intersects(m_worldBox);
}

// 0x00412830
Mth::BBox* CCollStaticInstance::GetWorldBBox()
{
    return &m_worldBox;
}

// 0x00411330 (the position compared exactly; NaN is unequal)
void CCollStaticInstance::RescaleIfAt(const Mth::Vector* position, const Mth::Vector* scale)
{
    const Mth::Vector& at = m_matrix.row[3];
    if (!(position->x == at.x) || !(position->y == at.y) || !(position->z == at.z))
        return;
    Mth::Vector scaled{scale->x * m_scale.x, scale->y * m_scale.y, scale->z * m_scale.z, scale->w * m_scale.w};
    Call<void>(24, &scaled); // SetScale
}

// 0x00412F50
void CCollStaticInstance::DebugRender(int a, int b)
{
    Mth::Matrix transform = m_matrix;
    transform.ScaleRows(m_scale);
    DrawGeometry(m_geometry, &transform, a, b);
}

// 0x00413000
CCollMovTri* CCollMovTri::Construct(CCollObjTriData* geometry)
{
    CCollMovable::Construct();
    m_vtable = Vt::kCollMovTri;
    new (&m_box) Mth::BBox();
    SetGeometry(geometry);
    m_unknownD0 = geometry->m_unknown3C;
    m_boxDirty = true;
    return this;
}

// 0x00413080
CCollMovTri* CCollMovTri::ConstructCopy(const CCollMovTri* other)
{
    CCollMovable::ConstructCopy(other);
    m_vtable = Vt::kCollMovTri;
    m_radius = other->m_radius;
    m_boxDirty = other->m_boxDirty;
    m_box = other->m_box;
    m_unknownCC = other->m_unknownCC;
    m_unknownD0 = other->m_unknownD0;
    return this;
}

// 0x00413070
void CCollMovTri::Destruct()
{
    m_vtable = Vt::kCollMovTri;
    CCollMovable::Destruct();
}

// 0x004143D0
CCollMovTri* CCollMovTri::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00414400
CCollMovTri* CCollMovTri::Clone(int argument)
{
    void* memory = Mem::New(sizeof(CCollMovTri));
    CCollMovTri* copy = memory ? static_cast<CCollMovTri*>(memory)->ConstructCopy(this) : nullptr;
    copy->m_flags |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->m_geometry = CloneGeometry(m_geometry, argument, 0);
    return copy;
}

// 0x00411780 (a null geometry crashes, as in the original)
void CCollMovTri::SetGeometry(CCollObjTriData* geometry)
{
    m_geometry = geometry;
    if (geometry)
        m_checksum = geometry->m_checksum;
    m_radius = 0.0f;
    for (int i = 0; i < geometry->m_numVertices; ++i) {
        Mth::Vector v = m_geometry->GetRawVertexPos(i);
        double length = std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                  static_cast<double>(v.z) * v.z);
        if (length > m_radius)
            m_radius = static_cast<float>(length);
        geometry = m_geometry;
    }
    m_boxDirty = true;
}

// 0x00412F90
void CCollMovTri::SetWorldPosition(const Mth::Vector* position)
{
    m_position = *position;
    m_boxDirty = true;
}

// 0x00411810
bool CCollMovTri::WithinBBox(const Mth::BBox* box)
{
    if (m_boxDirty) {
        UpdateBBox();
        m_boxDirty = false;
    }
    return box->Intersects(m_box);
}

// 0x00412FC0
Mth::BBox* CCollMovTri::GetBBox()
{
    if (m_boxDirty) {
        UpdateBBox();
        m_boxDirty = false;
    }
    return &m_box;
}

// 0x0040B390
void CCollMovTri::UpdateBBox()
{
    m_box.min = {m_position.x - m_radius, m_position.y - m_radius, m_position.z - m_radius, 1.0f};
    m_box.max = {m_radius + m_position.x, m_position.y + m_radius, m_position.z + m_radius, 1.0f};
}

// 0x00413130
void CCollMovTri::DebugRender(int a, int b)
{
    Mth::Matrix transform = m_orientation;
    transform.row[3] = m_position;
    DrawGeometry(m_geometry, &transform, a, b);
}

// 0x00414D60
void CollGeometryStubs::DebugRender(const Mth::Matrix* /*transform*/, int /*a*/, int /*b*/, int /*c*/)
{
}

// 0x0040AE80
void CollGeometryForwarders::RotateY(const Mth::Vector* centre, int quarterTurns)
{
    m_geometry->RotateY(centre, quarterTurns);
}

// 0x0040AE90
void CollGeometryForwarders::Scale(const Mth::Vector* centre, const Mth::Vector* scale)
{
    m_geometry->Scale(centre, scale);
}

// 0x0040AEA0
void CollGeometryForwarders::DebugRender(int a, int b)
{
    Stubs::IgnoreArguments2(a, b);
}

// 0x0040AEB0
void CollGeometryForwarders::AddOcclusionPolys()
{
    m_geometry->AddOcclusionPolys();
}

// 0x0040AEC0
bool CollGeometryForwarders::IsPointBehindFaces(const Mth::Vector* point, int unused)
{
    return m_geometry->IsPointBehindFaces(point, unused);
}

// 0x00414990 (a failed allocation of the box crashes when there are geometries, as in the original)
CCollObj* CreateMovableCollision(int type, CCollObjTriData* geometries, int count, int value)
{
    CCollObj* made;
    if (type == 1) {
        void* memory = Mem::New(sizeof(CCollMovBBox));
        CCollMovBBox* box = memory ? static_cast<CCollMovBBox*>(memory)->Construct() : nullptr;
        if (count >= 1) {
            Mth::BBox bounds;
            for (int i = 0; i < count; ++i) {
                bounds.AddPoint(geometries[i].m_min);
                bounds.AddPoint(geometries[i].m_max);
            }
            box->m_box = bounds;
        }
        made = box;
    } else if (type == 4) {
        if (count == 1) {
            void* memory = Mem::New(sizeof(CCollMovTri));
            made = memory ? static_cast<CCollMovTri*>(memory)->Construct(geometries) : nullptr;
        } else if (count > 1) {
            void* memory = Mem::New(sizeof(CCollMulti));
            CCollMulti* multi = memory ? static_cast<CCollMulti*>(memory)->Construct() : nullptr;
            for (int i = 0; i < count; ++i) {
                void* part = Mem::New(sizeof(CCollMovTri));
                multi->AddChild(part ? static_cast<CCollMovTri*>(part)->Construct(&geometries[i]) : nullptr);
            }
            made = multi;
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }
    if (made) {
        made->Call<void>(11, value);
        RegisterMovable(made); // (the original also pushes `value`, which RegisterMovable doesn't use)
    }
    return made;
}

} // namespace Nx

namespace Nx {

// 0x00640860 (a static initialiser): the shared orientation starts as the rotation of no angles.
void ConstructSharedOrientation() { g_sharedOrientation.SetFromEulerAngles(0.0f, 0.0f, 0.0f); }

} // namespace Nx
