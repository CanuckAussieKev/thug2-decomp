// The animation cache (0x00480590-0x0048137B): see anim_cache.h. From the disassembly.

#include "game/gfx/anim_cache.h"

#include <intrin.h>

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

constexpr uint32_t kAnimHeap = 0x8F21A798; // AnimHeap
constexpr int kEntries = 50;
constexpr int kSlots = 18;

CAnimCache*& Cache() { return GameVariable<CAnimCache*>(0x006F92B0); }
Mem::PoolPair& SlotPools() { return GameVariable<Mem::PoolPair>(0x006F92B8); }

Mem::Manager* Manager() { return Mem::Manager::Get(); }

// The AnimHeap made current (allocations and frees in between go to it).
void PushAnimHeap() { Manager()->PushContext(Manager()->GetNamedHeap(kAnimHeap, 1)); }
void PopHeap() { Manager()->PopContext(); }

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
    for (CAnimCacheSlot*& slot : entry->mp_slots) {
        if (slot) {
            slot->Clear();
            if (slot)
                CAnimCacheSlot::Free(slot);
            slot = nullptr;
        }
    }
}

// The fields Release and the destructor empty.
void Empty(CAnimCacheEntry* entry)
{
    entry->mp_rotationKeys = nullptr;
    entry->mp_translationKeys = nullptr;
    entry->m_key = 0;
    entry->m_bones = 0;
    entry->m_rotationKeySize = 0;
    entry->m_translationKeySize = 0;
    entry->m_size = 0;
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
    if (entry->mp_data) {
        PushAnimHeap();
        Manager()->Free(entry->mp_data);
        PopHeap();
        entry->mp_data = nullptr;
    }
}

} // namespace

// --- the slots ---

// 0x00480A70
void __cdecl CAnimCacheSlot::Free(CAnimCacheSlot* slot)
{
    SlotPools().Free(slot);
}

void CAnimCacheSlot::Clear()
{
    std::memset(m_rotationKeys, 0, sizeof(m_rotationKeys));
    std::memset(m_translationKeys, 0, sizeof(m_translationKeys));
    m_time = 0x8000;
}

// --- an entry ---

void CAnimCacheEntry::Construct()
{
    m_flags &= 0xA000;
    mp_rotationKeys = nullptr;
    mp_translationKeys = nullptr;
    m_key = 0;
    m_bones = 0;
    m_rotationKeySize = 0;
    m_translationKeySize = 0;
    m_allocated = 0;
    m_size = 0;
    mp_data = nullptr;
    m_type = 0;
}

// 0x00480810
uint32_t CAnimCacheEntry::Allocate(uint32_t key, int16_t bones, int rotationKeys, int translationKeys, int type)
{
    m_flags |= kReleasing;
    m_bones = bones;
    m_type = type;
    KeySizes(type, m_rotationKeySize, m_translationKeySize);
    uint32_t granularity = Cache()->m_granularity;
    m_size = m_rotationKeySize * rotationKeys + m_translationKeySize * translationKeys;
    m_allocated = (m_size / granularity + 1) * granularity;
    PushAnimHeap();
    if (Manager()->Available() < m_allocated) {
        m_type = 0;
        PopHeap();
        return 0;
    }
    m_key = key;
    mp_data = static_cast<uint8_t*>(Mem::New(m_allocated));
    std::memset(mp_data, 0, m_allocated);
    mp_rotationKeys = mp_data;
    mp_translationKeys = mp_data + m_rotationKeySize * rotationKeys;
    PopHeap();
    m_flags &= ~(kBusy | kReleasing);
    m_lastUsed = 0;
    return m_allocated;
}

// 0x00480AD0
uint32_t CAnimCacheEntry::Release()
{
    uint16_t flags = m_flags;
    while (flags & kBusy) {
        // (forever, as the original: the flags were read once)
    }
    m_flags = flags | kReleasing;
    if (mp_releaseCallback) {
        mp_releaseCallback(mp_releaseArg);
        mp_releaseCallback = nullptr;
    }
    uint32_t allocated = m_allocated;
    m_flags &= kReleasing;
    Empty(this);
    FreeData(this);
    FreeSlots(this);
    m_type = 0;
    return allocated;
}

