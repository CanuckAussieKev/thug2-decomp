// The asset manager.

#include "game/ass/resource_registry.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/file/async.h"
#include "game/lst/key_map.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/net/task_vtable.h"

namespace AssetStore {

AssetStore::CResourceRegistry* g_assetManager;
int g_assManReferences;
FrameLoop::Hub* g_mainLoopManager;

namespace {

static VtableSlot kAssManVtable_slots[] = {&AssetStore::CResourceRegistry::ScalarDelete, &AssetStore::CResourceRegistry::StartCallback,
        &AssetStore::CResourceRegistry::StopCallback};
void** const kAssManVtable = reinterpret_cast<void**>(kAssManVtable_slots);

int& RefTally() { return g_assManReferences; }

// The script integer the loads look up (a debug leftover: set, a failed asset isn't deleted).
int KeepFailedAssets() { return Scripts::FetchIntValue(0x25DC7904u); }

// The file name extensions and their types (0x0067C650, ended by a null name).
struct ExtensionType {
    const char* extension;
    int type;
};
// (the extensions are the exe's strings; the table's last entry stays null)
ExtensionType g_extensions[13]{
                      {"BIN", 1}, {"COL", 4},
                      {"SCN", 2}, {"GEOM", 2},
                      {"MDL", 7}, {"SKA", 5},
                      {"FAM", 5}, {"SKE", 6},
                      {"SKIN", 7}, {"TEX", 3},
                      {"CUT", 9}, {"QB", 10}};
const ExtensionType* Extensions() { return g_extensions; }

// The hash table lookup inlined in the original: the first item with the key and a value.
CResource* Find(ListKit::KeyMap* table, uint32_t checksum)
{
    for (ListKit::KeyMap::Item* item = &table->m_buckets[((1u << table->m_bits) - 1) & checksum]; item;
         item = item->next) {
        if (item->key == checksum && item->value)
            return static_cast<CResource*>(item->value);
    }
    return nullptr;
}
void WaitUntilLoaded(CResource* asset)
{
    while (!asset->VIsLoaded())
        FileLoad::AsyncWaitForIO(false);
}
// `asset` onto the end of the circular list `*head`.
void Append(CResource** head, CResource* asset)
{
    if (*head) {
        asset->m_succ = *head;
        asset->m_lastValue = (*head)->m_lastValue;
        (*head)->m_lastValue->m_succ = asset;
        (*head)->m_lastValue = asset;
    } else {
        *head = asset;
        asset->m_succ = asset;
        asset->m_lastValue = asset;
    }
}

// A new asset of `type` (null for a type that isn't one, or if the allocation fails).
CResource* MakeAsset(int type, bool withCutscene)
{
    void* memory;
    switch (type) {
    case kScene:
        memory = MemSys::AllocZeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->Scene() : nullptr;
    case kTextureDictionary:
        memory = MemSys::AllocZeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->TextureDictionary() : nullptr;
    case kSceneCollision:
        memory = MemSys::AllocZeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->SceneCollision() : nullptr;
    case kAnimation:
        memory = MemSys::AllocZeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->Animation() : nullptr;
    case kSkeleton:
        memory = MemSys::AllocZeroed(0x4C);
        return memory ? static_cast<AssetConstructors*>(memory)->Skeleton() : nullptr;
    case kSkinnedModel:
        memory = MemSys::AllocZeroed(0x60);
        return memory ? static_cast<AssetConstructors*>(memory)->SkinnedModel() : nullptr;
    case kSkinCollision:
        memory = MemSys::AllocZeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->SkinCollision() : nullptr;
    case kCutscene:
        // (ReadResourceFromStream has no case for cutscenes)
        if (!withCutscene)
            return nullptr;
        memory = MemSys::AllocZeroed(0x4C);
        return memory ? static_cast<AssetConstructors*>(memory)->Cutscene() : nullptr;
    case kNodeArray:
        memory = MemSys::AllocZeroed(0x64);
        return memory ? static_cast<AssetConstructors*>(memory)->NodeArray() : nullptr;
    case kOwnedNodeArray: {
        memory = MemSys::AllocZeroed(0x64);
        CResource* asset = memory ? static_cast<AssetConstructors*>(memory)->NodeArray() : nullptr;
        static_cast<AssetConstructors*>(asset)->MakeNodeArrayOwner(); // (null if the allocation failed: a crash)
        return asset;
    }
    default:
        return nullptr;
    }
}

// The manager's callback given to a new asset (cleared after one asset if it's a one-shot).
void GiveCallback(CResourceRegistry* manager, CResource* asset)
{
    asset->ChangeHook(manager->m_callback, manager->m_callbackData0, manager->m_callbackData1);
    if (manager->m_callbackOnce) {
        manager->m_callback = nullptr;
        manager->m_callbackOnce = false;
    }
}
// `asset` depends on the asset `group` (if given): each knows the other.
void JoinGroup(CResourceRegistry* manager, CResource* asset, uint32_t group)
{
    if (!group)
        return;
    CResource* owner = manager->ObtainLoadedItem(group, 1);
    asset->ChangeCluster(owner);
    owner->AddDependent(asset);
}

} // namespace

// 0x00407F80
CResourceRegistry* CResourceRegistry::MakeNew()
{
    GameModules::PartUnit::MakeNew();
    m_vtable = kAssManVtable;
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    m_assets = memory ? new (memory) ListKit::KeyMap(10) : nullptr;
    m_callback = nullptr;
    m_callbackOnce = false;
    m_pending = nullptr;
    m_readyFlag = nullptr;
    m_unknown15 = false;
    auto* task = static_cast<TaskList::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (task) {
        task->MakeNew(0);
        reinterpret_cast<void**>(task)[7] = this;                                  // +0x1C the data
        task->m_vtable = GameCode::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&ProcessTask); // +0x18 the callback
    }
    m_task = task;
    return this;
}

