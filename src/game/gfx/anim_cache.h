#pragma once

// The animation cache (CAnimCacheManager, the object at 0x006F92B0): animations' keys decompressed into the
// AnimHeap (0x8F21A798) - 50 entries, each holding one animation's rotation and translation keys and the key
// positions of up to 18 users (slots) playing it. Entries are given back oldest first when the heap runs short
// (the animation code at 0x00485270 does that) and, with m_evictUnused, as soon as nobody uses them.

#include <cstddef>
#include <cstdint>

namespace Gfx2D {

// A user's place in a cached animation (0x1E4 bytes, from the item pools 0x006F92B8): a key position per bone for
// the rotations and the translations, and its time.
struct CAnimCacheSlot {
    uint32_t m_rotationKeys[60];    // +0x000
    uint32_t m_translationKeys[60]; // +0x0F0
    uint16_t m_clock;                // +0x1E0 (15 bits, signed; bit 15 kept)
    uint16_t m_pad1E2;

    // 0x00480A70 (cdecl): back to whichever item pool holds it, or freed.
    static void __cdecl Dealloc(CAnimCacheSlot* slot);
    // Its key positions cleared and its time 0x8000 (the code that frees a slot does this first).
    void Clear();
};
static_assert(offsetof(CAnimCacheSlot, m_clock) == 0x1E0 && sizeof(CAnimCacheSlot) == 0x1E4,
              "layout must match the original");

// One cached animation (0x168 bytes).
struct CAnimCacheEntry {
    enum Flags : uint16_t {
        kUserMask = 0x1FFF,  // how many play it
        kKeep = 0x2000,      // never given back as the oldest
        kBusy = 0x4000,      // Release waits (forever) while it's set
        kReleasing = 0x8000,
    };

    // The constructor (inlined into 0x004809F0): empty, the flags' 0xA000 bits kept.
    void MakeNew();
    // 0x00480810: room for `rotationKeys` rotation and `translationKeys` translation keys of the cache's `type` (2: 8
    // bytes each, 3: 16 each, 4: 20 / 16, others nothing), rounded up past the cache's granularity, allocated on
    // the AnimHeap (zeroed); the animation `key`, `bones`. The size allocated, or 0 (not in use) when the heap
    // hasn't room.
    uint32_t Carve(uint32_t key, int16_t bones, int rotationKeys, int translationKeys, int type);
    // 0x00480AD0: given back: the release callback called, the keys freed, every slot cleared and freed; the size
    // it had. Waits forever while it's busy, as the original.
    uint32_t Release();
    // 0x00481200: the destructor body - the same without the wait and the callback (cleared before its check).
    void DestroyBody();
    // 0x00480BF0: a new slot in the first free place, its index (-1 with none free; a failed allocation crashes, as
    // in the original).
    int NewSlot();
    // 0x00480C90: a user gone (nothing with none): slot `index` cleared and freed (never slot 0).
    void FreeSlot(int index);
    // 0x00480590: slot `index`'s time set to `time` (15 bits); the difference from its last time (0 without the
    // slot).
    int16_t SetSlotTime(int index, int16_t time);
    // 0x004805E0 / 0x004805F0: the size of a rotation / translation key.
    uint8_t GetRotationKeySize();
    uint8_t GetTranslationKeySize();
    // 0x00480600 / 0x00480610 (the argument isn't used): the rotation / translation keys.
    uint8_t* GetRotationKeys(int unused);
    uint8_t* GetTranslationKeys(int unused);
    // 0x00480620 / 0x00480630: where each bone's rotation / translation keys start.
    uint16_t* GetRotationOffsets();
    uint16_t* GetTranslationOffsets();
    // 0x00480640: called with `arg` when the entry is given back.
    void SetReleaseCallback(void(__cdecl* callback)(void*), void* arg);

