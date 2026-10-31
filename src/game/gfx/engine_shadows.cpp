// The engine's shadows: see engine_shadows.h. The x87 code is followed: products kept unrounded where the original
// keeps them on its stack, rounded to floats where it stores them.

#include "game/gfx/engine_shadows.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/view_cam.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/lists/key_map.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/engine3d/coll_static.h"

namespace Engine3D {

namespace {

extern void** const kShadowVtable;
extern void** const kDetailedShadowVtable;
extern void** const kSimpleShadowVtable;
extern void** const kShadowVolumeVtable;

void DeleteThrough0(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}

CShadowVolume** ShadowVolumes() { return g_shadowVolumes; }
ListKit::KeyMap& EdgeTable() { return g_shadowEdgeTable; }

// The engine's model calls not rewritten yet: the shadow caster added for a render target (0x0048C870), a model
// deleted (0x0048C3C0), a file name with its extension (0x00487A70).
void AddShadowCaster(uint8_t* texture, CRenderModel* model, int unused)
{
    Engine3D::AddShadowCaster(texture, model, reinterpret_cast<CWorldScene*>(unused));
}
void DeleteModel(CRenderModel* model) { Engine::DeleteGeomObject(model); }
void AddExtension(const char* name, const char* extension, char* out)
{
    Gfx2D::MakeModelFileName(name, extension, out);
}

// A new camera (null without memory: the original goes on with it) at the origin; turned to look along a direction
// (normalised) by LookAlong: rows (z, -x, -y), (y, -z, -x), the direction.
Gfx2D::ViewCam* NewShadowCamera()
{
    void* memory = MemSys::AllocZeroed(0xC4);
    Gfx2D::ViewCam* camera = memory ? static_cast<Gfx2D::ViewCam*>(memory)->MakeNew() : nullptr;
    camera->ChangeLocation(Maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    return camera;
}
void LookAlong(Gfx2D::ViewCam* camera, Maths::Vec4 v)
{
    Maths::Mat4 matrix;
    matrix.row[0] = {v.z, -v.x, -v.y, 0.0f};
    matrix.row[1] = {v.y, -v.z, -v.x, 0.0f};
    matrix.row[2] = v;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    camera->ChangeMat(matrix);
}
// The camera's place for a shadow at `position`: `distance` back along the light (the x product left unrounded).
Maths::Vec4 BackAlongLight(const Maths::Vec4& position, const Maths::Vec4& direction, float distance)
{
    double d = distance;
    return {static_cast<float>(position.x - d * direction.x),
            static_cast<float>(position.y - static_cast<float>(d * direction.y)),
            static_cast<float>(position.z - static_cast<float>(d * direction.z)),
            static_cast<float>(position.w - static_cast<float>(d * direction.w))};
}

} // namespace

// 0x004A16C0
CShadowBase* CShadowBase::MakeNew()
{
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
    m_kindTag = 0;
    return this;
}

// 0x004A16D0
void CShadowBase::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
}

// 0x004A18B0
CShadowBase* CShadowBase::Teardown(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A18A0
void CShadowBase::TickFrame(const Maths::Vec4*, const Maths::Mat4*, Maths::Vec4) {}

// 0x004A19C0
CFullShadow* CFullShadow::MakeNew(CRenderModel* model, Maths::Vec4 direction, float distance)
{
    m_vtable = reinterpret_cast<void**>(kDetailedShadowVtable);
    m_kindTag = 1;
    m_bitmap = static_cast<uint8_t*>(CreateRenderTarget(0x100, 0x100, 0x10, 0x10));
    AddShadowCaster(m_bitmap, model, 0);
    m_reach = distance;
    m_direction = direction;
    m_direction.MakeUnit();
    m_viewCamera = NewShadowCamera();
    Maths::Vec4 v = m_direction;
    v.MakeUnit();
    LookAlong(m_viewCamera, v);
    model->ActivateShadow(1);
    return this;
}

// 0x004A16E0
void CFullShadow::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kDetailedShadowVtable);
    RemoveTextureShadowCaster(m_bitmap);
    if (m_bitmap)
        DeleteThrough0(m_bitmap);
    if (m_viewCamera)
        DeleteThrough0(m_viewCamera);
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
}

