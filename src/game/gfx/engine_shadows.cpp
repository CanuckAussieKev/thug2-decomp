// The engine's shadows: see engine_shadows.h. The x87 code is followed: products kept unrounded where the original
// keeps them on its stack, rounded to floats where it stores them.

#include "game/gfx/engine_shadows.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kShadowVtable = 0x00649E78;
constexpr uint32_t kDetailedShadowVtable = 0x00649E98;
constexpr uint32_t kSimpleShadowVtable = 0x00649EB8;
constexpr uint32_t kShadowVolumeVtable = 0x00649ED8;

void DeleteThrough0(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}

CShadowVolume** ShadowVolumes() { return &GameVariable<CShadowVolume*>(0x007013D8); }
Lst::HashTable& EdgeTable() { return GameVariable<Lst::HashTable>(0x00701418); }

// The engine's model calls not rewritten yet: the shadow caster added for a render target (0x0048C870), a model
// deleted (0x0048C3C0), a file name with its extension (0x00487A70).
void AddShadowCaster(uint8_t* texture, CModel* model, int unused)
{
    GameFunction<void(__cdecl*)(uint8_t*, CModel*, int)>(0x0048C870)(texture, model, unused);
}
void DeleteModel(CModel* model) { GameFunction<void(__cdecl*)(CModel*)>(0x0048C3C0)(model); }
void AddExtension(const char* name, const char* extension, char* out)
{
    GameFunction<void(__cdecl*)(const char*, const char*, char*)>(0x00487A70)(name, extension, out);
}

// A new camera (null without memory: the original goes on with it) at the origin; turned to look along a direction
// (normalised) by LookAlong: rows (z, -x, -y), (y, -z, -x), the direction.
Gfx::Camera* NewShadowCamera()
{
    void* memory = Mem::Calloc(0xC4);
    Gfx::Camera* camera = memory ? static_cast<Gfx::Camera*>(memory)->Construct() : nullptr;
    camera->SetPos(Mth::Vector{0.0f, 0.0f, 0.0f, 1.0f});
    return camera;
}
void LookAlong(Gfx::Camera* camera, Mth::Vector v)
{
    Mth::Matrix matrix;
    matrix.row[0] = {v.z, -v.x, -v.y, 0.0f};
    matrix.row[1] = {v.y, -v.z, -v.x, 0.0f};
    matrix.row[2] = v;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    camera->SetMatrix(matrix);
}
// The camera's place for a shadow at `position`: `distance` back along the light (the x product left unrounded).
Mth::Vector BackAlongLight(const Mth::Vector& position, const Mth::Vector& direction, float distance)
{
    double d = distance;
    return {static_cast<float>(position.x - d * direction.x),
            static_cast<float>(position.y - static_cast<float>(d * direction.y)),
            static_cast<float>(position.z - static_cast<float>(d * direction.z)),
            static_cast<float>(position.w - static_cast<float>(d * direction.w))};
}

} // namespace

// 0x004A16C0
CShadow* CShadow::Construct()
{
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
    m_type = 0;
    return this;
}

// 0x004A16D0
void CShadow::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
}

// 0x004A18B0
CShadow* CShadow::Destroy(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A18A0
void CShadow::Update(const Mth::Vector*, const Mth::Matrix*, Mth::Vector) {}

// 0x004A19C0
CDetailedShadow* CDetailedShadow::Construct(CModel* model, Mth::Vector direction, float distance)
{
    m_vtable = reinterpret_cast<void**>(kDetailedShadowVtable);
    m_type = 1;
    mp_texture = static_cast<uint8_t*>(CreateRenderTarget(0x100, 0x100, 0x10, 0x10));
    AddShadowCaster(mp_texture, model, 0);
    m_distance = distance;
    m_direction = direction;
    m_direction.Normalize();
    mp_camera = NewShadowCamera();
    Mth::Vector v = m_direction;
    v.Normalize();
    LookAlong(mp_camera, v);
    model->EnableShadow(1);
    return this;
}

// 0x004A16E0
void CDetailedShadow::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kDetailedShadowVtable);
    RemoveTextureShadowCaster(mp_texture);
    if (mp_texture)
        DeleteThrough0(mp_texture);
    if (mp_camera)
        DeleteThrough0(mp_camera);
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
}

