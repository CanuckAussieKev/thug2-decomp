#pragma once

#include <cstdint>

#include "engine/core/mem.h"

namespace file {

class AsyncFileHandle;

// A level's bundle of asset files, loaded as one file and then split into one allocation per asset (created
// by the level loader, 0x004678A0, which walks the entries by type).
//
// The file format: formats/bundle_format.h (read through its reader; TableRow is the bundle's own copy of
// an entry).
class AssetBundle : public memory::HeapObject {
public:
    struct TableRow {
        uint32_t offset;
        uint32_t size;
        uint32_t name;
        uint32_t type;
    };
    static constexpr int kBundleCapacity = 450;

    AssetBundle();
    // 0x005B6EA0: the loaded file freed and an async load closed - not the assets themselves.
    ~AssetBundle();
    AssetBundle(const AssetBundle&) = delete;
    AssetBundle& operator=(const AssetBundle&) = delete;

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

    // (the original's table at 0x0065822C held only the destructor: no vtable here)
    AsyncFileHandle* file_ref_{nullptr}; // an async load in progress (closed when the bundle is split / destroyed)
    void* bundle_bytes_{nullptr};        // the whole file, as loaded (shrinks as the assets are split off)
    uint8_t* loaded_data_{nullptr};      // the start of the file
    int num_{};                 // number of entries
    int unknown14_{};
    TableRow entries_[kBundleCapacity]{};
    void* assets_[kBundleCapacity]{}; // each asset's own copy
    bool ready_flag_{false};
    uint8_t pad2341_[3]{};
    memory::Arena* asset_heap_{nullptr};
    memory::Arena* file_heap_{nullptr};
    int unknown234c_{0};
    int unknown2350_{0};
};
static_assert(sizeof(AssetBundle) == 0x2350, "the original's 0x2354 bytes less its vtable");

} // namespace file