// 0x00481200
void CAnimCacheEntry::DestroyBody()
{
    m_flags &= ~(kKeep | kBusy);
    mp_releaseCallback = nullptr;
    Empty(this);
    FreeData(this);
    if (mp_releaseCallback) { // (never: cleared above)
        mp_releaseCallback(mp_releaseArg);
        mp_releaseCallback = nullptr;
    }
    FreeSlots(this);
    m_type = 0;
}

// 0x00480BF0
int CAnimCacheEntry::NewSlot()
{
    for (int i = 0; i < kSlots; ++i) {
        if (mp_slots[i])
            continue;
        auto* slot = static_cast<CAnimCacheSlot*>(SlotPools().Allocate(sizeof(CAnimCacheSlot)));
        if (slot)
            slot->m_time = 0x8000;
        mp_slots[i] = slot;
        slot->m_time &= 0x7FFF; // (null: a crash, as the original)
        std::memset(mp_slots[i]->m_rotationKeys, 0, sizeof(slot->m_rotationKeys));
        std::memset(mp_slots[i]->m_translationKeys, 0, sizeof(slot->m_translationKeys));
        return i;
    }
    return -1;
}

// 0x00480C90
void CAnimCacheEntry::FreeSlot(int index)
{
    if (!(m_flags & kUserMask))
        return;
    CAnimCacheSlot* slot = mp_slots[index];
    if (slot && index > 0) {
        slot->Clear();
        if (mp_slots[index])
            CAnimCacheSlot::Free(mp_slots[index]);
        mp_slots[index] = nullptr;
    }
    m_flags = WithUsers(m_flags, -1);
}

// 0x00480590
int16_t CAnimCacheEntry::SetSlotTime(int index, int16_t time)
{
    CAnimCacheSlot* slot = mp_slots[index];
    if (!slot)
        return 0;
    uint16_t old = slot->m_time;
    int last = static_cast<int16_t>(old << 1) >> 1; // (15 bits, signed)
    slot->m_time = static_cast<uint16_t>((old & 0x8000) | (static_cast<uint16_t>(time) & 0x7FFF));
    return static_cast<int16_t>(time - last);
}

// 0x004805E0
uint8_t CAnimCacheEntry::GetRotationKeySize() { return m_rotationKeySize; }

// 0x004805F0
uint8_t CAnimCacheEntry::GetTranslationKeySize() { return m_translationKeySize; }

// 0x00480600
uint8_t* CAnimCacheEntry::GetRotationKeys(int /*unused*/) { return mp_rotationKeys; }

// 0x00480610
uint8_t* CAnimCacheEntry::GetTranslationKeys(int /*unused*/) { return mp_translationKeys; }

// 0x00480620
uint16_t* CAnimCacheEntry::GetRotationOffsets() { return m_rotationOffsets; }

// 0x00480630
uint16_t* CAnimCacheEntry::GetTranslationOffsets() { return m_translationOffsets; }

// 0x00480640
void CAnimCacheEntry::SetReleaseCallback(void(__cdecl* callback)(void*), void* arg)
{
    mp_releaseCallback = callback;
    mp_releaseArg = arg;
}

// --- the cache ---

// 0x00480660
CAnimCache* __cdecl CAnimCache::Get() { return Cache(); }

// 0x00480D10
void __cdecl CAnimCache::Create(uint32_t heapSize, uint32_t granularity, int type)
{
    if (Cache())
        return;
    Manager()->InitNamedHeap(kAnimHeap, heapSize, reinterpret_cast<const char*>(0x0064875C), 0); // "AnimHeap"
    Dbg::Printf(reinterpret_cast<const char*>(0x00648734), heapSize >> 10); // "Initializing CAnimCacheManager at %dk\n"
    auto* cache = static_cast<CAnimCache*>(Mem::Calloc(sizeof(CAnimCache)));
    if (cache) {
        std::memset(cache->mp_entries, 0, sizeof(cache->mp_entries));
        cache->m_used = 0;
        cache->m_granularity = 0;
        cache->m_type = 0;
        cache->m_unknownD8 = 0;
        cache->m_count = 0;
        cache->m_evictUnused = false;
    }
    Cache() = cache;
    cache->Init(granularity, type);
}

// 0x00481340
void __cdecl CAnimCache::Destroy()
{
    if (CAnimCache* cache = Cache()) {
        cache->DestroyBody();
        Mem::Free(cache);
    }
    Cache() = nullptr;
    Manager()->DeleteNamedHeap(kAnimHeap, 1);
}

