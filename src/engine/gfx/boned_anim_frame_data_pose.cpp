// An animation's frames (see boned_anim_frame_data.h): from the disassembly of 0x00484EB0-0x004859E8.

#include "engine/gfx/boned_anim_frame_data.h"

#include <cstdint>
#include <cstring>

#include "engine/gfx/anim_blend.h"
#include "engine/gfx/anim_cache.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "formats/byte_stream.h"
#include "engine/objects/walk_hang.h"
#include "platform/stopwatch.h"
#include "engine/core/ints.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace gfx {

namespace {

constexpr uint32_t kTwiceTranslation = 0x4000;
constexpr uint32_t kCacheOnLoad = 0x800;
constexpr uint32_t kNoAutoCache = 0x1000;
constexpr uint32_t kTranslationKeys = 0x20000;
constexpr uint32_t kBoneBits = 0x80000;
constexpr uint32_t kShortCounts = 0x400000;
constexpr uint32_t kCompressed = 0x800000;
constexpr uint32_t kOtherInterpolation = 0x9000000;
constexpr uint32_t kCompressedB = 0x10000000;
constexpr float kKeyScale = 1.0f / 32.0f; // 0x006487CC: a compressed translation's units
constexpr float kFramesPerSecond = 60.0f; // 0x00645EA0
constexpr int kCachesPerFrame = 10;

// The animation cache (the manager 0x006F92B0 through its getter) and its entries' calls (none rewritten yet).
void* cache_manager() { return ClipStore::get_pull(); }
int cache_format(void* manager) { return static_cast<ClipStore*>(manager)->fetch_kind(); }

// The compressed keys' decoders: the next rotation key (0x00482C20) / translation key (0x00482E70) into `key`,
// returning where the next one starts; a rotation key as a quaternion (0x00482A70, 0x00483D00 - the quaternion at +4
// of its output).
const uint8_t* decode_rotation(const uint8_t* cursor, void* key, uint32_t flags, const uint8_t* table)
{
    return decompress_quat_key(cursor, static_cast<ShortKey*>(key), flags, table);
}
const uint8_t* decode_translation(const uint8_t* cursor, void* key, uint32_t)
{
    return decompress_trans_key(cursor, static_cast<ShortKey*>(key));
}
void rotation_to_quat_a(void* out, const void* key)
{
    expand_quat_key(static_cast<FloatKey*>(out), static_cast<const ShortKey*>(key));
}
void rotation_to_quat_b(void* out, const void* key)
{
    expand_quat_key_with_w(static_cast<FloatKey*>(out), static_cast<const ShortKey*>(key));
}

uint32_t g_caches_built;
uint64_t g_last_cache_vblank;

// The vblank count when caches were last built (0x006FA2E0) and how many have been since (0x006FA2D8).
uint64_t& last_cache_vblank() { return g_last_cache_vblank; }
uint32_t& caches_built() { return g_caches_built; }

// A plain key's time: its first short (rotation keys: 15 bits, signed).
int rotation_key_time(const uint8_t* key)
{
    return static_cast<int16_t>(static_cast<int16_t>(*raw::as<const uint16_t>(key) << 1) >> 1);
}
int translation_key_time(const uint8_t* key) { return *raw::as<const int16_t>(key); }

// The pair of keys of `count` around `time` (each key 8 bytes) and how far between: the last key - itself twice, 0.
// Without one, `from` / `to` / `fraction` stay as they were.
void find_keys(uint8_t* keys, int count, float time, int (*keyframe_time)(const uint8_t*), uint8_t*& from, uint8_t*& to,
               float& fraction)
{
    for (int i = 0; i < count; ++i) {
        uint8_t* key = keys + i * 8;
        if (i == count - 1) {
            from = key;
            to = key;
            fraction = 0.0f;
            return;
        }
        const double t0 = keyframe_time(key);
        if (!(t0 <= time))
            continue;
        const double t1 = keyframe_time(key + 8);
        if (!(t1 >= time))
            continue;
        from = key;
        to = key + 8;
        fraction = static_cast<float>((time - t0) / (t1 - t0));
        return;
    }
}

} // namespace

