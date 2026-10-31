#include "game/file/asset_bundle.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/mem.h"
#include "game/file/async.h"
#include "game/file/packs.h"
#include "game/core/code_address.h"

namespace FileLoad {

namespace {
extern void** const kAssetBundleVtable;
} // namespace

// 0x005B6CF0
AssetBundle::AssetBundle()
{
    m_vtable = kAssetBundleVtable;
    m_fileData = nullptr;
    m_loadedData = nullptr;
    m_fileRef = nullptr;
    m_readyFlag = false;
    m_fileHeap = nullptr;
    m_assetHeap = nullptr;
    m_unknown234C = 0;
    m_unknown2350 = 0;
}

// 0x005B6EA0
AssetBundle* AssetBundle::Teardown(unsigned int flags)
{
    m_vtable = kAssetBundleVtable;
    if (m_fileData) {
        MemSys::FreeMalloc(m_fileData);
        m_fileData = nullptr;
    }
    if (m_fileRef)
        AsyncClose(m_fileRef);
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x005B6D30
void AssetBundle::SetHeaps(MemSys::Arena* fileHeap, MemSys::Arena* assetHeap)
{
    m_fileHeap = fileHeap;
    m_assetHeap = assetHeap;
}

// 0x005B7090
bool AssetBundle::ReadIn(const char* name, int unused1, int /*unused2*/)
{
    int size = 0;
    MemSys::EnterHeapScope(m_fileHeap);
    m_fileData = Packs::LoadFileInto(name, &size, nullptr);
    m_loadedData = static_cast<uint8_t*>(m_fileData);
    MemSys::LeaveHeapScope();
    return Split(unused1, size);
}

// 0x005B6EF0: copy each asset into its own block (from the last, shrinking the file's block as it goes),
// then free the file
bool AssetBundle::Split(int /*unused*/, int size)
{
    if (m_fileRef) {
        AsyncClose(m_fileRef);
        m_fileRef = nullptr;
    }
    uint8_t* end = m_loadedData + size;
    auto* header = reinterpret_cast<uint32_t*>(m_loadedData);
    auto* entries = m_loadedData + 8;
    if (entries > end)
        return false;
    m_num = static_cast<int>(header[1]);
    // The entries are copied before the table is checked against the file size (and the count against
    // kBundleCapacity is never checked) - as in the original.
    uint32_t tableSize = static_cast<uint32_t>(m_num) * sizeof(TableRow);
    std::memcpy(m_entries, entries, tableSize);
    if (entries + tableSize > end)
        return false;

    for (int i = 0; i < m_num; ++i)
        m_assets[i] = m_loadedData + m_entries[i].offset;
    for (int i = m_num - 1; i >= 0; --i) {
        uint32_t assetSize = m_entries[i].size;
        void* source = m_assets[i];
        MemSys::EnterHeapScope(m_assetHeap);
        m_assets[i] = MemSys::AllocBlock(assetSize);
        std::memcpy(m_assets[i], source, assetSize);
        MemSys::LeaveHeapScope();

        // The asset has its own copy: give the end of the file's block back.
        MemSys::EnterHeapScope(m_fileHeap);
        m_fileData = MemSys::TrimBlockEnd(MemSys::MallocSize(m_fileData) - assetSize, m_fileData);
        MemSys::LeaveHeapScope();
    }
    MemSys::FreeMalloc(m_fileData);
    m_fileData = nullptr;
    m_loadedData = nullptr;
    m_readyFlag = true;
    return true;
}

// 0x005B6D50
bool AssetBundle::IsLoaded()
{
    return m_readyFlag;
}

// 0x005B6D60
void* AssetBundle::ObtainLoadedItem(int index)
{
    return m_assets[index];
}

// 0x005B6D70
AssetBundle::TableRow* AssetBundle::GetEntry(int index)
{
    return &m_entries[index];
}

// 0x005B6D80
AssetBundle::TableRow* AssetBundle::FindEntry(uint32_t name, uint32_t type, int /*unused*/)
{
    for (int i = 0; i < m_num; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type)
            return &m_entries[i];
    }
    return nullptr;
}

// 0x005B6DC0
void* AssetBundle::FindAsset(uint32_t name, uint32_t type, int /*unused*/)
{
    for (int i = 0; i < m_num; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type)
            return m_assets[i];
    }
    return nullptr;
}

// 0x005B6E60
bool AssetBundle::HasAsset(uint32_t name, uint32_t type)
{
    for (int i = 0; i < m_num; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type)
            return true;
    }
    return false;
}

// 0x005B6E00
bool AssetBundle::FreeAsset(uint32_t name, uint32_t type)
{
    bool found = false;
    for (int i = 0; i < m_num; ++i) {
        if (m_entries[i].name == name && m_entries[i].type == type) {
            MemSys::FreeMalloc(m_assets[i]);
            m_assets[i] = nullptr;
            found = true;
        }
    }
    return found;
}

namespace {
static VtableSlot kAssetBundleVtable_slots[] = {&FileLoad::AssetBundle::Teardown};
void** const kAssetBundleVtable = reinterpret_cast<void**>(kAssetBundleVtable_slots);
} // namespace

} // namespace File

namespace {

// The constructor returns `this` in EAX (callers use it).
struct AssetBundleAdapter {
    FileLoad::AssetBundle* MakeNew() { return new (this) FileLoad::AssetBundle; }
};

} // namespace
