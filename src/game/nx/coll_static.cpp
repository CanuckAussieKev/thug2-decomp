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

namespace Engine3D {

Maths::Vector g_sharedOrigin;        // the origin and orientation every static object reports
Maths::Matrix g_sharedOrientation;

namespace {

static VtableSlot kInstanceVtable_slots[] = {&Engine3D::CCollStaticInstance::ScalarDelete, Stubs::ReturnTrue,
        &Engine3D::CollFaceAccess::GetTriangleAttrs, &Engine3D::CollFaceAccess::GetFaceTerrain, &Engine3D::CHitMesh::AssignShapeData,
        &Engine3D::CCollStaticInstance::PlaceAtAbsolutePoint, &Engine3D::CCollStaticInstance::SetOrientation,
        &Engine3D::CCollScaled::GetAbsolutePoint, &Engine3D::CHitMovingMesh::GetOrientation, &Engine3D::CCollScaled::ReadVertexPoint,
        Stubs::ReturnZero, Stubs::IgnoreArgument, &Engine3D::CCollStaticInstance::Clone, &Engine3D::CCollStaticInstance::InsideBounds,
        &Engine3D::CCollStaticInstance::CollideWithLine, &Engine3D::CCollStaticInstance::CollideWithRect,
        &Engine3D::CStaticCollider::CollideWithSphere, &Engine3D::CStaticCollider::CollideWithCapsule,
        &Engine3D::CollGeometryForwarders::IsPointBehindFaces, &Engine3D::CCollStaticInstance::DrawDiagnostic, Stubs::ReturnZero,
        &Engine3D::CCollStaticInstance::GetWorldBounds, Stubs::IgnoreArguments2, &Engine3D::CCollStaticInstance::RescaleIfAt,
        &Engine3D::CCollStaticInstance::ChangeScale, &Engine3D::CHitMovingMesh::GetAbsolutePoint};
void** const kInstanceVtable = reinterpret_cast<void**>(kInstanceVtable_slots);

// The geometry's own methods: 0x00416BF0 a copy, 0x00416400 moved.
CTriMeshSource* CloneGeometry(CTriMeshSource* geometry, int a, int b) { return geometry->Clone(a, b != 0); }
void TranslateGeometry(CTriMeshSource* geometry, const Maths::Vector* delta) { geometry->Translate(delta); }
// 0x00414D60 through our stub (the original's is the same empty function).
void DrawGeometry(CTriMeshSource* geometry, const Maths::Matrix* transform, int a, int b)
{
    reinterpret_cast<CollGeometryStubs*>(geometry)->DrawDiagnostic(transform, a, b, 1);
}
const uint8_t* Face(const CTriMeshSource* geometry, int face)
{
    return geometry->m_faces + face * (geometry->m_smallFaces ? 8 : 10);
}

} // namespace

// 0x00410BF0
uint32_t CollFaceAccess::GetTriangleAttrs(int face) const
{
    return *reinterpret_cast<const uint16_t*>(Face(m_geometry, face));
}

// 0x00411750
uint16_t CollFaceAccess::GetFaceTerrain(int face) const
{
    return *reinterpret_cast<const uint16_t*>(Face(m_geometry, face) + 2);
}

// 0x00412730 (not a function in Ghidra: the array constructor's element constructor)
CStaticCollider* CStaticCollider::Construct()
{
    m_flags = 0;
    m_geometry = nullptr;
    m_vtable = Vt::kCollStatic;
    m_octets = nullptr;
    m_unknown14 = 0;
    return this;
}

// 0x00412760
void CStaticCollider::Destruct()
{
    m_vtable = Vt::kCollStatic;
    if ((m_flags & kOwnsGeometry) && m_octets) {
        MemSys::FreeMalloc(m_octets);
        m_octets = nullptr;
    }
    DestroyBase();
}

// 0x004139A0
CHitMesh* CStaticCollider::Clone(bool instance)
{
    CTriMeshSource* geometry = instance ? m_geometry : CloneGeometry(m_geometry, 0, 1);
    if (!geometry)
        return nullptr;
    CCollStaticInstance* copyInstance = nullptr;
    CStaticCollider* copy = nullptr;
    CHitMesh* made;
    if (instance) {
        void* memory = MemSys::New(sizeof(CCollStaticInstance));
        copyInstance = memory ? static_cast<CCollStaticInstance*>(memory)->Construct() : nullptr;
        made = copyInstance;
    } else {
        auto* memory = static_cast<CStaticCollider*>(MemSys::New(sizeof(CStaticCollider)));
        if (memory) {
            memory->m_flags = 0;
            memory->m_geometry = nullptr;
            memory->m_vtable = Vt::kCollStatic;
            memory->m_octets = nullptr;
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
        if (!instance && m_octets) {
            uint32_t size = m_geometry->m_numFaces;
            MemSys::PushMemProfile("Collision face-rejection optimization");
            MemSys::EnterHeapScope(MemSys::BottomUpHeap());
            copy->m_octets = static_cast<uint8_t*>(MemSys::AllocBlock(size));
            std::memcpy(copy->m_octets, m_octets, size);
            MemSys::LeaveHeapScope();
            MemSys::PopMemProfile();
        } else {
            copy->m_octets = nullptr;
        }
    }
    return made;
}

// 0x00410C20
void CStaticCollider::PlaceAtAbsolutePoint(const Maths::Vector* position)
{
    const auto* current = Call<const Maths::Vector*>(7);
    Maths::Vector delta{position->x - current->x, position->y - current->y, position->z - current->z,
                      position->w - current->w};
    TranslateGeometry(m_geometry, &delta);
}

// 0x004123A0
const Maths::Vector* CStaticCollider::GetAbsolutePoint() const
{
    return &g_sharedOrigin;
}

// 0x004123B0
const Maths::Matrix* CStaticCollider::GetOrientation() const
{
    return &g_sharedOrientation;
}

// 0x00410980
Maths::Vector CStaticCollider::ReadVertexPoint(int index) const
{
    Maths::Vector raw = m_geometry->ReadStoredVertex(index);
    return {raw.x, raw.y, raw.z, 1.0f};
}

// 0x00410C70
bool CStaticCollider::InsideBounds(const Maths::BBox* box) const
{
    return box->Intersects(*reinterpret_cast<const Maths::BBox*>(&m_geometry->m_lowerLimit));
}

// 0x00412750
const Maths::BBox* CStaticCollider::GetGeometryBBox() const
{
    return reinterpret_cast<const Maths::BBox*>(&m_geometry->m_lowerLimit);
}

// 0x004127C0
CCollStaticInstance* CCollStaticInstance::Construct()
{
    CCollScaled::Construct();
    m_vtable = kInstanceVtable;
    new (&m_worldBox) Maths::BBox();
    m_octets = nullptr;
    m_unknownC4 = 0;
    return this;
}

// 0x00414C30
CCollStaticInstance* CCollStaticInstance::ConstructCopy(const CCollStaticInstance* other)
{
    static_cast<CCollScaled*>(this)->ConstructCopy(other);
    m_vtable = kInstanceVtable;
    m_worldBox = other->m_worldBox;
    m_octets = other->m_octets;
    m_unknownC4 = other->m_unknownC4;
    return this;
}

// 0x00412840
void CCollStaticInstance::Destruct()
{
    m_vtable = kInstanceVtable;
    if ((m_flags & kOwnsGeometry) && m_octets) {
        MemSys::FreeMalloc(m_octets);
        m_octets = nullptr;
    }
    DestroyBase();
}

// 0x00413ED0
CCollStaticInstance* CCollStaticInstance::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00414CD0
CCollStaticInstance* CCollStaticInstance::Clone(int /*argument*/)
{
    void* memory = MemSys::New(sizeof(CCollStaticInstance));
    CCollStaticInstance* copy = memory ? static_cast<CCollStaticInstance*>(memory)->ConstructCopy(this) : nullptr;
    copy->SetSharedGeometry(m_geometry); // (on a null copy too: a crash, as in the original)
    return copy;
}

// 0x00411480
void CCollStaticInstance::SetSharedGeometry(CTriMeshSource* geometry)
{
    m_flags |= kOwnsGeometry | kSharedGeometry;
    m_geometry = geometry;
    m_worldBox.min = geometry->m_lowerLimit;
    m_worldBox.max = geometry->m_upperLimit;
    m_octets = nullptr;
}

// 0x00413F00
void CCollStaticInstance::PlaceAtAbsolutePoint(const Maths::Vector* position)
{
    m_basisFrame.row[3] = *position;
    UpdateInverse();
    UpdateWorldBBox();
}

// 0x00413F30
void CCollStaticInstance::SetOrientation(const Maths::Matrix* orientation)
{
    CCollScaled::SetOrientation(orientation);
    UpdateWorldBBox();
}

// 0x00413F50
void CCollStaticInstance::ChangeScale(const Maths::Vector* scale)
{
    m_sizeFactor = *scale;
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
        Maths::Vector raw;
        m_geometry->ReadStoredVertex(i, &raw);
        Maths::Vector scaled{raw.x * m_sizeFactor.x, raw.y * m_sizeFactor.y, raw.z * m_sizeFactor.z, raw.w * m_sizeFactor.w};
        Maths::Vector placed;
        m_basisFrame.CarryPointThrough(placed, scaled);
        m_worldBox.AddPoint(placed);
    }
}

// 0x00411460
bool CCollStaticInstance::InsideBounds(const Maths::BBox* box) const
{
    return box->Intersects(m_worldBox);
}

// 0x00412830
Maths::BBox* CCollStaticInstance::GetWorldBounds()
{
    return &m_worldBox;
}

// 0x00411330 (the position compared exactly; NaN is unequal)
void CCollStaticInstance::RescaleIfAt(const Maths::Vector* position, const Maths::Vector* scale)
{
    const Maths::Vector& at = m_basisFrame.row[3];
    if (!(position->x == at.x) || !(position->y == at.y) || !(position->z == at.z))
        return;
    Maths::Vector scaled{scale->x * m_sizeFactor.x, scale->y * m_sizeFactor.y, scale->z * m_sizeFactor.z, scale->w * m_sizeFactor.w};
    Call<void>(24, &scaled); // ChangeScale
}

// 0x00412F50
void CCollStaticInstance::DrawDiagnostic(int a, int b)
{
    Maths::Matrix transform = m_basisFrame;
    transform.ScaleRows(m_sizeFactor);
    DrawGeometry(m_geometry, &transform, a, b);
}

// 0x00413000
CHitMovingTriangles* CHitMovingTriangles::Construct(CTriMeshSource* geometry)
{
    CHitMovingMesh::Construct();
    m_vtable = Vt::kCollMovTri;
    new (&m_box) Maths::BBox();
    AssignShapeData(geometry);
    m_unknownD0 = geometry->m_unknown3C;
    m_boxDirty = true;
    return this;
}

// 0x00413080
CHitMovingTriangles* CHitMovingTriangles::ConstructCopy(const CHitMovingTriangles* other)
{
    CHitMovingMesh::ConstructCopy(other);
    m_vtable = Vt::kCollMovTri;
    m_radius = other->m_radius;
    m_boxDirty = other->m_boxDirty;
    m_box = other->m_box;
    m_unknownCC = other->m_unknownCC;
    m_unknownD0 = other->m_unknownD0;
    return this;
}

// 0x00413070
void CHitMovingTriangles::Destruct()
{
    m_vtable = Vt::kCollMovTri;
    CHitMovingMesh::Destruct();
}

// 0x004143D0
CHitMovingTriangles* CHitMovingTriangles::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00414400
CHitMovingTriangles* CHitMovingTriangles::Clone(int argument)
{
    void* memory = MemSys::New(sizeof(CHitMovingTriangles));
    CHitMovingTriangles* copy = memory ? static_cast<CHitMovingTriangles*>(memory)->ConstructCopy(this) : nullptr;
    copy->m_flags |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->m_geometry = CloneGeometry(m_geometry, argument, 0);
    return copy;
}

// 0x00411780 (a null geometry crashes, as in the original)
void CHitMovingTriangles::AssignShapeData(CTriMeshSource* geometry)
{
    m_geometry = geometry;
    if (geometry)
        m_checksum = geometry->m_checksum;
    m_radius = 0.0f;
    for (int i = 0; i < geometry->m_numVertices; ++i) {
        Maths::Vector v = m_geometry->ReadStoredVertex(i);
        double length = std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                                  static_cast<double>(v.z) * v.z);
        if (length > m_radius)
            m_radius = static_cast<float>(length);
        geometry = m_geometry;
    }
    m_boxDirty = true;
}