// 0x00408930
void CResourceRegistry::Destruct()
{
    m_vtable = kAssManVtable;
    UnloadAssetsInList(m_readyFlag, true);  // (inlined)
    UnloadAssetsInList(m_pending, true);
    if (ListKit::KeyMap* table = m_assets) {
        if (table->m_buckets) {
            table->EmptyTable();
            MemSys::Hub::Pull()->Dealloc(table->m_buckets);
            table->m_buckets = nullptr;
        }
        MemSys::Hub::Pull()->Dealloc(table);
    }
    if (m_task)
        reinterpret_cast<void(__thiscall*)(TaskList::SchedulerTask*, int)>(m_task->m_vtable[2])(m_task, 1);
    GameModules::PartUnit::DestroyBody();
}

// 0x00408A10
CResourceRegistry* CResourceRegistry::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00407E90
void CResourceRegistry::StartCallback()
{
    reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(g_mainLoopManager) + 0x04)->QueueJob(m_task); // the logic tasks
}

// 0x00407AE0
void CResourceRegistry::StopCallback()
{
    m_task->DropOut();
}

// 0x00408060
void* CResourceRegistry::ReadResourceFromStream(uint32_t checksum, int type, uint32_t group, void* data, uint32_t size,
                                   bool permanent, bool copy, void* params, int extra)
{
    MemSys::PushMemProfile("Asset Overhead");
    CResource* asset = MakeAsset(type, false);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->m_pinned = permanent;
    asset->m_curPhase = kUnloaded;
    asset->SetChecksum(checksum);
    GiveCallback(this, asset);
    // (the original formats "%08x from data stream" with the checksum for the memory profile, unused)
    JoinGroup(this, asset, group);
    MemSys::PopMemProfile();
    int failed = reinterpret_cast<int(__thiscall*)(CResource*, void*, uint32_t, bool, void*, int)>(asset->m_vtable[4])(
        asset, data, size, copy, params, extra);
    MemSys::PushMemProfile("Asset Overhead");
    if (failed) {
        if (!KeepFailedAssets())
            asset->VDelete();
        MemSys::PopMemProfile();
        return nullptr;
    }
    m_assets->StoreEntry(checksum, asset);
    Append(&m_readyFlag, asset);
    MemSys::PopMemProfile();
    return asset->VData();
}

