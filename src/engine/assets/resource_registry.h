#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>

#include "engine/assets/resource_entry.h"
#include "engine/sys/game_module.h"
#include "engine/core/owner.h"

namespace frame_loop {
struct Hub;
}

namespace lists {
class KeyMap;
}
namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks

namespace assets {

// The asset manager (0x3C bytes, vtable 0x00645658; a module): every asset by checksum in a hash table, and in one
// of two circular lists - loaded, or still being read (an async load). The singleton is 0x006918DC (references
// 0x006918E0).
class ResourceRegistry : public modules::PartUnit {
public:
    ResourceRegistry(const ResourceRegistry&) = delete;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;
    // 0x00407F80: the hash table (10 bits) and the task running process_pending_assets.
    ResourceRegistry();
    // 0x00408930 / 0x00408A10: every asset unloaded, the hash table and the task deleted.
    ~ResourceRegistry() override;
    // 0x00407E90 / 0x00407AE0: the module's start and stop: the task into / out of the main loop's logic tasks.
    void start_callback() override;
    void stop_callback() override;

    // 0x00408060: an asset of `type` from data in memory (load_from_buffer with `data`, `size`, `copy`, `params`,
    // `extra`); `group` is the checksum of an asset it depends on. Returns the asset's data, or null if it failed
    // (the asset deleted - unless the script integer 0x25DC7904 is set, a debug leftover).
    void* read_resource_from_stream(uint32_t checksum, int type, uint32_t group, void* data, uint32_t size,
                                    bool permanent, bool copy, void* params, int extra);
    // 0x00408440: an asset loaded from a file (Load with `name`, `async`, `use_archive`, `params`, `extra`). A zero
    // checksum is the name's, a zero type the extension's. An async one goes on the pending list.
    void* load_asset(const char* name, int type, uint32_t checksum, uint32_t group, bool async, bool use_archive,
                     bool permanent, void* params, int extra);
    // 0x004087B0: the asset's data if it's there (waiting for it to load), else LoadAsset.
    void* fetch_or_load_resource(const char* name, int type, uint32_t checksum, uint32_t group, bool async,
                                 bool use_archive, bool permanent, void* params, int extra);
    // 0x00408870: an animation by name (its checksum `base` + `offset` if `base` isn't zero); false if one with
    // that checksum is there already or loading failed.
    bool load_anim(const char* name, uint32_t base, uint32_t offset, bool async, bool use_archive);
    // 0x00407EC0: Reload with `name` (false if there's no such asset).
    bool refresh_resource(uint32_t checksum, const char* name, int unused);

    // 0x00407B20: out of its group's dependents and its list (an unfinished one aborted), unloaded, deleted, out
    // of the hash table.
    void discard_loaded_item(Resource* asset);
    // 0x00407F20: every asset (or only those not permanent) unloaded, both lists from the tail.
    void unload_all_assets(bool all);
    // 0x00407F50: the same for one type.
    void unload_all_assets_of_type(bool all, int type);
    // 0x00407CF0 / 0x00407D40: one list's (from its tail round to `head`). The second gathers first (at most 100:
    // more would overrun the original's array).
    void unload_assets_in_list(Resource* head, bool all);
    void unload_assets_of_type(Resource* head, bool all, int type);

    // 0x00407B90: the asset, waiting for it to finish loading (null if there's none).
    Resource* obtain_loaded_item(uint32_t checksum, int unused);
    // 0x00407C00: its data, waiting the same way. 0x00407C70: by name.
    void* get_asset_data(uint32_t checksum, int unused);
    void* get_asset_data_by_name(const char* name, int unused);
    // 0x00407CA0
    bool asset_exists(uint32_t checksum);

    // 0x00407A50 / 0x00407A60 / 0x00407A70
    void set_permanent_by_default(bool permanent);
    bool get_permanent_by_default() const;
    void set_unknown18(int value);

    // 0x00407DD0 (the task's work, each frame): each pending asset that's read (and whose group is loaded) or being
    // processed is processed; finished, its callback runs and it moves to the loaded list. (The list is circular and
    // the walk only ends at a null link: with an asset pending it wouldn't end - async loading looks unused.)
    void process_pending_assets();
    // 0x00407EB0: the task's callback (the manager its data).
    static void process_task(tasks::Task<ResourceRegistry>* task);

    // 0x00407A80: `asset` out of the circular list `*head` (false if it isn't in it).
    static bool remove_from_list(Resource** head, Resource* asset);
    // 0x004079C0: the type of a file name's extension (0 if none matches; thiscall, the manager unused).
    int get_type_from_extension(const char* name);

    bool permanent_by_default_; // +0x14 what LoadAnim loads with
    bool unknown15_;            // +0x15 cleared by the constructor
    uint8_t pad16[2];
    int unknown18_;         // +0x18 set by 0x00407A70 (not read here)
    lists::KeyMap* assets_; // +0x1C
    Resource* ready_flag_;  // +0x20 the head of the loaded list
    Resource* pending_;     // +0x24 the head of the pending list
    LoadCallback callback_; // +0x28 given to the next assets loaded
    void* callback_data0_;  // +0x2C
    void* callback_data1_;  // +0x30
    bool callback_once_;    // +0x34 the callback cleared after one asset
    uint8_t pad35[3];
    std::unique_ptr<tasks::SchedulerTask> task_; // +0x38 a tasks::Task<ResourceRegistry> running process_task
};
static_assert(sizeof(ResourceRegistry) == 0x3C && offsetof(ResourceRegistry, assets_) == 0x1C &&
                  offsetof(ResourceRegistry, task_) == 0x38,
              "layout must match the original");

// 0x004088E0: the first call with `create` makes the manager; every call takes a reference. Returns the manager.
ResourceRegistry* create_asset_manager(bool create);
// 0x00407980: drops a reference; the last deletes it.
void release_asset_manager();
extern ResourceRegistry* g_asset_manager;
inline ResourceRegistry*& asset_manager() { return g_asset_manager; }

extern memory::owner<frame_loop::Hub*> g_main_loop_manager;

} // namespace assets