    uint32_t m_lookupKey;                      // +0x000 the animation (0: free)
    uint32_t m_lastUsed;                 // +0x004
    int m_kindTag;                          // +0x008 (0: not in use)
    uint8_t* m_payload;                    // +0x00C
    uint32_t m_allocated;                // +0x010
    uint32_t m_extent;                     // +0x014
    uint8_t* mp_rotationKeys;            // +0x018
    uint8_t* mp_translationKeys;         // +0x01C
    void(__cdecl* mp_releaseCallback)(void*); // +0x020
    void* mp_releaseArg;                 // +0x024
    CAnimCacheSlot* mp_slots[18];        // +0x028
    uint8_t m_rotationKeySize;           // +0x070
    uint8_t m_translationKeySize;        // +0x071
    uint16_t m_rotationOffsets[60];      // +0x072
    uint16_t m_translationOffsets[60];   // +0x0EA
    int16_t m_bones;                     // +0x162
    uint16_t m_flagBits;                    // +0x164
    uint16_t m_pad166;
};
static_assert(offsetof(CAnimCacheEntry, mp_slots) == 0x28 && offsetof(CAnimCacheEntry, m_rotationKeySize) == 0x70 &&
                  offsetof(CAnimCacheEntry, m_translationOffsets) == 0xEA &&
                  offsetof(CAnimCacheEntry, m_flagBits) == 0x164 && sizeof(CAnimCacheEntry) == 0x168,
              "layout must match the original");

class CClipStore {
public:
    // 0x00480660: the cache (0x006F92B0).
    static CClipStore* __cdecl Pull();
    // 0x00480D10: made once - the AnimHeap of `heapSize` bytes ("Initializing CAnimCacheManager at %dk"), then
    // PrepareAll(`granularity`, `type`) (on a null cache too: a crash, as in the original).
    static void __cdecl Fabricate(uint32_t heapSize, uint32_t granularity, int type);
    // 0x00481340: destroyed and freed, the AnimHeap deleted.
    static void __cdecl Teardown();
    // 0x00480940: what the AnimHeap has left.
    static uint32_t __cdecl HeapAvailable();

    // 0x004809F0: 50 new entries (null where out of memory), the granularity and type.
    void PrepareAll(uint32_t granularity, int type);
    // 0x004812F0: each entry destroyed and freed; nothing used.
    void DestroyBody();
    // 0x00480800
    int FetchKind();
    // 0x00480670: what an entry for that many keys of `type` would take (`key` isn't used).
    uint32_t FetchRecordBytes(uint32_t key, int rotationKeys, int translationKeys, int type);
    // 0x00480980: a new entry for `key` in the first entry not in use (none if `key` comes first or none is free, or
    // the heap hasn't room).
    CAnimCacheEntry* AddRow(uint32_t key, int16_t bones, int rotationKeys, int translationKeys, int type);
    // 0x004806C0: the key of the least recently used entry (before FrameClock::GetTime) of at least `size` that may go,
    // its users in `users` (0 with none).
    uint32_t FindOldest(uint32_t size, uint32_t* users);
    // 0x00480ED0: that entry given back if it has fewer than `maxUsers` users.
    bool FreeOldest(uint32_t maxUsers, uint32_t size);
    // 0x00480DB0: every entry in use given back (only those nobody uses with `unusedOnly`).
    void Drain(bool unusedOnly);
    // 0x00480DF0: every entry whose key is `key` given back (the count taken down for each).
    void DropOut(uint32_t key);
    // 0x00480E30: a user of `key`'s entry: its new slot's index, or -1.
    int AddUser(uint32_t key);
    // 0x00480E80: a user of `key`'s entries gone; an entry nobody uses then removed with m_evictUnused.
    void RemoveUser(uint32_t key, int slot);
    // 0x00480F20: the unused entries given back and the rest moved down the AnimHeap (through a copy on the default
    // heap; everything given back when that can't be had). Always 0.
    int Defragment();

    CAnimCacheEntry* m_slotArray[50]; // +0x00
    int m_num;                     // +0xC8
    uint32_t m_takenCount;                 // +0xCC bytes
    uint32_t m_granularity;          // +0xD0
    int m_kindTag;                      // +0xD4
    uint32_t m_unknownD8;
    bool m_evictUnused;              // +0xDC
    uint8_t m_padDD[3];
};
static_assert(offsetof(CClipStore, m_num) == 0xC8 && offsetof(CClipStore, m_evictUnused) == 0xDC &&
                  sizeof(CClipStore) == 0xE0,
              "layout must match the original");

} // namespace Gfx
