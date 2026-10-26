#pragma once

#include <cstdint>

namespace Mem {
class Heap;
}

namespace File {

class AsyncFileHandle;

// A level's bundle of asset files, loaded as one file and then split into one allocation per asset (created
// by the level loader, 0x004678A0, which walks the entries by type).
//
// File format: a header {uint32 unknown, uint32 count}, then `count` entries {offset from the start of the
// file, size, name checksum, type checksum}, then the asset data.
class AssetBundle {
public:
    struct Entry {
        uint32_t offset;
        uint32_t size;
        uint32_t name;
        uint32_t type;
    };
    static constexpr int kMaxEntries = 450;

    AssetBundle();
    // The vtable's only slot (scalar deleting destructor). Doesn't free the assets themselves.
    AssetBundle* Destroy(unsigned int flags);

    // The heap the bundle file is loaded into (`fileHeap`), and the heap each asset is copied out to.
    void SetHeaps(Mem::Heap* fileHeap, Mem::Heap* assetHeap);
    // Load `name` and split it. `unused` arguments are ignored (as in the original).
    bool Load(const char* name, int unused1, int unused2);
    // Split the loaded file (`size` bytes at m_loadedData) into the assets.
    bool Split(int unused, int size);

    bool IsLoaded();
    void* GetAsset(int index);
    Entry* GetEntry(int index);
    Entry* FindEntry(uint32_t name, uint32_t type, int unused);
    void* FindAsset(uint32_t name, uint32_t type, int unused);
    bool HasAsset(uint32_t name, uint32_t type);
    // Free every asset with this name and type; whether there was one.
    bool FreeAsset(uint32_t name, uint32_t type);

    void** m_vtable;                 // 0x0065822C
    AsyncFileHandle* m_file;         // an async load in progress (closed when the bundle is split / destroyed)
    void* m_fileData;                // the whole file, as loaded (shrinks as the assets are split off)
    uint8_t* m_loadedData;           // the start of the file
    int m_count;                     // number of entries
    int m_unknown14;
    Entry m_entries[kMaxEntries];
    void* m_assets[kMaxEntries];     // each asset's own copy
    bool m_loaded;
    uint8_t m_pad2341[3];
    Mem::Heap* m_assetHeap;
    Mem::Heap* m_fileHeap;
    int m_unknown234C;
    int m_unknown2350;
};
static_assert(sizeof(AssetBundle) == 0x2354, "allocated as 0x2354 bytes by the original");

} // namespace File
