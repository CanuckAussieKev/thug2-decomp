#pragma once

#include <cstddef>
#include <cstdint>

#include "game/ass/asset.h"
#include "game/sys/module.h"

namespace Lst {
class HashTable;
}
namespace Tsk {
class BaseTask;
}

namespace Ass {

// The asset manager (0x3C bytes, vtable 0x00645658; a module): every asset by checksum in a hash table, and in one
// of two circular lists - loaded, or still being read (an async load). The singleton is 0x006918DC (references
// 0x006918E0).
class CAssMan : public Mdl::Module {
public:
    // 0x00407F80: the hash table (10 bits) and the task running ProcessPendingAssets (returns this).
    CAssMan* Construct();
    // 0x00408930: every asset unloaded, the hash table and the task deleted.
    void Destruct();
    // 0x00408A10 (scalar deleting)
    CAssMan* ScalarDelete(uint8_t flags);
    // 0x00407E90 / 0x00407AE0: the module's start and stop: the task into / out of the main loop's logic tasks.
    void StartCallback();
    void StopCallback();

    // 0x00408060: an asset of `type` from data in memory (LoadFromBuffer with `data`, `size`, `copy`, `params`,
    // `extra`); `group` is the checksum of an asset it depends on. Returns the asset's data, or null if it failed
    // (the asset deleted - unless the script integer 0x25DC7904 is set, a debug leftover).
    void* LoadAssetFromStream(uint32_t checksum, int type, uint32_t group, void* data, uint32_t size, bool permanent,
                              bool copy, void* params, int extra);
    // 0x00408440: an asset loaded from a file (Load with `name`, `async`, `useArchive`, `params`, `extra`). A zero
    // checksum is the name's, a zero type the extension's. An async one goes on the pending list.
    void* LoadAsset(const char* name, int type, uint32_t checksum, uint32_t group, bool async, bool useArchive,
                    bool permanent, void* params, int extra);
    // 0x004087B0: the asset's data if it's there (waiting for it to load), else LoadAsset.
    void* LoadOrGetAsset(const char* name, int type, uint32_t checksum, uint32_t group, bool async, bool useArchive,
                         bool permanent, void* params, int extra);
    // 0x00408870: an animation by name (its checksum `base` + `offset` if `base` isn't zero); false if one with
    // that checksum is there already or loading failed.
    bool LoadAnim(const char* name, uint32_t base, uint32_t offset, bool async, bool useArchive);
    // 0x00407EC0: Reload with `name` (false if there's no such asset).
    bool ReloadAsset(uint32_t checksum, const char* name, int unused);

    // 0x00407B20: out of its group's dependents and its list (an unfinished one aborted), unloaded, deleted, out
    // of the hash table.
    void UnloadAsset(CAsset* asset);
    // 0x00407F20: every asset (or only those not permanent) unloaded, both lists from the tail.
    void UnloadAllAssets(bool all);
    // 0x00407F50: the same for one type.
    void UnloadAllAssetsOfType(bool all, int type);
    // 0x00407CF0 / 0x00407D40: one list's (from its tail round to `head`). The second gathers first (at most 100:
    // more would overrun the original's array).
    void UnloadAssetsInList(CAsset* head, bool all);
    void UnloadAssetsOfType(CAsset* head, bool all, int type);

    // 0x00407B90: the asset, waiting for it to finish loading (null if there's none).
    CAsset* GetAsset(uint32_t checksum, int unused);
    // 0x00407C00: its data, waiting the same way. 0x00407C70: by name.
    void* GetAssetData(uint32_t checksum, int unused);
    void* GetAssetDataByName(const char* name, int unused);
    // 0x00407CA0
    bool AssetExists(uint32_t checksum);

    // 0x00407A50 / 0x00407A60 / 0x00407A70
    void SetPermanentByDefault(bool permanent);
    bool GetPermanentByDefault() const;
    void SetUnknown18(int value);

    // 0x00407DD0 (the task's work, each frame): each pending asset that's read (and whose group is loaded) or being
    // processed is processed; finished, its callback runs and it moves to the loaded list. (The list is circular and
    // the walk only ends at a null link: with an asset pending it wouldn't end - async loading looks unused.)
    void ProcessPendingAssets();
    // 0x00407EB0: the task's callback (the manager its data).
    static void __cdecl ProcessTask(Tsk::BaseTask* task);

    // 0x00407A80: `asset` out of the circular list `*head` (false if it isn't in it).
    static bool RemoveFromList(CAsset** head, CAsset* asset);
    // 0x004079C0: the type of a file name's extension (0 if none matches; thiscall, the manager unused).
    int GetTypeFromExtension(const char* name);

    bool m_permanentByDefault;    // +0x14 what LoadAnim loads with
    bool m_unknown15;             // +0x15 cleared by the constructor
    uint8_t pad16[2];
    int m_unknown18;              // +0x18 set by 0x00407A70 (not read here)
    Lst::HashTable* m_assets;     // +0x1C
    CAsset* m_loaded;             // +0x20 the head of the loaded list
    CAsset* m_pending;            // +0x24 the head of the pending list
    LoadCallback m_callback;      // +0x28 given to the next assets loaded
    void* m_callbackData0;        // +0x2C
    void* m_callbackData1;        // +0x30
    bool m_callbackOnce;          // +0x34 the callback cleared after one asset
    uint8_t pad35[3];
    Tsk::BaseTask* m_task;        // +0x38 a Tsk::Task<CAssMan> running ProcessTask
};
static_assert(sizeof(CAssMan) == 0x3C && offsetof(CAssMan, m_assets) == 0x1C && offsetof(CAssMan, m_task) == 0x38,
              "layout must match the original");

// 0x004088E0: the first call with `create` makes the manager; every call takes a reference. Returns the manager.
CAssMan* CreateAssetManager(bool create);
// 0x00407980: drops a reference; the last deletes it.
void ReleaseAssetManager();
inline CAssMan*& AssetManager() { return *reinterpret_cast<CAssMan**>(0x006918DC); }

} // namespace Ass
