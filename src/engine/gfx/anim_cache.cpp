// The animation cache (0x00480590-0x0048137B): see anim_cache.h. From the disassembly.

#include "engine/gfx/anim_cache.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "platform/stopwatch.h"

namespace Gfx2D {

MemSys::PoolPair g_animSlotPools; // (CAnimCachePredictor; script_startup.h)

namespace {

constexpr uint32_t kAnimHeap = 0x8F21A798; // AnimHeap
constexpr int kEntries = 50;
constexpr int kSlots = 18;

CClipStore* g_clipStore;

CClipStore*& Cache() { return g_clipStore; }
MemSys::PoolPair& SlotPools() { return g_animSlotPools; }

MemSys::Hub* Hub() { return MemSys::Hub::Pull(); }

// The AnimHeap made current (allocations and frees in between go to it).
void PushAnimHeap() { Hub()->EnterHeapScope(Hub()->FetchHeapByKey(kAnimHeap, 1)); }
void PopHeap() { Hub()->LeaveHeapScope(); }

// The sizes of a rotation and a translation key for a cache type.
void KeySizes(int type, uint8_t& rotation, uint8_t& translation)
{
    switch (type) {
    case 2:
        rotation = translation = 8;
        break;
    case 3:
        rotation = translation = 16;
        break;
    case 4:
        rotation = 20;
        translation = 16;
        break;
    default:
        rotation = translation = 0;
        break;
    }
}

// The flags with the user count changed by `delta` (13 bits, wrapping).
uint16_t WithUsers(uint16_t flags, int delta)
{
    return static_cast<uint16_t>((flags & ~CAnimCacheEntry::kUserMask) | ((flags + delta) & CAnimCacheEntry::kUserMask));
}

// Every slot cleared and given back (Release and the destructor).
void FreeSlots(CAnimCacheEntry* entry)
{
    for (CAnimCacheSlot*& slot : entry->m_slotsPtr) {
        if (slot) {
            slot->Clear();
            if (slot)
                CAnimCacheSlot::Dealloc(slot);
            slot = nullptr;
        }
    }
}

// The fields Release and the destructor empty.
void Vacant(CAnimCacheEntry* entry)
{
    entry->m_rotationKeysPtr = nullptr;
    entry->m_translationKeysPtr = nullptr;
    entry->m_lookupKey = 0;
    entry->m_bones = 0;
    entry->m_rotationKeySize = 0;
    entry->m_translationKeySize = 0;
    entry->m_extent = 0;
    entry->m_allocated = 0;
}

// The original's REP MOVSD / MOVSB copy: dwords forwards, then the odd bytes - kept for overlapping blocks (an entry
// whose Allocate failed keeps its old, smaller block with the new size: its copy runs into what follows).
void CopyForwards(uint8_t* to, const uint8_t* from, uint32_t size)
{
    __movsd(reinterpret_cast<unsigned long*>(to), reinterpret_cast<const unsigned long*>(from), size / 4);
    __movsb(to + (size & ~3u), from + (size & ~3u), size & 3);
}

void FreeData(CAnimCacheEntry* entry)
{
    if (entry->m_payload) {
        PushAnimHeap();
        Hub()->Dealloc(entry->m_payload);
        PopHeap();
        entry->m_payload = nullptr;
    }
}

} // namespace

// --- the slots ---

// 0x00480A70
void __cdecl CAnimCacheSlot::Dealloc(CAnimCacheSlot* slot)
{
    SlotPools().Dealloc(slot);
}

void CAnimCacheSlot::Clear()
{
    std::memset(m_rotationKeys, 0, sizeof(m_rotationKeys));
    std::memset(m_translationKeys, 0, sizeof(m_translationKeys));
    m_clock = 0x8000;
}

// --- an entry ---

void CAnimCacheEntry::MakeNew()
{
    m_flagBits &= 0xA000;
    m_rotationKeysPtr = nullptr;
    m_translationKeysPtr = nullptr;
    m_lookupKey = 0;
    m_bones = 0;
    m_rotationKeySize = 0;
    m_translationKeySize = 0;
    m_allocated = 0;
    m_extent = 0;
    m_payload = nullptr;
    m_kindTag = 0;
}

// 0x00480810
uint32_t CAnimCacheEntry::Carve(uint32_t key, int16_t bones, int rotationKeys, int translationKeys, int type)
{
    m_flagBits |= kReleasing;
    m_bones = bones;
    m_kindTag = type;
    KeySizes(type, m_rotationKeySize, m_translationKeySize);
    uint32_t granularity = Cache()->m_granularity;
    m_extent = m_rotationKeySize * rotationKeys + m_translationKeySize * translationKeys;
    m_allocated = (m_extent / granularity + 1) * granularity;
    PushAnimHeap();
    if (Hub()->Obtainable() < m_allocated) {
        m_kindTag = 0;
        PopHeap();
        return 0;
    }
    m_lookupKey = key;
    m_payload = static_cast<uint8_t*>(MemSys::Alloc(m_allocated));
    std::memset(m_payload, 0, m_allocated);
    m_rotationKeysPtr = m_payload;
    m_translationKeysPtr = m_payload + m_rotationKeySize * rotationKeys;
    PopHeap();
    m_flagBits &= ~(kBusy | kReleasing);
    m_lastUsed = 0;
    return m_allocated;
}

// 0x00480AD0
uint32_t CAnimCacheEntry::Release()
{
    uint16_t flags = m_flagBits;
    while (flags & kBusy) {
        // (forever, as the original: the flags were read once)
    }
    m_flagBits = flags | kReleasing;
    if (m_releaseCallback) {
        m_releaseCallback(m_releaseArg);
        m_releaseCallback = nullptr;
    }
    uint32_t allocated = m_allocated;
    m_flagBits &= kReleasing;
    Vacant(this);
    FreeData(this);
    FreeSlots(this);
    m_kindTag = 0;
    return allocated;
}

// 0x00481200
void CAnimCacheEntry::DestroyBody()
{
    m_flagBits &= ~(kKeep | kBusy);
    m_releaseCallback = nullptr;
    Vacant(this);
    FreeData(this);
    if (m_releaseCallback) { // (never: cleared above)
        m_releaseCallback(m_releaseArg);
        m_releaseCallback = nullptr;
    }
    FreeSlots(this);
    m_kindTag = 0;
}

// 0x00480BF0
int CAnimCacheEntry::NewSlot()
{
    for (int i = 0; i < kSlots; ++i) {
        if (m_slotsPtr[i])
            continue;
        auto* slot = static_cast<CAnimCacheSlot*>(SlotPools().Carve(sizeof(CAnimCacheSlot)));
        if (slot)
            slot->m_clock = 0x8000;
        m_slotsPtr[i] = slot;
        slot->m_clock &= 0x7FFF; // (null: a crash, as the original)
        std::memset(m_slotsPtr[i]->m_rotationKeys, 0, sizeof(slot->m_rotationKeys));
        std::memset(m_slotsPtr[i]->m_translationKeys, 0, sizeof(slot->m_translationKeys));
        return i;
    }
    return -1;
}

// 0x00480C90
void CAnimCacheEntry::FreeSlot(int index)
{
    if (!(m_flagBits & kUserMask))
        return;
    CAnimCacheSlot* slot = m_slotsPtr[index];
    if (slot && index > 0) {
        slot->Clear();
        if (m_slotsPtr[index])
            CAnimCacheSlot::Dealloc(m_slotsPtr[index]);
        m_slotsPtr[index] = nullptr;
    }
    m_flagBits = WithUsers(m_flagBits, -1);
}

// 0x00480590
int16_t CAnimCacheEntry::SetSlotTime(int index, int16_t time)
{
    CAnimCacheSlot* slot = m_slotsPtr[index];
    if (!slot)
        return 0;
    uint16_t old = slot->m_clock;
    int last = static_cast<int16_t>(old << 1) >> 1; // (15 bits, signed)
    slot->m_clock = static_cast<uint16_t>((old & 0x8000) | (static_cast<uint16_t>(time) & 0x7FFF));
    return static_cast<int16_t>(time - last);
}

// 0x004805E0
uint8_t CAnimCacheEntry::GetRotationKeySize() { return m_rotationKeySize; }

// 0x004805F0
uint8_t CAnimCacheEntry::GetTranslationKeySize() { return m_translationKeySize; }

// 0x00480600
uint8_t* CAnimCacheEntry::GetRotationKeys(int /*unused*/) { return m_rotationKeysPtr; }

// 0x00480610
uint8_t* CAnimCacheEntry::GetTranslationKeys(int /*unused*/) { return m_translationKeysPtr; }

// 0x00480620
uint16_t* CAnimCacheEntry::GetRotationOffsets() { return m_rotationOffsets; }

// 0x00480630
uint16_t* CAnimCacheEntry::GetTranslationOffsets() { return m_translationOffsets; }

// 0x00480640
void CAnimCacheEntry::SetReleaseCallback(void(__cdecl* callback)(void*), void* arg)
{
    m_releaseCallback = callback;
    m_releaseArg = arg;
}

// --- the cache ---

// 0x00480660
CClipStore* __cdecl CClipStore::Pull() { return Cache(); }

// 0x00480D10
void __cdecl CClipStore::Fabricate(uint32_t heapSize, uint32_t granularity, int type)
{
    if (Cache())
        return;
    Hub()->CreateLabeledZone(kAnimHeap, heapSize, "AnimHeap", 0); // "AnimHeap"
    DebugOut::Printf("Initializing CAnimCacheManager at %dk\n", heapSize >> 10); // "Initializing CAnimCacheManager at %dk\n"
    auto* cache = static_cast<CClipStore*>(MemSys::AllocZeroed(sizeof(CClipStore)));
    if (cache) {
        std::memset(cache->m_slotArray, 0, sizeof(cache->m_slotArray));
        cache->m_takenCount = 0;
        cache->m_granularity = 0;
        cache->m_kindTag = 0;
        cache->m_unknownD8 = 0;
        cache->m_num = 0;
        cache->m_evictUnused = false;
    }
    Cache() = cache;
    cache->PrepareAll(granularity, type);
}

// 0x00481340
void __cdecl CClipStore::Teardown()
{
    if (CClipStore* cache = Cache()) {
        cache->DestroyBody();
        MemSys::Dealloc(cache);
    }
    Cache() = nullptr;
    Hub()->ReleaseLabeledZone(kAnimHeap, 1);
}

// 0x00480940
uint32_t __cdecl CClipStore::HeapAvailable()
{
    PushAnimHeap();
    uint32_t available = Hub()->Obtainable();
    PopHeap();
    return available;
}

// 0x004809F0
void CClipStore::PrepareAll(uint32_t granularity, int type)
{
    for (CAnimCacheEntry*& entry : m_slotArray) {
        entry = static_cast<CAnimCacheEntry*>(MemSys::AllocZeroed(sizeof(CAnimCacheEntry)));
        if (entry)
            entry->MakeNew();
    }
    m_granularity = granularity;
    m_kindTag = type;
}

// 0x004812F0
void CClipStore::DestroyBody()
{
    for (CAnimCacheEntry*& entry : m_slotArray) {
        if (entry) {
            entry->DestroyBody();
            MemSys::Dealloc(entry);
            entry = nullptr;
        }
    }
    m_takenCount = 0;
    m_granularity = 0;
    m_kindTag = 0;
}

// 0x00480800
int CClipStore::FetchKind() { return m_kindTag; }

// 0x00480670
uint32_t CClipStore::FetchRecordBytes(uint32_t /*key*/, int rotationKeys, int translationKeys, int type)
{
    uint8_t rotation, translation;
    KeySizes(type, rotation, translation);
    uint32_t size = rotation * rotationKeys + translation * translationKeys;
    return (size / m_granularity + 1) * m_granularity;
}

// 0x00480980
CAnimCacheEntry* CClipStore::AddRow(uint32_t key, int16_t bones, int rotationKeys, int translationKeys, int type)
{
    int i = 0;
    for (;; ++i) {
        CAnimCacheEntry* entry = m_slotArray[i];
        if (entry->m_lookupKey == key)
            return nullptr;
        if (entry->m_kindTag == 0)
            break;
        if (i + 1 >= kEntries)
            return nullptr;
    }
    uint32_t allocated = m_slotArray[i]->Carve(key, bones, rotationKeys, translationKeys, type);
    m_takenCount += allocated;
    if (!allocated)
        return nullptr;
    ++m_num;
    return m_slotArray[i];
}

// 0x004806C0
uint32_t CClipStore::FindOldest(uint32_t size, uint32_t* users)
{
    uint32_t key = 0;
    uint32_t oldest = FrameClock::GetTime();
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_allocated >= size && entry->m_lastUsed < oldest && !(entry->m_flagBits & CAnimCacheEntry::kKeep)) {
            *users = entry->m_flagBits & CAnimCacheEntry::kUserMask;
            oldest = entry->m_lastUsed;
            key = entry->m_lookupKey;
        }
    }
    return key;
}