// 0x004A18E0
CFullShadow* CFullShadow::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A1910
void CFullShadow::ChangePlace(Maths::Vec4 position)
{
    m_viewCamera->ChangeLocation(BackAlongLight(position, m_direction, m_reach));
    AimTextureShadowCaster(m_bitmap, m_viewCamera);
}

// 0x004A1BA0
void CFullShadow::SetLightDirection(const Maths::Vec4* direction)
{
    Maths::Vec4 position = *m_viewCamera->FetchLocation();
    m_direction = *direction;
    m_direction.MakeUnit();
    m_viewCamera->ChangeLocation(Maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    LookAlong(m_viewCamera, m_direction);
    m_viewCamera->ChangeLocation(position);
    AimTextureShadowCaster(m_bitmap, m_viewCamera);
}

// 0x004A1750
CBlobShadow* CBlobShadow::MakeNew()
{
    m_vtable = reinterpret_cast<void**>(kSimpleShadowVtable);
    m_kindTag = 2;
    m_sizeFactor = 1.0f;
    m_shift = 1.0f;
    m_meshInstance = nullptr;
    return this;
}

// 0x004A1780
void CBlobShadow::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kSimpleShadowVtable);
    if (m_meshInstance) {
        DeleteModel(m_meshInstance);
        m_meshInstance = nullptr;
    }
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
}

// 0x004A1990
CBlobShadow* CBlobShadow::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A17E0
void CBlobShadow::ChangeMesh(const char* name)
{
    if (m_meshInstance) {
        DeleteModel(m_meshInstance);
        m_meshInstance = nullptr;
    }
    m_meshInstance = static_cast<CRenderModel*>(SpawnModel());
    char fileName[0x200] = {};
    AddExtension(name, ".mdl", fileName);
    m_meshInstance->AddFileGeom(fileName, 0, true, 0, false, false, true, false);
}

// 0x004A1CD0
void CBlobShadow::TickFrame(const Maths::Vec4* position, const Maths::Mat4* matrix, Maths::Vec4 normal)
{
    const Maths::Vec4& forward = matrix->row[2];
    Maths::Mat4 placed;
    placed.row[0] = {static_cast<float>(static_cast<double>(forward.z) * normal.y - static_cast<double>(forward.y) * normal.z),
                     static_cast<float>(static_cast<double>(forward.x) * normal.z - static_cast<double>(forward.z) * normal.x),
                     static_cast<float>(static_cast<double>(forward.y) * normal.x - static_cast<double>(forward.x) * normal.y),
                     0.0f};
    placed.row[1] = normal;
    placed.row[2] = forward;
    placed.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    placed.StretchBy(Maths::Vec4{m_sizeFactor, m_sizeFactor, m_sizeFactor, 1.0f});
    double offset = m_shift;
    float dx = static_cast<float>(normal.x * offset);
    float dy = static_cast<float>(normal.y * offset);
    float dz = static_cast<float>(normal.z * offset);
    placed.row[3] = {static_cast<float>(static_cast<double>(dx) + position->x),
                     static_cast<float>(static_cast<double>(dy) + position->y),
                     static_cast<float>(static_cast<double>(dz) + position->z),
                     static_cast<float>(offset * normal.w + position->w)};
    if (m_meshInstance)
        m_meshInstance->Render(&placed, true, nullptr);
}

// 0x004A1880
void CBlobShadow::Conceal()
{
    if (m_meshInstance)
        m_meshInstance->ChangeLive(0);
}

// 0x004A1890
void CBlobShadow::Show()
{
    if (m_meshInstance)
        m_meshInstance->ChangeLive(1);
}

// 0x004A1F80
CShadowVolume* CShadowVolume::MakeNew(CRenderModel*, Maths::Vec4 direction, float distance)
{
    CShadowBase::MakeNew();
    m_reach = distance;
    m_vtable = reinterpret_cast<void**>(kShadowVolumeVtable);
    m_kindTag = 3;
    m_direction = direction;
    m_direction.MakeUnit();
    m_viewCamera = NewShadowCamera();
    Maths::Vec4 v = m_direction;
    v.MakeUnit();
    LookAlong(m_viewCamera, v);
    m_modelKey = 0;
    m_edges = nullptr;
    return this;
}

