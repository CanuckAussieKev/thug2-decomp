// The asset manager.

#include "engine/assets/resource_registry.h"

#include <cstddef>
#include <iterator>
#include <memory>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/assets/resource_entry.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/file/async.h"
#include "engine/lists/key_map.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include <string_view>

namespace assets {

namespace {
constexpr uint32_t kAssertOnMissingAssets = 0x25DC7904U; // AssertOnMissingAssets
constexpr size_t kExtensionCount = 13;                   // the file extensions and the null entry that ends them
constexpr int kAssetTableBits = 10;                      // the key map's size (KeyMap(bits))
constexpr size_t kUnloadBatchSize = 100;                 // the most assets one unload gathers

// NOT ORIGINAL grouping - the asset manager and the main loop manager (their owners) and the asset manager's
// references. Made on first use, never destroyed (registry_state()).
struct RegistryState {
    std::unique_ptr<assets::ResourceRegistry> asset_manager_;
    int ass_man_references_{};
    memory::HeapUnique<frame_loop::Hub> main_loop_manager_;
};

RegistryState& registry_state()
{
    static core::NoDestroy<RegistryState> state;
    return state.get();
}
} // namespace

ResourceRegistry* asset_manager() { return registry_state().asset_manager_.get(); }
frame_loop::Hub* get_main_loop_manager() { return registry_state().main_loop_manager_.get(); }
memory::HeapUnique<frame_loop::Hub>& main_loop_manager_owner() { return registry_state().main_loop_manager_; }

namespace {

int& ref_tally() { return registry_state().ass_man_references_; }

// The script integer the loads look up (a debug leftover: set, a failed asset isn't deleted).
int keep_failed_assets() { return script::fetch_int_value(kAssertOnMissingAssets); }

// The file name extensions and their types (0x0067C650, ended by a null name).
struct ExtensionType {
    const char* extension;
    ResourceKind type;
};
// (the extensions are the exe's strings; the table's last entry stays null)
constexpr std::array<ExtensionType, 13> kExtensions{{{.extension = "BIN", .type = ResourceKind::kBinary}, {.extension = "COL", .type = ResourceKind::kSceneCollision}, {.extension = "SCN", .type = ResourceKind::kScene},  {.extension = "GEOM", .type = ResourceKind::kScene}, {.extension = "MDL", .type = ResourceKind::kSkinnedModel}, {.extension = "SKA", .type = ResourceKind::kAnimation},
                               {.extension = "FAM", .type = ResourceKind::kAnimation}, {.extension = "SKE", .type = ResourceKind::kSkeleton}, {.extension = "SKIN", .type = ResourceKind::kSkinnedModel}, {.extension = "TEX", .type = ResourceKind::kResTexDict},  {.extension = "CUT", .type = ResourceKind::kCutscene}, {.extension = "QB", .type = ResourceKind::kNodeArray}}};
const std::array<ExtensionType, kExtensionCount>& get_extensions() { return kExtensions; }

// The hash table lookup inlined in the original: the first item with the key and a value.
Resource* get_find(lists::KeyMap* table, uint32_t checksum)
{
    for (const lists::KeyMap::Item* item = &core::at(table->bucket_run(), ((1U << table->bits()) - 1) & checksum); item;
         item = item->next) {
        if (item->key == checksum && item->value) {
            return static_cast<Resource*>(item->value);
        }
    }
    return nullptr;
}
void wait_until_loaded(Resource* asset)
{
    while (!asset->is_loaded()) {
        file::async_wait_for_io(false);
    }
}
// `asset` onto the end of the circular list `*head`.
void get_append(Resource** head, Resource* asset)
{
    if (*head) {
        asset->set_succ(*head);
        asset->set_last_value((*head)->last_value());
        (*head)->last_value()->set_succ(asset);
        (*head)->set_last_value(asset);
    } else {
        *head = asset;
        asset->set_succ(asset);
        asset->set_last_value(asset);
    }
}

// A new asset of `type` (null for a type that isn't one, or if the allocation fails).
gsl::owner<Resource*> make_asset(ResourceKind type, bool with_cutscene)
{
    switch (type) {
    case ResourceKind::kScene:
        return new SceneAsset;
    case ResourceKind::kResTexDict:
        return new TexDictAsset;
    case ResourceKind::kSceneCollision:
        return new SceneCollisionAsset;
    case ResourceKind::kAnimation:
        return new AnimResource;
    case ResourceKind::kSkeleton:
        return new RigResource;
    case ResourceKind::kSkinnedModel:
        return new SkinResource;
    case ResourceKind::kSkinCollision:
        return new SkinCollisionAsset;
    case ResourceKind::kCutscene:
        // (read_resource_from_stream has no case for cutscenes)
        if (!with_cutscene) {
            return nullptr;
        }
        return new CutsceneResource;
    case ResourceKind::kNodeArray:
        return new NodeTableResource;
    case ResourceKind::kOwnedNodeArray: {
        gsl::owner<assets::NodeTableResource*> asset{memory::make_owned<NodeTableResource>()};
        asset->make_owner(); // (null if the allocation failed: a crash)
        return asset;
    }
    case ResourceKind::kNone:
    case ResourceKind::kBinary:
        break;
    }
    return nullptr;
}

// The manager's callback given to a new asset (cleared after one asset if it's a one-shot).
void give_callback(ResourceRegistry* manager, Resource* asset)
{
    asset->change_hook(manager->callback(), {.first_word_ = manager->callback_data0(), .data1 = manager->callback_data1()});
    if (manager->callback_once()) {
        manager->set_callback(nullptr);
        manager->set_callback_once(false);
    }
}
// `asset` depends on the asset `group` (if given): each knows the other.
void join_group(ResourceRegistry* manager, Resource* asset, uint32_t group)
{
    if (group == 0U) {
        return;
    }
    Resource* owner = manager->obtain_loaded_item(group, 1);
    asset->change_cluster(owner);
    owner->add_dependent(asset);
}

} // namespace

// 0x00407F80
ResourceRegistry::ResourceRegistry()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    assets_ = memory ? memory::construct_in<lists::KeyMap>(memory, kAssetTableBits) : nullptr;
    task_ = std::make_unique<tasks::Task<ResourceRegistry>>(&process_task, this, 0);
}