// 0x00480940
uint32_t __cdecl CAnimCache::HeapAvailable()
{
    PushAnimHeap();
    uint32_t available = Manager()->Available();
    PopHeap();
    return available;
}

// 0x004809F0
void CAnimCache::Init(uint32_t granularity, int type)
{
    for (CAnimCacheEntry*& entry : mp_entries) {
        entry = static_cast<CAnimCacheEntry*>(Mem::Calloc(sizeof(CAnimCacheEntry)));
        if (entry)
            entry->Construct();
    }
    m_granularity = granularity;
    m_type = type;
}

// 0x004812F0
void CAnimCache::DestroyBody()
{
    for (CAnimCacheEntry*& entry : mp_entries) {
        if (entry) {
            entry->DestroyBody();
            Mem::Free(entry);
            entry = nullptr;
        }
    }
    m_used = 0;
    m_granularity = 0;
    m_type = 0;
}

// 0x00480800
int CAnimCache::GetType() { return m_type; }

// 0x00480670
uint32_t CAnimCache::GetEntrySize(uint32_t /*key*/, int rotationKeys, int translationKeys, int type)
{
    uint8_t rotation, translation;
    KeySizes(type, rotation, translation);
    uint32_t size = rotation * rotationKeys + translation * translationKeys;
    return (size / m_granularity + 1) * m_granularity;
}

// 0x00480980
CAnimCacheEntry* CAnimCache::AddEntry(uint32_t key, int16_t bones, int rotationKeys, int translationKeys, int type)
{
    int i = 0;
    for (;; ++i) {
        CAnimCacheEntry* entry = mp_entries[i];
        if (entry->m_key == key)
            return nullptr;
        if (entry->m_type == 0)
            break;
        if (i + 1 >= kEntries)
            return nullptr;
    }
    uint32_t allocated = mp_entries[i]->Allocate(key, bones, rotationKeys, translationKeys, type);
    m_used += allocated;
    if (!allocated)
        return nullptr;
    ++m_count;
    return mp_entries[i];
}

// 0x004806C0
uint32_t CAnimCache::FindOldest(uint32_t size, uint32_t* users)
{
    uint32_t key = 0;
    uint32_t oldest = Tmr::GetTime();
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_allocated >= size && entry->m_lastUsed < oldest && !(entry->m_flags & CAnimCacheEntry::kKeep)) {
            *users = entry->m_flags & CAnimCacheEntry::kUserMask;
            oldest = entry->m_lastUsed;
            key = entry->m_key;
        }
    }
    return key;
}

// 0x00480ED0
bool CAnimCache::FreeOldest(uint32_t maxUsers, uint32_t size)
{
    uint32_t users = 999;
    uint32_t key = FindOldest(size, &users);
    if (maxUsers > users && key) {
        Remove(key);
        return true;
    }
    return false;
}

// 0x00480DB0
void CAnimCache::Flush(bool unusedOnly)
{
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_type && (!unusedOnly || !(entry->m_flags & CAnimCacheEntry::kUserMask)))
            m_used -= entry->Release();
    }
}

// 0x00480DF0
void CAnimCache::Remove(uint32_t key)
{
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_key == key) {
            --m_count;
            m_used -= entry->Release();
        }
    }
}

// 0x00480E30
int CAnimCache::AddUser(uint32_t key)
{
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_key == key) {
            int slot = entry->NewSlot();
            if (slot == -1)
                return -1;
            entry->m_flags = WithUsers(entry->m_flags, 1);
            return slot;
        }
    }
    return -1;
}

// 0x00480E80
void CAnimCache::RemoveUser(uint32_t key, int slot)
{
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_key == key) {
            entry->FreeSlot(slot);
            if (!(entry->m_flags & CAnimCacheEntry::kUserMask) && m_evictUnused)
                Remove(key);
        }
    }
}