// 0x004A18E0
CDetailedShadow* CDetailedShadow::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A1910
void CDetailedShadow::SetPosition(Mth::Vector position)
{
    mp_camera->SetPos(BackAlongLight(position, m_direction, m_distance));
    AimTextureShadowCaster(mp_texture, mp_camera);
}

// 0x004A1BA0
void CDetailedShadow::SetLightDirection(const Mth::Vector* direction)
{
    Mth::Vector position = *mp_camera->GetPos();
    m_direction = *direction;
    m_direction.Normalize();
    mp_camera->SetPos(Mth::Vector{0.0f, 0.0f, 0.0f, 1.0f});
    LookAlong(mp_camera, m_direction);
    mp_camera->SetPos(position);
    AimTextureShadowCaster(mp_texture, mp_camera);
}

// 0x004A1750
CSimpleShadow* CSimpleShadow::Construct()
{
    m_vtable = reinterpret_cast<void**>(kSimpleShadowVtable);
    m_type = 2;
    m_scale = 1.0f;
    m_offset = 1.0f;
    mp_model = nullptr;
    return this;
}

// 0x004A1780
void CSimpleShadow::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kSimpleShadowVtable);
    if (mp_model) {
        DeleteModel(mp_model);
        mp_model = nullptr;
    }
    m_vtable = reinterpret_cast<void**>(kShadowVtable);
}

// 0x004A1990
CSimpleShadow* CSimpleShadow::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A17E0
void CSimpleShadow::SetModel(const char* name)
{
    if (mp_model) {
        DeleteModel(mp_model);
        mp_model = nullptr;
    }
    mp_model = static_cast<CModel*>(CreateModel());
    char fileName[0x200] = {};
    AddExtension(name, ".mdl", fileName);
    mp_model->AddFileGeom(fileName, 0, true, 0, false, false, true, false);
}

// 0x004A1CD0
void CSimpleShadow::Update(const Mth::Vector* position, const Mth::Matrix* matrix, Mth::Vector normal)
{
    const Mth::Vector& forward = matrix->row[2];
    Mth::Matrix placed;
    placed.row[0] = {static_cast<float>(static_cast<double>(forward.z) * normal.y - static_cast<double>(forward.y) * normal.z),
                     static_cast<float>(static_cast<double>(forward.x) * normal.z - static_cast<double>(forward.z) * normal.x),
                     static_cast<float>(static_cast<double>(forward.y) * normal.x - static_cast<double>(forward.x) * normal.y),
                     0.0f};
    placed.row[1] = normal;
    placed.row[2] = forward;
    placed.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    placed.Scale(Mth::Vector{m_scale, m_scale, m_scale, 1.0f});
    double offset = m_offset;
    float dx = static_cast<float>(normal.x * offset);
    float dy = static_cast<float>(normal.y * offset);
    float dz = static_cast<float>(normal.z * offset);
    placed.row[3] = {static_cast<float>(static_cast<double>(dx) + position->x),
                     static_cast<float>(static_cast<double>(dy) + position->y),
                     static_cast<float>(static_cast<double>(dz) + position->z),
                     static_cast<float>(offset * normal.w + position->w)};
    if (mp_model)
        mp_model->Render(&placed, true, nullptr);
}

// 0x004A1880
void CSimpleShadow::Hide()
{
    if (mp_model)
        mp_model->SetActive(0);
}

// 0x004A1890
void CSimpleShadow::Show()
{
    if (mp_model)
        mp_model->SetActive(1);
}

// 0x004A1F80
CShadowVolume* CShadowVolume::Construct(CModel*, Mth::Vector direction, float distance)
{
    CShadow::Construct();
    m_distance = distance;
    m_vtable = reinterpret_cast<void**>(kShadowVolumeVtable);
    m_type = 3;
    m_direction = direction;
    m_direction.Normalize();
    mp_camera = NewShadowCamera();
    Mth::Vector v = m_direction;
    v.Normalize();
    LookAlong(mp_camera, v);
    m_modelKey = 0;
    mp_edges = nullptr;
    return this;
}

