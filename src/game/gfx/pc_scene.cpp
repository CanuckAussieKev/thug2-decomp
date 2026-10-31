// The PC scene and sector, the PC screen effects: see pc_scene.h.

#include "game/gfx/pc_scene.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/ass/asset.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/geom.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_grass.h"
#include "game/gfx/screen_effects.h"
#include "game/gfx/sector.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/lst/hashtable.h"
#include "game/core/code_address.h"
#include "game/nx/coll_movable.h"
#include "game/core/stubs.h"
#include "game/mc/memcard.h"
#include "game/gfx/device.h"

namespace Nx {

namespace {
extern void** const kCPcSceneVtable;
extern void** const kCPcScreenEffectsVtable;
extern void** const kCPcSectorVtable;
} // namespace

uint32_t g_bloomOn;
uint32_t g_screenEffectUnknown5724;
uint32_t g_screenEffectUnknown5728;
// The empty bounding box (all zero at start, as the original's) and its guard.
Mth::BBox g_emptyBoundingBox{Mth::BBox(Mth::Vector{0.0f, 0.0f, 0.0f, 0.0f})};
uint32_t g_emptyBoundingBoxGuard;

uint32_t g_bloomAlphaFirst{0x10};
uint32_t g_bloomAlphaSecond{0x10};
uint32_t g_screenEffectUnknown984{0x42A00000};
uint32_t g_screenEffectUnknown988{0x43000000};
uint32_t g_bloomCutOff{0x80808080};
uint32_t g_screenEffectUnknown990{0x80808080};
uint32_t g_screenEffectUnknown994{0x80808080};

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
CPcGeom* GeometryOf(void* sector) { return static_cast<CPcGeom*>(static_cast<CPcSector*>(sector)->geom); } // (+0x14: the getter 0x004A2BA0, folded with others)

// The first sector of a fresh iteration of the table (null if none).
void* FirstSector(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* NextSector(Lst::HashTable* table)
{
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}

} // namespace

// --- the sector ---

// 0x004C6550
CPcSector* CPcSector::Construct()
{
    reinterpret_cast<CSector*>(this)->CSector::Construct();
    vtable = kCPcSectorVtable;
    unknown20[0] = unknown20[1] = unknown20[2] = 0.0f;
    unknown2C = 1.0f;
    return this;
}

// 0x004C65B0
void CPcSector::DestroyBody() { reinterpret_cast<CSector*>(this)->CSector::DestroyBody(); }

// 0x004C6580
CPcSector* CPcSector::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C6530
void CPcSector::SetActive(bool on)
{
    if (on && geom)
        Engine::ShatterGeom(geom);
}

// 0x004C65C0
CPcSector* CPcSector::Clone(int, int)
{
    void* memory = Mem::New(sizeof(CPcSector));
    return memory ? static_cast<CPcSector*>(memory)->Construct() : nullptr;
}

// 0x004C64E0
const Mth::BBox* CPcSector::GetBoundingBox()
{
    auto* empty = &g_emptyBoundingBox; // (the game's, made the first time: its guard's bit 0)
    uint32_t& made = g_emptyBoundingBoxGuard;
    if (!(made & 1)) {
        made |= 1;
        new (empty) Mth::BBox();
    }
    return empty;
}

// --- the scene ---

// 0x004C6130
CPcScene* CPcScene::Construct(int arg)
{
    reinterpret_cast<CScene*>(this)->CScene::Construct(arg);
    vtable = kCPcSceneVtable;
    return this;
}

// 0x004C6180
void CPcScene::DestroyBody() { reinterpret_cast<CScene*>(this)->CScene::DestroyBody(); }

// 0x004C6150
CPcScene* CPcScene::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C6190
CPcSector* CPcScene::CreateSector()
{
    void* memory = Mem::New(sizeof(CPcSector));
    return memory ? static_cast<CPcSector*>(memory)->Construct() : nullptr;
}

// 0x004C6200
void CPcScene::UnloadSectors()
{
    auto* table = static_cast<Lst::HashTable*>(sectors);
    for (void* sector = FirstSector(table); sector; sector = NextSector(table)) {
    }
}

// 0x004C6270
void CPcScene::PostLoad(sVertexArena* arena)
{
    auto* table = static_cast<Lst::HashTable*>(sectors);
    void* sector = FirstSector(table);
    if (arena) {
        Device()->CreateVertexBuffer(arena->size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                     reinterpret_cast<IDirect3DVertexBuffer9**>(&arena->buffer), nullptr);
        static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->Lock(0, 0, &arena->data, 0);
        arena->used = 0;
        meshes->comObject = arena->buffer;
        meshes->unknown80 = arena->size;
    }
    for (; sector; sector = NextSector(table)) {
        CPcGeom* geom = GeometryOf(sector);
        geom->CreateMeshArray(arena);
        geom->AddToMeshList(true);
    }
    if (arena) {
        static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->Unlock();
        arena->data = nullptr;
    }
    meshes->AllocateMeshList();
    for (sector = FirstSector(table); sector; sector = NextSector(table))
        GeometryOf(sector)->AddToMeshList(false);
    meshes->SortMeshes();
}

// --- the screen effects ---

// 0x004C6480
CPcScreenEffects* CPcScreenEffects::Construct()
{
    reinterpret_cast<CScreenEffects*>(this)->CScreenEffects::Construct();
    vtable = kCPcScreenEffectsVtable;
    return this;
}

// 0x004C64A0
void CPcScreenEffects::DestroyBody()
{
    vtable = kCPcScreenEffectsVtable;
    reinterpret_cast<CScreenEffects*>(this)->CScreenEffects::DestroyBody();
}

// 0x004C64B0
CPcScreenEffects* CPcScreenEffects::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C63C0
void CPcScreenEffects::SetBloom(int on) { g_bloomOn = on != 0; }
// 0x004C63E0
void CPcScreenEffects::SetBloomCutOff(uint32_t colour) { g_bloomCutOff = colour; }
// 0x004C63F0
void CPcScreenEffects::SetBloomAlphas(uint32_t first, uint32_t second)
{
    g_bloomAlphaFirst = first;
    g_bloomAlphaSecond = second;
}
// 0x004C6410
void CPcScreenEffects::SetUnknown990(uint32_t value) { g_screenEffectUnknown990 = value; }
// 0x004C6420
void CPcScreenEffects::SetUnknown5724(int on) { g_screenEffectUnknown5724 = on != 0; }
// 0x004C6460
void CPcScreenEffects::SetUnknown994(uint32_t value) { g_screenEffectUnknown994 = value; }
// 0x004C6470
void CPcScreenEffects::SetUnknown984(uint32_t value) { g_screenEffectUnknown984 = value; }
// 0x004C6440
void CPcScreenEffects::SetUnknown988(uint32_t value) { g_screenEffectUnknown988 = value; }
// 0x004C6450
void CPcScreenEffects::SetUnknown5728(uint32_t value) { g_screenEffectUnknown5728 = value; }

namespace {

// The engine and mesh code the loaders go through.
void SetSectorChecksum(CPcSector* sector, uint32_t checksum)
{
    reinterpret_cast<Ass::CAsset*>(sector)->SetChecksum(checksum); // (folded with CAsset's)
}
sMesh* NewMesh()
{
    void* memory = Mem::New(sizeof(sMesh));
    return memory ? static_cast<sMesh*>(memory)->Construct() : nullptr; // (its constructor)
}
void SetMeshSphereAndBox(sMesh* mesh, const float* centre, float radius, const Mth::Vector* min, const Mth::Vector* max)
{
    mesh->SetBoundingSphere(centre, radius, static_cast<int>(reinterpret_cast<intptr_t>(min)),
                            static_cast<int>(reinterpret_cast<intptr_t>(max)));
}
// The rest of a mesh's set-up, the same for both loaders: the scene's box grown, a billboard made, the mesh added.
void FinishMesh(CPcSector* sector, CPcGeom* geom, sMesh* mesh, uint32_t billboardType,
                const Mth::Vector* pivot, const Mth::Vector* axis, const Mth::Vector* min, const Mth::Vector* max,
                uint8_t lightGroup)
{
    auto* bounds = reinterpret_cast<Mth::BBox*>(*reinterpret_cast<uint8_t**>(geom->scene + 0x280) + 0x44);
    bounds->AddPoint(*min);
    bounds->AddPoint(*max);
    if (sector->flags & 0x800000) {
        mesh->MakeBillboard(static_cast<int>(billboardType), pivot, axis);
        g_billboardManager.AddMesh(mesh);
    }
    if (sector->flags & 0x200000)
        mesh->flags |= 0x200;
    geom->AddMesh(mesh);
    reinterpret_cast<uint8_t*>(mesh)[0x1F] = lightGroup;
    MakeGrass(geom, mesh);
}
// The meshes the load put on the list, counted (to a node that is the head, data -1, or null).
uint32_t CountLoadedMeshes(const CPcGeom* geom)
{
    uint32_t count = 0;
    for (Mem::ListNode* node = geom->loadList->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

} // namespace

// 0x004C6650
bool CPcSector::LoadFromMemory(uint8_t** cursor)
{
    auto* geometry = static_cast<CPcGeom*>(geom);
    auto* p = reinterpret_cast<const uint32_t*>(*cursor);
    SetSectorChecksum(this, *p++);
    uint32_t lightGroup = *p++;
    flags = *p++;
    geometry->meshCount = *p++;
    float box[6];
    std::memcpy(box, p, sizeof(box));
    p += 6;
    geometry->bounds.min = {box[0], box[1], box[2], 1.0f};
    geometry->bounds.max = {box[3], box[4], box[5], 1.0f};
    p += 4; // (a bounding sphere, not kept)
    uint32_t billboardType = 0;
    Mth::Vector pivot, axis; // (their w not set, as the original)
    if (flags & 0x800000) {
        billboardType = p[0];
        p += 4; // (the type and a vector not kept)
        std::memcpy(&pivot, p, 12);
        p += 3;
        pivot.z = -pivot.z;
        std::memcpy(&axis, p, 12);
        p += 3;
    }
    float centre[4] = {0, 0, 0, box[3]}; // (its w the box's last value, as the original's stack has it)
    for (uint32_t i = 0; i < geometry->meshCount; ++i) {
        sMesh* mesh = NewMesh();
        std::memcpy(centre, p, 12);
        p += 3;
        float radius;
        std::memcpy(&radius, p++, 4);
        Mth::Vector min, max;
        std::memcpy(&min, p, 12);
        p += 3;
        std::memcpy(&max, p, 12);
        p += 3;
        uint32_t meshFlags = *p++;
        if (meshFlags & 0x400)
            mesh->flags |= 2;
        if (meshFlags & 0x20000)
            mesh->flags |= 0x20000;
        uint32_t material = *p++;
        uint32_t lists = *p++;
        // The index lists: read and dropped (the PC's are built from the vertices' own).
        uint16_t* indices[8] = {};
        uint32_t counts[8] = {};
        for (uint32_t j = 0; j < lists; ++j) {
            counts[j] = *p++;
            Mem::Manager* manager = Mem::Manager::Get();
            manager->PushContext(manager->m_topDownHeap);
            uint32_t bytes = counts[j] * 2;
            indices[j] = static_cast<uint16_t*>(Mem::New(bytes));
            Mem::Manager::Get()->PopContext();
            std::memcpy(indices[j], p, bytes);
            p = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p) + bytes);
        }
        mesh->loadIndex = static_cast<uint16_t>(i);
        auto* at = reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(p));
        mesh->LoadFromMemory(&at, material, *reinterpret_cast<sScene**>(geometry->scene + 0x280),
                             nullptr); // (the vertices)
        p = reinterpret_cast<const uint32_t*>(at);
        SetMeshSphereAndBox(mesh, centre, radius, &min, &max);
        FinishMesh(this, geometry, mesh, billboardType, &pivot, &axis, &min, &max,
                   static_cast<uint8_t>(lightGroup));
        for (uint16_t* list : indices)
            Mem::Manager::Get()->Free(list);
    }
    geometry->meshCount = CountLoadedMeshes(geometry);
    *cursor = reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(p));
    return true;
}