// 0x00480F20
int CAnimCache::Defragment()
{
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_type && !(entry->m_flags & CAnimCacheEntry::kUserMask))
            m_used -= entry->Release();
    }
    uint32_t total = 0;
    int withData = 0;
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->mp_data) {
            total += entry->m_allocated;
            ++withData;
        }
    }
    auto* copy = static_cast<uint8_t*>(Mem::New(total));
    auto* oldData = static_cast<uint8_t**>(Mem::New(withData * 4));
    if (!copy || !oldData) {
        if (copy)
            Mem::Free(copy);
        for (CAnimCacheEntry* entry : mp_entries) {
            if (entry->m_type)
                m_used -= entry->Release();
        }
        return 0;
    }
    // The keys copied out, in order...
    uint8_t* to = copy;
    uint8_t** old = oldData;
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->mp_data) {
            CopyForwards(to, entry->mp_data, entry->m_allocated);
            to += entry->m_allocated;
            *old++ = entry->mp_data;
        }
    }
    // ...every entry's freed and its slots cleared, then allocated again (lowest first)...
    uint32_t offset = 0;
    PushAnimHeap();
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_type) {
            Manager()->Free(entry->mp_data);
            entry->mp_data = nullptr;
            for (CAnimCacheSlot* slot : entry->mp_slots) {
                if (slot)
                    slot->Clear();
            }
        }
    }
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_type)
            entry->mp_data = static_cast<uint8_t*>(Mem::New(entry->m_allocated));
    }
    PopHeap();
    // ...and the keys copied back, the pointers into them moved with them.
    old = oldData;
    for (CAnimCacheEntry* entry : mp_entries) {
        if (entry->m_type) {
            CopyForwards(entry->mp_data, copy + offset, entry->m_allocated);
            offset += entry->m_allocated;
            int moved = static_cast<int>(entry->mp_data - *old++);
            entry->mp_rotationKeys += moved;
            entry->mp_translationKeys += moved;
        }
    }
    Mem::Free(copy);
    Mem::Free(oldData);
    return 0;
}

} // namespace Gfx

REPLACE(0x00480590, &Gfx::CAnimCacheEntry::SetSlotTime);
REPLACE_SOURCE_ONLY(0x004805E0, &Gfx::CAnimCacheEntry::GetRotationKeySize);    // 4 bytes
REPLACE_SOURCE_ONLY(0x004805F0, &Gfx::CAnimCacheEntry::GetTranslationKeySize); // 4 bytes
REPLACE(0x00480600, &Gfx::CAnimCacheEntry::GetRotationKeys);
REPLACE(0x00480610, &Gfx::CAnimCacheEntry::GetTranslationKeys);
REPLACE_SOURCE_ONLY(0x00480620, &Gfx::CAnimCacheEntry::GetRotationOffsets);    // 4 bytes
REPLACE(0x00480630, &Gfx::CAnimCacheEntry::GetTranslationOffsets);
REPLACE(0x00480640, &Gfx::CAnimCacheEntry::SetReleaseCallback);
REPLACE(0x00480660, Gfx::CAnimCache::Get);
REPLACE(0x00480670, &Gfx::CAnimCache::GetEntrySize);
REPLACE(0x004806C0, &Gfx::CAnimCache::FindOldest);
REPLACE(0x00480800, &Gfx::CAnimCache::GetType);
REPLACE(0x00480810, &Gfx::CAnimCacheEntry::Allocate);
REPLACE(0x00480940, Gfx::CAnimCache::HeapAvailable);
REPLACE(0x00480980, &Gfx::CAnimCache::AddEntry);
REPLACE(0x004809F0, &Gfx::CAnimCache::Init);
REPLACE(0x00480A70, Gfx::CAnimCacheSlot::Free);
REPLACE(0x00480AD0, &Gfx::CAnimCacheEntry::Release);
REPLACE(0x00480BF0, &Gfx::CAnimCacheEntry::NewSlot);
REPLACE(0x00480C90, &Gfx::CAnimCacheEntry::FreeSlot);
REPLACE(0x00480D10, Gfx::CAnimCache::Create);
REPLACE(0x00480DB0, &Gfx::CAnimCache::Flush);
REPLACE(0x00480DF0, &Gfx::CAnimCache::Remove);
REPLACE(0x00480E30, &Gfx::CAnimCache::AddUser);
REPLACE(0x00480E80, &Gfx::CAnimCache::RemoveUser);
REPLACE(0x00480ED0, &Gfx::CAnimCache::FreeOldest);
REPLACE(0x00480F20, &Gfx::CAnimCache::Defragment);
REPLACE(0x00481200, &Gfx::CAnimCacheEntry::DestroyBody);
REPLACE(0x004812F0, &Gfx::CAnimCache::DestroyBody);
REPLACE(0x00481340, Gfx::CAnimCache::Destroy);