// 0x00480ED0
bool CClipStore::FreeOldest(uint32_t maxUsers, uint32_t size)
{
    uint32_t users = 999;
    uint32_t key = FindOldest(size, &users);
    if (maxUsers > users && key) {
        DropOut(key);
        return true;
    }
    return false;
}

// 0x00480DB0
void CClipStore::Drain(bool unusedOnly)
{
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_kindTag && (!unusedOnly || !(entry->m_flagBits & CAnimCacheEntry::kUserMask)))
            m_takenCount -= entry->Release();
    }
}

// 0x00480DF0
void CClipStore::DropOut(uint32_t key)
{
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_lookupKey == key) {
            --m_num;
            m_takenCount -= entry->Release();
        }
    }
}

// 0x00480E30
int CClipStore::AddUser(uint32_t key)
{
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_lookupKey == key) {
            int slot = entry->NewSlot();
            if (slot == -1)
                return -1;
            entry->m_flagBits = WithUsers(entry->m_flagBits, 1);
            return slot;
        }
    }
    return -1;
}

// 0x00480E80
void CClipStore::RemoveUser(uint32_t key, int slot)
{
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_lookupKey == key) {
            entry->FreeSlot(slot);
            if (!(entry->m_flagBits & CAnimCacheEntry::kUserMask) && m_evictUnused)
                DropOut(key);
        }
    }
}