// 0x00408930
ResourceRegistry::~ResourceRegistry()
{
    unload_assets_in_list(ready_flag_, true); // (inlined)
    unload_assets_in_list(pending_, true);
    if (lists::KeyMap* table = assets_) {
        if (table->buckets()) {
            table->empty_table();
            memory::Hub::dealloc(table->buckets());
            table->set_buckets(nullptr);
        }
        memory::Hub::dealloc(table);
    }
    task_.reset();
}

// 0x00407E90
void ResourceRegistry::start_callback()
{
    (&registry_state().main_loop_manager_->logic_tasks())->queue_job(task_.get()); // the logic tasks
}

// 0x00407AE0
void ResourceRegistry::stop_callback() { task_->drop_out(); }

// 0x00408060
void* ResourceRegistry::read_resource_from_stream(checksums::Name resource, ResourceKind type, checksums::Name group_name, void* data,
                                                  uint32_t size, bool permanent, bool copy, void* params, void* structure)
{
    const uint32_t group = group_name.value();
    const uint32_t checksum = resource.value();
    memory::push_mem_profile("Asset Overhead");
    gsl::owner<Resource*> asset = make_asset(type, false);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->set_pinned(permanent);
    asset->set_cur_phase(AssetState::kUnloaded);
    asset->set_checksum(checksum);
    give_callback(this, asset);
    // (the original formats "%08x from data stream" with the checksum for the memory profile, unused)
    join_group(this, asset, group);
    memory::pop_mem_profile();
    const int failed = asset->load_from_buffer(data, size, copy, params, structure);
    memory::push_mem_profile("Asset Overhead");
    if (failed != 0) {
        if (keep_failed_assets() == 0) {
            delete asset;
        }
        memory::pop_mem_profile();
        return nullptr;
    }
    assets_->store_entry(checksum, asset);
    get_append(&ready_flag_, asset);
    memory::pop_mem_profile();
    return asset->get_data();
}

