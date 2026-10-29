#pragma once

#include <cstddef>
#include <cstdint>

namespace Mth {
class Quat;
class Vector;
} // namespace Mth

// An animation's frames (Gfx::CBonedAnimFrameData, 0x3C bytes - the .ska asset; constructor 0x00482AF0, destructor
// 0x00482B30, its loaders 0x00484200 / 0x00484370, the compressed frames' sampler 0x00484760 are in the code before
// these): per bone a list of rotation and translation keys, either plain (8 bytes a key: a time in frames, then the
// value) or compressed (a stream decoded key by key - 0x00482C20 / 0x00482E70) - those decoded once into the animation
// cache (the manager 0x006F92B0) when used. The members here are from the disassembly of 0x00484EB0-0x004859E8.
namespace Gfx {

class CBonedAnimFrameData {
public:
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

    uint32_t m_unknown0;       // +0x00 (the data's +8)
    uint32_t m_flags;          // +0x04
    int m_numBones;            // +0x08
    uint8_t* mp_unknownC;      // +0x0C (given to the rotation decoder)
    uint8_t* mp_rotKeys;       // +0x10
    uint8_t* mp_transKeys;     // +0x14
    uint8_t* mp_keyCounts;     // +0x18 plain: per bone the rotation and translation key counts (bytes, or shorts
                               // with flag 0x400000)
    uint32_t m_unknown1C;      // +0x1C
    uint32_t* mp_boneBits;     // +0x20 flag 0x80000: the bones used (bit arrays from +4)
    uint8_t* mp_cache;         // +0x24 the animation cache's entry
    void* mp_customKeys;       // +0x28
    uint16_t* mp_rotSizes;     // +0x2C compressed: per bone its rotation keys' bytes
    uint16_t* mp_transSizes;   // +0x30 and its translation keys'
    int16_t m_numRotKeys;      // +0x34
    int16_t m_numTransKeys;    // +0x36
    int16_t m_numCustomKeys;   // +0x38
    int16_t m_cacheBits;       // +0x3A (bits 1-8: the cache's priority)
};
static_assert(offsetof(CBonedAnimFrameData, mp_transSizes) == 0x30 && sizeof(CBonedAnimFrameData) == 0x3C,
              "layout must match the original");

} // namespace Gfx
