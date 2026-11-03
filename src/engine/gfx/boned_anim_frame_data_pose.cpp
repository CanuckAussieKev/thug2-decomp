// An animation's frames (see boned_anim_frame_data.h): from the disassembly of 0x00484EB0-0x004859E8.

#include "engine/gfx/boned_anim_frame_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/gfx/anim_blend.h"
#include "engine/gfx/anim_cache.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "formats/anim_format.h"
#include "formats/byte_stream.h"
#include "engine/objects/walk_hang.h"
#include "platform/stopwatch.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include "engine/core/no_destroy.h"

namespace gfx {

// (the bytes of a key: 8 for a short one, 16 for a full one, 20 for the quaternions' longest form)
constexpr int kLongKeyBytes = 16;
constexpr int kQuatKeyBytes = 20;
constexpr int kPartialMaskBits = 32; // a partial clip's bone bits, a word each
constexpr int kMaxCachedClips = 0x32;
constexpr int kMaxBones = 60;
constexpr int kClipFlagsHiShift = 11; // the clip flag that moves to the cache flags
constexpr int kCacheFlagsHiShift = 13;
constexpr int kCacheFlagsKeepMask = 0xDFFF;
constexpr int kClipHeaderBytes = 0xC;

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
KeyCursor decode_rotation(KeyCursor cursor, void* key, uint32_t flags, std::span<const uint8_t> table)
{
    return decompress_quat_key(cursor, static_cast<ShortKey*>(key), flags, table);
}
KeyCursor decode_translation(KeyCursor cursor, void* key, uint32_t /*unused*/)
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

// NOT ORIGINAL grouping - boned_anim_frame_data_pose.cpp's state (the original's globals). Made on first use, never
// destroyed (boned_anim_frame_data_pose_state()).
struct BonedAnimFrameDataPoseState {
    uint32_t caches_built_{};
    uint64_t last_cache_vblank_{};
};

BonedAnimFrameDataPoseState& boned_anim_frame_data_pose_state()
{
    static core::NoDestroy<BonedAnimFrameDataPoseState> state;
    return state.get();
}

// The vblank count when caches were last built (0x006FA2E0) and how many have been since (0x006FA2D8).
uint64_t& last_cache_vblank() { return boned_anim_frame_data_pose_state().last_cache_vblank_; }
uint32_t& caches_built() { return boned_anim_frame_data_pose_state().caches_built_; }

// A plain key's time: its first short (rotation keys: 15 bits, signed).
int rotation_key_time(const uint8_t* key)
{
    return static_cast<int16_t>(static_cast<int16_t>(anim_format::key_time_word(key) << 1) >> 1);
}
int translation_key_time(const uint8_t* key) { return static_cast<int16_t>(anim_format::key_time_word(key)); }

// Where a bone's keys start in the run and how many there are.
struct KeyRun {
    size_t keys{};
    int count{};
};

// The pair of keys of `count` around `time` (each key 8 bytes) and how far between: the last key - itself twice, 0.
// Without one, `from` / `to` / `fraction` stay as they were.
void find_keys(std::span<const uint8_t> run, KeyRun key_run, float time, int (*keyframe_time)(const uint8_t*),
               const uint8_t*& from, const uint8_t*& to, float& fraction)
{
    const auto keys = key_run.keys;
    const auto count = key_run.count;
    for (int i = 0; i < count; ++i) {
        const size_t at = keys + (static_cast<size_t>(i) * 8);
        const uint8_t* key = &core::at(run, at);
        if (i == count - 1) {
            from = key;
            to = key;
            fraction = 0.0f;
            return;
        }
        const double t0 = keyframe_time(key);
        if (!(t0 <= time))
            continue;
        const uint8_t* after = &core::at(run, at + kShortKeyBytes);
        const double t1 = keyframe_time(after);
        if (!(t1 >= time))
            continue;
        from = key;
        to = after;
        fraction = static_cast<float>((time - t0) / (t1 - t0));
        return;
    }
}

} // namespace

