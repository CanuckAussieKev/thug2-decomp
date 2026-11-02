#pragma once

// A boned animation's frame data (gfx::JointClipFrames, 0x3C bytes - the animation asset's engine data, read
// from a .ska file): for each bone, quaternion keys and translation keys - in time order, each with its time in
// frames (60 a second) in the low 15 bits of its first word -, compressed or as floats, and the animation's events.
// Compressed keys come in two forms: three shorts (a quaternion's x, y, z in units of 1/16384 - w follows from them,
// its sign in bit 15 of the time word; a translation in units of 1/32), or bytes coded against shared 256-entry
// tables (0x006F9AD8 quaternions, 0x006F92D8 translations, loaded from files at start-up) or the animation's own.
// 0x00482AF0 constructs one; 0x004856C0 loads it (boned_anim_frame_data_pose.cpp).

#include <cstddef>
#include <cstdint>

#include <gsl/pointers>
#include <array>
#include <span>
#include "formats/anim_format.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/gfx/frame_source.h"

namespace maths {
class QuatRotation;
class Vec4;
} // namespace maths

namespace gfx {

// The four floats of element `index` in a run of per-bone quadruples (rotations, translations).
inline std::span<float, 4> quad_at(std::span<float> floats, size_t index)
{
    return std::span<float, 4>(floats.subspan(index * 4, 4));
}

struct AnimCacheEntry;

class UserClipKey;

// A key as three shorts after the time word (8 bytes).
struct ShortKey {
    uint16_t time; // low 15 bits: the time in frames; bit 15: a quaternion's w is negative
    std::array<int16_t, 3> value;
};
static_assert(sizeof(ShortKey) == 8, "layout must match the original");

// A key with floats (formats/anim_format.h).
using FloatKey = anim_format::FloatKey;

// 0x00482970 (cdecl): 256 entries of four shorts read from `file` into `table`; false at the first short short of
// a read.
bool read_key_table(void* file, std::span<int16_t, 1024> table);

// A position in a run of key bytes (the clip's rotation or translation keys).
using KeyCursor = core::Cursor<const uint8_t>;
// An animation's own quaternion table (flag 0x2000): 256 entries of three shorts.
constexpr size_t kOwnKeyTableSize = 256 * 6;
// 0x004829F0 / 0x00482A30 (cdecl): the quaternion / translation table loaded from the file `name`.
bool load_quat_table(const char* name);
bool load_trans_table(const char* name);

// 0x00482C20 (cdecl): a compressed quaternion key at `data` into `out` (`flags`: the animation's - 0x100 a time
// word of its own, 0x2000 its own table `table` (6-byte entries), 0x8000 bytes for shorts, 0x10000 a byte names a
// table entry's w for a single value); returns the next key.
KeyCursor decompress_quat_key(KeyCursor data, ShortKey* out, uint32_t flags, std::span<const uint8_t> table);
// 0x00482E70 (cdecl): a compressed translation key (a flag byte: 0x80 from the shared table, 0x40 the time in its
// low six bits); returns the next key.
KeyCursor decompress_trans_key(KeyCursor data, ShortKey* out);

// 0x00482A70 (cdecl): a short key's time word and x, y, z as floats (no w).
void expand_quat_key(FloatKey* out, const ShortKey* in);
// 0x00483D00 (cdecl): a short key as a float quaternion key, w worked out (none: the identity).
void expand_quat_key_with_w(FloatKey* out, const ShortKey* in);

// 0x004838D0 / 0x00483990 (cdecl): the quaternion (w from x, y, z) / the translation (w 1) of a short key, or of a
// float key's x, y, z when `floats`.
void get_quat(const void* key, std::span<float, 4> out, bool floats);
void get_trans(const void* key, std::span<float, 4> out, bool floats);
// 0x00483A00 / 0x00483AE0 (cdecl): the same for short keys only (all three 0: the identity).
void get_short_quat(const ShortKey* key, std::span<float, 4> out);
void get_short_trans(const ShortKey* key, std::span<float, 4> out);
// 0x00483B30 / 0x00483C40 / 0x00483E10 / 0x00484080 (cdecl): the value a fraction `t` of the way from key `a` to key
// `b` (exactly 0 or 1: that key's own) - quaternions slerped (0x0047D470), translations lerped (0x0043D990).
void interpolate_quat(std::span<float, 4> out, const void* a, const void* b, float t, bool floats);
void interpolate_trans(std::span<float, 4> out, const void* a, const void* b, float t, bool floats);
void interpolate_short_quat(std::span<float, 4> out, const ShortKey* a, const ShortKey* b, float t);
void interpolate_short_trans(std::span<float, 4> out, const ShortKey* a, const ShortKey* b, float t);
// 0x00483F10 (cdecl): between two float quaternion keys, linearly, normalised (a length of 0 taken as 0.00001);
// pointing apart, `a` is first overwritten with -`b` (as the original - not -`a`).
void interpolate_float_quat(std::span<float, 4> out, FloatKey* a, const FloatKey* b, float t);
// 0x00484170 (cdecl): between two float translation keys, linearly (w 1).
void interpolate_float_trans(std::span<float, 4> out, const FloatKey* a, const FloatKey* b, float t);

// A frame source (made by 0x0048BFC0 for an animation controller; the fields used here): where each bone's key
// decompression got to, and which bones the skeleton's level of detail leaves out.
using FrameSource = render::FrameSource; // (its search cursors and bone masks: frame_source.h)

class JointClipFrames {
public:
    // 0x00484200: the pointers into a compressed animation's data (the format and its layout: anim_format.h - each
    // bone's key sizes, the own table, the partial bones, the keys, the events); false if an event can't be made
    // (0x00486680).
    bool set_up_compressed(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout);
    // 0x00484370: the same for an animation of float keys (bone names with flag 0x1000000, each bone's key counts -
    // bytes, or shorts with 0x400000 -, keys of 16 bytes with 0x8000000 / 0x1000000, else 8).
    bool set_up_float_keys(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout);
    // 0x004844C0: each bone's rotation and translation at `time` (seconds) from float keys (a key list's last key
    // alone; between two; a bone where neither fits keeps the previous bone's pair). True. `unused` / `unused2` are
    // the callers' (valid flags, a cache index) - not read.
    bool get_float_key_pose(std::span<float> rotations, std::span<float> translations, void* unused, float time, int unused2);
    // 0x00484760: the same from compressed keys - through the frame cache's entry when it has one (its search
    // cursor `cursor`; backwards when the cache says time went back), else decompressing from the bone's last
    // position in `source`; bones the source's mask or the partial-animation bits leave out are marked invalid in
    // `valid`. True.
    bool fetch_pose(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations, std::span<uint8_t> valid, float time, int cursor, FrameSource* source);
    // 0x00482AF0: empty.
    JointClipFrames();
    // 0x00482B30: its events deleted and their array freed; out of the frame cache (0x006F92B0) if it's in it.
    ~JointClipFrames();
    JointClipFrames(const JointClipFrames&) = delete;
    JointClipFrames& operator=(const JointClipFrames&) = delete;
    // 0x00482BA0: one more reference, and its index in the frame cache (-1: none).
    int acquire_cache_index();
    // 0x00482BD0: with a reference, one less, and the cache told the index is let go.
    void release_cache_index(int index);
    // 0x00482F00: bone `index`'s name.
    uint32_t fetch_joint_name(int index);
    // 0x00482F10: every event armed. True.
    bool arm_events();
    // 0x00482F40: the events between two times (in seconds, as frames) fired at `target` (`flag` passed to the
    // events' test). True.
    bool fire_events(float from, float to, void* target, int flag);

