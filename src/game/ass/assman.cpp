// The asset manager.

#include "game/ass/assman.h"

#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/file/async.h"
#include "game/lst/hashtable.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/net/task_vtable.h"
#include "hook/game_code.h"

namespace Ass {

GAME_GLOBAL(Ass::CAssMan*, g_assetManager, 0x006918DC);
GAME_GLOBAL(int, g_assManReferences, 0x006918E0);
GAME_GLOBAL(Mlp::Manager*, g_mainLoopManager, 0x00696D68);

namespace {

GAME_VTABLE(kAssManVtable, 0x00645658, &Ass::CAssMan::ScalarDelete, &Ass::CAssMan::StartCallback,
    &Ass::CAssMan::StopCallback);

int& References() { return g_assManReferences; }

// The script integer the loads look up (a debug leftover: set, a failed asset isn't deleted).
int KeepFailedAssets() { return Script::GetInteger(0x25DC7904u); }

// The file name extensions and their types (0x0067C650, ended by a null name).
struct ExtensionType {
    const char* extension;
    int type;
};
// (the extensions are the exe's strings; the table's last entry stays null)
GAME_GLOBAL_UNCHECKED(ExtensionType[13], g_extensions, 0x0067C650,
                      {GAME_TEXT(0x006455F8, "BIN"), 1}, {GAME_TEXT(0x006455F4, "COL"), 4},
                      {GAME_TEXT(0x006455F0, "SCN"), 2}, {GAME_TEXT(0x006455E8, "GEOM"), 2},
                      {GAME_TEXT(0x006455E4, "MDL"), 7}, {GAME_TEXT(0x006455E0, "SKA"), 5},
                      {GAME_TEXT(0x006455DC, "FAM"), 5}, {GAME_TEXT(0x006455D8, "SKE"), 6},
                      {GAME_TEXT(0x006455D0, "SKIN"), 7}, {GAME_TEXT(0x006455CC, "TEX"), 3},
                      {GAME_TEXT(0x006455C8, "CUT"), 9}, {GAME_TEXT(0x006455C4, "QB"), 10});
const ExtensionType* Extensions() { return g_extensions; }

// The hash table lookup inlined in the original: the first item with the key and a value.
CAsset* Find(Lst::HashTable* table, uint32_t checksum)
{
    for (Lst::HashTable::Item* item = &table->m_buckets[((1u << table->m_bits) - 1) & checksum]; item;
         item = item->next) {
        if (item->key == checksum && item->value)
            return static_cast<CAsset*>(item->value);
    }
    return nullptr;
}
void WaitUntilLoaded(CAsset* asset)
{
    while (!asset->VIsLoaded())
        File::AsyncWaitForIO(false);
}
// `asset` onto the end of the circular list `*head`.
void Append(CAsset** head, CAsset* asset)
{
    if (*head) {
        asset->m_next = *head;
        asset->m_prev = (*head)->m_prev;
        (*head)->m_prev->m_next = asset;
        (*head)->m_prev = asset;
    } else {
        *head = asset;
        asset->m_next = asset;
        asset->m_prev = asset;
    }
}

// A new asset of `type` (null for a type that isn't one, or if the allocation fails).
CAsset* MakeAsset(int type, bool withCutscene)
{
    void* memory;
    switch (type) {
    case kScene:
        memory = Mem::Calloc(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->Scene() : nullptr;
    case kTextureDictionary:
        memory = Mem::Calloc(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->TextureDictionary() : nullptr;
    case kSceneCollision:
        memory = Mem::Calloc(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->SceneCollision() : nullptr;
    case kAnimation:
        memory = Mem::Calloc(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->Animation() : nullptr;
    case kSkeleton:
        memory = Mem::Calloc(0x4C);
        return memory ? static_cast<AssetConstructors*>(memory)->Skeleton() : nullptr;
    case kSkinnedModel:
        memory = Mem::Calloc(0x60);
        return memory ? static_cast<AssetConstructors*>(memory)->SkinnedModel() : nullptr;
    case kSkinCollision:
        memory = Mem::Calloc(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->SkinCollision() : nullptr;
    case kCutscene:
        // (LoadAssetFromStream has no case for cutscenes)
        if (!withCutscene)
            return nullptr;
        memory = Mem::Calloc(0x4C);
        return memory ? static_cast<AssetConstructors*>(memory)->Cutscene() : nullptr;
    case kNodeArray:
        memory = Mem::Calloc(0x64);
        return memory ? static_cast<AssetConstructors*>(memory)->NodeArray() : nullptr;
    case kOwnedNodeArray: {
        memory = Mem::Calloc(0x64);
        CAsset* asset = memory ? static_cast<AssetConstructors*>(memory)->NodeArray() : nullptr;
        static_cast<AssetConstructors*>(asset)->MakeNodeArrayOwner(); // (null if the allocation failed: a crash)
        return asset;
    }
    default:
        return nullptr;
    }
}

// The manager's callback given to a new asset (cleared after one asset if it's a one-shot).
void GiveCallback(CAssMan* manager, CAsset* asset)
{
    asset->SetCallback(manager->m_callback, manager->m_callbackData0, manager->m_callbackData1);
    if (manager->m_callbackOnce) {
        manager->m_callback = nullptr;
        manager->m_callbackOnce = false;
    }
}
// `asset` depends on the asset `group` (if given): each knows the other.
void JoinGroup(CAssMan* manager, CAsset* asset, uint32_t group)
{
    if (!group)
        return;
    CAsset* owner = manager->GetAsset(group, 1);
    asset->SetGroup(owner);
    owner->AddDependent(asset);
}

} // namespace

// 0x00407F80
CAssMan* CAssMan::Construct()
{
    Mdl::Module::Construct();
    m_vtable = kAssManVtable;
    void* memory = Mem::New(sizeof(Lst::HashTable));
    m_assets = memory ? new (memory) Lst::HashTable(10) : nullptr;
    m_callback = nullptr;
    m_callbackOnce = false;
    m_pending = nullptr;
    m_loaded = nullptr;
    m_unknown15 = false;
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (task) {
        task->Construct(0);
        reinterpret_cast<void**>(task)[7] = this;                                  // +0x1C the data
        task->m_vtable = GameCode::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&ProcessTask); // +0x18 the callback
    }
    m_task = task;
    return this;
}

// 0x00408930
void CAssMan::Destruct()
{
    m_vtable = kAssManVtable;
    UnloadAssetsInList(m_loaded, true);  // (inlined)
    UnloadAssetsInList(m_pending, true);
    if (Lst::HashTable* table = m_assets) {
        if (table->m_buckets) {
            table->FlushAllItems();
            Mem::Manager::Get()->Free(table->m_buckets);
            table->m_buckets = nullptr;
        }
        Mem::Manager::Get()->Free(table);
    }
    if (m_task)
        reinterpret_cast<void(__thiscall*)(Tsk::BaseTask*, int)>(m_task->m_vtable[2])(m_task, 1);
    Mdl::Module::DestroyBody();
}

// 0x00408A10
CAssMan* CAssMan::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00407E90
void CAssMan::StartCallback()
{
    reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(g_mainLoopManager) + 0x04)->AddTask(m_task); // the logic tasks
}

// 0x00407AE0
void CAssMan::StopCallback()
{
    m_task->Remove();
}

// 0x00408060
void* CAssMan::LoadAssetFromStream(uint32_t checksum, int type, uint32_t group, void* data, uint32_t size,
                                   bool permanent, bool copy, void* params, int extra)
{
    Mem::PushMemProfile("Asset Overhead");
    CAsset* asset = MakeAsset(type, false);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->m_permanent = permanent;
    asset->m_state = kUnloaded;
    asset->SetChecksum(checksum);
    GiveCallback(this, asset);
    // (the original formats "%08x from data stream" with the checksum for the memory profile, unused)
    JoinGroup(this, asset, group);
    Mem::PopMemProfile();
    int failed = reinterpret_cast<int(__thiscall*)(CAsset*, void*, uint32_t, bool, void*, int)>(asset->m_vtable[4])(
        asset, data, size, copy, params, extra);
    Mem::PushMemProfile("Asset Overhead");
    if (failed) {
        if (!KeepFailedAssets())
            asset->VDelete();
        Mem::PopMemProfile();
        return nullptr;
    }
    m_assets->PutItem(checksum, asset);
    Append(&m_loaded, asset);
    Mem::PopMemProfile();
    return asset->VData();
}

// 0x00408440
void* CAssMan::LoadAsset(const char* name, int type, uint32_t checksum, uint32_t group, bool async, bool useArchive,
                         bool permanent, void* params, int extra)
{
    if (!checksum)
        checksum = Crc::GenerateCRCFromString(name);
    if (!type)
        type = GetTypeFromExtension(name);
    CAsset* asset = MakeAsset(type, true);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->m_permanent = permanent;
    asset->m_state = kUnloaded;
    GiveCallback(this, asset);
    asset->SetChecksum(checksum);
    JoinGroup(this, asset, group);
    int failed = reinterpret_cast<int(__thiscall*)(CAsset*, const char*, bool, bool, void*, int)>(asset->m_vtable[5])(
        asset, name, async, useArchive, params, extra);
    if (failed) {
        if (!KeepFailedAssets())
            asset->VDelete();
        return nullptr;
    }
    m_assets->PutItem(checksum, asset);
    Append(async ? &m_pending : &m_loaded, asset);
    return asset->VData();
}

// 0x004087B0
void* CAssMan::LoadOrGetAsset(const char* name, int type, uint32_t checksum, uint32_t group, bool async,
                              bool useArchive, bool permanent, void* params, int extra)
{
    if (!checksum)
        checksum = Crc::GenerateCRCFromString(name);
    void* data;
    if (CAsset* asset = Find(m_assets, checksum)) {
        data = asset->VData();
        WaitUntilLoaded(asset);
    } else {
        data = LoadAsset(name, type, checksum, group, async, useArchive, permanent, params, extra);
    }
    KeepFailedAssets(); // (looked up and not used)
    return data;
}

// 0x00408870
bool CAssMan::LoadAnim(const char* name, uint32_t base, uint32_t offset, bool async, bool useArchive)
{
    uint32_t checksum = Crc::GenerateCRCFromString(name);
    if (base)
        checksum = base + offset;
    if (AssetExists(checksum))
        return false;
    return LoadOrGetAsset(name, kAnimation, checksum, 0, async, useArchive, m_permanentByDefault, nullptr, 0) != nullptr;
}

// 0x00407EC0
bool CAssMan::ReloadAsset(uint32_t checksum, const char* name, int unused)
{
    CAsset* asset = GetAsset(checksum, unused);
    if (!asset)
        return false;
    GiveCallback(this, asset);
    return reinterpret_cast<int(__thiscall*)(CAsset*, const char*)>(asset->m_vtable[7])(asset, name) != 0;
}

// 0x00407B20
void CAssMan::UnloadAsset(CAsset* asset)
{
    uint32_t checksum = asset->m_checksum;
    if (CAsset* group = asset->m_group)
        group->RemoveDependent(asset);
    CAsset** list;
    if (asset->VIsLoaded()) {
        list = &m_loaded;
    } else {
        asset->VAbort();
        list = &m_pending;
    }
    RemoveFromList(list, asset);
    asset->VUnload();
    asset->VDelete();
    m_assets->FlushItem(checksum);
}

// 0x00407CF0
void CAssMan::UnloadAssetsInList(CAsset* head, bool all)
{
    if (!head)
        return;
    CAsset* asset = head->m_prev;
    for (;;) {
        if (!asset)
            return;
        CAsset* previous = asset->m_prev;
        if (all || !asset->m_permanent)
            UnloadAsset(asset);
        if (asset == head)
            return;
        asset = previous;
    }
}

// 0x00407D40
void CAssMan::UnloadAssetsOfType(CAsset* head, bool all, int type)
{
    if (!head)
        return;
    CAsset* found[100];
    int count = 0;
    CAsset* asset = head->m_prev;
    for (;;) {
        if (!asset)
            break;
        CAsset* previous = asset->m_prev;
        if ((all || !asset->m_permanent) && type == asset->VType())
            found[count++] = asset;
        if (asset == head)
            break;
        asset = previous;
    }
    for (int i = 0; i < count; ++i)
        UnloadAsset(found[i]);
}

// 0x00407F20
void CAssMan::UnloadAllAssets(bool all)
{
    UnloadAssetsInList(m_loaded, all);
    UnloadAssetsInList(m_pending, all);
}

// 0x00407F50
void CAssMan::UnloadAllAssetsOfType(bool all, int type)
{
    UnloadAssetsOfType(m_loaded, all, type);
    UnloadAssetsOfType(m_pending, all, type);
}

// 0x00407B90
CAsset* CAssMan::GetAsset(uint32_t checksum, int /*unused*/)
{
    CAsset* asset = Find(m_assets, checksum);
    if (asset && !asset->VIsLoaded() && !asset->VIsLoaded()) { // (asked twice in the original)
        do
            File::AsyncWaitForIO(false);
        while (!asset->VIsLoaded());
    }
    return asset;
}

// 0x00407C00
void* CAssMan::GetAssetData(uint32_t checksum, int /*unused*/)
{
    CAsset* asset = Find(m_assets, checksum);
    if (!asset)
        return nullptr;
    WaitUntilLoaded(asset);
    return asset->VData();
}

// 0x00407C70
void* CAssMan::GetAssetDataByName(const char* name, int unused)
{
    return GetAssetData(Crc::GenerateCRCFromString(name), unused);
}

// 0x00407CA0
bool CAssMan::AssetExists(uint32_t checksum)
{
    return Find(m_assets, checksum) != nullptr;
}

// 0x00407A50
void CAssMan::SetPermanentByDefault(bool permanent)
{
    m_permanentByDefault = permanent;
}

// 0x00407A60
bool CAssMan::GetPermanentByDefault() const
{
    return m_permanentByDefault;
}

// 0x00407A70
void CAssMan::SetUnknown18(int value)
{
    m_unknown18 = value;
}

// 0x00407DD0
void CAssMan::ProcessPendingAssets()
{
    CAsset* asset = m_pending;
    while (asset) {
        CAsset* next = asset->m_next;
        bool finished = false;
        switch (asset->m_state) {
        case kRead:
            // (a group that isn't loaded yet holds it back)
            if (asset->m_group && !asset->m_group->VIsLoaded())
                break;
            [[fallthrough]];
        case kProcessing:
            if (!asset->VProcessBuffer())
                break;
            if (asset->m_callback)
                asset->m_callback(asset, asset->m_size, asset->m_callbackData0, asset->m_callbackData1);
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
            Append(&m_loaded, asset);
        }
        asset = next;
    }
}

// 0x00407EB0
void __cdecl CAssMan::ProcessTask(Tsk::BaseTask* task)
{
    reinterpret_cast<CAssMan**>(task)[7]->ProcessPendingAssets();
}

// 0x00407A80
bool CAssMan::RemoveFromList(CAsset** head, CAsset* asset)
{
    if (asset->m_next == asset) {
        // (the only one: the list emptied without checking it's this list)
        *head = nullptr;
        asset->m_prev = nullptr;
        asset->m_next = nullptr;
        return true;
    }
    if (*head == asset)
        *head = asset->m_next;
    for (CAsset* item = *head; item; item = item->m_next) {
        if (item == asset) {
            asset->m_next->m_prev = asset->m_prev;
            asset->m_prev->m_next = asset->m_next;
            asset->m_prev = nullptr;
            asset->m_next = nullptr;
            return true;
        }
    }
    return false;
}

// 0x004079C0
int CAssMan::GetTypeFromExtension(const char* name)
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
CAssMan* CreateAssetManager(bool create)
{
    CAssMan* manager = AssetManager();
    if (!manager && create) {
        void* memory = Mem::Calloc(sizeof(CAssMan), nullptr, 1);
        if (memory) {
            manager = static_cast<CAssMan*>(memory)->Construct();
            AssetManager() = manager;
            ++References();
            return manager;
        }
        AssetManager() = nullptr;
        manager = nullptr;
    }
    ++References();
    return manager;
}

// 0x00407980
void ReleaseAssetManager()
{
    if (static_cast<unsigned int>(References()) > 1) {
        --References();
        return;
    }
    if (CAssMan* manager = AssetManager())
        reinterpret_cast<void(__thiscall*)(CAssMan*, int)>(manager->m_vtable[0])(manager, 1);
    AssetManager() = nullptr;
    References() = 0;
}

} // namespace Ass

REPLACE(0x00407F80, &Ass::CAssMan::Construct);
REPLACE(0x00408930, &Ass::CAssMan::Destruct);
REPLACE(0x00408A10, &Ass::CAssMan::ScalarDelete);
REPLACE(0x00407E90, &Ass::CAssMan::StartCallback);
REPLACE(0x00407AE0, &Ass::CAssMan::StopCallback);
REPLACE(0x00408060, &Ass::CAssMan::LoadAssetFromStream);
REPLACE(0x00408440, &Ass::CAssMan::LoadAsset);
REPLACE(0x004087B0, &Ass::CAssMan::LoadOrGetAsset);
REPLACE(0x00408870, &Ass::CAssMan::LoadAnim);
REPLACE(0x00407EC0, &Ass::CAssMan::ReloadAsset);
REPLACE(0x00407B20, &Ass::CAssMan::UnloadAsset);
REPLACE(0x00407CF0, &Ass::CAssMan::UnloadAssetsInList);
REPLACE(0x00407D40, &Ass::CAssMan::UnloadAssetsOfType);
REPLACE(0x00407F20, &Ass::CAssMan::UnloadAllAssets);
REPLACE(0x00407F50, &Ass::CAssMan::UnloadAllAssetsOfType);
REPLACE(0x00407B90, &Ass::CAssMan::GetAsset);
REPLACE(0x00407C00, &Ass::CAssMan::GetAssetData);
REPLACE(0x00407C70, &Ass::CAssMan::GetAssetDataByName);
REPLACE(0x00407CA0, &Ass::CAssMan::AssetExists);
REPLACE(0x00407A50, &Ass::CAssMan::SetPermanentByDefault);
REPLACE_SOURCE_ONLY(0x00407A60, &Ass::CAssMan::GetPermanentByDefault); // 4 bytes
REPLACE(0x00407A70, &Ass::CAssMan::SetUnknown18);
REPLACE(0x00407DD0, &Ass::CAssMan::ProcessPendingAssets);
REPLACE(0x00407EB0, Ass::CAssMan::ProcessTask);
REPLACE(0x00407A80, Ass::CAssMan::RemoveFromList);
REPLACE(0x004079C0, &Ass::CAssMan::GetTypeFromExtension);
REPLACE(0x004088E0, Ass::CreateAssetManager);
REPLACE(0x00407980, Ass::ReleaseAssetManager);