// 0x004A2120
void CShadowVolume::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kShadowVolumeVtable);
    if (m_viewCamera)
        DeleteThrough0(m_viewCamera);
    if (m_edges) {
        if (m_edges->count) {
            RemoveShadowVolume(this);
            ReleaseShadowEdges(m_modelKey);
        }
        m_edges = nullptr;
    }
    CShadowBase::DestroyBody();
}

// 0x004A22C0
CShadowVolume* CShadowVolume::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A1E70
void CShadowVolume::ChangePlace(Maths::Vec4 position)
{
    m_viewCamera->ChangeLocation(BackAlongLight(position, m_direction, m_reach));
}

// 0x004A21A0
void CShadowVolume::SetLightDirection(const Maths::Vec4* direction)
{
    Maths::Vec4 position = *m_viewCamera->FetchLocation();
    m_direction = *direction;
    m_direction.MakeUnit();
    m_viewCamera->ChangeLocation(Maths::Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    LookAlong(m_viewCamera, m_direction);
    m_viewCamera->ChangeLocation(position);
}

// 0x004A1EE0
Maths::Mat4* CShadowVolume::ChangeMat(const Maths::Mat4* matrix)
{
    return &(m_basisFrame = *matrix);
}

// 0x004A1E40
void __cdecl RemoveShadowVolume(CShadowVolume* volume)
{
    for (int i = 0; i < 16; ++i) {
        if (ShadowVolumes()[i] == volume) {
            ShadowVolumes()[i] = nullptr;
            return;
        }
    }
}

// 0x004A1EF0
ShadowEdges* __cdecl FindShadowEdges(uint32_t key)
{
    return static_cast<ShadowEdges*>(EdgeTable().FetchEntry(key));
}

// 0x004A1F30
void __cdecl ReleaseShadowEdges(uint32_t key)
{
    ShadowEdges* edges = FindShadowEdges(key);
    if (!edges || --edges->users != 0)
        return;
    if (edges->edges) {
        MemSys::Hub::Pull()->Dealloc(edges->edges);
        edges->edges = nullptr;
    }
    EdgeTable().EraseEntry(key);
    MemSys::Hub::Pull()->Dealloc(edges);
}

namespace {
static VtableSlot kShadowVtable_slots[] = {&Engine3D::CShadowBase::Teardown, &Engine3D::CShadowBase::TickFrame,
                &Engine3D::CollGeometryStubs::DrawDiagnostic, Stubs::IgnoreArgument, Stubs::IgnoreArgument, Stubs::DoNothing,
                Stubs::DoNothing, Stubs::IgnoreArguments2};
void** const kShadowVtable = reinterpret_cast<void**>(kShadowVtable_slots);
static VtableSlot kDetailedShadowVtable_slots[] = {&Engine3D::CFullShadow::Teardown, &Engine3D::CShadowBase::TickFrame,
                &Engine3D::CFullShadow::ChangePlace, &Engine3D::CFullShadow::SetLightDirection, Stubs::IgnoreArgument,
                Stubs::DoNothing, Stubs::DoNothing, Stubs::IgnoreArguments2};
void** const kDetailedShadowVtable = reinterpret_cast<void**>(kDetailedShadowVtable_slots);
static VtableSlot kSimpleShadowVtable_slots[] = {&Engine3D::CBlobShadow::Teardown, &Engine3D::CBlobShadow::TickFrame,
                &Engine3D::CollGeometryStubs::DrawDiagnostic, Stubs::IgnoreArgument, Stubs::IgnoreArgument,
                &Engine3D::CBlobShadow::Conceal, &Engine3D::CBlobShadow::Show, Stubs::IgnoreArguments2};
void** const kSimpleShadowVtable = reinterpret_cast<void**>(kSimpleShadowVtable_slots);
static VtableSlot kShadowVolumeVtable_slots[] = {&Engine3D::CShadowVolume::Teardown, &Engine3D::CShadowBase::TickFrame,
                &Engine3D::CShadowVolume::ChangePlace, &Engine3D::CShadowVolume::SetLightDirection, &Engine3D::CShadowVolume::ChangeMat,
                Stubs::DoNothing, Stubs::DoNothing, &Engine3D::CShadowVolume::ChangeMesh};
void** const kShadowVolumeVtable = reinterpret_cast<void**>(kShadowVolumeVtable_slots);
} // namespace

} // namespace Nx