// 0x00484EB0
bool JointClipFrames::apply_additive(maths::QuatRotation* rotations, maths::Vec4* translations, void*, float weight,
                                     const FrameSource* mask)
{
    const uint8_t* rot_keys = quat_keys_;
    const uint8_t* trans_keys = trans_keys_;
    const uint16_t* rot_sizes = quat_sizes_;
    const uint16_t* trans_sizes = trans_sizes_;
    const uint32_t mask_bit = mask && mask->resume_ ? 1u << (mask->mask_bit_ & 31) : 0u;
    for (int i = 0; i < bone_count_; ++i) {
        bool skip = mask_bit ? (mask->bone_masks_[i] & mask_bit) != 0 : false;
        if (flag_bits_ & kBoneBits)
            skip = !(partial_bones_[1 + i / 32] & (1u << (i % 32)));
        if (!skip) {
            maths::QuatRotation& rotation = rotations[i];
            if (*rot_sizes) {
                uint8_t key[8];
                uint8_t decoded[20];
                decode_rotation(rot_keys, key, flag_bits_, key_table_);
                rotation_to_quat_b(decoded, key);
                maths::QuatRotation added = rotation;
                added *= *raw::field_at<maths::QuatRotation>(decoded, 4);
                maths::QuatRotation to = added;
                float blended[4];
                const float* result = interpolate_rotation(blended, &rotation.x, &to.x, weight);
                rotation = {result[0], result[1], result[2], result[3]};
            }
            const uint32_t flags = flag_bits_;
            if (!(flags & kTranslationKeys) || *trans_sizes) {
                maths::Vec4& translation = translations[i];
                uint8_t key[8];
                decode_translation(trans_keys, key, flags);
                const auto* values = raw::field_at<const int16_t>(key, 2);
                const double x = values[0] * static_cast<double>(kKeyScale);
                auto y = static_cast<float>(values[1] * static_cast<double>(kKeyScale));
                auto z = static_cast<float>(values[2] * static_cast<double>(kKeyScale));
                maths::Vec4 target{};
                if ((flag_bits_ & kTwiceTranslation) && (flag_bits_ & kTranslationKeys)) {
                    auto z_sum = static_cast<float>(static_cast<double>(z) + translation.z);
                    auto w_sum = static_cast<float>(static_cast<double>(translation.w) + 1.0f);
                    target.x = static_cast<float>((x + translation.x) + translation.x);
                    target.y = static_cast<float>((static_cast<double>(y) + translation.y) + translation.y);
                    target.z = static_cast<float>(static_cast<double>(z_sum) + translation.z);
                    target.w = static_cast<float>(static_cast<double>(w_sum) + translation.w);
                } else {
                    target.x = static_cast<float>(x + translation.x);
                    target.y = static_cast<float>(static_cast<double>(y) + translation.y);
                    target.z = static_cast<float>(static_cast<double>(z) + translation.z);
                    target.w = static_cast<float>(static_cast<double>(translation.w) + 1.0f);
                }
                maths::Vec4 moved{};
                translation = *walk::get_lerp(&moved, &translation, &target, weight);
            }
        }
        rot_keys += *rot_sizes++;
        trans_keys += *trans_sizes++;
    }
    return true;
}

// 0x00485250
bool JointClipFrames::apply_additive_if_compressed(maths::QuatRotation* rotations, maths::Vec4* translations,
                                                   void* unused, float weight, const FrameSource* mask)
{
    if (!(flag_bits_ & kCompressed))
        return false;
    return apply_additive(rotations, translations, unused, weight, mask);
}

