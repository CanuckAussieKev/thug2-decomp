#include "engine/file/asset_bundle.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/mem.h"
#include "engine/file/async.h"
#include "engine/file/bundle_format.h"
#include "engine/file/packs.h"
#include "engine/util/byte_stream.h"

namespace file {

// 0x005B6CF0
AssetBundle::AssetBundle() = default;

// 0x005B6EA0
AssetBundle::~AssetBundle()
{
    if (bundle_bytes_) {
        memory::free_malloc(bundle_bytes_);
        bundle_bytes_ = nullptr;
    }
    if (file_ref_)
        async_close(file_ref_);
}

// 0x005B6D30
void AssetBundle::set_heaps(memory::Arena* file_heap, memory::Arena* asset_heap)
{
    file_heap_ = file_heap;
    asset_heap_ = asset_heap;
}

// 0x005B7090
bool AssetBundle::read_in(const char* name, int unused1, int /*unused2*/)
{
    int size = 0;
    memory::enter_heap_scope(file_heap_);
    bundle_bytes_ = packs::load_file_into(name, &size, nullptr);
    loaded_data_ = static_cast<uint8_t*>(bundle_bytes_);
    memory::leave_heap_scope();
    return get_split(unused1, size);
}

// 0x005B6EF0: copy each asset into its own block (from the last, shrinking the file's block as it goes),
// then free the file
bool AssetBundle::get_split(int /*unused*/, int size)
{
    if (file_ref_) {
        async_close(file_ref_);
        file_ref_ = nullptr;
    }
    // Not original - the table read through bundle_format's reader: the count at most kBundleCapacity, each asset
    // within the file. The original copied `count` entries into its 450 before checking the table against the file
    // (a count past 450 wrote past the table) and never checked the assets' offsets / sizes.
    util::ByteReader reader(loaded_data_, size > 0 ? static_cast<size_t>(size) : 0);
    bundle_format::Entry table[kBundleCapacity];
    int count = 0;
    bool ok = bundle_format::read_bundle_table(reader, table, kBundleCapacity, &count);
    num_ = 0;
    if (!ok)
        return false;
    num_ = count;
    for (int i = 0; i < num_; ++i)
        entries_[i] = {table[i].offset, table[i].size, table[i].name, table[i].type};

    for (int i = 0; i < num_; ++i)
        assets_[i] = loaded_data_ + entries_[i].offset;
    for (int i = num_ - 1; i >= 0; --i) {
        uint32_t asset_size = entries_[i].size;
        void* source = assets_[i];
        memory::enter_heap_scope(asset_heap_);
        assets_[i] = memory::alloc_block(asset_size);
        std::memcpy(assets_[i], source, asset_size);
        memory::leave_heap_scope();

        // The asset has its own copy: give the end of the file's block back.
        memory::enter_heap_scope(file_heap_);
        bundle_bytes_ = memory::trim_block_end(memory::malloc_size(bundle_bytes_) - asset_size, bundle_bytes_);
        memory::leave_heap_scope();
    }
    memory::free_malloc(bundle_bytes_);
    bundle_bytes_ = nullptr;
    loaded_data_ = nullptr;
    ready_flag_ = true;
    return true;
}

// 0x005B6D50
bool AssetBundle::is_loaded() { return ready_flag_; }

// 0x005B6D60
void* AssetBundle::obtain_loaded_item(int index) { return assets_[index]; }

// 0x005B6D70
AssetBundle::TableRow* AssetBundle::get_entry(int index) { return &entries_[index]; }

// 0x005B6D80
AssetBundle::TableRow* AssetBundle::find_entry(uint32_t name, uint32_t type, int /*unused*/)
{
    for (int i = 0; i < num_; ++i) {
        if (entries_[i].name == name && entries_[i].type == type)
            return &entries_[i];
    }
    return nullptr;
}

// 0x005B6DC0
void* AssetBundle::find_asset(uint32_t name, uint32_t type, int /*unused*/)
{
    for (int i = 0; i < num_; ++i) {
        if (entries_[i].name == name && entries_[i].type == type)
            return assets_[i];
    }
    return nullptr;
}

// 0x005B6E60
bool AssetBundle::has_asset(uint32_t name, uint32_t type)
{
    for (int i = 0; i < num_; ++i) {
        if (entries_[i].name == name && entries_[i].type == type)
            return true;
    }
    return false;
}

// 0x005B6E00
bool AssetBundle::free_asset(uint32_t name, uint32_t type)
{
    bool found = false;
    for (int i = 0; i < num_; ++i) {
        if (entries_[i].name == name && entries_[i].type == type) {
            memory::free_malloc(assets_[i]);
            assets_[i] = nullptr;
            found = true;
        }
    }
    return found;
}

} // namespace file