// 0x004A2120
void CShadowVolume::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kShadowVolumeVtable);
    if (mp_camera)
        DeleteThrough0(mp_camera);
    if (mp_edges) {
        if (mp_edges->count) {
            RemoveShadowVolume(this);
            ReleaseShadowEdges(m_modelKey);
        }
        mp_edges = nullptr;
    }
    CShadow::DestroyBody();
}

// 0x004A22C0
CShadowVolume* CShadowVolume::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A1E70
void CShadowVolume::SetPosition(Mth::Vector position)
{
    mp_camera->SetPos(BackAlongLight(position, m_direction, m_distance));
}

// 0x004A21A0
void CShadowVolume::SetLightDirection(const Mth::Vector* direction)
{
    Mth::Vector position = *mp_camera->GetPos();
    m_direction = *direction;
    m_direction.Normalize();
    mp_camera->SetPos(Mth::Vector{0.0f, 0.0f, 0.0f, 1.0f});
    LookAlong(mp_camera, m_direction);
    mp_camera->SetPos(position);
}

// 0x004A1EE0
Mth::Matrix* CShadowVolume::SetMatrix(const Mth::Matrix* matrix)
{
    return &(m_matrix = *matrix);
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
    return static_cast<ShadowEdges*>(EdgeTable().GetItem(key));
}

// 0x004A1F30
void __cdecl ReleaseShadowEdges(uint32_t key)
{
    ShadowEdges* edges = FindShadowEdges(key);
    if (!edges || --edges->users != 0)
        return;
    if (edges->edges) {
        Mem::Manager::Get()->Free(edges->edges);
        edges->edges = nullptr;
    }
    EdgeTable().FlushItem(key);
    Mem::Manager::Get()->Free(edges);
}

} // namespace Nx

REPLACE(0x004A16C0, &Nx::CShadow::Construct);
REPLACE(0x004A16D0, &Nx::CShadow::DestroyBody);
REPLACE(0x004A18B0, &Nx::CShadow::Destroy);
REPLACE_SOURCE_ONLY(0x004A18A0, &Nx::CShadow::Update); // 3 bytes (RET 0x18)
REPLACE(0x004A19C0, &Nx::CDetailedShadow::Construct);
REPLACE(0x004A16E0, &Nx::CDetailedShadow::DestroyBody);
REPLACE(0x004A18E0, &Nx::CDetailedShadow::Destroy);
REPLACE(0x004A1910, &Nx::CDetailedShadow::SetPosition);
REPLACE(0x004A1BA0, &Nx::CDetailedShadow::SetLightDirection);
REPLACE(0x004A1750, &Nx::CSimpleShadow::Construct);
REPLACE(0x004A1780, &Nx::CSimpleShadow::DestroyBody);
REPLACE(0x004A1990, &Nx::CSimpleShadow::Destroy);
REPLACE(0x004A17E0, &Nx::CSimpleShadow::SetModel);
REPLACE(0x004A1CD0, &Nx::CSimpleShadow::Update);
REPLACE(0x004A1880, &Nx::CSimpleShadow::Hide);
REPLACE(0x004A1890, &Nx::CSimpleShadow::Show);
REPLACE(0x004A1F80, &Nx::CShadowVolume::Construct);
REPLACE(0x004A2120, &Nx::CShadowVolume::DestroyBody);
REPLACE(0x004A22C0, &Nx::CShadowVolume::Destroy);
REPLACE(0x004A1E70, &Nx::CShadowVolume::SetPosition);
REPLACE(0x004A21A0, &Nx::CShadowVolume::SetLightDirection);
REPLACE(0x004A1EE0, &Nx::CShadowVolume::SetMatrix);
REPLACE(0x004A1E40, Nx::RemoveShadowVolume);
REPLACE(0x004A1EF0, Nx::FindShadowEdges);
REPLACE(0x004A1F30, Nx::ReleaseShadowEdges);