    // 0x00482B90 (cdecl; the frame cache's callback when it drops `data`'s entry): no cache entry.
    static void forget_cache_entry(JointClipFrames* data);

    // The events after the keys (the header's custom key count), then their count - not when one fails.
    bool read_events(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout);

    // 0x00484EB0: an additive pose: each bone not masked out - by `mask` (a frame source: with resume_, its bone masks
    // at its level of detail) or by the frames' own bit array (flag 0x80000) - gets its first rotation key added
    // (pose rotation * key, then slerped there by `weight`) and its first translation key (a weight of the way to
    // the translation plus the key - with flags 0x4000 and 0x20000, twice the translation). True. (`unused` isn't.)
    bool apply_additive(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations, void* unused,
                        float weight, const FrameSource* mask);
    // 0x00485250: the same for compressed frames (flag 0x800000; a tail jump), else false.
    bool apply_additive_if_compressed(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations,
                                      void* unused, float weight, const FrameSource* mask);
    // 0x00485270: the compressed keys decoded into an animation cache entry in `format` (3 and 4: rotations as
    // quaternions - two decoders -, translations as floats; others as they come) - at most 10 a frame (the vblank
    // count 0x006FA2E0 and the count 0x006FA2D8). Older entries are freed first when the cache is short of room.
    void build_cache(int format);
    // 0x004856C0: loaded from the .ska's data (flags +4, bones +8, then the keys - compressed 0x10000000 or 0x800000
    // ones by their loaders; plain ones not at all: false), then cached at once with flag 0x800. Whether loaded.
    bool read_in(const uint8_t* data, uint32_t size, bool unused);
    // 0x00485730: each bone's rotation and translation at `time` (seconds, at 60 frames a second) - the pair of keys
    // around it and how far between (the last key: both it, 0) given to 0x00483B30 / 0x00483C40 (slerped / lerped;
    // flags 0x9000000: their other way). A bone without keys around the time reuses the bone before's pair (the
    // original's: its pointers aren't reset). Compressed frames are cached first (unless flag 0x1000) and sampled by
    // 0x00484760 (a tail jump with all the arguments). True.
    bool get_frames(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations, std::span<uint8_t> arg2, float time, int arg4,
                    void* arg5);

