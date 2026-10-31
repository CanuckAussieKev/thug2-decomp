// The asset manager.

#include "engine/assets/resource_registry.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/file/async.h"
#include "engine/lists/key_map.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/net/task_vtable.h"

namespace assets {

assets::ResourceRegistry* g_asset_manager;
int g_ass_man_references;
frame_loop::Hub* g_main_loop_manager;

namespace {

static VtableSlot g_ass_man_vtable_slots[] = {&assets::ResourceRegistry::scalar_delete,
                                              &assets::ResourceRegistry::start_callback,
                                              &assets::ResourceRegistry::stop_callback};
void** const kAssManVtable = reinterpret_cast<void**>(g_ass_man_vtable_slots);

int& ref_tally() { return g_ass_man_references; }

// The script integer the loads look up (a debug leftover: set, a failed asset isn't deleted).
int keep_failed_assets() { return script::fetch_int_value(0x25DC7904u); }

// The file name extensions and their types (0x0067C650, ended by a null name).
struct ExtensionType {
    const char* extension;
    int type;
};
// (the extensions are the exe's strings; the table's last entry stays null)
ExtensionType g_extensions[13]{{"BIN", 1}, {"COL", 4}, {"SCN", 2},  {"GEOM", 2}, {"MDL", 7}, {"SKA", 5},
                               {"FAM", 5}, {"SKE", 6}, {"SKIN", 7}, {"TEX", 3},  {"CUT", 9}, {"QB", 10}};
const ExtensionType* get_extensions() { return g_extensions; }

// The hash table lookup inlined in the original: the first item with the key and a value.
Resource* get_find(lists::KeyMap* table, uint32_t checksum)
{
    for (lists::KeyMap::Item* item = &table->buckets_[((1u << table->bits_) - 1) & checksum]; item; item = item->next) {
        if (item->key == checksum && item->value)
            return static_cast<Resource*>(item->value);
    }
    return nullptr;
}
void wait_until_loaded(Resource* asset)
{
    while (!asset->v_is_loaded())
        file::async_wait_for_io(false);
}
// `asset` onto the end of the circular list `*head`.
void get_append(Resource** head, Resource* asset)
{
    if (*head) {
        asset->succ_ = *head;
        asset->last_value_ = (*head)->last_value_;
        (*head)->last_value_->succ_ = asset;
        (*head)->last_value_ = asset;
    } else {
        *head = asset;
        asset->succ_ = asset;
        asset->last_value_ = asset;
    }
}

// A new asset of `type` (null for a type that isn't one, or if the allocation fails).
Resource* make_asset(int type, bool with_cutscene)
{
    void* memory;
    switch (type) {
    case kScene:
        memory = memory::alloc_zeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->get_scene() : nullptr;
    case kResTexDict:
        memory = memory::alloc_zeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->texture_bank() : nullptr;
    case kSceneCollision:
        memory = memory::alloc_zeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->scene_collision() : nullptr;
    case kAnimation:
        memory = memory::alloc_zeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->get_animation() : nullptr;
    case kSkeleton:
        memory = memory::alloc_zeroed(0x4C);
        return memory ? static_cast<AssetConstructors*>(memory)->get_skeleton() : nullptr;
    case kSkinnedModel:
        memory = memory::alloc_zeroed(0x60);
        return memory ? static_cast<AssetConstructors*>(memory)->skinned_model() : nullptr;
    case kSkinCollision:
        memory = memory::alloc_zeroed(0x50);
        return memory ? static_cast<AssetConstructors*>(memory)->skin_collision() : nullptr;
    case kCutscene:
        // (read_resource_from_stream has no case for cutscenes)
        if (!with_cutscene)
            return nullptr;
        memory = memory::alloc_zeroed(0x4C);
        return memory ? static_cast<AssetConstructors*>(memory)->get_cutscene() : nullptr;
    case kNodeArray:
        memory = memory::alloc_zeroed(0x64);
        return memory ? static_cast<AssetConstructors*>(memory)->get_node_array() : nullptr;
    case kOwnedNodeArray: {
        memory = memory::alloc_zeroed(0x64);
        Resource* asset = memory ? static_cast<AssetConstructors*>(memory)->get_node_array() : nullptr;
        static_cast<AssetConstructors*>(asset)->make_node_array_owner(); // (null if the allocation failed: a crash)
        return asset;
    }
    default:
        return nullptr;
    }
}

// The manager's callback given to a new asset (cleared after one asset if it's a one-shot).
void give_callback(ResourceRegistry* manager, Resource* asset)
{
    asset->change_hook(manager->callback_, manager->callback_data0_, manager->callback_data1_);
    if (manager->callback_once_) {
        manager->callback_ = nullptr;
        manager->callback_once_ = false;
    }
}
// `asset` depends on the asset `group` (if given): each knows the other.
void join_group(ResourceRegistry* manager, Resource* asset, uint32_t group)
{
    if (!group)
        return;
    Resource* owner = manager->obtain_loaded_item(group, 1);
    asset->change_cluster(owner);
    owner->add_dependent(asset);
}

} // namespace

// 0x00407F80
ResourceRegistry* ResourceRegistry::make_new()
{
    modules::PartUnit::make_new();
    vtable_ = kAssManVtable;
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    assets_ = memory ? new (memory) lists::KeyMap(10) : nullptr;
    callback_ = nullptr;
    callback_once_ = false;
    pending_ = nullptr;
    ready_flag_ = nullptr;
    unknown15_ = false;
    auto* task = static_cast<tasks::SchedulerTask*>(memory::alloc_zeroed(0x20));
    if (task) {
        task->make_new(0);
        reinterpret_cast<void**>(task)[7] = this; // +0x1C the data
        task->vtable_ = game_code::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&process_task); // +0x18 the callback
    }
    task_ = task;
    return this;
}

