// The engine's scene (see engine_scene.h): its members of 0x0049D1A0-0x0049E2EF - the constructor and destructor,
// the statistics, sectors added and cloned, the collision added and the super sectors (the collision grid) brought up
// to date, a scene added over it - and the nodes of its sector lists.

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
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kSceneVtable = 0x006499E0;
constexpr uint32_t kCollStaticSize = 0x18;
void** const kSectorNodeVtable = reinterpret_cast<void**>(0x006499D8);
void** const kSectorHeadVtable = reinterpret_cast<void**>(0x006499DC);
void** const kCollNodeVtable = reinterpret_cast<void**>(0x00645D7C); // (the collision grid's lists')
void** const kCollHeadVtable = reinterpret_cast<void**>(0x00645D80);

template <typename R = void, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The rest of the scene (0x0049C910-0x0049D18E, engine_scene.cpp): the sector of a name, a sector deleted, a scene
// file's collision data read, the collision let go of.
CSector* SceneSector(CScene* scene, uint32_t name) { return static_cast<CSector*>(scene->GetSector(name)); }
bool DeleteSceneSector(CScene* scene, CSector* sector) { return scene->DeleteSector(sector); }
bool SceneLoadCollisionData(CScene* scene, const uint8_t* data, int* count, CCollStatic** statics, uint8_t** objects,
                            Mth::BBox* box, int flag)
{
    return scene->LoadCollisionData(data, count, statics, objects, box, flag);
}
bool SceneUnloadCollision(CScene* scene) { return scene->UnloadCollision(); }

// The platform's new sector (slot 7), its geometry and collision cleared.
CSector* NewSector(CScene* scene)
{
    auto* sector = Virtual<CSector*>(scene, 7);
    sector->mp_geom = nullptr;
    sector->mp_collObj = nullptr;
    return sector;
}

CCollStatic* CollStaticAt(CCollStatic* statics, int index)
{
    return reinterpret_cast<CCollStatic*>(reinterpret_cast<uint8_t*>(statics) + index * kCollStaticSize);
}