    // NOT ORIGINAL - the runs the pointers below point into, derived from the layout (formats/anim_format.h): the
    // translation keys follow the rotation keys; compressed, each bone's keys take its size's bytes, float keys
    // 8 or 16 bytes each (flags 0x8000000 / 0x1000000). Nothing stored: the frame data keeps its 0x3C bytes.
    [[nodiscard]] std::span<const uint16_t> rotation_sizes() const;
    [[nodiscard]] std::span<const uint16_t> translation_sizes() const;
    [[nodiscard]] std::span<const uint8_t> rotation_key_run() const;
    [[nodiscard]] std::span<const uint8_t> translation_key_run() const;
    [[nodiscard]] std::span<const uint8_t> own_key_table() const;
    [[nodiscard]] std::span<const uint32_t> partial_bone_run() const;
    [[nodiscard]] std::span<UserClipKey*> events() const;
    [[nodiscard]] std::span<const uint32_t> bone_label_run() const;

    float duration_{0.0f}; // +0x00: seconds (the file's third word)
    uint32_t flag_bits_{};                    // +0x04
    int bone_count_{0};                       // +0x08
    uint8_t* key_table_{nullptr};             // +0x0C (flag 0x2000: the animation's own quaternion table)
    uint8_t* quat_keys_{nullptr};             // +0x10
    uint8_t* trans_keys_{nullptr};            // +0x14
    // NOT ORIGINAL - the per-bone key counts (two bytes a count, two counts a bone with the wide counts, else one).
    [[nodiscard]] std::span<const uint8_t> key_count_run() const
    {
        return {key_counts_, static_cast<size_t>(bone_count_) * ((flag_bits_ & 0x400000) ? 4 : 2)};
    }
    uint8_t* key_counts_{nullptr};            // +0x18
    uint32_t* bone_labels_{nullptr};          // +0x1C
    uint32_t* partial_bones_{nullptr};        // +0x20 (flag 0x80000: a count, then a bit for each bone)
    AnimCacheEntry* cache_slot_{nullptr};     // +0x24 its keys in the animation cache (null: not cached)
    gsl::owner<UserClipKey**> events_ptr_{nullptr}; // +0x28 the custom keys (create_custom_anim_key; each owned too)
    uint16_t* quat_sizes_{nullptr};                    // +0x2C (each bone's quaternion keys' size in bytes)
    uint16_t* trans_sizes_{nullptr};                   // +0x30
    int16_t quat_key_count_{};                // +0x34
    int16_t trans_key_count_{};               // +0x36
    int16_t event_count_{0};                  // +0x38
    uint16_t bits_{};                         // +0x3A (bits 1-8: references to its cache entry)
};
static_assert(offsetof(JointClipFrames, cache_slot_) == 0x24 && offsetof(JointClipFrames, bits_) == 0x3A &&
                  sizeof(JointClipFrames) == 0x3C,
              "layout must match the original");

inline constexpr float kTinyLength{0.00001f};
} // namespace gfx