// 0x00408930
void ResourceRegistry::destruct()
{
    vtable_ = kAssManVtable;
    unload_assets_in_list(ready_flag_, true); // (inlined)
    unload_assets_in_list(pending_, true);
    if (lists::KeyMap* table = assets_) {
        if (table->buckets_) {
            table->empty_table();
            memory::Hub::get_pull()->dealloc(table->buckets_);
            table->buckets_ = nullptr;
        }
        memory::Hub::get_pull()->dealloc(table);
    }
    if (task_)
        reinterpret_cast<void(__thiscall*)(tasks::SchedulerTask*, int)>(task_->vtable_[2])(task_, 1);
    modules::PartUnit::destroy_body();
}

// 0x00408A10
ResourceRegistry* ResourceRegistry::scalar_delete(uint8_t flags)
{
    destruct();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00407E90
void ResourceRegistry::start_callback()
{
    reinterpret_cast<tasks::PileUp*>(reinterpret_cast<uint8_t*>(g_main_loop_manager) + 0x04)
        ->queue_job(task_); // the logic tasks
}

// 0x00407AE0
void ResourceRegistry::stop_callback() { task_->drop_out(); }

// 0x00408060
void* ResourceRegistry::read_resource_from_stream(uint32_t checksum, int type, uint32_t group, void* data,
                                                  uint32_t size, bool permanent, bool copy, void* params, int extra)
{
    memory::push_mem_profile("Asset Overhead");
    Resource* asset = make_asset(type, false);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->pinned_ = permanent;
    asset->cur_phase_ = kUnloaded;
    asset->set_checksum(checksum);
    give_callback(this, asset);
    // (the original formats "%08x from data stream" with the checksum for the memory profile, unused)
    join_group(this, asset, group);
    memory::pop_mem_profile();
    int failed = reinterpret_cast<int(__thiscall*)(Resource*, void*, uint32_t, bool, void*, int)>(asset->vtable_[4])(
        asset, data, size, copy, params, extra);
    memory::push_mem_profile("Asset Overhead");
    if (failed) {
        if (!keep_failed_assets())
            asset->v_delete();
        memory::pop_mem_profile();
        return nullptr;
    }
    assets_->store_entry(checksum, asset);
    get_append(&ready_flag_, asset);
    memory::pop_mem_profile();
    return asset->v_data();
}

// 0x00408440
void* ResourceRegistry::load_asset(const char* name, int type, uint32_t checksum, uint32_t group, bool async,
                                   bool use_archive, bool permanent, void* params, int extra)
{
    if (!checksum)
        checksum = checksums::hash_from_text(name);
    if (!type)
        type = get_type_from_extension(name);
    Resource* asset = make_asset(type, true);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->pinned_ = permanent;
    asset->cur_phase_ = kUnloaded;
    give_callback(this, asset);
    asset->set_checksum(checksum);
    join_group(this, asset, group);
    int failed = reinterpret_cast<int(__thiscall*)(Resource*, const char*, bool, bool, void*, int)>(asset->vtable_[5])(
        asset, name, async, use_archive, params, extra);
    if (failed) {
        if (!keep_failed_assets())
            asset->v_delete();
        return nullptr;
    }
    assets_->store_entry(checksum, asset);
    get_append(async ? &pending_ : &ready_flag_, asset);
    return asset->v_data();
}

// 0x004087B0
void* ResourceRegistry::fetch_or_load_resource(const char* name, int type, uint32_t checksum, uint32_t group,
                                               bool async, bool use_archive, bool permanent, void* params, int extra)
{
    if (!checksum)
        checksum = checksums::hash_from_text(name);
    void* data;
    if (Resource* asset = get_find(assets_, checksum)) {
        data = asset->v_data();
        wait_until_loaded(asset);
    } else {
        data = load_asset(name, type, checksum, group, async, use_archive, permanent, params, extra);
    }
    keep_failed_assets(); // (looked up and not used)
    return data;
}

// 0x00408870
bool ResourceRegistry::load_anim(const char* name, uint32_t base, uint32_t offset, bool async, bool use_archive)
{
    uint32_t checksum = checksums::hash_from_text(name);
    if (base)
        checksum = base + offset;
    if (asset_exists(checksum))
        return false;
    return fetch_or_load_resource(name, kAnimation, checksum, 0, async, use_archive, permanent_by_default_, nullptr,
                                  0) != nullptr;
}

// 0x00407EC0
bool ResourceRegistry::refresh_resource(uint32_t checksum, const char* name, int unused)
{
    Resource* asset = obtain_loaded_item(checksum, unused);
    if (!asset)
        return false;
    give_callback(this, asset);
    return reinterpret_cast<int(__thiscall*)(Resource*, const char*)>(asset->vtable_[7])(asset, name) != 0;
}

// 0x00407B20
void ResourceRegistry::discard_loaded_item(Resource* asset)
{
    uint32_t checksum = asset->checksum_;
    if (Resource* group = asset->bundle_)
        group->remove_dependent(asset);
    Resource** list;
    if (asset->v_is_loaded()) {
        list = &ready_flag_;
    } else {
        asset->v_abort();
        list = &pending_;
    }
    remove_from_list(list, asset);
    asset->v_unload();
    asset->v_delete();
    assets_->erase_entry(checksum);
}

// 0x00407CF0
void ResourceRegistry::unload_assets_in_list(Resource* head, bool all)
{
    if (!head)
        return;
    Resource* asset = head->last_value_;
    for (;;) {
        if (!asset)
            return;
        Resource* previous = asset->last_value_;
        if (all || !asset->pinned_)
            discard_loaded_item(asset);
        if (asset == head)
            return;
        asset = previous;
    }
}

// 0x00407D40
void ResourceRegistry::unload_assets_of_type(Resource* head, bool all, int type)
{
    if (!head)
        return;
    Resource* found[100];
    int count = 0;
    Resource* asset = head->last_value_;
    for (;;) {
        if (!asset)
            break;
        Resource* previous = asset->last_value_;
        if ((all || !asset->pinned_) && type == asset->v_type())
            found[count++] = asset;
        if (asset == head)
            break;
        asset = previous;
    }
    for (int i = 0; i < count; ++i)
        discard_loaded_item(found[i]);
}

// 0x00407F20
void ResourceRegistry::unload_all_assets(bool all)
{
    unload_assets_in_list(ready_flag_, all);
    unload_assets_in_list(pending_, all);
}

// 0x00407F50
void ResourceRegistry::unload_all_assets_of_type(bool all, int type)
{
    unload_assets_of_type(ready_flag_, all, type);
    unload_assets_of_type(pending_, all, type);
}

// 0x00407B90
Resource* ResourceRegistry::obtain_loaded_item(uint32_t checksum, int /*unused*/)
{
    Resource* asset = get_find(assets_, checksum);
    if (asset && !asset->v_is_loaded() && !asset->v_is_loaded()) { // (asked twice in the original)
        do
            file::async_wait_for_io(false);
        while (!asset->v_is_loaded());
    }
    return asset;
}

// 0x00407C00
void* ResourceRegistry::get_asset_data(uint32_t checksum, int /*unused*/)
{
    Resource* asset = get_find(assets_, checksum);
    if (!asset)
        return nullptr;
    wait_until_loaded(asset);
    return asset->v_data();
}

// 0x00407C70
void* ResourceRegistry::get_asset_data_by_name(const char* name, int unused)
{
    return get_asset_data(checksums::hash_from_text(name), unused);
}

// 0x00407CA0
bool ResourceRegistry::asset_exists(uint32_t checksum) { return get_find(assets_, checksum) != nullptr; }

// 0x00407A50
void ResourceRegistry::set_permanent_by_default(bool permanent) { permanent_by_default_ = permanent; }

// 0x00407A60
bool ResourceRegistry::get_permanent_by_default() const { return permanent_by_default_; }

// 0x00407A70
void ResourceRegistry::set_unknown18(int value) { unknown18_ = value; }

// 0x00407DD0
void ResourceRegistry::process_pending_assets()
{
    Resource* asset = pending_;
    while (asset) {
        Resource* next = asset->succ_;
        bool finished = false;
        switch (asset->cur_phase_) {
        case kRead:
            // (a group that isn't loaded yet holds it back)
            if (asset->bundle_ && !asset->bundle_->v_is_loaded())
                break;
            [[fallthrough]];
        case kProcessing:
            if (!asset->v_process_buffer())
                break;
            if (asset->callback_)
                asset->callback_(asset, asset->extent_, asset->callback_data0_, asset->callback_data1_);
            finished = true;
            break;
        case kLoaded:
            finished = true;
            break;
        default:
            break;
        }
        if (finished) {
            remove_from_list(&pending_, asset);
            get_append(&ready_flag_, asset);
        }
        asset = next;
    }
}

// 0x00407EB0
void __cdecl ResourceRegistry::process_task(tasks::SchedulerTask* task)
{
    reinterpret_cast<ResourceRegistry**>(task)[7]->process_pending_assets();
}

// 0x00407A80
bool ResourceRegistry::remove_from_list(Resource** head, Resource* asset)
{
    if (asset->succ_ == asset) {
        // (the only one: the list emptied without checking it's this list)
        *head = nullptr;
        asset->last_value_ = nullptr;
        asset->succ_ = nullptr;
        return true;
    }
    if (*head == asset)
        *head = asset->succ_;
    for (Resource* item = *head; item; item = item->succ_) {
        if (item == asset) {
            asset->succ_->last_value_ = asset->last_value_;
            asset->last_value_->succ_ = asset->succ_;
            asset->last_value_ = nullptr;
            asset->succ_ = nullptr;
            return true;
        }
    }
    return false;
}

// 0x004079C0
int ResourceRegistry::get_type_from_extension(const char* name)
{
    const char* dot = name + std::strlen(name);
    while (dot > name && *dot != '.')
        --dot;
    if (*dot != '.')
        return 0;
    for (const ExtensionType* entry = get_extensions(); entry->extension; ++entry) {
        if (_stricmp(entry->extension, dot + 1) == 0)
            return entry->type;
    }
    return 0;
}

// 0x004088E0
ResourceRegistry* create_asset_manager(bool create)
{
    ResourceRegistry* manager = asset_manager();
    if (!manager && create) {
        void* memory = memory::alloc_zeroed(sizeof(ResourceRegistry), nullptr, 1);
        if (memory) {
            manager = static_cast<ResourceRegistry*>(memory)->make_new();
            asset_manager() = manager;
            ++ref_tally();
            return manager;
        }
        asset_manager() = nullptr;
        manager = nullptr;
    }
    ++ref_tally();
    return manager;
}

// 0x00407980
void release_asset_manager()
{
    if (static_cast<unsigned int>(ref_tally()) > 1) {
        --ref_tally();
        return;
    }
    if (ResourceRegistry* manager = asset_manager())
        reinterpret_cast<void(__thiscall*)(ResourceRegistry*, int)>(manager->vtable_[0])(manager, 1);
    asset_manager() = nullptr;
    ref_tally() = 0;
}

} // namespace assets