// 0x00480F20
int CClipStore::Defragment()
{
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_kindTag && !(entry->m_flagBits & CAnimCacheEntry::kUserMask))
            m_takenCount -= entry->Release();
    }
    uint32_t total = 0;
    int withData = 0;
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_payload) {
            total += entry->m_allocated;
            ++withData;
        }
    }
    auto* copy = static_cast<uint8_t*>(MemSys::Alloc(total));
    auto* oldData = static_cast<uint8_t**>(MemSys::Alloc(withData * 4));
    if (!copy || !oldData) {
        if (copy)
            MemSys::Dealloc(copy);
        for (CAnimCacheEntry* entry : m_slotArray) {
            if (entry->m_kindTag)
                m_takenCount -= entry->Release();
        }
        return 0;
    }
    // The keys copied out, in order...
    uint8_t* to = copy;
    uint8_t** old = oldData;
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_payload) {
            CopyForwards(to, entry->m_payload, entry->m_allocated);
            to += entry->m_allocated;
            *old++ = entry->m_payload;
        }
    }
    // ...every entry's freed and its slots cleared, then allocated again (lowest first)...
    uint32_t offset = 0;
    PushAnimHeap();
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_kindTag) {
            Hub()->Dealloc(entry->m_payload);
            entry->m_payload = nullptr;
            for (CAnimCacheSlot* slot : entry->m_slotsPtr) {
                if (slot)
                    slot->Clear();
            }
        }
    }
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_kindTag)
            entry->m_payload = static_cast<uint8_t*>(MemSys::Alloc(entry->m_allocated));
    }
    PopHeap();
    // ...and the keys copied back, the pointers into them moved with them.
    old = oldData;
    for (CAnimCacheEntry* entry : m_slotArray) {
        if (entry->m_kindTag) {
            CopyForwards(entry->m_payload, copy + offset, entry->m_allocated);
            offset += entry->m_allocated;
            int moved = static_cast<int>(entry->m_payload - *old++);
            entry->m_rotationKeysPtr += moved;
            entry->m_translationKeysPtr += moved;
        }
    }
    MemSys::Dealloc(copy);
    MemSys::Dealloc(oldData);
    return 0;
}

} // namespace Gfx