// 0x00485270
void JointClipFrames::build_cache(int format)
{
    if (caches_built() >= kCachesPerFrame) {
        if (frame_clock::fetch_retrace_count() == last_cache_vblank())
            return;
        caches_built() = 0;
        last_cache_vblank() = frame_clock::fetch_retrace_count();
    } else if (frame_clock::fetch_retrace_count() != last_cache_vblank()) {
        caches_built() = 0;
        last_cache_vblank() = frame_clock::fetch_retrace_count();
    }
    ClipStore* manager = ClipStore::get_pull();
    const uint32_t size = manager->fetch_record_bytes(bone_count_, static_cast<int>(quat_key_count_),
                                                      static_cast<int>(trans_key_count_), format);
    const int priority = static_cast<int16_t>(static_cast<int16_t>(bits_ << 7) >> 8);
    if (manager->num_ == 0x32 || ClipStore::heap_available() < size) {
        if (manager->free_oldest(priority, size))
            cache_slot_ = nullptr;
    }
    AnimCacheEntry* entry =
        manager->add_row(cell::of(this), static_cast<int16_t>(bone_count_),
                         static_cast<int>(quat_key_count_), static_cast<int>(trans_key_count_), format);
    auto* cache = raw::bytes(entry);
    if (!cache)
        return;
    auto* rot_offsets = entry->get_rotation_offsets();
    auto* trans_offsets = entry->get_translation_offsets();
    const uint32_t rot_size = entry->get_rotation_key_size();
    const uint32_t trans_size = entry->get_translation_key_size();
    const int bones = bone_count_;
    uint8_t* rot_data = entry->get_rotation_keys(0);
    uint8_t* trans_data = entry->get_translation_keys(0);
    // (one buffer for a rotation key and a converted translation: the latter's bytes 2-3 are the former's)
    alignas(4) uint8_t key[16];
    alignas(4) uint8_t quat_a[16];
    alignas(4) uint8_t quat_b[20];
    alignas(4) uint8_t translation[8];

    // The rotations: each bone's keys one after another, the bones' starts in rot_offsets
    const uint16_t* sizes = quat_sizes_;
    const uint8_t* cursor = quat_keys_;
    const uint8_t* end = cursor + *sizes;
    uint32_t offset = 0;
    uint32_t bone = 0;
    rot_offsets[0] = 0;
    trans_offsets[0] = 0;
    for (;;) {
        if (*sizes == 0) {
            do {
                end = cursor + sizes[1];
                ++sizes;
                ++bone;
                rot_offsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
            } while (static_cast<uint16_t>(bone) != ints::as_unsigned(bones) && *sizes == 0);
            if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
                break;
        }
        cursor = decode_rotation(cursor, key, flag_bits_, key_table_);
        const uint8_t* source = key;
        if (format == 3) {
            rotation_to_quat_a(quat_a, key);
            source = quat_a;
        } else if (format == 4) {
            rotation_to_quat_b(quat_b, key);
            source = quat_b;
        }
        std::memcpy(rot_data + static_cast<uint16_t>(offset), source, rot_size);
        offset += rot_size;
        if (cursor != end)
            continue;
        end = cursor + sizes[1];
        ++sizes;
        ++bone;
        rot_offsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
        if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
            break;
    }

    // The translations
    sizes = trans_sizes_;
    cursor = trans_keys_;
    end = cursor + *sizes;
    offset = 0;
    bone = 0;
    for (;;) {
        if (*sizes == 0) {
            do {
                end = cursor + sizes[1];
                ++sizes;
                ++bone;
                trans_offsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
            } while (static_cast<uint16_t>(bone) != ints::as_unsigned(bones) && *sizes == 0);
            if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
                break;
        }
        cursor = decode_translation(cursor, translation, flag_bits_);
        const uint8_t* source = translation;
        if (format >= 3 && format <= 4) {
            const auto* values = raw::field_at<const int16_t>(translation, 2);
            std::memcpy(key, translation, 2);
            for (int i = 0; i < 3; ++i) {
                auto value = static_cast<float>(values[i] * static_cast<double>(kKeyScale));
                std::memcpy(key + 4 + i * 4, &value, 4);
            }
            source = key;
        }
        std::memcpy(trans_data + static_cast<uint16_t>(offset), source, trans_size);
        offset += trans_size;
        if (cursor != end)
            continue;
        end = cursor + sizes[1];
        ++sizes;
        ++bone;
        trans_offsets[static_cast<uint16_t>(bone)] = static_cast<uint16_t>(offset);
        if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
            break;
    }

    cache_slot_ = cache;
    entry->set_release_callback([](void* data) { JointClipFrames::forget_cache_entry(static_cast<JointClipFrames*>(data)); },
                                this); // (the entry's owner and its release callback)
    uint16_t& cache_flags = *raw::field_at<uint16_t>(cache_slot_, 0x164);
    cache_flags = static_cast<uint16_t>(((flag_bits_ >> 11) & 1) << 13 | (cache_flags & 0xDFFF));
    ++caches_built();
    last_cache_vblank() = frame_clock::fetch_retrace_count();
}