// 0x00412F90
void CHitMovingTriangles::PlaceAtAbsolutePoint(const Maths::Vector* position)
{
    m_anchorPoint = *position;
    m_boxDirty = true;
}

// 0x00411810
bool CHitMovingTriangles::InsideBounds(const Maths::BBox* box)
{
    if (m_boxDirty) {
        UpdateBBox();
        m_boxDirty = false;
    }
    return box->Intersects(m_box);
}

// 0x00412FC0
Maths::BBox* CHitMovingTriangles::GetLocalBounds()
{
    if (m_boxDirty) {
        UpdateBBox();
        m_boxDirty = false;
    }
    return &m_box;
}

// 0x0040B390
void CHitMovingTriangles::UpdateBBox()
{
    m_box.min = {m_anchorPoint.x - m_radius, m_anchorPoint.y - m_radius, m_anchorPoint.z - m_radius, 1.0f};
    m_box.max = {m_radius + m_anchorPoint.x, m_anchorPoint.y + m_radius, m_anchorPoint.z + m_radius, 1.0f};
}

// 0x00413130
void CHitMovingTriangles::DrawDiagnostic(int a, int b)
{
    Maths::Matrix transform = m_orientation;
    transform.row[3] = m_anchorPoint;
    DrawGeometry(m_geometry, &transform, a, b);
}

