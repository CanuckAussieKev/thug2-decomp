// The engine's scene (see engine_scene.h): its members of 0x0049D1A0-0x0049E2EF - the constructor and destructor,
// the statistics, sectors added and cloned, the collision added and the super sectors (the collision grid) brought up
// to date, a scene added over it - and the nodes of its sector lists.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/sector.h"
#include "game/gfx/texture_dict.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/core/stubs.h"

namespace Engine3D {

namespace {

extern void** const kSceneVtable;
constexpr uint32_t kCollStaticSize = 0x18;
extern void** const kSectorNodeVtable;
extern void** const kSectorHeadVtable;
void** const& kCollNodeVtable = Vt::kCollGridNode; // (the collision grid's lists')
void** const& kCollHeadVtable = Vt::kCollGridHead;

template <typename R = void, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The rest of the scene (0x0049C910-0x0049D18E, engine_scene.cpp): the sector of a name, a sector deleted, a scene
// file's collision data read, the collision let go of.
CWorldCell* SceneSector(CWorldScene* scene, uint32_t name) { return static_cast<CWorldCell*>(scene->LookUpSector(name)); }
bool DeleteSceneSector(CWorldScene* scene, CWorldCell* sector) { return scene->RemoveSceneSector(sector); }
bool SceneLoadCollisionData(CWorldScene* scene, const uint8_t* data, int* count, CStaticCollider** statics, uint8_t** objects,
                            Maths::BBox* box, int flag)
{
    return scene->LoadCollisionData(data, count, statics, objects, box, flag);
}
bool SceneUnloadCollision(CWorldScene* scene) { return scene->UnloadCollision(); }

// The platform's new sector (slot 7), its geometry and collision cleared.
CWorldCell* NewSector(CWorldScene* scene)
{
    auto* sector = Virtual<CWorldCell*>(scene, 7);
    sector->m_geomHandle = nullptr;
    sector->mp_collObj = nullptr;
    return sector;
}

CStaticCollider* CollStaticAt(CStaticCollider* statics, int index)
{
    return reinterpret_cast<CStaticCollider*>(reinterpret_cast<uint8_t*>(statics) + index * kCollStaticSize);
}

// The lists (ListKit::Foremost / Node): a head ends its list (its data is -1).
bool IsHead(const MemSys::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node`, or null at the head.
MemSys::ListNode* NextNode(const MemSys::ListNode* node) { return IsHead(node->next) ? nullptr : node->next; }
// The first node, or null when the list's empty (the original checks the head's next is no head and not null).
MemSys::ListNode* FirstNode(const MemSys::ListNode* head)
{
    return IsHead(head->next) ? nullptr : head->next;
}
// A head (on the stack or new).
void SetupListSentinel(MemSys::ListNode* head, void** vtable)
{
    head->vtable = vtable;
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
}
// A new head (null without memory).
MemSys::ListNode* NewHead()
{
    auto* head = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (head)
        SetupListSentinel(head, kSectorHeadVtable);
    return head;
}
// A new node (null without memory) for `data`.
MemSys::ListNode* NewNode(void** vtable, void* data)
{
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->vtable = vtable;
        node->data = data;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    return node;
}
// `node` put at the end of the list (no memory: written through null, as the original).
void AppendLast(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// Every node of a list deleted (its virtual [0]).
void DeleteNodes(MemSys::ListNode* head)
{
    for (MemSys::ListNode* node = FirstNode(head); node;) {
        MemSys::ListNode* next = NextNode(node);
        Virtual<void*>(node, 0, 1u);
        node = next;
    }
}

// The sectors in the table in turn, as far as the first null item or value.
template <typename Body>
void ForEachSector(CWorldScene* scene, Body body)
{
    scene->mp_sectors->m_iterBucket = -1;
    scene->mp_sectors->m_iterItem = nullptr;
    for (;;) {
        ListKit::KeyMap* table = scene->mp_sectors;
        if (!table->StepIterator() || !table->m_iterItem || !table->m_iterItem->value)
            break;
        body(static_cast<CWorldCell*>(table->m_iterItem->value));
    }
}

void PutSector(CWorldScene* scene, CWorldCell* sector)
{
    uint32_t key = sector->m_checksum;
    scene->mp_sectors->StoreEntry(key, sector);
}

// A geom's counts (the engine's CGeom calls: 0x0048DF70, the slot-33 dispatcher, 0x0049C6F0).
int GeomCount70(void* geom) // (slot 36, through the jump 0x0048DF70)
{
    return reinterpret_cast<int(__thiscall*)(void*)>((*static_cast<void***>(geom))[36])(geom);
}
int GeomSlot33(void* geom) { return reinterpret_cast<int(__thiscall*)(void*)>(&VirtualSlot33Jump)(geom); }
int GeomCountC6F0(void* geom) // (slot 35, through the jump 0x0049C6F0)
{
    return reinterpret_cast<int(__thiscall*)(void*)>((*static_cast<void***>(geom))[35])(geom);
}

} // namespace

// 0x0049D390 (the original's SEH frame around the table's `new` left out)
CWorldScene* CWorldScene::MakeNew(int bits)
{
    m_vtable = reinterpret_cast<void**>(kSceneVtable);
    new (&m_unknownBox) Maths::BBox();
    new (&m_collisionBox) Maths::BBox();
    mp_collGrid = nullptr;
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    mp_sectors = memory ? new (memory) ListKit::KeyMap(bits) : nullptr;
    m_label[0] = '\0';
    m_pipName[0] = '\0';
    mp_collGrid = nullptr;
    mp_collStatics = nullptr;
    mp_collObjects = nullptr;
    m_collStaticCount = 0;
    m_addName[0] = '\0';
    m_addData = 0;
    mp_addCollStatics = nullptr;
    mp_addCollObjects = nullptr;
    m_addCollStaticCount = 0;
    m_sectorKept = false;
    m_added = false;
    mp_hiddenSectors = NewHead();
    mp_addSceneSectors = NewHead();
    m_hasCollision = false;
    m_isSky = false;
    return this;
}

// 0x0049DD10
void CWorldScene::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kSceneVtable);
    if (mp_sectors) {
        ForEachSector(this, [](CWorldCell* sector) {
            sector->ClearFlags(kSectorInGrid);
            Virtual<void*>(sector, 0, 1u);
        });
        if (ListKit::KeyMap* table = mp_sectors) {
            if (table->m_buckets) {
                table->EmptyTable();
                MemSys::Dealloc(table->m_buckets);
                table->m_buckets = nullptr;
            }
            MemSys::Dealloc(table);
        }
    }
    SceneUnloadCollision(this);
    if (mp_hiddenSectors)
        Virtual<void*>(mp_hiddenSectors, 0, 1u);
    if (mp_addSceneSectors)
        Virtual<void*>(mp_addSceneSectors, 0, 1u);
    if (m_addData)
        UnloadTexDict(reinterpret_cast<CTextureSet*>(m_addData));
}

// 0x0049E1D0
CWorldScene* CWorldScene::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x0049D1A0 (the third count is added twice under the same name, as the original)
void CWorldScene::FetchDiagnostics(Scripts::CStruct* info)
{
    info->AppendWord(0xC3F4169A, m_label);
    int sectors = 0, count70 = 0, slot33 = 0, slot33Again = 0, countC6F0 = 0;
    if (mp_sectors) {
        ForEachSector(this, [&](CWorldCell* sector) {
            ++sectors;
            if (void* geom = sector->m_geomHandle) {
                count70 += GeomCount70(geom);
                slot33 += GeomSlot33(geom);
                slot33Again += GeomSlot33(geom);
                countC6F0 += GeomCountC6F0(geom);
            }
        });
    }
    info->PutInt(0x4A6BF967, sectors);
    info->PutInt(0x0AC30A9D, m_collStaticCount);
    info->PutInt(0x969D3AF6, count70);
    info->PutInt(0xD576DF05, slot33);
    info->PutInt(0x1E3061EE, slot33Again);
    info->PutInt(0x1E3061EE, slot33Again);
    info->PutInt(0x78CAD2E8, countC6F0);
    info->PutInt(0x5714C480, *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(m_textureSet) + 0x10));
}

// 0x0049D4E0
void CWorldScene::InsertSector(CWorldCell* sector) { PutSector(this, sector); }

// 0x0049D510
uint32_t CWorldScene::DuplicateSceneSector(CWorldCell* sector, CWorldScene* scene, int instance, bool flag, uint32_t name)
{
    if (!scene)
        scene = this;
    CWorldCell* clone = sector->Replicate(instance, flag, scene);
    if (!clone)
        return 0;
    if (name)
        clone->m_checksum = name;
    PutSector(scene, clone);
    if (flag)
        scene->m_sectorKept = true;
    return clone->m_checksum;
}

// 0x0049DF30
uint32_t CWorldScene::CloneSectorNamed(uint32_t sectionId, CWorldScene* scene, int instance, bool flag, uint32_t name)
{
    CWorldCell* sector = SceneSector(this, sectionId);
    if (!sector)
        return 0;
    return DuplicateSceneSector(sector, scene, instance, flag, name);
}

// 0x0049D590 (the original's SEH frame around the grid's `new` left out)
bool CWorldScene::AddCollisionData(const uint8_t* data, int flag)
{
    SceneLoadCollisionData(this, data, &m_collStaticCount, &mp_collStatics, &mp_collObjects, &m_collisionBox, flag);
    if (m_collStaticCount > 0) {
        if (m_hasCollision) {
            void* memory = MemSys::Alloc(sizeof(CCollGrid));
            CCollGrid* grid = nullptr;
            if (memory) {
                grid = static_cast<CCollGrid*>(memory);
                new (memory) Maths::BBox();
                for (int x = 0; x < CCollGrid::kSize; ++x) {
                    for (int z = 0; z < CCollGrid::kSize; ++z)
                        grid->Cell(x, z)->MakeNew();
                }
            }
            mp_collGrid = grid;
            grid->PrepareAll(&m_collisionBox);
            mp_collGrid->Fill(mp_collStatics, m_collStaticCount);
        }
        for (int i = 0; i < m_collStaticCount; ++i) {
            CStaticCollider* coll = CollStaticAt(mp_collStatics, i);
            uint32_t name = reinterpret_cast<CHitMesh*>(coll)->m_checksum;
            auto* sector = static_cast<CWorldCell*>(mp_sectors->FetchEntry(name));
            if (!sector) {
                sector = NewSector(this);
                sector->m_checksum = name;
                sector->ChangeLive(true);
                PutSector(this, sector);
            }
            sector->SetCollObj(reinterpret_cast<CHitMesh*>(CollStaticAt(mp_collStatics, i)));
            sector->ChangeBits(kSectorInGrid);
        }
    }
    if (Scripts::FetchIntValue(0xBA1E4254))
    {
        void* weather = g_weather; // the weather (slot 1, through the jump 0x004A1090)
        reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(weather))[1])(weather);
    }
    return true;
}

// 0x0049DF70 (the original sprintfs into its 128 bytes unbounded)
bool CWorldScene::LoadCollisionFile(const char* name, int flag)
{
    std::snprintf(m_pipName, sizeof(m_pipName), "levels\\%s\\%s%s.col.%s", name, name, "", PlatformExtension());
    auto* data = static_cast<const uint8_t*>(Packs::ReadIn(m_pipName, false, nullptr, nullptr, nullptr));
    if (!data)
        return false;
    AddCollisionData(data, flag);
    return true;
}

// 0x0049D780 (the original's four list heads on its stack, and its SEH frame)
void CWorldScene::UpdateSuperSectors(MemSys::ListNode* added)
{
    if (!mp_collGrid)
        return;
    MemSys::ListNode toAdd, toRemove, moved, toDelete; // (collision objects; sectors)
    SetupListSentinel(&toAdd, kCollHeadVtable);
    SetupListSentinel(&toRemove, kCollHeadVtable);
    SetupListSentinel(&moved, kCollHeadVtable);
    SetupListSentinel(&toDelete, kSectorHeadVtable);
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    ForEachSector(this, [&](CWorldCell* sector) {
        if (sector->FetchBits() & kSectorAddToGrid) {
            AppendLast(&toAdd, NewNode(kCollNodeVtable, sector->mp_collObj));
            sector->ClearFlags(kSectorAddToGrid);
            sector->ChangeBits(kSectorInGrid);
        }
        if (sector->FetchBits() & (kSectorDelete | kSectorRemoveFromGrid)) {
            AppendLast(&toRemove, NewNode(kCollNodeVtable, sector->mp_collObj));
            if (sector->FetchBits() & kSectorDelete)
                AppendLast(&toDelete, NewNode(kSectorNodeVtable, sector));
            else
                sector->ClearFlags(kSectorRemoveFromGrid);
        }
        if (sector->FetchBits() & kSectorMoved) {
            AppendLast(&moved, NewNode(kCollNodeVtable, sector->mp_collObj));
            sector->ClearFlags(kSectorMoved);
        }
    });
    if (added && !IsHead(added->next) && added->next) {
        for (MemSys::ListNode* node = added->next; node;) {
            MemSys::ListNode* next = NextNode(node);
            auto* sector = static_cast<CWorldCell*>(node->data);
            bool listed = false;
            for (MemSys::ListNode* removing = FirstNode(&toRemove); removing && !listed; removing = NextNode(removing))
                listed = removing->data == sector->mp_collObj;
            if (!listed) {
                AppendLast(&toRemove, NewNode(kCollNodeVtable, sector->mp_collObj));
                sector->ClearFlags(kSectorRemoveFromGrid);
            }
            node = next;
        }
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
    mp_collGrid->TickFrame(&toAdd, &toRemove, &moved);
    DeleteNodes(&toAdd);
    DeleteNodes(&toRemove);
    DeleteNodes(&moved);
    for (MemSys::ListNode* node = FirstNode(&toDelete); node;) {
        MemSys::ListNode* next = NextNode(node);
        auto* sector = static_cast<CWorldCell*>(node->data);
        uint32_t key = sector->m_checksum;
        mp_sectors->EraseEntry(key);
        sector->ClearFlags(kSectorInGrid);
        if (sector)
            Virtual<void*>(sector, 0, 1u);
        Virtual<void*>(node, 0, 1u);
        node = next;
    }
    m_sectorKept = false;
}

// 0x0049E1B0
bool CWorldScene::UpdateSuperSectorsIfMarked(MemSys::ListNode* added)
{
    if (!m_sectorKept)
        return false;
    UpdateSuperSectors(added);
    return true;
}

// 0x0049DFD0
bool CWorldScene::LoadAddScene(const char* name)
{
    Maths::BBox box;
    if (!mp_collStatics)
        return false;
    std::snprintf(m_addPipName, sizeof(m_addPipName), "levels\\%s\\%s%s.col.%s", name, name, "", PlatformExtension());
    // (the base's collision file, not the one just named - as the original)
    if (auto* data = static_cast<const uint8_t*>(Packs::ReadIn(m_pipName, false, nullptr, nullptr, nullptr)))
        SceneLoadCollisionData(this, data, &m_addCollStaticCount, &mp_addCollStatics, &mp_addCollObjects, &box, 0);
    if (m_addCollStaticCount > 0) {
        for (int i = 0; i < m_addCollStaticCount; ++i) {
            uint32_t sectionId = reinterpret_cast<CHitMesh*>(CollStaticAt(mp_addCollStatics, i))->m_checksum;
            auto* sector = static_cast<CWorldCell*>(mp_sectors->FetchEntry(sectionId));
            if (!sector) {
                sector = NewSector(this);
                sector->m_checksum = sectionId;
                sector->ChangeLive(true);
                sector->ChangeBits(kSectorAddToGrid);
                m_sectorKept = true;
                PutSector(this, sector);
                AppendLast(mp_addSceneSectors, NewNode(kSectorNodeVtable, sector));
            }
            sector->SetCollObj(reinterpret_cast<CHitMesh*>(CollStaticAt(mp_addCollStatics, i)));
        }
        UpdateSuperSectors(mp_hiddenSectors);
    }
    return Virtual<bool>(this, 5, name);
}

// 0x0049DDE0
bool CWorldScene::FlipExtraScene()
{
    if (!FirstNode(mp_addSceneSectors))
        return false;
    m_added = !m_added;
    MemSys::ListNode* hide = m_added ? mp_hiddenSectors : mp_addSceneSectors;
    MemSys::ListNode* show = m_added ? mp_addSceneSectors : mp_hiddenSectors;
    for (MemSys::ListNode* node = FirstNode(hide); node;) {
        MemSys::ListNode* next = NextNode(node);
        auto* sector = static_cast<CWorldCell*>(node->data);
        sector->ChangeBits(kSectorRemoveFromGrid);
        m_sectorKept = true;
        uint32_t key = sector->m_checksum;
        mp_sectors->EraseEntry(key);
        sector->ChangeLive(false);
        node = next;
    }
    for (MemSys::ListNode* node = FirstNode(show); node;) {
        MemSys::ListNode* next = NextNode(node);
        auto* sector = static_cast<CWorldCell*>(node->data);
        sector->ChangeBits(kSectorAddToGrid);
        m_sectorKept = true;
        PutSector(this, sector);
        sector->ChangeLive(true);
        node = next;
    }
    UpdateSuperSectors(hide);
    return m_added;
}

// 0x0049E200 (the added collision's array and file are let go of but not cleared, as the original)
bool CWorldScene::UnloadAddedScene()
{
    if (m_added)
        FlipExtraScene();
    if (!Virtual<bool>(this, 6))
        return false;
    if (CStaticCollider* statics = mp_addCollStatics) {
        auto* array = reinterpret_cast<uint8_t*>(statics) - 4;
        if (*reinterpret_cast<int*>(array))
            Virtual<void*>(statics, 0, 3u); // (the vector deleting destructor)
        else
            MemSys::Dealloc(array);
    }
    if (mp_addCollObjects)
        Packs::Offload(m_addPipName);
    if (mp_hiddenSectors)
        DeleteNodes(mp_hiddenSectors);
    if (mp_addSceneSectors) {
        for (MemSys::ListNode* node = FirstNode(mp_addSceneSectors); node;) {
            MemSys::ListNode* next = NextNode(node);
            DeleteSceneSector(this, static_cast<CWorldCell*>(node->data));
            Virtual<void*>(node, 0, 1u);
            node = next;
        }
    }
    m_added = false;
    return true;
}

// --- the sector lists' nodes ------------------------------------------------------------------------------------

// 0x0049D300
void SectorListNodes::DestroyBody() { reinterpret_cast<MemSys::ListNode*>(this)->UnlinkAs(kSectorNodeVtable); }

// 0x0049D320
MemSys::ListNode* SectorListNodes::DestroyNode(unsigned int flags)
{
    auto* node = reinterpret_cast<MemSys::ListNode*>(this);
    node->UnlinkAs(kSectorNodeVtable);
    if (flags & 1)
        MemSys::Dealloc(node);
    return node;
}

// 0x0049D360
MemSys::ListNode* SectorListNodes::DestroyHead(unsigned int flags)
{
    auto* node = reinterpret_cast<MemSys::ListNode*>(this);
    node->UnlinkAs(kSectorNodeVtable);
    if (flags & 1)
        MemSys::Dealloc(node);
    return node;
}

namespace {
static VtableSlot kSceneVtable_slots[] = {&Engine3D::CWorldScene::Teardown, &Engine3D::CWorldScene::PostLoad, &Engine3D::CWorldScene::PostAdd,
                &Engine3D::CWorldScene::LoadTextures, &Engine3D::CWorldScene::LoadCollision, &Engine3D::CWorldScene::AddCollision,
                &Engine3D::CWorldScene::UnloadAddScene, &Engine3D::CWorldScene::PlatCreateSector, Stubs::IgnoreArgument,
                &Engine3D::CWorldScene::GetDefaultColour, &Engine3D::CWorldScene::SetRendered};
void** const kSceneVtable = reinterpret_cast<void**>(kSceneVtable_slots);
static VtableSlot kSectorNodeVtable_slots[] = {&Engine3D::SectorListNodes::DestroyNode};
void** const kSectorNodeVtable = reinterpret_cast<void**>(kSectorNodeVtable_slots);
static VtableSlot kSectorHeadVtable_slots[] = {&Engine3D::SectorListNodes::DestroyHead};
void** const kSectorHeadVtable = reinterpret_cast<void**>(kSectorHeadVtable_slots);
} // namespace

} // namespace Nx

namespace Vt {
static VtableSlot kCollGridNode_slots[] = {&Engine3D::CollGridListNodes::DestroyNode};
void** const kCollGridNode = reinterpret_cast<void**>(kCollGridNode_slots);
static VtableSlot kCollGridHead_slots[] = {&Engine3D::CollGridListNodes::DestroyHead};
void** const kCollGridHead = reinterpret_cast<void**>(kCollGridHead_slots);
} // namespace Vt