// 0x00408440
void* CResourceRegistry::LoadAsset(const char* name, int type, uint32_t checksum, uint32_t group, bool async, bool useArchive,
                         bool permanent, void* params, int extra)
{
    if (!checksum)
        checksum = Checksums::HashFromText(name);
    if (!type)
        type = GetTypeFromExtension(name);
    CResource* asset = MakeAsset(type, true);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->m_pinned = permanent;
    asset->m_curPhase = kUnloaded;
    GiveCallback(this, asset);
    asset->SetChecksum(checksum);
    JoinGroup(this, asset, group);
    int failed = reinterpret_cast<int(__thiscall*)(CResource*, const char*, bool, bool, void*, int)>(asset->m_vtable[5])(
        asset, name, async, useArchive, params, extra);
    if (failed) {
        if (!KeepFailedAssets())
            asset->VDelete();
        return nullptr;
    }
    m_assets->StoreEntry(checksum, asset);
    Append(async ? &m_pending : &m_readyFlag, asset);
    return asset->VData();
}

// 0x004087B0
void* CResourceRegistry::FetchOrLoadResource(const char* name, int type, uint32_t checksum, uint32_t group, bool async,
                              bool useArchive, bool permanent, void* params, int extra)
{
    if (!checksum)
        checksum = Checksums::HashFromText(name);
    void* data;
    if (CResource* asset = Find(m_assets, checksum)) {
        data = asset->VData();
        WaitUntilLoaded(asset);
    } else {
        data = LoadAsset(name, type, checksum, group, async, useArchive, permanent, params, extra);
    }
    KeepFailedAssets(); // (looked up and not used)
    return data;
}

// 0x00408870
bool CResourceRegistry::LoadAnim(const char* name, uint32_t base, uint32_t offset, bool async, bool useArchive)
{
    uint32_t checksum = Checksums::HashFromText(name);
    if (base)
        checksum = base + offset;
    if (AssetExists(checksum))
        return false;
    return FetchOrLoadResource(name, kAnimation, checksum, 0, async, useArchive, m_permanentByDefault, nullptr, 0) != nullptr;
}

// 0x00407EC0
bool CResourceRegistry::RefreshResource(uint32_t checksum, const char* name, int unused)
{
    CResource* asset = ObtainLoadedItem(checksum, unused);
    if (!asset)
        return false;
    GiveCallback(this, asset);
    return reinterpret_cast<int(__thiscall*)(CResource*, const char*)>(asset->m_vtable[7])(asset, name) != 0;
}

// 0x00407B20
void CResourceRegistry::DiscardLoadedItem(CResource* asset)
{
    uint32_t checksum = asset->m_checksum;
    if (CResource* group = asset->m_bundle)
        group->RemoveDependent(asset);
    CResource** list;
    if (asset->VIsLoaded()) {
        list = &m_readyFlag;
    } else {
        asset->VAbort();
        list = &m_pending;
    }
    RemoveFromList(list, asset);
    asset->VUnload();
    asset->VDelete();
    m_assets->EraseEntry(checksum);
}

// 0x00407CF0
void CResourceRegistry::UnloadAssetsInList(CResource* head, bool all)
{
    if (!head)
        return;
    CResource* asset = head->m_lastValue;
    for (;;) {
        if (!asset)
            return;
        CResource* previous = asset->m_lastValue;
        if (all || !asset->m_pinned)
            DiscardLoadedItem(asset);
        if (asset == head)
            return;
        asset = previous;
    }
}

// 0x00407D40
void CResourceRegistry::UnloadAssetsOfType(CResource* head, bool all, int type)
{
    if (!head)
        return;
    CResource* found[100];
    int count = 0;
    CResource* asset = head->m_lastValue;
    for (;;) {
        if (!asset)
            break;
        CResource* previous = asset->m_lastValue;
        if ((all || !asset->m_pinned) && type == asset->VType())
            found[count++] = asset;
        if (asset == head)
            break;
        asset = previous;
    }
    for (int i = 0; i < count; ++i)
        DiscardLoadedItem(found[i]);
}

// 0x00407F20
void CResourceRegistry::UnloadAllAssets(bool all)
{
    UnloadAssetsInList(m_readyFlag, all);
    UnloadAssetsInList(m_pending, all);
}

// 0x00407F50
void CResourceRegistry::UnloadAllAssetsOfType(bool all, int type)
{
    UnloadAssetsOfType(m_readyFlag, all, type);
    UnloadAssetsOfType(m_pending, all, type);
}

// 0x00407B90
CResource* CResourceRegistry::ObtainLoadedItem(uint32_t checksum, int /*unused*/)
{
    CResource* asset = Find(m_assets, checksum);
    if (asset && !asset->VIsLoaded() && !asset->VIsLoaded()) { // (asked twice in the original)
        do
            FileLoad::AsyncWaitForIO(false);
        while (!asset->VIsLoaded());
    }
    return asset;
}

