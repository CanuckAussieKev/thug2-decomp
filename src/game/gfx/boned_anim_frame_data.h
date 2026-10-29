#pragma once

// A boned animation's frame data (Gfx::CBonedAnimFrameData, 0x3C bytes - the animation asset's engine data, read
// from a .ska file): for each bone, quaternion keys and translation keys - in time order, each with its time in
// frames (60 a second) in the low 15 bits of its first word -, compressed or as floats, and the animation's events.
// Compressed keys come in two forms: three shorts (a quaternion's x, y, z in units of 1/16384 - w follows from them,
// its sign in bit 15 of the time word; a translation in units of 1/32), or bytes coded against shared 256-entry
// tables (0x006F9AD8 quaternions, 0x006F92D8 translations, loaded from files at start-up) or the animation's own.
// 0x00482AF0 constructs one; 0x004856C0 loads it (boned_anim_frame_data_pose.cpp).

#include <cstddef>
#include <cstdint>

namespace Mth {
class Quat;
class Vector;
} // namespace Mth

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

// A frame source (made by 0x0048BFC0 for an animation controller; the fields used here): where each bone's key
// decompression got to, and which bones the skeleton's level of detail leaves out.
struct CFrameSource {
    uint32_t m_unknown00[2];
    const uint8_t* mp_quatAt[64];    // +0x008
    const uint8_t* mp_transAt[64];   // +0x108
    bool m_resume;                   // +0x208 start each bone's search where it last ended
    uint8_t m_pad209[3];
    const uint32_t* mp_boneMasks;    // +0x20C (a word for each bone)
    int m_maskBit;                   // +0x210
};
static_assert(offsetof(CFrameSource, mp_transAt) == 0x108 && offsetof(CFrameSource, m_maskBit) == 0x210,
              "layout must match the original");

// An entry of the frame cache (0x006F92B0, 50 of them; its code isn't rewritten here - only the key searches): an
// animation's keys decompressed into float keys, quaternions then translations in one block, with search cursors
// for the controllers using it.
class CFrameCacheEntry {
public:
    // The keys of `bone` around `frame`: `first` / `second` (the same key when there's only one, or past the last),
    // starting from the cursor `cursor` holds when it still fits (-1: none) and leaving it there; -2 while the
    // entry is being filled (bit 15 of its flags). Returns `cursor`. Each has its own quirks, kept as the original.
    // 0x00482FB0 / 0x00483210: quaternion / translation keys, searching forwards.
    int FindQuatKeys(int bone, float frame, uint8_t** first, uint8_t** second, int cursor);
    int FindTransKeys(int bone, float frame, uint8_t** first, uint8_t** second, int cursor);
    // 0x00483470 / 0x004836C0: the same when the animation runs backwards (a cursor can step back one key).
    int FindQuatKeysBackwards(int bone, float frame, uint8_t** first, uint8_t** second, int cursor);
    int FindTransKeysBackwards(int bone, float frame, uint8_t** first, uint8_t** second, int cursor);

    void* mp_data;                   // +0x00 the frame data it holds
    uint32_t m_unknown04[4];         // (+0x04: the frame it was last used)
    uint32_t m_size;                 // +0x14 the keys' size
    uint8_t* mp_quatKeys;            // +0x18
    uint8_t* mp_transKeys;           // +0x1C
    uint32_t m_unknown20[2];
    uint8_t** mp_cursors[18];        // +0x28 (each: 60 quaternion cursors, 60 translation cursors, ...)
    uint8_t m_quatStride;            // +0x70
    uint8_t m_transStride;           // +0x71
    uint16_t m_quatOffsets[60];      // +0x72
    uint16_t m_transOffsets[60];     // +0xEA
    uint16_t m_boneCount;            // +0x162
    uint16_t m_flags;                // +0x164 (0x4000 searching, 0x8000 not ready)
};
static_assert(offsetof(CFrameCacheEntry, mp_cursors) == 0x28 && offsetof(CFrameCacheEntry, m_quatOffsets) == 0x72 &&
                  offsetof(CFrameCacheEntry, m_transOffsets) == 0xEA && offsetof(CFrameCacheEntry, m_flags) == 0x164,
              "layout must match the original");