// 0x00408440
void* ResourceRegistry::load_asset(const char* name, ResourceKind type, AssetKey asset_key, bool async,
                                   bool use_archive, bool permanent, void* params, void* structure)
{
    auto checksum = asset_key.checksum;
    const auto group = asset_key.group;
    if (checksum == 0U) {
        checksum = checksums::hash_from_text(name);
    }
    if (type == ResourceKind::kNone) {
        type = get_type_from_extension(name);
    }
    gsl::owner<Resource*> asset = make_asset(type, true);
    // (a type that isn't one leaves a null asset: the original crashes here)
    asset->set_pinned(permanent);
    asset->set_cur_phase(AssetState::kUnloaded);
    give_callback(this, asset);
    asset->set_checksum(checksum);
    join_group(this, asset, group);
    const int failed = asset->read_in(name, async, use_archive, params, structure);
    if (failed != 0) {
        if (keep_failed_assets() == 0) {
            delete asset;
        }
        return nullptr;
    }
    assets_->store_entry(checksum, asset);
    get_append(async ? &pending_ : &ready_flag_, asset);
    return asset->get_data();
}

// 0x004087B0
void* ResourceRegistry::fetch_or_load_resource(const char* name, ResourceKind type, uint32_t checksum, uint32_t group,
                                               bool async, bool use_archive, bool permanent, void* params, void* structure)
{
    if (checksum == 0U) {
        checksum = checksums::hash_from_text(name);
    }
    void* data = nullptr;
    if (Resource* asset = get_find(assets_, checksum)) {
        data = asset->get_data();
        wait_until_loaded(asset);
    } else {
        data = load_asset(name, type, {.checksum = checksum, .group = group}, async, use_archive, permanent, params, structure);
    }
    keep_failed_assets(); // (looked up and not used)
    return data;
}

// 0x00408870
bool ResourceRegistry::load_anim(const char* name, uint32_t base, uint32_t offset, bool async, bool use_archive)
{
    uint32_t checksum = checksums::hash_from_text(name);
    if (base != 0U) {
        checksum = base + offset;
    }
    if (asset_exists(checksum)) {
        return false;
    }
    return fetch_or_load_resource(name, ResourceKind::kAnimation, checksum, 0, async, use_archive, permanent_by_default_, nullptr,
                                  nullptr) != nullptr;
}

// 0x00407EC0
bool ResourceRegistry::refresh_resource(uint32_t checksum, const char* name, int unused)
{
    Resource* asset = obtain_loaded_item(checksum, unused);
    if (!asset) {
        return false;
    }
    give_callback(this, asset);
    return asset->reload(name);
}

// 0x00407B20
void ResourceRegistry::discard_loaded_item(Resource* asset)
{
    const uint32_t checksum = asset->checksum();
    if (Resource* group = asset->bundle()) {
        group->remove_dependent(asset);
    }
    Resource** list = nullptr;
    if (asset->is_loaded()) {
        list = &ready_flag_;
    } else {
        asset->cancel_now();
        list = &pending_;
    }
    remove_from_list(list, asset);
    asset->offload();
    gsl::owner<Resource*> owned = memory::owned_element(asset); // (the table's)
    delete owned;
    assets_->erase_entry(checksum);
}

// 0x00407CF0
void ResourceRegistry::unload_assets_in_list(Resource* head, bool all)
{
    if (!head) {
        return;
    }
    Resource* asset = head->last_value();
    for (;;) {
        if (!asset) {
            return;
        }
        Resource* previous = asset->last_value();
        if (all || !asset->pinned()) {
            discard_loaded_item(asset);
        }
        if (asset == head) {
            return;
        }
        asset = previous;
    }
}

// 0x00407D40
void ResourceRegistry::unload_assets_of_type(Resource* head, bool all, ResourceKind type)
{
    if (!head) {
        return;
    }
    std::array<Resource*, kUnloadBatchSize> found{};
    int count = 0;
    Resource* asset = head->last_value();
    for (;;) {
        if (!asset) {
            break;
        }
        Resource* previous = asset->last_value();
        if ((all || !asset->pinned()) && type == asset->fetch_kind()) {
            core::at(found, count++) = asset;
        }
        if (asset == head) {
            break;
        }
        asset = previous;
    }
    for (int i = 0; i < count; ++i) {
        discard_loaded_item(core::at(found, i));
    }
}