// 0x004C6B10
bool CPcSector::LoadFromFile(File::Handle file, uint32_t unknown)
{
    auto* geometry = static_cast<CPcGeom*>(geom);
    auto read = [file](void* into, int size, int count) { File::Read(into, size, count, file); };
    uint32_t checksum;
    read(&checksum, 4, 1);
    SetSectorChecksum(this, checksum);
    uint32_t lightGroup;
    read(&lightGroup, 4, 1);
    read(&flags, 4, 1);
    read(&geometry->meshCount, 4, 1);
    float box[6];
    read(box, 4, 6);
    // (the box's corners kept: the meshes' are read over them, their w still 1)
    Mth::Vector min = {box[0], box[1], box[2], 1.0f}, max = {box[3], box[4], box[5], 1.0f};
    geometry->bounds.min = min;
    geometry->bounds.max = max;
    uint32_t sphere[4];
    read(sphere, 4, 4); // (not kept)
    uint32_t billboardType = 0;
    Mth::Vector unused, pivot, axis;
    if (flags & 0x800000) {
        read(&billboardType, 4, 1);
        read(&unused, 0xC, 1);
        read(&pivot, 0xC, 1);
        read(&axis, 0xC, 1);
        unused.z = -unused.z;
        unused.w = 0.0f;
        pivot.w = 0.0f;
        axis.w = 0.0f;
        pivot.z = -pivot.z;
    }
    float centre[4] = {}; // (its w never read in: zero here)
    for (uint32_t i = 0; i < geometry->meshCount; ++i) {
        sMesh* mesh = NewMesh();
        read(centre, 4, 3);
        float radius;
        read(&radius, 4, 1);
        read(&min, 4, 3);
        read(&max, 4, 3);
        uint32_t meshFlags;
        read(&meshFlags, 4, 1);
        if (meshFlags & 0x400)
            mesh->flags |= 2;
        if (meshFlags & 0x20000)
            mesh->flags |= 0x20000;
        uint32_t material, lists;
        read(&material, 4, 1);
        read(&lists, 4, 1);
        uint16_t* indices[8] = {};
        uint32_t counts[8] = {};
        for (uint32_t j = 0; j < lists; ++j) { // (read and dropped)
            read(&counts[j], 4, 1);
            Mem::Manager* manager = Mem::Manager::Get();
            manager->PushContext(manager->m_topDownHeap);
            indices[j] = static_cast<uint16_t*>(Mem::New(counts[j] * 2));
            Mem::Manager::Get()->PopContext();
            read(indices[j], 2, static_cast<int>(counts[j]));
        }
        mesh->loadIndex = static_cast<uint16_t>(i);
        mesh->LoadFromFile(file, material, *reinterpret_cast<sScene**>(geometry->scene + 0x280),
                           reinterpret_cast<sVertexArena*>(static_cast<uintptr_t>(flags & 0x800000 ? 0 : unknown)),
                           ((flags >> 23) & 1) != 0); // (the vertices; read as a byte: bit 23)
        SetMeshSphereAndBox(mesh, centre, radius, &min, &max);
        FinishMesh(this, geometry, mesh, billboardType, &pivot, &axis, &min, &max,
                   static_cast<uint8_t>(lightGroup));
        for (uint16_t* list : indices)
            Mem::Manager::Get()->Free(list);
    }
    geometry->meshCount = CountLoadedMeshes(geometry);
    return true;
}

