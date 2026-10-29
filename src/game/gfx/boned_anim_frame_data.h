#pragma once

// A boned animation's frame data (Gfx::CBonedAnimFrameData, 0x3C bytes - the animation asset's engine data, read
// from a .ska file): for each bone, quaternion keys and translation keys - in time order, each with its time in
// frames (60 a second) in the low 15 bits of its first word -, compressed or as floats, and the animation's events.
// Compressed keys come in two forms: three shorts (a quaternion's x, y, z in units of 1/16384 - w follows from them,
// its sign in bit 15 of the time word; a translation in units of 1/32), or bytes coded against shared 256-entry
// tables (0x006F9AD8 quaternions, 0x006F92D8 translations, loaded from files at start-up) or the animation's own.
// 0x00482AF0 constructs one; 0x004856C0 loads it (not rewritten here).

#include <cstddef>
#include <cstdint>

namespace Gfx {

class CAnimEvent;

// A key as three shorts after the time word (8 bytes).
struct CShortKey {
    uint16_t time;   // low 15 bits: the time in frames; bit 15: a quaternion's w is negative
    int16_t value[3];
};
static_assert(sizeof(CShortKey) == 8, "layout must match the original");

// A key with floats (a quaternion's x, y, z, w or a translation's x, y, z after the time word).
struct CFloatKey {
    uint16_t time;
    uint16_t pad;
    float value[4];
};
static_assert(offsetof(CFloatKey, value) == 4, "layout must match the original");

// The shared tables (256 entries of four shorts).
inline constexpr uint32_t kQuatTable = 0x006F9AD8, kTransTable = 0x006F92D8;

// 0x00482970 (cdecl): 256 entries of four shorts read from `file` into `table`; false at the first short short of
// a read.
bool __cdecl ReadKeyTable(void* file, int16_t* table);
// 0x004829F0 / 0x00482A30 (cdecl): the quaternion / translation table loaded from the file `name`.
bool __cdecl LoadQuatTable(const char* name);
bool __cdecl LoadTransTable(const char* name);

// 0x00482C20 (cdecl): a compressed quaternion key at `data` into `out` (`flags`: the animation's - 0x100 a time
// word of its own, 0x2000 its own table `table` (6-byte entries), 0x8000 bytes for shorts, 0x10000 a byte names a
// table entry's w for a single value); returns the next key.
const uint8_t* __cdecl DecompressQuatKey(const uint8_t* data, CShortKey* out, uint32_t flags, const uint8_t* table);
// 0x00482E70 (cdecl): a compressed translation key (a flag byte: 0x80 from the shared table, 0x40 the time in its
// low six bits); returns the next key.
const uint8_t* __cdecl DecompressTransKey(const uint8_t* data, CShortKey* out);

// 0x00482A70 (cdecl): a short key's time word and x, y, z as floats (no w).
void __cdecl ExpandQuatKey(CFloatKey* out, const CShortKey* in);
// 0x00483D00 (cdecl): a short key as a float quaternion key, w worked out (none: the identity).
void __cdecl ExpandQuatKeyWithW(CFloatKey* out, const CShortKey* in);

// 0x004838D0 / 0x00483990 (cdecl): the quaternion (w from x, y, z) / the translation (w 1) of a short key, or of a
// float key's x, y, z when `floats`.
void __cdecl GetQuat(const void* key, float* out, bool floats);
void __cdecl GetTrans(const void* key, float* out, bool floats);
// 0x00483A00 / 0x00483AE0 (cdecl): the same for short keys only (all three 0: the identity).
void __cdecl GetShortQuat(const CShortKey* key, float* out);
void __cdecl GetShortTrans(const CShortKey* key, float* out);
// 0x00483B30 / 0x00483C40 / 0x00483E10 / 0x00484080 (cdecl): the value a fraction `t` of the way from key `a` to key
// `b` (exactly 0 or 1: that key's own) - quaternions slerped (0x0047D470), translations lerped (0x0043D990).
void __cdecl InterpolateQuat(float* out, const void* a, const void* b, float t, bool floats);
void __cdecl InterpolateTrans(float* out, const void* a, const void* b, float t, bool floats);
void __cdecl InterpolateShortQuat(float* out, const CShortKey* a, const CShortKey* b, float t);
void __cdecl InterpolateShortTrans(float* out, const CShortKey* a, const CShortKey* b, float t);
// 0x00483F10 (cdecl): between two float quaternion keys, linearly, normalised (a length of 0 taken as 0.00001);
// pointing apart, `a` is first overwritten with -`b` (as the original - not -`a`).
void __cdecl InterpolateFloatQuat(float* out, CFloatKey* a, const CFloatKey* b, float t);
// 0x00484170 (cdecl): between two float translation keys, linearly (w 1).
void __cdecl InterpolateFloatTrans(float* out, const CFloatKey* a, const CFloatKey* b, float t);

class CBonedAnimFrameData {
public:
    // 0x00482AF0: empty. Returns this.
    CBonedAnimFrameData* Construct();
    // 0x00482B30: its events deleted and their array freed; out of the frame cache (0x006F92B0) if it's in it.
    void DestroyBody();
    // 0x00482BA0: one more reference, and its index in the frame cache (-1: none).
    int AcquireCacheIndex();
    // 0x00482BD0: with a reference, one less, and the cache told the index is let go.
    void ReleaseCacheIndex(int index);
    // 0x00482F00: bone `index`'s name.
    uint32_t GetBoneName(int index);
    // 0x00482F10: every event armed. True.
    bool ArmEvents();
    // 0x00482F40: the events between two times (in seconds, as frames) fired at `target` (`flag` passed to the
    // events' test). True.
    bool FireEvents(float from, float to, void* target, int flag);

    // 0x00482B90 (cdecl; the frame cache's callback when it drops `data`'s entry): no cache entry.
    static void __cdecl ForgetCacheEntry(CBonedAnimFrameData* data);

    uint32_t m_unknown00;
    uint32_t m_flags;              // +0x04
    int m_boneCount;               // +0x08
    uint8_t* mp_keyTable;          // +0x0C (flag 0x2000: the animation's own quaternion table)
    uint8_t* mp_quatKeys;          // +0x10
    uint8_t* mp_transKeys;         // +0x14
    uint8_t* mp_keyCounts;         // +0x18
    uint32_t* mp_boneNames;        // +0x1C
    uint32_t* mp_partialBones;     // +0x20 (flag 0x80000: a count, then a bit for each bone)
    uint8_t* mp_cacheEntry;        // +0x24 (+4: the frame it was last used)
    CAnimEvent** mp_events;        // +0x28
    uint16_t* mp_quatSizes;        // +0x2C (each bone's quaternion keys' size in bytes)
    uint16_t* mp_transSizes;       // +0x30
    int16_t m_quatKeyCount;        // +0x34
    int16_t m_transKeyCount;       // +0x36
    int16_t m_eventCount;          // +0x38
    uint16_t m_bits;               // +0x3A (bits 1-8: references to its cache entry)
};
static_assert(offsetof(CBonedAnimFrameData, mp_cacheEntry) == 0x24 && offsetof(CBonedAnimFrameData, m_bits) == 0x3A &&
                  sizeof(CBonedAnimFrameData) == 0x3C,
              "layout must match the original");

} // namespace Gfx