// 0x004856C0
bool JointClipFrames::read_in(const uint8_t* data, uint32_t size, bool)
{
    // The headers read and the sections laid out through anim_format.h. Not original - bounded by the animation's
    // size (0: not known): one whose sections or keys run past it isn't loaded (the original used whatever its
    // counts and sizes said).
    anim_format::AnimHeader header;
    anim_format::AnimLayout layout;
    const bool valid = anim_format::lay_out_animation(data, size, &header, &layout) &&
                       (size == 0 || anim_format::check_bone_keys(header, layout));
    util::ByteReader words(data, 0xC);
    words.u32();
    flag_bits_ = words.u32();
    unknown00_ = words.u32();
    bool loaded = false;
    if (!valid)
        flag_bits_ &= ~(kCompressedB | kCompressed | kCacheOnLoad); // (nothing to read or cache)
    if (flag_bits_ & kCompressedB)
        loaded = set_up_float_keys(header, layout);
    else if (flag_bits_ & kCompressed)
        loaded = set_up_compressed(header, layout);
    if (flag_bits_ & kCacheOnLoad)
        build_cache(cache_format(cache_manager()));
    return loaded;
}

// 0x00485730
bool JointClipFrames::get_frames(maths::QuatRotation* rotations, maths::Vec4* translations, uint8_t* arg2, float time,
                                 int arg4, void* arg5)
{
    const uint32_t flags = flag_bits_;
    if (flags & kCompressed) {
        if (!(flags & kNoAutoCache) && !cache_slot_)
            build_cache(cache_format(cache_manager()));
        return fetch_pose(raw::as<float>(rotations), raw::as<float>(translations), arg2, time, arg4,
                          static_cast<FrameSource*>(arg5));
    }
    const float frame = time * kFramesPerSecond;
    uint8_t* rot_keys = quat_keys_;
    uint8_t* trans_keys = trans_keys_;
    uint8_t* rot_from = rot_keys;
    uint8_t* rot_to = rot_keys;
    uint8_t* trans_from = trans_keys;
    uint8_t* trans_to = trans_keys;
    float rot_fraction = 0.0f;
    float trans_fraction = 0.0f;
    for (int i = 0; i < bone_count_; ++i) {
        int rot_count = 0;
        int trans_count = 0;
        if (flag_bits_ & kShortCounts) {
            rot_count = raw::as<int16_t>(key_counts_)[i * 2];
            trans_count = raw::as<int16_t>(key_counts_)[i * 2 + 1];
        } else {
            rot_count = key_counts_[i * 2];
            trans_count = key_counts_[i * 2 + 1];
        }
        find_keys(rot_keys, rot_count, frame, rotation_key_time, rot_from, rot_to, rot_fraction);
        interpolate_quat(raw::as<float>(rotations), rot_from, rot_to, rot_fraction,
                         (flag_bits_ & kOtherInterpolation) != 0);
        find_keys(trans_keys, trans_count, frame, translation_key_time, trans_from, trans_to, trans_fraction);
        interpolate_trans(raw::as<float>(translations), trans_from, trans_to, trans_fraction,
                          (flag_bits_ & kOtherInterpolation) != 0);
        rot_keys += rot_count * 8;
        rotations++;
        trans_keys += trans_count * 8;
        translations++;
    }
    return true;
}

} // namespace gfx