namespace {
static VtableSlot kCPcSceneVtable_slots[] = {&Nx::CPcScene::Destroy, &Nx::CPcScene::PostLoad, &Nx::CScene::PostAdd,
                &Mc::Card::AlwaysTrue, &Mc::Card::AlwaysTrue, &Nx::CScene::AddCollision, Stubs::ReturnFalse,
                &Nx::CPcScene::CreateSector, Stubs::IgnoreArgument, &Nx::CScene::GetDefaultColour,
                &Nx::CScene::SetRendered};
void** const kCPcSceneVtable = reinterpret_cast<void**>(kCPcSceneVtable_slots);
static VtableSlot kCPcScreenEffectsVtable_slots[] = {&Nx::CPcScreenEffects::Destroy, Stubs::DoNothing,
                &Nx::CPcScreenEffects::SetBloom, &Nx::CPcScreenEffects::SetBloomCutOff,
                &Nx::CPcScreenEffects::SetBloomAlphas, &Nx::CPcScreenEffects::SetUnknown990,
                &Nx::CPcScreenEffects::SetUnknown5724, Stubs::IgnoreArgument, &Nx::CPcScreenEffects::SetUnknown994,
                Stubs::IgnoreArgument, &Nx::CPcScreenEffects::SetUnknown984, &Nx::CPcScreenEffects::SetUnknown988,
                &Nx::CPcScreenEffects::SetUnknown5728};
void** const kCPcScreenEffectsVtable = reinterpret_cast<void**>(kCPcScreenEffectsVtable_slots);
static VtableSlot kCPcSectorVtable_slots[] = {&Nx::CPcSector::Destroy, &Nx::CPcSector::SetActive,
                &Nx::CSector::PlatGetShatter, &Nx::CPcSector::Clone, Stubs::IgnoreArgument, Stubs::DoNothing,
                Stubs::IgnoreArgument, Stubs::IgnoreArgument, Stubs::IgnoreArgument, &Nx::CPcSector::GetBoundingBox,
                &Nx::CCollMovable::GetOrientation};
void** const kCPcSectorVtable = reinterpret_cast<void**>(kCPcSectorVtable_slots);
} // namespace

} // namespace Nx