// The lists (Lst::Head / Node): a head ends its list (its data is -1).
bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node`, or null at the head.
Mem::ListNode* NextNode(const Mem::ListNode* node) { return IsHead(node->next) ? nullptr : node->next; }
// The first node, or null when the list's empty (the original checks the head's next is no head and not null).
Mem::ListNode* FirstNode(const Mem::ListNode* head)
{
    return IsHead(head->next) ? nullptr : head->next;
}
// A head (on the stack or new).
void InitHead(Mem::ListNode* head, void** vtable)
{
    head->vtable = vtable;
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
}
// A new head (null without memory).
Mem::ListNode* NewHead()
{
    auto* head = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (head)
        InitHead(head, kSectorHeadVtable);
    return head;
}
// A new node (null without memory) for `data`.
Mem::ListNode* NewNode(void** vtable, void* data)
{
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
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
void AddToTail(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// Every node of a list deleted (its virtual [0]).
void DeleteNodes(Mem::ListNode* head)
{
    for (Mem::ListNode* node = FirstNode(head); node;) {
        Mem::ListNode* next = NextNode(node);
        Virtual<void*>(node, 0, 1u);
        node = next;
    }
}

// The sectors in the table in turn, as far as the first null item or value.
template <typename Body>
void ForEachSector(CScene* scene, Body body)
{
    scene->mp_sectors->m_iterBucket = -1;
    scene->mp_sectors->m_iterItem = nullptr;
    for (;;) {
        Lst::HashTable* table = scene->mp_sectors;
        if (!table->IterateNext() || !table->m_iterItem || !table->m_iterItem->value)
            break;
        body(static_cast<CSector*>(table->m_iterItem->value));
    }
}

void PutSector(CScene* scene, CSector* sector)
{
    uint32_t key = sector->m_checksum;
    scene->mp_sectors->PutItem(key, sector);
}

// A geom's counts (the engine's CGeom calls: 0x0048DF70, the slot-33 dispatcher, 0x0049C6F0).
int GeomCount70(void* geom) { return GameFunction<int(__thiscall*)(void*)>(0x0048DF70)(geom); }
int GeomSlot33(void* geom) { return reinterpret_cast<int(__thiscall*)(void*)>(&VirtualSlot33Jump)(geom); }
int GeomCountC6F0(void* geom) { return GameFunction<int(__thiscall*)(void*)>(0x0049C6F0)(geom); }

} // namespace

// 0x0049D390 (the original's SEH frame around the table's `new` left out)
CScene* CScene::Construct(int bits)
{
    m_vtable = reinterpret_cast<void**>(kSceneVtable);
    new (&m_unknownBox) Mth::BBox();
    new (&m_collisionBox) Mth::BBox();
    mp_collGrid = nullptr;
    void* memory = Mem::New(sizeof(Lst::HashTable));
    mp_sectors = memory ? new (memory) Lst::HashTable(bits) : nullptr;
    m_name[0] = '\0';
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
void CScene::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kSceneVtable);
    if (mp_sectors) {
        ForEachSector(this, [](CSector* sector) {
            sector->ClearFlags(kSectorInGrid);
            Virtual<void*>(sector, 0, 1u);
        });
        if (Lst::HashTable* table = mp_sectors) {
            if (table->m_buckets) {
                table->FlushAllItems();
                Mem::Free(table->m_buckets);
                table->m_buckets = nullptr;
            }
            Mem::Free(table);
        }
    }
    SceneUnloadCollision(this);
    if (mp_hiddenSectors)
        Virtual<void*>(mp_hiddenSectors, 0, 1u);
    if (mp_addSceneSectors)
        Virtual<void*>(mp_addSceneSectors, 0, 1u);
    if (m_addData)
        UnloadTexDict(reinterpret_cast<CTexDict*>(m_addData));
}

// 0x0049E1D0
CScene* CScene::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049D1A0 (the third count is added twice under the same name, as the original)
void CScene::GetDebugInfo(Script::CStruct* info)
{
    info->AddString(0xC3F4169A, m_name);
    int sectors = 0, count70 = 0, slot33 = 0, slot33Again = 0, countC6F0 = 0;
    if (mp_sectors) {
        ForEachSector(this, [&](CSector* sector) {
            ++sectors;
            if (void* geom = sector->mp_geom) {
                count70 += GeomCount70(geom);
                slot33 += GeomSlot33(geom);
                slot33Again += GeomSlot33(geom);
                countC6F0 += GeomCountC6F0(geom);
            }
        });
    }
    info->AddInteger(0x4A6BF967, sectors);
    info->AddInteger(0x0AC30A9D, m_collStaticCount);
    info->AddInteger(0x969D3AF6, count70);
    info->AddInteger(0xD576DF05, slot33);
    info->AddInteger(0x1E3061EE, slot33Again);
    info->AddInteger(0x1E3061EE, slot33Again);
    info->AddInteger(0x78CAD2E8, countC6F0);
    info->AddInteger(0x5714C480, *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(mp_texDict) + 0x10));
}

// 0x0049D4E0
void CScene::AddSector(CSector* sector) { PutSector(this, sector); }

// 0x0049D510
uint32_t CScene::CloneSector(CSector* sector, CScene* scene, int instance, bool flag, uint32_t name)
{
    if (!scene)
        scene = this;
    CSector* clone = sector->Clone(instance, flag, scene);
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
uint32_t CScene::CloneSectorNamed(uint32_t sectorName, CScene* scene, int instance, bool flag, uint32_t name)
{
    CSector* sector = SceneSector(this, sectorName);
    if (!sector)
        return 0;
    return CloneSector(sector, scene, instance, flag, name);
}

// 0x0049D590 (the original's SEH frame around the grid's `new` left out)
bool CScene::AddCollisionData(const uint8_t* data, int flag)
{
    SceneLoadCollisionData(this, data, &m_collStaticCount, &mp_collStatics, &mp_collObjects, &m_collisionBox, flag);
    if (m_collStaticCount > 0) {
        if (m_hasCollision) {
            void* memory = Mem::New(sizeof(CCollGrid));
            CCollGrid* grid = nullptr;
            if (memory) {
                grid = static_cast<CCollGrid*>(memory);
                new (memory) Mth::BBox();
                for (int x = 0; x < CCollGrid::kSize; ++x) {
                    for (int z = 0; z < CCollGrid::kSize; ++z)
                        grid->Cell(x, z)->Construct();
                }
            }
            mp_collGrid = grid;
            grid->Init(&m_collisionBox);
            mp_collGrid->Fill(mp_collStatics, m_collStaticCount);
        }
        for (int i = 0; i < m_collStaticCount; ++i) {
            CCollStatic* coll = CollStaticAt(mp_collStatics, i);
            uint32_t name = reinterpret_cast<CCollObj*>(coll)->m_checksum;
            auto* sector = static_cast<CSector*>(mp_sectors->GetItem(name));
            if (!sector) {
                sector = NewSector(this);
                sector->m_checksum = name;
                sector->SetActive(true);
                PutSector(this, sector);
            }
            sector->SetCollObj(reinterpret_cast<CCollObj*>(CollStaticAt(mp_collStatics, i)));
            sector->SetFlags(kSectorInGrid);
        }
    }
    if (Script::GetInteger(0xBA1E4254))
        GameFunction<void(__thiscall*)(void*)>(0x004A1090)(GameVariable<void*>(0x006FA7C8)); // the weather
    return true;
}

// 0x0049DF70 (the original sprintfs into its 128 bytes unbounded)
bool CScene::LoadCollisionFile(const char* name, int flag)
{
    std::snprintf(m_pipName, sizeof(m_pipName), "levels\\%s\\%s%s.col.%s", name, name, "", PlatformExtension());
    auto* data = static_cast<const uint8_t*>(Pip::Load(m_pipName, false, nullptr, nullptr, nullptr));
    if (!data)
        return false;
    AddCollisionData(data, flag);
    return true;
}

// 0x0049D780 (the original's four list heads on its stack, and its SEH frame)
void CScene::UpdateSuperSectors(Mem::ListNode* added)
{
    if (!mp_collGrid)
        return;
    Mem::ListNode toAdd, toRemove, moved, toDelete; // (collision objects; sectors)
    InitHead(&toAdd, kCollHeadVtable);
    InitHead(&toRemove, kCollHeadVtable);
    InitHead(&moved, kCollHeadVtable);
    InitHead(&toDelete, kSectorHeadVtable);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    ForEachSector(this, [&](CSector* sector) {
        if (sector->GetFlags() & kSectorAddToGrid) {
            AddToTail(&toAdd, NewNode(kCollNodeVtable, sector->mp_collObj));
            sector->ClearFlags(kSectorAddToGrid);
            sector->SetFlags(kSectorInGrid);
        }
        if (sector->GetFlags() & (kSectorDelete | kSectorRemoveFromGrid)) {
            AddToTail(&toRemove, NewNode(kCollNodeVtable, sector->mp_collObj));
            if (sector->GetFlags() & kSectorDelete)
                AddToTail(&toDelete, NewNode(kSectorNodeVtable, sector));
            else
                sector->ClearFlags(kSectorRemoveFromGrid);
        }
        if (sector->GetFlags() & kSectorMoved) {
            AddToTail(&moved, NewNode(kCollNodeVtable, sector->mp_collObj));
            sector->ClearFlags(kSectorMoved);
        }
    });
    if (added && !IsHead(added->next) && added->next) {
        for (Mem::ListNode* node = added->next; node;) {
            Mem::ListNode* next = NextNode(node);
            auto* sector = static_cast<CSector*>(node->data);
            bool listed = false;
            for (Mem::ListNode* removing = FirstNode(&toRemove); removing && !listed; removing = NextNode(removing))
                listed = removing->data == sector->mp_collObj;
            if (!listed) {
                AddToTail(&toRemove, NewNode(kCollNodeVtable, sector->mp_collObj));
                sector->ClearFlags(kSectorRemoveFromGrid);
            }
            node = next;
        }
    }
    Mem::Manager::Get()->PopContext();
    mp_collGrid->Update(&toAdd, &toRemove, &moved);
    DeleteNodes(&toAdd);
    DeleteNodes(&toRemove);
    DeleteNodes(&moved);
    for (Mem::ListNode* node = FirstNode(&toDelete); node;) {
        Mem::ListNode* next = NextNode(node);
        auto* sector = static_cast<CSector*>(node->data);
        uint32_t key = sector->m_checksum;
        mp_sectors->FlushItem(key);
        sector->ClearFlags(kSectorInGrid);
        if (sector)
            Virtual<void*>(sector, 0, 1u);
        Virtual<void*>(node, 0, 1u);
        node = next;
    }
    m_sectorKept = false;
}

// 0x0049E1B0
bool CScene::UpdateSuperSectorsIfMarked(Mem::ListNode* added)
{
    if (!m_sectorKept)
        return false;
    UpdateSuperSectors(added);
    return true;
}

// 0x0049DFD0
bool CScene::LoadAddScene(const char* name)
{
    Mth::BBox box;
    if (!mp_collStatics)
        return false;
    std::snprintf(m_addPipName, sizeof(m_addPipName), "levels\\%s\\%s%s.col.%s", name, name, "", PlatformExtension());
    // (the base's collision file, not the one just named - as the original)
    if (auto* data = static_cast<const uint8_t*>(Pip::Load(m_pipName, false, nullptr, nullptr, nullptr)))
        SceneLoadCollisionData(this, data, &m_addCollStaticCount, &mp_addCollStatics, &mp_addCollObjects, &box, 0);
    if (m_addCollStaticCount > 0) {
        for (int i = 0; i < m_addCollStaticCount; ++i) {
            uint32_t sectorName = reinterpret_cast<CCollObj*>(CollStaticAt(mp_addCollStatics, i))->m_checksum;
            auto* sector = static_cast<CSector*>(mp_sectors->GetItem(sectorName));
            if (!sector) {
                sector = NewSector(this);
                sector->m_checksum = sectorName;
                sector->SetActive(true);
                sector->SetFlags(kSectorAddToGrid);
                m_sectorKept = true;
                PutSector(this, sector);
                AddToTail(mp_addSceneSectors, NewNode(kSectorNodeVtable, sector));
            }
            sector->SetCollObj(reinterpret_cast<CCollObj*>(CollStaticAt(mp_addCollStatics, i)));
        }
        UpdateSuperSectors(mp_hiddenSectors);
    }
    return Virtual<bool>(this, 5, name);
}

// 0x0049DDE0
bool CScene::ToggleAddScene()
{
    if (!FirstNode(mp_addSceneSectors))
        return false;
    m_added = !m_added;
    Mem::ListNode* hide = m_added ? mp_hiddenSectors : mp_addSceneSectors;
    Mem::ListNode* show = m_added ? mp_addSceneSectors : mp_hiddenSectors;
    for (Mem::ListNode* node = FirstNode(hide); node;) {
        Mem::ListNode* next = NextNode(node);
        auto* sector = static_cast<CSector*>(node->data);
        sector->SetFlags(kSectorRemoveFromGrid);
        m_sectorKept = true;
        uint32_t key = sector->m_checksum;
        mp_sectors->FlushItem(key);
        sector->SetActive(false);
        node = next;
    }
    for (Mem::ListNode* node = FirstNode(show); node;) {
        Mem::ListNode* next = NextNode(node);
        auto* sector = static_cast<CSector*>(node->data);
        sector->SetFlags(kSectorAddToGrid);
        m_sectorKept = true;
        PutSector(this, sector);
        sector->SetActive(true);
        node = next;
    }
    UpdateSuperSectors(hide);
    return m_added;
}

// 0x0049E200 (the added collision's array and file are let go of but not cleared, as the original)
bool CScene::UnloadAddedScene()
{
    if (m_added)
        ToggleAddScene();
    if (!Virtual<bool>(this, 6))
        return false;
    if (CCollStatic* statics = mp_addCollStatics) {
        auto* array = reinterpret_cast<uint8_t*>(statics) - 4;
        if (*reinterpret_cast<int*>(array))
            Virtual<void*>(statics, 0, 3u); // (the vector deleting destructor)
        else
            Mem::Free(array);
    }
    if (mp_addCollObjects)
        Pip::Unload(m_addPipName);
    if (mp_hiddenSectors)
        DeleteNodes(mp_hiddenSectors);
    if (mp_addSceneSectors) {
        for (Mem::ListNode* node = FirstNode(mp_addSceneSectors); node;) {
            Mem::ListNode* next = NextNode(node);
            DeleteSceneSector(this, static_cast<CSector*>(node->data));
            Virtual<void*>(node, 0, 1u);
            node = next;
        }
    }
    m_added = false;
    return true;
}

// --- the sector lists' nodes ------------------------------------------------------------------------------------

// 0x0049D300
void SectorListNodes::DestroyBody() { reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(kSectorNodeVtable); }

// 0x0049D320
Mem::ListNode* SectorListNodes::DestroyNode(unsigned int flags)
{
    auto* node = reinterpret_cast<Mem::ListNode*>(this);
    node->UnlinkAs(kSectorNodeVtable);
    if (flags & 1)
        Mem::Free(node);
    return node;
}

// 0x0049D360
Mem::ListNode* SectorListNodes::DestroyHead(unsigned int flags)
{
    auto* node = reinterpret_cast<Mem::ListNode*>(this);
    node->UnlinkAs(kSectorNodeVtable);
    if (flags & 1)
        Mem::Free(node);
    return node;
}

} // namespace Nx

REPLACE(0x0049D390, &Nx::CScene::Construct);
REPLACE(0x0049DD10, &Nx::CScene::DestroyBody);
REPLACE(0x0049E1D0, &Nx::CScene::Destroy);
REPLACE(0x0049D1A0, &Nx::CScene::GetDebugInfo);
REPLACE(0x0049D4E0, &Nx::CScene::AddSector);
REPLACE(0x0049D510, &Nx::CScene::CloneSector);
REPLACE(0x0049DF30, &Nx::CScene::CloneSectorNamed);
REPLACE(0x0049D590, &Nx::CScene::AddCollisionData);
REPLACE(0x0049DF70, &Nx::CScene::LoadCollisionFile);
REPLACE(0x0049D780, &Nx::CScene::UpdateSuperSectors);
REPLACE(0x0049E1B0, &Nx::CScene::UpdateSuperSectorsIfMarked);
REPLACE(0x0049DFD0, &Nx::CScene::LoadAddScene);
REPLACE(0x0049DDE0, &Nx::CScene::ToggleAddScene);
REPLACE(0x0049E200, &Nx::CScene::UnloadAddedScene);
REPLACE(0x0049D300, &Nx::SectorListNodes::DestroyBody);
REPLACE(0x0049D320, &Nx::SectorListNodes::DestroyNode);
REPLACE(0x0049D360, &Nx::SectorListNodes::DestroyHead);
