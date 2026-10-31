#pragma once

#include <cstdint>

namespace memory {
class Arena;
}

namespace file {

class AsyncFileHandle;

// A level's bundle of asset files, loaded as one file and then split into one allocation per asset (created
// by the level loader, 0x004678A0, which walks the entries by type).
//
// File format: a header {uint32 unknown, uint32 count}, then `count` entries {offset from the start of the
// file, size, name checksum, type checksum}, then the asset data.
class AssetBundle {
public:
    struct TableRow {
        uint32_t offset;
        uint32_t size;
        uint32_t name;
        uint32_t type;
    };
    static constexpr int kBundleCapacity = 450;

    AssetBundle();
    // The vtable's only slot (scalar deleting destructor). Doesn't free the assets themselves.
    AssetBundle* teardown(unsigned int flags);

    // The heap the bundle file is loaded into (`file_heap`), and the heap each asset is copied out to.
    void set_heaps(memory::Arena* file_heap, memory::Arena* asset_heap);
    // Load `name` and split it. `unused` arguments are ignored (as in the original).
    bool read_in(const char* name, int unused1, int unused2);
    // Split the loaded file (`size` bytes at loaded_data_) into the assets.
    bool get_split(int unused, int size);

    bool is_loaded();
    void* obtain_loaded_item(int index);
    TableRow* get_entry(int index);
    TableRow* find_entry(uint32_t name, uint32_t type, int unused);
    void* find_asset(uint32_t name, uint32_t type, int unused);
    bool has_asset(uint32_t name, uint32_t type);
    // Free every asset with this name and type; whether there was one.
    bool free_asset(uint32_t name, uint32_t type);

    void** vtable_;             // 0x0065822C
    AsyncFileHandle* file_ref_; // an async load in progress (closed when the bundle is split / destroyed)
    void* bundle_bytes_;        // the whole file, as loaded (shrinks as the assets are split off)
    uint8_t* loaded_data_;      // the start of the file
    int num_;                   // number of entries
    int unknown14_;
    TableRow entries_[kBundleCapacity];
    void* assets_[kBundleCapacity]; // each asset's own copy
    bool ready_flag_;
    uint8_t pad2341_[3];
    memory::Arena* asset_heap_;
    memory::Arena* file_heap_;
    int unknown234c_;
    int unknown2350_;
};
static_assert(sizeof(AssetBundle) == 0x2354, "allocated as 0x2354 bytes by the original");

} // namespace file