// 0x00407C00
void* CResourceRegistry::GetAssetData(uint32_t checksum, int /*unused*/)
{
    CResource* asset = Find(m_assets, checksum);
    if (!asset)
        return nullptr;
    WaitUntilLoaded(asset);
    return asset->VData();
}

// 0x00407C70
void* CResourceRegistry::GetAssetDataByName(const char* name, int unused)
{
    return GetAssetData(Checksums::HashFromText(name), unused);
}

// 0x00407CA0
bool CResourceRegistry::AssetExists(uint32_t checksum)
{
    return Find(m_assets, checksum) != nullptr;
}

// 0x00407A50
void CResourceRegistry::SetPermanentByDefault(bool permanent)
{
    m_permanentByDefault = permanent;
}

// 0x00407A60
bool CResourceRegistry::GetPermanentByDefault() const
{
    return m_permanentByDefault;
}

// 0x00407A70
void CResourceRegistry::SetUnknown18(int value)
{
    m_unknown18 = value;
}

// 0x00407DD0
void CResourceRegistry::ProcessPendingAssets()
{
    CResource* asset = m_pending;
    while (asset) {
        CResource* next = asset->m_succ;
        bool finished = false;
        switch (asset->m_curPhase) {
        case kRead:
            // (a group that isn't loaded yet holds it back)
            if (asset->m_bundle && !asset->m_bundle->VIsLoaded())
                break;
            [[fallthrough]];
        case kProcessing:
            if (!asset->VProcessBuffer())
                break;
            if (asset->m_callback)
                asset->m_callback(asset, asset->m_extent, asset->m_callbackData0, asset->m_callbackData1);
            finished = true;
            break;
        case kLoaded:
            finished = true;
            break;
        default:
            break;
        }
        if (finished) {
            RemoveFromList(&m_pending, asset);
            Append(&m_readyFlag, asset);
        }
        asset = next;
    }
}

// 0x00407EB0
void __cdecl CResourceRegistry::ProcessTask(TaskList::SchedulerTask* task)
{
    reinterpret_cast<CResourceRegistry**>(task)[7]->ProcessPendingAssets();
}

// 0x00407A80
bool CResourceRegistry::RemoveFromList(CResource** head, CResource* asset)
{
    if (asset->m_succ == asset) {
        // (the only one: the list emptied without checking it's this list)
        *head = nullptr;
        asset->m_lastValue = nullptr;
        asset->m_succ = nullptr;
        return true;
    }
    if (*head == asset)
        *head = asset->m_succ;
    for (CResource* item = *head; item; item = item->m_succ) {
        if (item == asset) {
            asset->m_succ->m_lastValue = asset->m_lastValue;
            asset->m_lastValue->m_succ = asset->m_succ;
            asset->m_lastValue = nullptr;
            asset->m_succ = nullptr;
            return true;
        }
    }
    return false;
}

// 0x004079C0
int CResourceRegistry::GetTypeFromExtension(const char* name)
{
    const char* dot = name + std::strlen(name);
    while (dot > name && *dot != '.')
        --dot;
    if (*dot != '.')
        return 0;
    for (const ExtensionType* entry = Extensions(); entry->extension; ++entry) {
        if (_stricmp(entry->extension, dot + 1) == 0)
            return entry->type;
    }
    return 0;
}

// 0x004088E0
CResourceRegistry* CreateAssetManager(bool create)
{
    CResourceRegistry* manager = AssetManager();
    if (!manager && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CResourceRegistry), nullptr, 1);
        if (memory) {
            manager = static_cast<CResourceRegistry*>(memory)->MakeNew();
            AssetManager() = manager;
            ++RefTally();
            return manager;
        }
        AssetManager() = nullptr;
        manager = nullptr;
    }
    ++RefTally();
    return manager;
}

// 0x00407980
void ReleaseAssetManager()
{
    if (static_cast<unsigned int>(RefTally()) > 1) {
        --RefTally();
        return;
    }
    if (CResourceRegistry* manager = AssetManager())
        reinterpret_cast<void(__thiscall*)(CResourceRegistry*, int)>(manager->m_vtable[0])(manager, 1);
    AssetManager() = nullptr;
    RefTally() = 0;
}

} // namespace Ass