// 0x00414D60
void CollGeometryStubs::DrawDiagnostic(const Maths::Matrix* /*transform*/, int /*a*/, int /*b*/, int /*c*/)
{
}

// 0x0040AE80
void CollGeometryForwarders::RotateY(const Maths::Vector* centre, int quarterTurns)
{
    m_geometry->RotateY(centre, quarterTurns);
}

// 0x0040AE90
void CollGeometryForwarders::Scale(const Maths::Vector* centre, const Maths::Vector* scale)
{
    m_geometry->Scale(centre, scale);
}

// 0x0040AEA0
void CollGeometryForwarders::DrawDiagnostic(int a, int b)
{
    Stubs::IgnoreArguments2(a, b);
}

// 0x0040AEB0
void CollGeometryForwarders::AddOcclusionPolys()
{
    m_geometry->AddOcclusionPolys();
}

// 0x0040AEC0
bool CollGeometryForwarders::IsPointBehindFaces(const Maths::Vector* point, int unused)
{
    return m_geometry->IsPointBehindFaces(point, unused);
}

// 0x00414990 (a failed allocation of the box crashes when there are geometries, as in the original)
CHitMesh* CreateMovableCollision(int type, CTriMeshSource* geometries, int count, int value)
{
    CHitMesh* made;
    if (type == 1) {
        void* memory = MemSys::New(sizeof(CHitMovingBox));
        CHitMovingBox* box = memory ? static_cast<CHitMovingBox*>(memory)->Construct() : nullptr;
        if (count >= 1) {
            Maths::BBox bounds;
            for (int i = 0; i < count; ++i) {
                bounds.AddPoint(geometries[i].m_lowerLimit);
                bounds.AddPoint(geometries[i].m_upperLimit);
            }
            box->m_box = bounds;
        }
        made = box;
    } else if (type == 4) {
        if (count == 1) {
            void* memory = MemSys::New(sizeof(CHitMovingTriangles));
            made = memory ? static_cast<CHitMovingTriangles*>(memory)->Construct(geometries) : nullptr;
        } else if (count > 1) {
            void* memory = MemSys::New(sizeof(CHitMeshGroup));
            CHitMeshGroup* multi = memory ? static_cast<CHitMeshGroup*>(memory)->Construct() : nullptr;
            for (int i = 0; i < count; ++i) {
                void* part = MemSys::New(sizeof(CHitMovingTriangles));
                multi->AddChild(part ? static_cast<CHitMovingTriangles*>(part)->Construct(&geometries[i]) : nullptr);
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

namespace Engine3D {

// 0x00640860 (a static initialiser): the shared orientation starts as the rotation of no angles.
void ConstructSharedOrientation() { g_sharedOrientation.SetFromEulerAngles(0.0f, 0.0f, 0.0f); }

} // namespace Nx
