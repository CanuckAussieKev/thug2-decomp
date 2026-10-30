#include "game/file/asset_bundle.h"

#include <cstring>
#include <new>

#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/file/pip.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace File {

namespace {
extern void** const kAssetBundleVtable;
} // namespace


// 0x005B6CF0
AssetBundle::AssetBundle()
{
    m_vtable = kAssetBundleVtable;
    m_fileData = nullptr;
    m_loadedData = nullptr;
    m_file = nullptr;
    m_loaded = false;
    m_fileHeap = nullptr;
    m_assetHeap = nullptr;
    m_unknown234C = 0;
    m_unknown2350 = 0;
}

// 0x005B6EA0
AssetBundle* AssetBundle::Destroy(unsigned int flags)
{
    m_vtable = kAssetBundleVtable;
    if (m_fileData) {
        Mem::FreeMalloc(m_fileData);
        m_fileData = nullptr;
    }
    if (m_file)
        AsyncClose(m_file);
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x005B6D30
void AssetBundle::SetHeaps(Mem::Heap* fileHeap, Mem::Heap* assetHeap)
{
    m_fileHeap = fileHeap;
    m_assetHeap = assetHeap;
}

// 0x005B7090
bool AssetBundle::Load(const char* name, int unused1, int /*unused2*/)
{
    int size = 0;
    Mem::PushContext(m_fileHeap);
    m_fileData = Pip::LoadFileInto(name, &size, nullptr);
    m_loadedData = static_cast<uint8_t*>(m_fileData);
    Mem::PopContext();
    return Split(unused1, size);
}

// 0x005B6EF0: copy each asset into its own block (from the last, shrinking the file's block as it goes),
// then free the file
bool AssetBundle::Split(int /*unused*/, int size)
{
    if (m_file) {
        AsyncClose(m_file);
        m_file = nullptr;
    }
    uint8_t* end = m_loadedData + size;
    auto* header = reinterpret_cast<uint32_t*>(m_loadedData);
    auto* entries = m_loadedData + 8;
    if (entries > end)
        return false;
    m_count = static_cast<int>(header[1]);
    // The entries are copied before the table is checked against the file size (and the count against
    // kMaxEntries is never checked) - as in the original.
    uint32_t tableSize = static_cast<uint32_t>(m_count) * sizeof(Entry);
    std::memcpy(m_entries, entries, tableSize);
    if (entries + tableSize > end)
        return false;

    for (int i = 0; i < m_count; ++i)
        m_assets[i] = m_loadedData + m_entries[i].offset;
    for (int i = m_count - 1; i >= 0; --i) {
        uint32_t assetSize = m_entries[i].size;
        void* source = m_assets[i];
        Mem::PushContext(m_assetHeap);
        m_assets[i] = Mem::Malloc(assetSize);
        std::memcpy(m_assets[i], source, assetSize);
        Mem::PopContext();

        // The asset has its own copy: give the end of the file's block back.
        Mem::PushContext(m_fileHeap);
        m_fileData = Mem::ReallocateShrink(Mem::MallocSize(m_fileData) - assetSize, m_fileData);
        Mem::PopContext();
    }
    Mem::FreeMalloc(m_fileData);
    m_fileData = nullptr;
    m_loadedData = nullptr;
    m_loaded = true;
    return true;
}

// 0x005B6D50
bool AssetBundle::IsLoaded()
{
    return m_loaded;
}

// 0x005B6D60
void* AssetBundle::GetAsset(int index)
{
    return m_assets[index];
}

// 0x005B6D70
AssetBundle::Entry* AssetBundle::GetEntry(int index)
{
    return &m_entries[index];
}

// 0x005B6D80
AssetBundle::Entry* AssetBundle::FindEntry(uint32_t name, uint32_t type, int /*unused*/)
{
    for (int i = 0; i < m_count; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type)
            return &m_entries[i];
    }
    return nullptr;
}

// 0x005B6DC0
void* AssetBundle::FindAsset(uint32_t name, uint32_t type, int /*unused*/)
{
    for (int i = 0; i < m_count; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type)
            return m_assets[i];
    }
    return nullptr;
}

// 0x005B6E60
bool AssetBundle::HasAsset(uint32_t name, uint32_t type)
{
    for (int i = 0; i < m_count; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type)
            return true;
    }
    return false;
}

// 0x005B6E00
bool AssetBundle::FreeAsset(uint32_t name, uint32_t type)
{
    bool found = false;
    for (int i = 0; i < m_count; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type) {
            Mem::FreeMalloc(m_assets[i]);
            m_assets[i] = nullptr;
            found = true;
        }
    }
    return found;
}

namespace {
GAME_VTABLE(kAssetBundleVtable, 0x0065822C, &File::AssetBundle::Destroy);
} // namespace

} // namespace File

namespace {

// The constructor returns `this` in EAX (callers use it).
struct AssetBundleAdapter {
    File::AssetBundle* Construct() { return new (this) File::AssetBundle; }
};

} // namespace

REPLACE(0x005B6CF0, &AssetBundleAdapter::Construct);
REPLACE(0x005B6EA0, &File::AssetBundle::Destroy);
REPLACE(0x005B6D30, &File::AssetBundle::SetHeaps);
REPLACE(0x005B7090, &File::AssetBundle::Load);
REPLACE(0x005B6EF0, &File::AssetBundle::Split);
REPLACE(0x005B6D50, &File::AssetBundle::IsLoaded);
REPLACE(0x005B6D60, &File::AssetBundle::GetAsset);
REPLACE(0x005B6D70, &File::AssetBundle::GetEntry);
REPLACE(0x005B6D80, &File::AssetBundle::FindEntry);
REPLACE(0x005B6DC0, &File::AssetBundle::FindAsset);
REPLACE(0x005B6E60, &File::AssetBundle::HasAsset);
REPLACE(0x005B6E00, &File::AssetBundle::FreeAsset);