// 0x00407F20
void ResourceRegistry::unload_all_assets(bool all)
{
    unload_assets_in_list(ready_flag_, all);
    unload_assets_in_list(pending_, all);
}

// 0x00407F50
void ResourceRegistry::unload_all_assets_of_type(bool all, ResourceKind type)
{
    unload_assets_of_type(ready_flag_, all, type);
    unload_assets_of_type(pending_, all, type);
}

// 0x00407B90
Resource* ResourceRegistry::obtain_loaded_item(uint32_t checksum, int /*unused*/)
{
    Resource* asset = get_find(assets_, checksum);
    if (asset && !asset->is_loaded() && !asset->is_loaded()) { // (asked twice in the original)
        file::async_wait_for_io(false);
        while (!asset->is_loaded()) {
            file::async_wait_for_io(false);
        }
    }
    return asset;
}

// 0x00407C00
void* ResourceRegistry::get_asset_data(uint32_t checksum, int /*unused*/)
{
    Resource* asset = get_find(assets_, checksum);
    if (!asset) {
        return nullptr;
    }
    wait_until_loaded(asset);
    return asset->get_data();
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
        Resource* next = asset->succ();
        bool finished = false;
        switch (asset->cur_phase()) {
        case AssetState::kRead:
            // (a group that isn't loaded yet holds it back)
            if (asset->bundle() && !asset->bundle()->is_loaded()) {
                break;
            }
            [[fallthrough]];
        case AssetState::kProcessing:
            if (!asset->process_buffer()) {
                break;
            }
            if (asset->callback()) {
                asset->callback()(asset, asset->extent(), asset->callback_data0(), asset->callback_data1());
            }
            finished = true;
            break;
        case AssetState::kLoaded:
            finished = true;
            break;
        case AssetState::kUnloaded:
        case AssetState::kLoading:
        case AssetState::kAborted:
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
void ResourceRegistry::process_task(tasks::Task<ResourceRegistry>* task) { task->content()->process_pending_assets(); }

// 0x00407A80
bool ResourceRegistry::remove_from_list(Resource** head, Resource* asset)
{
    if (asset->succ() == asset) {
        // (the only one: the list emptied without checking it's this list)
        *head = nullptr;
        asset->set_last_value(nullptr);
        asset->set_succ(nullptr);
        return true;
    }
    if (*head == asset) {
        *head = asset->succ();
    }
    for (const Resource* item = *head; item; item = item->succ()) {
        if (item == asset) {
            asset->succ()->set_last_value(asset->last_value());
            asset->last_value()->set_succ(asset->succ());
            asset->set_last_value(nullptr);
            asset->set_succ(nullptr);
            return true;
        }
    }
    return false;
}

// 0x004079C0
ResourceKind ResourceRegistry::get_type_from_extension(const char* name)
{
    const size_t dot = std::string_view(name).rfind('.');
    if (dot == std::string_view::npos) {
        return ResourceKind::kNone;
    }
    // (the table ends with an entry without an extension; the original would have read on past it without one)
    for (const ExtensionType& entry : get_extensions()) {
        if (!entry.extension) {
            break;
        }
        if (_stricmp(entry.extension, std::next(name, static_cast<std::ptrdiff_t>(dot + 1))) == 0) {
            return entry.type;
        }
    }
    return ResourceKind::kNone;
}

// 0x004088E0
ResourceRegistry* create_asset_manager(bool create)
{
    ResourceRegistry* manager = asset_manager();
    if (!manager && create) {
        registry_state().asset_manager_ = std::make_unique<ResourceRegistry>(); // (null when the heap is full)
        manager = registry_state().asset_manager_.get();
        if (manager) {
            ++ref_tally();
            return manager;
        }
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
    memory::destroy_then_clear(registry_state().asset_manager_);
    ref_tally() = 0;
}

} // namespace assets