class CBonedAnimFrameData {
public:
    // 0x00484200: the pointers into a compressed animation's data (`data`: the header - bones, key counts, events,
    // the two key blocks' sizes -, each bone's key sizes, the own table, the partial bones, the keys, the events,
    // each part 4-aligned); false if an event can't be made (0x00486680).
    bool SetUpCompressed(uint8_t* data);
    // 0x00484370: the same for an animation of float keys (bone names with flag 0x1000000, each bone's key counts -
    // bytes, or shorts with 0x400000 -, keys of 16 bytes with 0x8000000 / 0x1000000, else 8).
    bool SetUpFloatKeys(uint8_t* data);
    // 0x004844C0: each bone's rotation and translation at `time` (seconds) from float keys (a key list's last key
    // alone; between two; a bone where neither fits keeps the previous bone's pair). True. `unused` / `unused2` are
    // the callers' (valid flags, a cache index) - not read.
    bool GetFloatKeyPose(float* rotations, float* translations, void* unused, float time, int unused2);
    // 0x00484760: the same from compressed keys - through the frame cache's entry when it has one (its search
    // cursor `cursor`; backwards when the cache says time went back), else decompressing from the bone's last
    // position in `source`; bones the source's mask or the partial-animation bits leave out are marked invalid in
    // `valid`. True.
    bool GetPose(float* rotations, float* translations, uint8_t* valid, float time, int cursor, CFrameSource* source);
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

    // The events after the keys (`header`'s fourth word: how many), then their count - not when one fails.
    bool ReadEvents(const uint32_t* header, uint8_t* at);

    // 0x00484EB0: an additive pose: each bone not masked out - by `mask` (a skeleton's bone mask: +0x208 on, +0x20C the
    // bones' masks, +0x210 the bit) or by the frames' own bit array (flag 0x80000) - gets its first rotation key added
    // (pose rotation * key, then slerped there by `weight`) and its first translation key (a weight of the way to
    // the translation plus the key - with flags 0x4000 and 0x20000, twice the translation). True. (`unused` isn't.)
    bool ApplyAdditive(Mth::Quat* rotations, Mth::Vector* translations, void* unused, float weight, uint8_t* mask);
    // 0x00485250: the same for compressed frames (flag 0x800000; a tail jump), else false.
    bool ApplyAdditiveIfCompressed(Mth::Quat* rotations, Mth::Vector* translations, void* unused, float weight,
                                   uint8_t* mask);
    // 0x00485270: the compressed keys decoded into an animation cache entry in `format` (3 and 4: rotations as
    // quaternions - two decoders -, translations as floats; others as they come) - at most 10 a frame (the vblank
    // count 0x006FA2E0 and the count 0x006FA2D8). Older entries are freed first when the cache is short of room.
    void BuildCache(int format);
    // 0x004856C0: loaded from the .ska's data (flags +4, bones +8, then the keys - compressed 0x10000000 or 0x800000
    // ones by their loaders; plain ones not at all: false), then cached at once with flag 0x800. Whether loaded.
    bool Load(const uint8_t* data, uint32_t size, bool unused);
    // 0x00485730: each bone's rotation and translation at `time` (seconds, at 60 frames a second) - the pair of keys
    // around it and how far between (the last key: both it, 0) given to 0x00483B30 / 0x00483C40 (slerped / lerped;
    // flags 0x9000000: their other way). A bone without keys around the time reuses the bone before's pair (the
    // original's: its pointers aren't reset). Compressed frames are cached first (unless flag 0x1000) and sampled by
    // 0x00484760 (a tail jump with all the arguments). True.
    bool GetFrames(Mth::Quat* rotations, Mth::Vector* translations, uint8_t* arg2, float time, int arg4, void* arg5);

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