// 0x00484EB0
bool JointClipFrames::apply_additive(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations,
                                     void* /*unused*/, float weight, const FrameSource* mask) const
{
    const std::span<const uint8_t> rot_run = rotation_key_run();
    const std::span<const uint8_t> trans_run = translation_key_run();
    const std::span<const uint16_t> rot_sizes = rotation_sizes();
    const std::span<const uint16_t> trans_sizes = translation_sizes();
    size_t rot_keys = 0;
    size_t trans_keys = 0;
    const uint32_t mask_bit = mask && mask->resume() ? 1u << (mask->mask_bit() & 31) : 0u;
    const std::span<const uint32_t> bone_masks = mask_bit && mask->bone_masks() ? std::span<const uint32_t>(mask->bone_masks(), static_cast<size_t>(bone_count_)) : std::span<const uint32_t>();
    for (int i = 0; i < bone_count_; ++i) {
        bool skip = mask_bit ? (core::at(bone_masks, i) & mask_bit) != 0 : false;
        if (flag_bits_ & kBoneBits)
            skip = !(core::at(partial_bone_run(), 1 + (i / kPartialMaskBits)) & (1u << (i % kPartialMaskBits)));
        if (!skip) {
            maths::QuatRotation& rotation = core::at(rotations, i);
            if (core::at(rot_sizes, i)) {
                std::array<uint8_t, kShortKeyBytes> key{};
                std::array<uint8_t, kQuatKeyBytes> decoded{};
                decode_rotation(KeyCursor(rot_run, static_cast<std::ptrdiff_t>(rot_keys)), key.data(), flag_bits_, own_key_table());
                rotation_to_quat_b(decoded.data(), key.data());
                maths::QuatRotation added = rotation;
                const FloatKey decoded_key = anim_format::float_key_at(decoded.data());
                const maths::QuatRotation turn{.x = std::get<0>(decoded_key.value), .y = std::get<1>(decoded_key.value), .z = std::get<2>(decoded_key.value),
                                               .w = std::get<3>(decoded_key.value)};
                added *= turn;
                maths::QuatRotation to = added;
                std::array<float, 4> blended{};
                interpolate_rotation(blended, std::span<const float, 4>(rotation.data(), 4), std::span<float, 4>(to.data(), 4),
                                     weight);
                rotation = {.x = std::get<0>(blended), .y = std::get<1>(blended), .z = std::get<2>(blended), .w = std::get<3>(blended)};
            }
            const uint32_t flags = flag_bits_;
            if (!(flags & kTranslationKeys) || core::at(trans_sizes, i)) {
                maths::Vec4& translation = core::at(translations, i);
                std::array<uint8_t, kShortKeyBytes> key{};
                decode_translation(KeyCursor(trans_run, static_cast<std::ptrdiff_t>(trans_keys)), key.data(), flags);
                ShortKey decoded_key{};
                std::memcpy(&decoded_key, key.data(), sizeof(decoded_key));
                const std::array<int16_t, 3>& values = decoded_key.value;
                const double x = std::get<0>(values) * static_cast<double>(kKeyScale);
                auto y = static_cast<float>(std::get<1>(values) * static_cast<double>(kKeyScale));
                auto z = static_cast<float>(std::get<2>(values) * static_cast<double>(kKeyScale));
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
        rot_keys += core::at(rot_sizes, i);
        trans_keys += core::at(trans_sizes, i);
    }
    return true;
}

// 0x00485250
bool JointClipFrames::apply_additive_if_compressed(std::span<maths::QuatRotation> rotations,
                                                   std::span<maths::Vec4> translations, void* unused, float weight,
                                                   const FrameSource* mask) const
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
    const uint32_t size = manager->fetch_record_bytes(bone_count_, {.rotation = static_cast<int>(quat_key_count_), .translation = static_cast<int>(trans_key_count_)}, format);
    const int priority = static_cast<int16_t>(static_cast<int16_t>(bits_ << 7) >> 8);
    if (manager->num() == kMaxCachedClips || ClipStore::heap_available() < size) {
        if (manager->free_oldest({.max_users_ = static_cast<uint32_t>(priority), .size = size}))
            cache_slot_ = nullptr;
    }
    AnimCacheEntry* entry =
        manager->add_row(this, static_cast<int16_t>(bone_count_),
                         static_cast<int>(quat_key_count_), static_cast<int>(trans_key_count_), format);
    if (!entry)
        return;
    std::array<uint16_t, kMaxBones>& rot_offsets = entry->rotation_offsets();
    std::array<uint16_t, kMaxBones>& trans_offsets = entry->translation_offsets();
    const uint32_t rot_size = entry->get_rotation_key_size();
    const uint32_t trans_size = entry->get_translation_key_size();
    const int bones = bone_count_;
    // (the entry's block: the rotation keys at its start, the translation keys after them)
    const std::span<uint8_t> payload(entry->payload(), entry->allocated());
    const std::span<uint8_t> rot_data = payload.subspan(static_cast<size_t>(entry->get_rotation_keys(0) - entry->payload()));
    const std::span<uint8_t> trans_data = payload.subspan(static_cast<size_t>(entry->get_translation_keys(0) - entry->payload()));
    // (one buffer for a rotation key and a converted translation: the latter's bytes 2-3 are the former's)
    alignas(4) std::array<uint8_t, kLongKeyBytes> key{};
    alignas(4) std::array<uint8_t, kLongKeyBytes> quat_a{};
    alignas(4) std::array<uint8_t, kQuatKeyBytes> quat_b{};
    alignas(4) std::array<uint8_t, kShortKeyBytes> translation{};
    // A bone's keys' size (the original read the word after the last bone's too, at the end: never used).
    auto size_after = [bones](std::span<const uint16_t> sizes, uint32_t bone) -> uint16_t {
        return bone + 1 < ints::as_unsigned(bones) ? core::at(sizes, bone + 1) : 0;
    };

    // The rotations: each bone's keys one after another, the bones' starts in rot_offsets
    std::span<const uint16_t> sizes = rotation_sizes();
    KeyCursor cursor(rotation_key_run());
    KeyCursor end = cursor + core::at(sizes, 0);
    uint32_t offset = 0;
    uint32_t bone = 0;
    core::at(rot_offsets, 0) = 0;
    core::at(trans_offsets, 0) = 0;
    for (;;) {
        if (core::at(sizes, bone) == 0) {
            for (bool again = true; again;
                 again = static_cast<uint16_t>(bone) != ints::as_unsigned(bones) && core::at(sizes, bone) == 0) {
                end = cursor + size_after(sizes, bone);
                ++bone;
                core::at(rot_offsets, static_cast<uint16_t>(bone)) = static_cast<uint16_t>(offset);
            }
            if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
                break;
        }
        cursor = decode_rotation(cursor, key.data(), flag_bits_, own_key_table());
        std::span<const uint8_t> source = key;
        if (format == 3) {
            rotation_to_quat_a(quat_a.data(), key.data());
            source = quat_a;
        } else if (format == 4) {
            rotation_to_quat_b(quat_b.data(), key.data());
            source = quat_b;
        }
        core::copy_into(source.first(rot_size), rot_data.subspan(static_cast<uint16_t>(offset), rot_size));
        offset += rot_size;
        if (cursor != end)
            continue;
        end = cursor + size_after(sizes, bone);
        ++bone;
        core::at(rot_offsets, static_cast<uint16_t>(bone)) = static_cast<uint16_t>(offset);
        if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
            break;
    }

    // The translations
    sizes = translation_sizes();
    cursor = KeyCursor(translation_key_run());
    end = cursor + core::at(sizes, 0);
    offset = 0;
    bone = 0;
    for (;;) {
        if (core::at(sizes, bone) == 0) {
            for (bool again = true; again;
                 again = static_cast<uint16_t>(bone) != ints::as_unsigned(bones) && core::at(sizes, bone) == 0) {
                end = cursor + size_after(sizes, bone);
                ++bone;
                core::at(trans_offsets, static_cast<uint16_t>(bone)) = static_cast<uint16_t>(offset);
            }
            if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
                break;
        }
        cursor = decode_translation(cursor, translation.data(), flag_bits_);
        std::span<const uint8_t> source = translation;
        if (format >= 3 && format <= 4) {
            ShortKey decoded_key{};
            std::memcpy(&decoded_key, translation.data(), sizeof(decoded_key));
            const std::array<int16_t, 3>& values = decoded_key.value;
            std::memcpy(key.data(), translation.data(), 2);
            for (int i = 0; i < 3; ++i) {
                auto value = static_cast<float>(core::at(values, i) * static_cast<double>(kKeyScale));
                std::memcpy(&core::at(key, 4 + (i * 4)), &value, 4);
            }
            source = key;
        }
        core::copy_into(source.first(trans_size), trans_data.subspan(static_cast<uint16_t>(offset), trans_size));
        offset += trans_size;
        if (cursor != end)
            continue;
        end = cursor + size_after(sizes, bone);
        ++bone;
        core::at(trans_offsets, static_cast<uint16_t>(bone)) = static_cast<uint16_t>(offset);
        if (static_cast<uint16_t>(bone) == ints::as_unsigned(bones))
            break;
    }

    cache_slot_ = entry;
    entry->set_release_callback([](void* data) { JointClipFrames::forget_cache_entry(static_cast<JointClipFrames*>(data)); },
                                this); // (the entry's owner and its release callback)
    uint16_t& cache_flags = entry->flag_bits();
    cache_flags = static_cast<uint16_t>(((flag_bits_ >> kClipFlagsHiShift) & 1) << kCacheFlagsHiShift | (cache_flags & kCacheFlagsKeepMask));
    ++caches_built();
    last_cache_vblank() = frame_clock::fetch_retrace_count();
}

// 0x004856C0
bool JointClipFrames::read_in(const uint8_t* data, uint32_t size, bool /*unused*/)
{
    // The headers read and the sections laid out through anim_format.h. Not original - bounded by the animation's
    // size: one whose sections or keys run past it isn't loaded (the original used whatever its
    // counts and sizes said).
    anim_format::AnimHeader header;
    anim_format::AnimLayout layout;
    const bool valid = anim_format::lay_out_animation(data, size, &header, &layout) &&
                       anim_format::check_bone_keys(header, layout);
    util::ByteReader words(data, kClipHeaderBytes);
    words.u32();
    flag_bits_ = words.u32();
    duration_ = words.f32();
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
bool JointClipFrames::get_frames(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations,
                                 std::span<uint8_t> arg2, float time, int arg4, void* arg5)
{
    const uint32_t flags = flag_bits_;
    if (flags & kCompressed) {
        if (!(flags & kNoAutoCache) && !cache_slot_)
            build_cache(cache_format(cache_manager()));
        return fetch_pose(rotations, translations, arg2, time, arg4,
                          static_cast<FrameSource*>(arg5));
    }
    const float frame = time * kFramesPerSecond;
    const std::span<const uint8_t> rot_run = rotation_key_run();
    const std::span<const uint8_t> trans_run = translation_key_run();
    size_t rot_keys = 0;
    size_t trans_keys = 0;
    const uint8_t* rot_from = quat_keys_;
    const uint8_t* rot_to = quat_keys_;
    const uint8_t* trans_from = trans_keys_;
    const uint8_t* trans_to = trans_keys_;
    float rot_fraction = 0.0f;
    float trans_fraction = 0.0f;
    for (int i = 0; i < bone_count_; ++i) {
        int rot_count = 0;
        int trans_count = 0;
        if (flag_bits_ & kShortCounts) {
            rot_count = anim_format::wide_key_count(key_count_run(), i * 2);
            trans_count = anim_format::wide_key_count(key_count_run(), (i * 2) + 1);
        } else {
            rot_count = core::at(key_count_run(), i * 2);
            trans_count = core::at(key_count_run(), (i * 2) + 1);
        }
        find_keys(rot_run, {.keys = rot_keys, .count = rot_count}, frame, rotation_key_time, rot_from, rot_to, rot_fraction);
        interpolate_quat(std::span<float, 4>(core::at(rotations, i).data(), 4), rot_from, rot_to, rot_fraction,
                         (flag_bits_ & kOtherInterpolation) != 0);
        find_keys(trans_run, {.keys = trans_keys, .count = trans_count}, frame, translation_key_time, trans_from, trans_to, trans_fraction);
        interpolate_trans(std::span<float, 4>(core::at(translations, i).data(), 4), trans_from, trans_to, trans_fraction,
                          (flag_bits_ & kOtherInterpolation) != 0);
        rot_keys += static_cast<size_t>(rot_count) * kShortKeyBytes;
        trans_keys += static_cast<size_t>(trans_count) * kShortKeyBytes;
    }
    return true;
}

} // namespace gfx
