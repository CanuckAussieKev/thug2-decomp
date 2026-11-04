// A boned animation's frame data: its keys decompressed and interpolated, its events, its frame cache entry - see
// boned_anim_frame_data.h.

#include "engine/gfx/boned_anim_frame_data.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/anim_blend.h"
#include "formats/anim_format.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/custom_anim_key.h"
#include "engine/maths/maths.h"
#include "engine/maths/vec4.h"
#include "formats/byte_stream.h"
#include "engine/objects/walk_hang.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include <gsl/assert>
#include <gsl/pointers>

namespace gfx {

// The key tables' size in shorts (256 entries of four).
constexpr std::size_t kShortKeyTableSize = 1024;
constexpr int kBitsPerByte = 8;
// A key's time word: the sign (of the quaternion's w) in the top bit, the time below.
constexpr int kTimeSignBit = 0x8000;
constexpr int kTimeValueMask = 0x7FFF;
constexpr int kCompressedTimeMask = 0x7FF; // a compressed key's time has 11 bits
constexpr int kReferenceCountMask = 0x1FE; // the reference count's bits (steps of 2)
// A rotation key's flags (the clip's) and header bits.
constexpr int kQuatLongHeaderFlag = 0x100; // the key has a time word of its own before the header
constexpr int kQuatByteValuesFlag = 0x8000; // bytes moved up instead of shorts
constexpr int kQuatTableWFlag = 0x10000; // a byte names the w of an entry of the shared table
constexpr int kQuatFileTableFlag = 0x2000; // the clip's own table (6-byte entries)
constexpr int kFileTableEntryBytes = 6;
constexpr int kQuatValueBytes = 6; // three shorts
constexpr int kKeyCompressedBit = 0x4000;
constexpr int kKeyByteBitsMask = 0x3800;
constexpr int kKeyByteBitX = 0x2000;
constexpr int kKeyByteBitY = 0x1000;
constexpr int kKeyByteBitZ = 0x0800;
// A translation key's head byte.
constexpr int kTransShortTimeFlag = 0x40; // the time is in the head byte
constexpr int kTransShortTimeMask = 0x3F;
constexpr int kTransTableFlag = 0x80; // the value is an entry of the translation table
// Partial clips: one bit for each bone, 32 to a word.
constexpr int kPartialMaskBits = 32;
constexpr int kPartialMaskRound = 31;
constexpr int kJointClipKeepMask = 0xFE00;
constexpr int kBitsByteMask = 0xFF;
constexpr double kClipFrameRate = 60.0;
// The cached clip's flag_bits_.
constexpr int kFlagUnusable = 0x8000; // key queries fail
constexpr int kFlagQuerying = 0x4000; // set while a key query runs
constexpr int kFlagNoTransKeys = 0x20000; // a bone without translation keys has none
constexpr int kFlagWideKeyCounts = 0x400000;
constexpr int kFlagPartialBones = 0x80000;
constexpr int kFlagsOtherWay = 0x9000000; // the quaternions / translations are interpolated the other way
constexpr int kKeyBytes = 16;

namespace {
// NOT ORIGINAL grouping - boned_anim_frame_data.cpp's state (the original's globals). Made on first use, never
// destroyed (boned_anim_frame_data_state()).
struct BonedAnimFrameDataState {
    // The shared key tables (256 entries of four shorts), loaded from files at start-up.
    std::array<int16_t, 1024> quat_table_{};
    std::array<int16_t, kShortKeyTableSize> trans_table_{};
};

BonedAnimFrameDataState& boned_anim_frame_data_state()
{
    static core::NoDestroy<BonedAnimFrameDataState> state;
    return state.get();
}

constexpr double kQuatUnit = 1.0 / 16384.0; // (0x006487C8)
constexpr double kTransUnit = 1.0 / 32.0;   // (0x006487CC)

float get_scaled(int16_t value, double unit) { return static_cast<float>(value * unit); }

uint16_t get_word(KeyCursor cursor) { return static_cast<uint16_t>(cursor[0] | (cursor[1] << kBitsPerByte)); }
uint16_t get_word(std::span<const uint8_t> bytes, size_t at)
{
    return static_cast<uint16_t>(core::at(bytes, at) | (core::at(bytes, at + 1) << kBitsPerByte));
}

// The summed sizes of every bone's keys.
size_t total_size(std::span<const uint16_t> sizes)
{
    size_t total = 0;
    for (const uint16_t size : sizes) {
        total += size;
    }
    return total;
}

// The time word of a key at the head of `in`, as the original merges it (the sign bit, then the rest).
uint16_t time_word(uint16_t out, uint16_t in)
{
    auto merged = static_cast<uint16_t>((out & kTimeValueMask) | (in & kTimeSignBit));
    return static_cast<uint16_t>((merged & kTimeSignBit) | (in & kTimeValueMask));
}

// A rotation key's x, y and z (its w found from them).
struct QuatPart {
    float x{};
    float y{};
    float z{};
};

// w from x, y, z: the square root of what's left of 1 (x87 at 53 bits, in this order), negative with bit 15 of the
// time word.
float quat_w(QuatPart part, uint16_t time, bool nan_to_zero)
{
    const auto x = part.x;
    const auto y = part.y;
    const auto z = part.z;
    double left = 1.0 - (static_cast<double>(x) * x);
    left -= static_cast<double>(y) * y;
    left -= static_cast<double>(z) * z;
    // (0x004838D0 tests "below 0 or unordered" - NaN gives 0; the others only "below 0")
    if (nan_to_zero ? !(left >= 0.0) : left < 0.0) {
        left = 0.0;
    }
    const double w = std::sqrt(left);
    return static_cast<float>(((time & kTimeSignBit) != 0) ? -w : w);
}

// The frame cache (0x006F92B0) and its members (not rewritten here).
void* frame_cache() { return ClipStore::get_pull(); }
void cache_remove(void* cache, JointClipFrames* data)
{
    static_cast<ClipStore*>(cache)->drop_out(data);
}
int cache_acquire(void* cache, JointClipFrames* data)
{
    return static_cast<ClipStore*>(cache)->add_user(data);
}
void cache_release(void* cache, JointClipFrames* data, int index)
{
    static_cast<ClipStore*>(cache)->remove_user(data, index);
}
// The events' arming and firing.
void arm_event(UserClipKey* event, bool armed) { event->change_live(armed); }
bool fire_event(UserClipKey* event, void* target) { return event->process_if_active(target); }

uint16_t reference_bits(uint16_t bits, int change)
{
    const int count = (bits & 0xFFFE) + (2 * change);
    return static_cast<uint16_t>((bits & ~kReferenceCountMask) | (count & kReferenceCountMask));
}

} // namespace

// --- the shared tables --------------------------------------------------------------------------------------

// 0x00482970
bool read_key_table(void* file, std::span<int16_t, kShortKeyTableSize> table)
{
    for (int16_t& value : table) {
        if (file::get_take_in(&value, 2, 1, static_cast<file::Hdl>(file)) == 0) {
            return false;
        }
    }
    return true;
}

// 0x004829F0
bool load_quat_table(const char* name)
{
    bool loaded = false;
    file::Hdl file = file::unseal(name, "rb");
    if (file) {
        loaded = read_key_table(file, boned_anim_frame_data_state().quat_table_);
    }
    file::shut(file); // (even when it didn't open)
    return loaded;
}

// 0x00482A30
bool load_trans_table(const char* name)
{
    bool loaded = false;
    file::Hdl file = file::unseal(name, "rb");
    if (file) {
        loaded = read_key_table(file, boned_anim_frame_data_state().trans_table_);
    }
    file::shut(file);
    return loaded;
}

// --- decompressing keys -------------------------------------------------------------------------------------

// 0x00482C20
KeyCursor decompress_quat_key(KeyCursor data, ShortKey* out, uint32_t flags, std::span<const uint8_t> table)
{
    const auto& shared = boned_anim_frame_data_state().quat_table_;
    const uint16_t first = get_word(data);
    uint16_t header = 0;
    if ((flags & kQuatLongHeaderFlag) != 0U) {
        header = get_word(data + 2);
        out->time = static_cast<uint16_t>((header & kTimeSignBit) | (first & kTimeValueMask));
        data += 4;
    } else {
        header = first;
        out->time = first;
        data += 2;
    }
    // A short, a byte moved up, or (with the header's bit) a byte - in the 0x10000 form the w of that table entry.
    auto value = [&](uint16_t bit) -> int16_t {
        if (header & bit) {
            const uint8_t table_index = *data++;
            return (flags & kQuatTableWFlag) ? core::at(shared, (table_index * 4) + 3)
                                             : static_cast<int16_t>(table_index);
        }
        if (flags & kQuatByteValuesFlag) {
            return static_cast<int16_t>(*data++ << kBitsPerByte);
        }
        auto v = static_cast<int16_t>(get_word(data));
        data += 2;
        return v;
    };
    if ((header & kKeyCompressedBit) == 0) {
        // Uncompressed: three shorts (or three bytes moved up).
        if ((flags & kQuatByteValuesFlag) != 0U) {
            for (int i = 0; i < 3; ++i) {
                core::at(out->value, i) = static_cast<int16_t>(data[i] << kBitsPerByte);
            }
            data += 3;
        } else {
            for (int i = 0; i < 3; ++i) {
                core::at(out->value, i) = static_cast<int16_t>(get_word(data + 2 * i));
            }
            data += kQuatValueBytes;
        }
    } else {
        if ((header & kKeyByteBitsMask) == 0) {
            // All three from a table entry.
            const uint8_t table_index = *data++;
            if ((flags & kQuatFileTableFlag) != 0U) {
                for (int i = 0; i < 3; ++i) {
                    core::at(out->value, i) =
                        static_cast<int16_t>(get_word(table, (table_index * kFileTableEntryBytes) + (2 * i)));
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    core::at(out->value, i) = core::at(shared, (table_index * 4) + i);
                }
            }
        } else {
            core::at(out->value, 0) = value(kKeyByteBitX);
            core::at(out->value, 1) = value(kKeyByteBitY);
            core::at(out->value, 2) = value(kKeyByteBitZ);
        }
        // The time: 11 bits here (and the sign).
        out->time = static_cast<uint16_t>((out->time & kTimeSignBit) | (out->time & kCompressedTimeMask));
    }
    if ((flags & kQuatLongHeaderFlag) != 0U) {
        out->time = static_cast<uint16_t>((out->time & kTimeSignBit) | (first & kTimeValueMask));
    }
    return data;
}

// 0x00482E70
KeyCursor decompress_trans_key(KeyCursor data, ShortKey* out)
{
    const uint8_t head = *data++;
    if ((head & kTransShortTimeFlag) != 0) {
        out->time = static_cast<uint16_t>(head & kTransShortTimeMask);
    } else {
        out->time = get_word(data);
        data += 2;
    }
    if ((head & kTransTableFlag) != 0) {
        const int entry = *data++ * 4;
        for (int i = 0; i < 3; ++i) {
            core::at(out->value, i) = core::at(boned_anim_frame_data_state().trans_table_, entry + i);
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            core::at(out->value, i) = static_cast<int16_t>(get_word(data + 2 * i));
        }
        data += kQuatValueBytes;
    }
    return data;
}

// 0x00482A70
void expand_quat_key(FloatKey* out, const ShortKey* in)
{
    out->time = time_word(out->time, in->time);
    for (int i = 0; i < 3; ++i) {
        core::at(out->value, i) = get_scaled(core::at(in->value, i), kQuatUnit);
    }
}

// 0x00483D00
void expand_quat_key_with_w(FloatKey* out, const ShortKey* in)
{
    out->time = time_word(out->time, in->time);
    if ((core::at(in->value, 0) == 0) && (core::at(in->value, 1) == 0) && (core::at(in->value, 2) == 0)) {
        core::at(out->value, 0) = core::at(out->value, 1) = core::at(out->value, 2) = 0.0F;
        core::at(out->value, 3) = 1.0F;
        return;
    }
    const float x = get_scaled(core::at(in->value, 0), kQuatUnit);
    const float y = get_scaled(core::at(in->value, 1), kQuatUnit);
    const float z = get_scaled(core::at(in->value, 2), kQuatUnit);
    const float w = quat_w({.x = x, .y = y, .z = z}, in->time, false);
    core::at(out->value, 0) = x;
    core::at(out->value, 1) = y;
    core::at(out->value, 2) = z;
    core::at(out->value, 3) = w;
}

// --- a key's value ------------------------------------------------------------------------------------------

// 0x004838D0
void get_quat(const void* key, std::span<float, 4> out, bool floats)
{
    if (floats) {
        std::memcpy(out.data(), static_cast<const FloatKey*>(key)->value.data(), 3 * sizeof(float));
    } else {
        const ShortKey& short_key = *static_cast<const ShortKey*>(key);
        for (int i = 0; i < 3; ++i) {
            core::at(out, i) = get_scaled(core::at(short_key.value, i), kQuatUnit);
        }
    }
    core::at(out, 3) = quat_w({.x = core::at(out, 0), .y = core::at(out, 1), .z = core::at(out, 2)}, static_cast<const ShortKey*>(key)->time, true);
}

// 0x00483990
void get_trans(const void* key, std::span<float, 4> out, bool floats)
{
    if (floats) {
        std::memcpy(out.data(), static_cast<const FloatKey*>(key)->value.data(), 3 * sizeof(float));
    } else {
        const ShortKey& short_key = *static_cast<const ShortKey*>(key);
        for (int i = 0; i < 3; ++i) {
            core::at(out, i) = get_scaled(core::at(short_key.value, i), kTransUnit);
        }
    }
    core::at(out, 3) = 1.0F;
}

// 0x00483A00
void get_short_quat(const ShortKey* key, std::span<float, 4> out)
{
    if ((core::at(key->value, 0) == 0) && (core::at(key->value, 1) == 0) && (core::at(key->value, 2) == 0)) {
        core::at(out, 0) = core::at(out, 1) = core::at(out, 2) = 0.0F;
        core::at(out, 3) = 1.0F;
        return;
    }
    const float x = get_scaled(core::at(key->value, 0), kQuatUnit);
    const float y = get_scaled(core::at(key->value, 1), kQuatUnit);
    const float z = get_scaled(core::at(key->value, 2), kQuatUnit);
    core::at(out, 0) = x;
    core::at(out, 1) = y;
    core::at(out, 2) = z;
    core::at(out, 3) = quat_w({.x = x, .y = y, .z = z}, key->time, false);
}

// 0x00483AE0
void get_short_trans(const ShortKey* key, std::span<float, 4> out)
{
    for (int i = 0; i < 3; ++i) {
        core::at(out, i) = get_scaled(core::at(key->value, i), kTransUnit);
    }
    core::at(out, 3) = 1.0F;
}

// --- between two keys ---------------------------------------------------------------------------------------

// 0x00483B30
void interpolate_quat(std::span<float, 4> out, const void* a, const void* b, float blend, bool floats)
{
    if (blend == 0.0F) {
        get_quat(a, out, floats);
        return;
    }
    if (blend == 1.0F) {
        get_quat(b, out, floats);
        return;
    }
    std::array<float, 4> from = {{0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, 4> to = {{0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, 4> result{};
    get_quat(a, from, floats);
    get_quat(b, to, floats);
    interpolate_rotation(result, from, to, blend);
    std::memcpy(out.data(), result.data(), sizeof(result));
}

// 0x00483C40
void interpolate_trans(std::span<float, 4> out, const void* a, const void* b, float blend, bool floats)
{
    if (blend == 0.0F) {
        get_trans(a, out, floats);
        return;
    }
    if (blend == 1.0F) {
        get_trans(b, out, floats);
        return;
    }
    maths::Vec4 from{};
    maths::Vec4 to{};
    maths::Vec4 result{};
    get_trans(a, std::span<float, 4>(from.data(), 4), floats);
    get_trans(b, std::span<float, 4>(to.data(), 4), floats);
    const maths::Vec4* lerped = walk::get_lerp(&result, &from, &to, blend);
    std::memcpy(out.data(), lerped, sizeof(maths::Vec4));
}

// 0x00483E10
void interpolate_short_quat(std::span<float, 4> out, const ShortKey* a, const ShortKey* b, float blend)
{
    if (blend == 0.0F) {
        get_short_quat(a, out);
        return;
    }
    if (blend == 1.0F) {
        get_short_quat(b, out);
        return;
    }
    std::array<float, 4> from = {{0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, 4> to = {{0.0F, 0.0F, 0.0F, 1.0F}};
    std::array<float, 4> result{};
    get_short_quat(a, from);
    get_short_quat(b, to);
    interpolate_rotation(result, from, to, blend);
    std::memcpy(out.data(), result.data(), sizeof(result));
}

// 0x00484080
void interpolate_short_trans(std::span<float, 4> out, const ShortKey* a, const ShortKey* b, float blend)
{
    if (blend == 0.0F) {
        get_short_trans(a, out);
        return;
    }
    if (blend == 1.0F) {
        get_short_trans(b, out);
        return;
    }
    maths::Vec4 from{};
    maths::Vec4 to{};
    maths::Vec4 result{};
    get_short_trans(a, std::span<float, 4>(from.data(), 4));
    get_short_trans(b, std::span<float, 4>(to.data(), 4));
    const maths::Vec4* lerped = walk::get_lerp(&result, &from, &to, blend);
    std::memcpy(out.data(), lerped, sizeof(maths::Vec4));
}

namespace {
uint32_t& cache_use_counter()
{
    static uint32_t cache_use_counter{};
    return cache_use_counter;
}
} // namespace

// 0x00483F10
void interpolate_float_quat(std::span<float, 4> out, FloatKey* a, const FloatKey* b, float blend)
{
    if (blend == 0.0F) {
        std::memcpy(out.data(), a->value.data(), 4 * sizeof(float));
        return;
    }
    if (blend == 1.0F) {
        std::memcpy(out.data(), b->value.data(), 4 * sizeof(float));
        return;
    }
    const auto& from_value = a->value;
    const auto& to_value = b->value;
    // (x87 at 53 bits, in the original's order: w, x, z, y)
    double dot = (static_cast<double>(core::at(from_value, 3)) * core::at(to_value, 3)) +
                 (static_cast<double>(core::at(from_value, 0)) * core::at(to_value, 0));
    dot += static_cast<double>(core::at(to_value, 2)) * core::at(from_value, 2);
    dot += static_cast<double>(core::at(from_value, 1)) * core::at(to_value, 1);
    if (dot < 0.0) {
        for (int i = 0; i < 4; ++i) {
            core::at(a->value, i) = -core::at(b->value, i);
        }
    }
    // x and w stay unrounded; y and z are rounded to floats on the way.
    const double x =
        ((static_cast<double>(core::at(to_value, 0)) - core::at(from_value, 0)) * blend) + core::at(from_value, 0);
    auto y = static_cast<float>(((static_cast<double>(core::at(to_value, 1)) - core::at(from_value, 1)) * blend) +
                                core::at(from_value, 1));
    auto z = static_cast<float>(((static_cast<double>(core::at(to_value, 2)) - core::at(from_value, 2)) * blend) +
                                core::at(from_value, 2));
    const double w =
        ((static_cast<double>(core::at(to_value, 3)) - core::at(from_value, 3)) * blend) + core::at(from_value, 3);
    double sum = (w * w) + (static_cast<double>(z) * z);
    sum += static_cast<double>(y) * y;
    sum += x * x;
    double length = std::sqrt(sum);
    if (length == 0.0) {
        length = static_cast<double>(kTinyLength); // 0.00001f
    }
    const double scale = 1.0 / length;
    core::at(out, 0) = static_cast<float>(scale * x);
    core::at(out, 1) = static_cast<float>(y * scale);
    core::at(out, 2) = static_cast<float>(z * scale);
    core::at(out, 3) = static_cast<float>(scale * w);
}

// 0x00484170
void interpolate_float_trans(std::span<float, 4> out, const FloatKey* a, const FloatKey* b, float blend)
{
    if (blend == 0.0F || blend == 1.0F) {
        const FloatKey* key = blend == 0.0F ? a : b;
        std::memcpy(out.data(), key->value.data(), 3 * sizeof(float));
        core::at(out, 3) = 1.0F;
        return;
    }
    for (int i = 0; i < 3; ++i) {
        core::at(out, i) = static_cast<float>(
            ((static_cast<double>(core::at(b->value, i)) - core::at(a->value, i)) * blend) + core::at(a->value, i));
    }
    core::at(out, 3) = 1.0F;
}

// --- the frame data -----------------------------------------------------------------------------------------

std::span<const uint16_t> JointClipFrames::rotation_sizes() const
{
    return {quat_sizes_, quat_sizes_ ? static_cast<size_t>(bone_count_) : 0};
}
std::span<const uint16_t> JointClipFrames::translation_sizes() const
{
    return {trans_sizes_, trans_sizes_ ? static_cast<size_t>(bone_count_) : 0};
}
std::span<const uint8_t> JointClipFrames::rotation_key_run() const
{
    return {quat_keys_, quat_keys_ ? static_cast<size_t>(trans_keys_ - quat_keys_) : 0};
}
std::span<const uint8_t> JointClipFrames::translation_key_run() const
{
    if (!trans_keys_) {
        return {};
    }
    if ((flag_bits_ & anim_format::kCompressed) != 0U) {
        return {trans_keys_, total_size(translation_sizes())};
    }
    const size_t key_size = ((flag_bits_ & (anim_format::kWideKeys | anim_format::kBoneNames)) != 0U) ? 16 : 8;
    return {trans_keys_, static_cast<size_t>(static_cast<uint16_t>(trans_key_count_)) * key_size};
}
std::span<const uint8_t> JointClipFrames::own_key_table() const
{
    return {key_table_, key_table_ ? kOwnKeyTableSize : 0};
}
std::span<const uint32_t> JointClipFrames::partial_bone_run() const
{
    return {partial_bones_, partial_bones_ ? 1 + ((static_cast<size_t>(bone_count_) + kPartialMaskRound) / kPartialMaskBits) : 0};
}
std::span<UserClipKey*> JointClipFrames::events() const
{
    return {events_ptr_, events_ptr_ ? static_cast<size_t>(event_count_) : 0};
}
std::span<const uint32_t> JointClipFrames::bone_label_run() const
{
    return {bone_labels_, bone_labels_ ? static_cast<size_t>(bone_count_) : 0};
}

// 0x00482AF0
JointClipFrames::JointClipFrames() { bits_ &= kJointClipKeepMask; }

// 0x00482B30
JointClipFrames::~JointClipFrames()
{
    for (int i = 0; i < event_count_; ++i) {
        gsl::owner<UserClipKey*> key = memory::owned_element(core::at(events(), i)); // (each the frames' own)
        delete key;
    }
    if (events_ptr_) {
        memory::free_malloc(static_cast<void*>(events_ptr_));
    }
    if (cache_slot_) {
        if (void* cache = frame_cache()) {
            cache_remove(cache, this);
        }
    }
}

// 0x00482B90
void JointClipFrames::forget_cache_entry(JointClipFrames* data)
{
    if (data) {
        data->cache_slot_ = nullptr;
    }
}

// 0x00482BA0
int JointClipFrames::acquire_cache_index()
{
    bits_ = reference_bits(bits_, 1);
    return cache_acquire(frame_cache(), this);
}

// 0x00482BD0
void JointClipFrames::release_cache_index(int index)
{
    // (the count as a signed byte)
    if (static_cast<int8_t>((bits_ >> 1) & kBitsByteMask) <= 0) {
        return;
    }
    bits_ = reference_bits(bits_, -1);
    cache_release(frame_cache(), this, index);
}

// 0x00482F00
uint32_t JointClipFrames::fetch_joint_name(int index) const { return core::at(bone_label_run(), index); }

// 0x00482F10
bool JointClipFrames::arm_events() const
{
    const int count = event_count_;
    for (int i = 0; i < count; ++i) {
        arm_event(core::at(events(), i), true);
    }
    return true;
}

// 0x00482F40
bool JointClipFrames::fire_events(float from, float to, void* target, int flag) const
{
    auto from_frame = static_cast<float>(static_cast<double>(from) * kClipFrameRate);
    auto to_frame = static_cast<float>(static_cast<double>(to) * kClipFrameRate);
    const int count = event_count_;
    for (int i = 0; i < count; ++i) {
        UserClipKey* event = core::at(events(), i);
        if (event->inside_range(from_frame, to_frame, flag != 0)) {
            fire_event(event, target);
        }
    }
    return true;
}

// --- the frame cache entry's key searches -------------------------------------------------------------------

namespace {

// A key's time: 15 bits, sign-extended (quaternion keys - bit 15 is w's sign), or the whole short (translations).
int quat_key_time(uint16_t word) { return static_cast<int16_t>(static_cast<uint16_t>(word << 1)) >> 1; }
int trans_key_time(uint16_t word) { return static_cast<int16_t>(word); }
int quat_key_time(const uint8_t* key) { return quat_key_time(anim_format::key_time_word(key)); }
int trans_key_time(const uint8_t* key) { return trans_key_time(anim_format::key_time_word(key)); }

// A cached animation's keys: the entry's block (allocated_ bytes - the keys' extent_ and the rounding after them, which
// the searches' look at the key after a bone's last one reads into), keys found by their offset in it.
class CachedKeys {
public:
    explicit CachedKeys(const AnimCacheEntry& entry)
        : bytes_(entry.payload(), entry.payload() ? entry.allocated() : 0),
          rotations_(offset_of(entry.rotation_keys_ptr())), translations_(offset_of(entry.translation_keys_ptr()))
    {
    }
    // Where the rotation / translation keys start.
    [[nodiscard]] std::ptrdiff_t rotations() const { return rotations_; }
    [[nodiscard]] std::ptrdiff_t translations() const { return translations_; }
    // A key's place, and the key at a place (the end included).
    [[nodiscard]] std::ptrdiff_t offset_of(const uint8_t* key) const { return key - bytes_.data(); }
    [[nodiscard]] uint8_t* key(std::ptrdiff_t offset) const
    {
        Expects(offset >= 0 && static_cast<size_t>(offset) <= bytes_.size());
        return bytes_.subspan(static_cast<size_t>(offset)).data();
    }
    // The times of the keys at a place (the time word: its first two bytes).
    [[nodiscard]] int quat_time(std::ptrdiff_t offset) const { return quat_key_time(word(offset)); }
    [[nodiscard]] int trans_time(std::ptrdiff_t offset) const { return trans_key_time(word(offset)); }

private:
    [[nodiscard]] uint16_t word(std::ptrdiff_t offset) const
    {
        return static_cast<uint16_t>(core::at(bytes_, offset) | (core::at(bytes_, offset + 1) << kBitsPerByte));
    }

    std::span<uint8_t> bytes_;
    std::ptrdiff_t rotations_;
    std::ptrdiff_t translations_;
};

// A slot's stored key for `bone` (null: none) as a place.
uint8_t*& slot_rotation(AnimCacheEntry& entry, int slot, int bone)
{
    return core::at(core::at(entry.slots_ptr(), slot)->rotation_keys_, bone);
}
uint8_t*& slot_translation(AnimCacheEntry& entry, int slot, int bone)
{
    return core::at(core::at(entry.slots_ptr(), slot)->translation_keys_, bone);
}

} // namespace

// 0x00482FB0
int AnimCacheEntry::find_quat_keys(KeyQuery query, uint8_t** first, uint8_t** second, int slot)
{
    const auto bone = query.bone;
    const auto frame = query.frame;
    if ((flag_bits_ & kFlagUnusable) != 0) {
        return -2;
    }
    flag_bits_ |= kFlagQuerying;
    const CachedKeys keys(*this);
    const int stride = rotation_key_size_;
    const std::ptrdiff_t start = keys.rotations() + core::at(rotation_offsets_, bone);
    std::ptrdiff_t end = 0;
    if (bone == bones_ - 1) {
        if (!(start + stride < keys.translations())) {
            *second = *first = keys.key(start);
            flag_bits_ &= ~kFlagQuerying;
            return slot;
        }
        end = keys.translations();
    } else {
        if (!(core::at(rotation_offsets_, bone) + stride < core::at(rotation_offsets_, bone + 1))) {
            *second = *first = keys.key(start);
            flag_bits_ &= ~kFlagQuerying;
            return slot;
        }
        end = keys.rotations() + core::at(rotation_offsets_, bone + 1);
    }
    // From where the slot's search stopped: its pair still around the frame, or the next one.
    if (slot != -1 && core::at(slots_ptr_, slot) && slot_rotation(*this, slot, bone)) {
        const std::ptrdiff_t key = keys.offset_of(slot_rotation(*this, slot, bone));
        const std::ptrdiff_t next = key + stride;
        if (static_cast<float>(keys.quat_time(key)) <= frame) {
            if (static_cast<float>(keys.quat_time(next)) >= frame) {
                *first = keys.key(key);
                *second = keys.key(next);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            const std::ptrdiff_t after = next + stride;
            if (after < end && static_cast<float>(keys.quat_time(after)) >= frame) {
                slot_rotation(*this, slot, bone) = keys.key(next);
                *first = keys.key(next);
                *second = keys.key(after);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
        }
    }
    // From the bone's first key.
    std::ptrdiff_t key = start;
    std::ptrdiff_t next = start + stride;
    if (static_cast<float>(keys.quat_time(next)) <= frame) {
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                next = key; // (past the last: the last alone)
                break;
            }
            if (!(static_cast<float>(keys.quat_time(next)) <= frame)) {
                break;
            }
        }
    }
    *first = keys.key(key);
    *second = keys.key(next);
    if (slot != -1 && core::at(slots_ptr_, slot)) {
        slot_rotation(*this, slot, bone) = keys.key(key);
    }
    flag_bits_ &= ~kFlagQuerying;
    return slot;
}

// 0x00483210
int AnimCacheEntry::find_trans_keys(AnimCacheEntry::KeyQuery query, uint8_t** first, uint8_t** second, int slot)
{
    const auto bone = query.bone;
    const auto frame = query.frame;
    if ((flag_bits_ & kFlagUnusable) != 0) {
        return -2;
    }
    flag_bits_ |= kFlagQuerying;
    const CachedKeys keys(*this);
    const int stride = translation_key_size_;
    const std::ptrdiff_t start = keys.translations() + core::at(translation_offsets_, bone);
    std::ptrdiff_t end = 0;
    if (bone == bones_ - 1) {
        end = keys.rotations() + static_cast<std::ptrdiff_t>(extent_);
        if (!(start + stride < end)) {
            *second = *first = keys.key(start);
            flag_bits_ &= ~kFlagQuerying;
            return slot;
        }
    } else {
        if (!(core::at(translation_offsets_, bone) + stride < core::at(translation_offsets_, bone + 1))) {
            *second = *first = keys.key(start);
            flag_bits_ &= ~kFlagQuerying;
            return slot;
        }
        end = keys.translations() + core::at(translation_offsets_, bone + 1);
    }
    if (slot != -1 && core::at(slots_ptr_, slot) && slot_translation(*this, slot, bone)) {
        const std::ptrdiff_t key = keys.offset_of(slot_translation(*this, slot, bone));
        const std::ptrdiff_t next = key + stride;
        if (static_cast<float>(keys.trans_time(key)) <= frame) {
            if (static_cast<float>(keys.trans_time(next)) >= frame) {
                *first = keys.key(key);
                *second = keys.key(next);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            const std::ptrdiff_t after = next + stride;
            if (after < end && static_cast<float>(keys.trans_time(after)) >= frame) {
                slot_translation(*this, slot, bone) = keys.key(next);
                *first = keys.key(next);
                *second = keys.key(after);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
        }
    }
    std::ptrdiff_t key = start;
    std::ptrdiff_t next = start + stride;
    if (static_cast<float>(keys.trans_time(next)) <= frame) {
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                // Past the last: the last alone, and (unlike the rotations') the slot's place left as it was.
                *first = keys.key(key);
                *second = keys.key(key);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            if (!(static_cast<float>(keys.trans_time(next)) <= frame)) {
                break;
            }
        }
    }
    *first = keys.key(key);
    *second = keys.key(next);
    if (slot != -1 && core::at(slots_ptr_, slot)) {
        slot_translation(*this, slot, bone) = keys.key(key);
    }
    flag_bits_ &= ~kFlagQuerying;
    return slot;
}

// 0x00483470
int AnimCacheEntry::find_quat_keys_backwards(AnimCacheEntry::KeyQuery query, uint8_t** first, uint8_t** second, int slot)
{
    const auto bone = query.bone;
    const auto frame = query.frame;
    if ((flag_bits_ & kFlagUnusable) != 0) {
        return -2;
    }
    flag_bits_ |= kFlagQuerying;
    const CachedKeys keys(*this);
    // (the last bone's keys end where they start here: always one key)
    const int offset = core::at(rotation_offsets_, bone);
    const int end_offset = core::at(rotation_offsets_, bone == bones_ - 1 ? bone : bone + 1);
    const int stride = rotation_key_size_;
    if (!(offset + stride < end_offset)) {
        *second = *first = keys.key(keys.rotations() + offset);
        flag_bits_ &= ~kFlagQuerying;
        return slot;
    }
    // From where the slot's search stopped: its pair still around the frame, or the one before.
    if (slot != -1 && core::at(slots_ptr_, slot) && slot_rotation(*this, slot, bone)) {
        const std::ptrdiff_t key = keys.offset_of(slot_rotation(*this, slot, bone));
        const std::ptrdiff_t next = key + stride;
        if (static_cast<float>(keys.quat_time(next)) >= frame) {
            if (static_cast<float>(keys.quat_time(key)) <= frame) {
                *first = keys.key(key);
                *second = keys.key(next);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            const std::ptrdiff_t before = key - stride;
            if (!(before < keys.rotations() + offset) && static_cast<float>(keys.quat_time(before)) <= frame &&
                static_cast<float>(keys.quat_time(key)) >= frame) {
                slot_rotation(*this, slot, bone) = keys.key(before);
                *first = keys.key(before);
                *second = keys.key(key);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
        }
    }
    std::ptrdiff_t key = keys.rotations() + core::at(rotation_offsets_, bone);
    std::ptrdiff_t next = key + stride;
    if (static_cast<float>(keys.quat_time(next)) <= frame) {
        const std::ptrdiff_t end = keys.rotations() + end_offset;
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                *first = keys.key(key);
                *second = keys.key(key);
                if (slot != -1) {
                    slot_rotation(*this, slot, bone) = keys.key(key); // (no check for a slot here, as the original)
                }
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            if (!(static_cast<float>(keys.quat_time(next)) <= frame)) {
                break;
            }
        }
    }
    *first = keys.key(key);
    *second = keys.key(next);
    if (slot != -1 && core::at(slots_ptr_, slot)) {
        slot_rotation(*this, slot, bone) = keys.key(key);
    }
    flag_bits_ &= ~kFlagQuerying;
    return slot;
}

// 0x004836C0
int AnimCacheEntry::find_trans_keys_backwards(int bone, float frame, uint8_t** first, uint8_t** second, int slot)
{
    if ((flag_bits_ & kFlagUnusable) != 0) {
        return -2;
    }
    flag_bits_ |= kFlagQuerying;
    const CachedKeys keys(*this);
    const int offset = core::at(translation_offsets_, bone);
    const int end_offset = core::at(translation_offsets_, bone == bones_ - 1 ? bone : bone + 1);
    const int stride = translation_key_size_;
    if (!(offset + stride < end_offset)) {
        *second = *first = keys.key(keys.translations() + offset);
        flag_bits_ &= ~kFlagQuerying;
        return slot;
    }
    if (slot != -1 && core::at(slots_ptr_, slot) && slot_translation(*this, slot, bone)) {
        const std::ptrdiff_t key = keys.offset_of(slot_translation(*this, slot, bone));
        const std::ptrdiff_t next = key + stride;
        if (static_cast<float>(keys.trans_time(next)) >= frame) {
            if (static_cast<float>(keys.trans_time(key)) <= frame) {
                *first = keys.key(key);
                *second = keys.key(next);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            const std::ptrdiff_t before = key - stride;
            if (!(before < keys.translations() + core::at(translation_offsets_, bone)) &&
                static_cast<float>(keys.trans_time(before)) <= frame && static_cast<float>(keys.trans_time(key)) >= frame) {
                slot_translation(*this, slot, bone) = keys.key(before);
                *first = keys.key(before);
                *second = keys.key(key);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
        }
    }
    std::ptrdiff_t key = keys.translations() + core::at(translation_offsets_, bone);
    std::ptrdiff_t next = key + stride;
    if (static_cast<float>(keys.trans_time(next)) <= frame) {
        const std::ptrdiff_t end = keys.translations() + end_offset;
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                *first = keys.key(key);
                *second = keys.key(key);
                flag_bits_ &= ~kFlagQuerying;
                return slot;
            }
            if (!(static_cast<float>(keys.trans_time(next)) <= frame)) {
                break;
            }
        }
    }
    *first = keys.key(key);
    *second = keys.key(next);
    if (slot != -1 && core::at(slots_ptr_, slot)) {
        slot_translation(*this, slot, bone) = keys.key(key);
    }
    flag_bits_ &= ~kFlagQuerying;
    return slot;
}

// --- setting up and reading the frame data ------------------------------------------------------------------

// The custom keys after the keys (the header's count), then their count - not when one fails. Not original - read
// through anim_format's reader within the animation (create_custom_anim_key's reader form).
bool JointClipFrames::read_events(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout)
{
    if (header.custom_key_count == 0U) {
        events_ptr_ = nullptr;
        event_count_ = static_cast<int16_t>(header.custom_key_count);
        return true;
    }
    events_ptr_ = memory::adopt<UserClipKey**>(memory::alloc_block(header.custom_key_count * 4));
    const std::span<UserClipKey*> made(events_ptr_, header.custom_key_count);
    util::ByteReader reader = anim_format::custom_key_reader(layout);
    for (uint32_t i = 0; i < header.custom_key_count; ++i) {
        gsl::owner<UserClipKey*> event = create_custom_anim_key(reader);
        if (!event) {
            return false;
        }
        core::at(made, i) = event;
    }
    event_count_ = static_cast<int16_t>(header.custom_key_count);
    return true;
}

// 0x00484200 (the sections from anim_format's layout)
bool JointClipFrames::set_up_compressed(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout)
{
    bone_count_ = static_cast<int>(header.bone_count);
    quat_key_count_ = static_cast<int16_t>(header.rotation_key_count);
    trans_key_count_ = static_cast<int16_t>(header.translation_key_count);
    quat_sizes_ = anim_format::rotation_size_list(layout);
    trans_sizes_ = anim_format::translation_size_list(layout);
    bone_labels_ = nullptr;
    if (layout.key_table != 0U) {
        key_table_ = anim_format::key_table_bytes(layout);
    }
    if (layout.partial_bones != 0U) {
        partial_bones_ = anim_format::partial_bone_bits(layout);
    }
    if (layout.rotation_keys != 0U) {
        quat_keys_ = anim_format::rotation_key_bytes(layout);
        trans_keys_ = anim_format::translation_key_bytes(layout);
    }
    return read_events(header, layout);
}

// 0x00484370 (the sections from anim_format's layout)
bool JointClipFrames::set_up_float_keys(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout)
{
    bone_count_ = static_cast<int>(header.bone_count);
    quat_key_count_ = static_cast<int16_t>(header.rotation_key_count);
    trans_key_count_ = static_cast<int16_t>(header.translation_key_count);
    bone_labels_ = anim_format::bone_name_list(layout); // (null without them)
    if (layout.partial_bones != 0U) {
        partial_bones_ = anim_format::partial_bone_bits(layout);
    }
    key_counts_ = anim_format::key_count_bytes(layout);
    quat_keys_ = anim_format::rotation_key_bytes(layout);
    trans_keys_ = anim_format::translation_key_bytes(layout);
    return read_events(header, layout);
}

// 0x004844C0
bool JointClipFrames::get_float_key_pose(PoseOut pose_out, void* /*unused*/, float time, int /*unused*/)
{
    const auto rotations = pose_out.rotations;
    const auto translations = pose_out.translations;
    auto quat_frame = static_cast<float>(static_cast<double>(time) * kClipFrameRate);
    const float trans_frame = quat_frame;
    // (kept from bone to bone when a bone's search finds nothing)
    const uint8_t* quat_from = quat_keys_;
    const uint8_t* quat_to = quat_keys_;
    float quat_t = 0.0F;
    const uint8_t* trans_from = trans_keys_;
    const uint8_t* trans_to = trans_keys_;
    float trans_t = 0.0F;
    // (each bone's keys from where the bone before's end in the runs)
    const std::span<const uint8_t> quat_run = rotation_key_run();
    const std::span<const uint8_t> trans_run = translation_key_run();
    size_t quat_keys = 0;
    size_t trans_keys = 0;
    for (int bone = 0; bone < bone_count_; ++bone) {
        int quat_count = 0;
        int trans_count = 0;
        if ((flag_bits_ & kFlagWideKeyCounts) != 0U) {
            quat_count = anim_format::wide_key_count(key_count_run(), 2 * bone);
            trans_count = anim_format::wide_key_count(key_count_run(), (2 * bone) + 1);
        } else {
            quat_count = core::at(key_count_run(), 2 * bone);
            trans_count = core::at(key_count_run(), (2 * bone) + 1);
        }
        for (int k = 0; k < quat_count; ++k) {
            const size_t at = quat_keys + (16 * static_cast<size_t>(k));
            const uint8_t* key = &core::at(quat_run, at);
            if (k == quat_count - 1) {
                quat_from = quat_to = key;
                quat_t = 0.0F;
                break;
            }
            const uint8_t* after = &core::at(quat_run, at + kKeyBytes);
            const int from = quat_key_time(key);
            const int to = quat_key_time(after);
            if (static_cast<float>(from) <= quat_frame && static_cast<float>(to) >= quat_frame) {
                quat_from = key;
                quat_to = after;
                quat_t =
                    static_cast<float>((static_cast<double>(quat_frame) - from) / (static_cast<double>(to) - from));
                break;
            }
        }
        interpolate_quat(quad_at(rotations, bone), quat_from, quat_to, quat_t, (flag_bits_ & kFlagsOtherWay) != 0);
        for (int k = 0; k < trans_count; ++k) {
            const size_t at = trans_keys + (16 * static_cast<size_t>(k));
            const uint8_t* key = &core::at(trans_run, at);
            if (k == trans_count - 1) {
                trans_from = trans_to = key;
                trans_t = 0.0F;
                break;
            }
            const uint8_t* after = &core::at(trans_run, at + kKeyBytes);
            const int from = trans_key_time(key);
            const int to = trans_key_time(after);
            if (static_cast<float>(from) <= trans_frame && static_cast<float>(to) >= trans_frame) {
                trans_from = key;
                trans_to = after;
                trans_t =
                    static_cast<float>((static_cast<double>(trans_frame) - from) / (static_cast<double>(to) - from));
                break;
            }
        }
        interpolate_trans(quad_at(translations, bone), trans_from, trans_to, trans_t, (flag_bits_ & kFlagsOtherWay) != 0);
        quat_keys += kKeyBytes * static_cast<size_t>(quat_count);
        trans_keys += kKeyBytes * static_cast<size_t>(trans_count);
    }
    return true;
}

namespace {

// The frame cache's time for a cursor (0x00480590 on the entry): its direction - negative when time went back.

} // namespace

// 0x00484760
bool JointClipFrames::fetch_pose(std::span<maths::QuatRotation> rotations, std::span<maths::Vec4> translations,
                                 std::span<uint8_t> valid, float time, int cursor, FrameSource* source)
{
    auto quat_frame = static_cast<float>(static_cast<double>(time) * kClipFrameRate);
    float trans_frame = quat_frame; // (each clamped in turn by the cache's keys - kept for the later bones)
    // (each bone's keys from where the bone before's end in the runs, its size's bytes)
    const std::span<const uint8_t> quat_run = rotation_key_run();
    const std::span<const uint8_t> trans_run = translation_key_run();
    const std::span<const uint16_t> quat_sizes = rotation_sizes();
    const std::span<const uint16_t> trans_sizes = translation_sizes();
    size_t quat_data = 0;
    size_t trans_data = 0;
    bool forwards = true;
    const uint32_t mask = source && source->resume() ? 1U << (source->mask_bit() & 31) : 0U;
    if (cache_slot_) {
        uint32_t& counter = cache_use_counter();
        cache_slot_->set_last_used(counter++);
        if (cursor != -1 &&
            cache_slot_->set_slot_time(
                {.index = cursor, .time = static_cast<int16_t>(static_cast<int>(static_cast<int64_t>(time)))}) < 0) {
            forwards = false;
        }
    }
    AnimCacheEntry* entry = cache_slot_;
    const std::span<const uint32_t> bone_masks =
        (mask != 0U) && source->bone_masks()
            ? std::span<const uint32_t>(source->bone_masks(), static_cast<size_t>(bone_count_))
            : std::span<const uint32_t>();
    for (int bone = 0; bone < bone_count_; ++bone) {
        const std::span<float, 4> rotation(core::at(rotations, bone).data(), 4);
        const std::span<float, 4> translation(core::at(translations, bone).data(), 4);
        const uint16_t quat_size = core::at(quat_sizes, bone);
        const uint16_t trans_size = core::at(trans_sizes, bone);
        bool skip = (mask != 0U) ? (core::at(bone_masks, bone) & mask) != 0 : false;
        if ((flag_bits_ & kFlagPartialBones) != 0U) {
            skip =
                (core::at(partial_bone_run(), 1 + (bone / kPartialMaskBits)) & (1U << (bone % kPartialMaskBits))) == 0U;
        }
        if (skip) {
            core::at(valid, bone) = 0;
        } else {
            core::at(valid, bone) = 1;
            // The rotation.
            if (quat_size == 0) {
                core::at(rotation, 0) = core::at(rotation, 1) = core::at(rotation, 2) = 0.0F;
                core::at(rotation, 3) = 1.0F;
            } else {
                bool done = false;
                if (cache_slot_) {
                    uint8_t* first = nullptr;
                    uint8_t* second = nullptr;
                    const int found = forwards
                                          ? entry->find_quat_keys({.bone = bone, .frame = quat_frame}, &first, &second, cursor)
                                          : entry->find_quat_keys_backwards({.bone = bone, .frame = quat_frame}, &first, &second, cursor);
                    if (found != -2) {
                        const int from = quat_key_time(first);
                        const int to = quat_key_time(second);
                        FloatKey key_a = anim_format::float_key_at(first);
                        const FloatKey key_b = anim_format::float_key_at(second);
                        if (from == to) {
                            interpolate_float_quat(rotation, &key_a, &key_b, 0.0F);
                        } else {
                            if (quat_frame < static_cast<float>(from)) {
                                quat_frame = static_cast<float>(from);
                            } else if (quat_frame > static_cast<float>(to)) {
                                quat_frame = static_cast<float>(to);
                            }
                            auto blend = static_cast<float>((static_cast<double>(quat_frame) - from) /
                                                            (static_cast<double>(to) - from));
                            interpolate_float_quat(rotation, &key_a, &key_b, blend);
                        }
                        anim_format::store_float_key(first, key_a); // (its values flipped in place when needed)
                        done = true;
                    }
                }
                if (!done) {
                    // Decompressing: from the bone's start (or where the source left it) to the pair around the frame.
                    const KeyCursor end(quat_run, static_cast<std::ptrdiff_t>(quat_data + quat_size));
                    KeyCursor at(quat_run, static_cast<std::ptrdiff_t>(quat_data));
                    if (source && source->resume() && core::at(source->quat_at(), bone)) {
                        at = KeyCursor(quat_run, core::at(source->quat_at(), bone) - quat_run.data());
                    }
                    ShortKey short_a{};
                    ShortKey short_b{};
                    float blend = 0.0F;
                    for (;;) {
                        if (source) {
                            core::at(source->quat_at(), bone) = at.rest().data();
                        }
                        at = decompress_quat_key(at, &short_a, flag_bits_, own_key_table());
                        if (at >= end) {
                            short_b = short_a;
                            blend = 0.0F;
                            break;
                        }
                        decompress_quat_key(at, &short_b, flag_bits_, own_key_table());
                        auto from = static_cast<float>(quat_key_time(short_a.time));
                        auto to = static_cast<float>(quat_key_time(short_b.time));
                        if (quat_frame >= from && quat_frame <= to) {
                            blend = static_cast<float>((static_cast<double>(quat_frame) - from) /
                                                       (static_cast<double>(to) - from));
                            break;
                        }
                    }
                    interpolate_short_quat(rotation, &short_a, &short_b, blend);
                }
            }
            // The translation (none for a bone without keys when flag 0x20000 says so).
            if (!(((flag_bits_ & kFlagNoTransKeys) != 0U) && trans_size == 0)) {
                bool done = false;
                if (cache_slot_) {
                    uint8_t* first = nullptr;
                    uint8_t* second = nullptr;
                    const int found =
                        forwards ? entry->find_trans_keys({.bone = bone, .frame = trans_frame}, &first, &second, cursor)
                                 : entry->find_trans_keys_backwards(bone, trans_frame, &first, &second, cursor);
                    if (found != -2) {
                        auto from = static_cast<float>(trans_key_time(first));
                        auto to = static_cast<float>(trans_key_time(second));
                        const bool add = ((flag_bits_ & 0x4000) != 0U) && ((flag_bits_ & 0x20000) != 0U);
                        const std::array<float, 4> before{core::at(translation, 0), core::at(translation, 1), core::at(translation, 2),
                                                          core::at(translation, 3)};
                        const FloatKey key_a = anim_format::float_key_at(first);
                        const FloatKey key_b = anim_format::float_key_at(second);
                        auto add_before = [&] {
                            for (int i = 0; i < 4; ++i) {
                                core::at(translation, i) += core::at(before, i);
                            }
                        };
                        if (from == to) {
                            interpolate_float_trans(translation, &key_a, &key_b, 0.0F);
                            if (add) {
                                add_before();
                            }
                        } else if (trans_frame >= from) { // (before the first key: left as it was)
                            trans_frame = maths::confine(trans_frame, from, to);
                            auto blend = static_cast<float>((static_cast<double>(trans_frame) - from) /
                                                            (static_cast<double>(to) - from));
                            interpolate_float_trans(translation, &key_a, &key_b, blend);
                            if (add) {
                                add_before();
                            }
                        }
                        done = true;
                    }
                }
                if (!done) {
                    const KeyCursor end(trans_run, static_cast<std::ptrdiff_t>(trans_data + trans_size));
                    KeyCursor at(trans_run, static_cast<std::ptrdiff_t>(trans_data));
                    if (source && source->resume() && core::at(source->trans_at(), bone)) {
                        at = KeyCursor(trans_run, core::at(source->trans_at(), bone) - trans_run.data());
                    }
                    ShortKey short_a{};
                    ShortKey short_b{};
                    float blend = 0.0F;
                    for (;;) {
                        if (source) {
                            core::at(source->trans_at(), bone) = at.rest().data();
                        }
                        at = decompress_trans_key(at, &short_a);
                        if (at >= end) {
                            short_b = short_a;
                            break;
                        }
                        decompress_trans_key(at, &short_b);
                        const int from = trans_key_time(short_a.time);
                        if (!(static_cast<float>(from) <= trans_frame)) { // (the frame before this key: this key alone)
                            short_b = short_a;
                            break;
                        }
                        const int to = trans_key_time(short_b.time);
                        if (static_cast<float>(to) >= trans_frame) {
                            blend = static_cast<float>((static_cast<double>(trans_frame) - from) /
                                                       (static_cast<double>(to) - from));
                            break;
                        }
                    }
                    std::array<float, 4> before{};
                    std::memcpy(before.data(), translation.data(), sizeof(before));
                    interpolate_short_trans(translation, &short_a, &short_b, blend);
                    if (((flag_bits_ & kFlagQuerying) != 0U) && ((flag_bits_ & kFlagNoTransKeys) != 0U)) {
                        for (int i = 0; i < 4; ++i) {
                            core::at(translation, i) = core::at(before, i) + core::at(translation, i);
                        }
                    }
                }
            }
        }
        quat_data += quat_size;
        trans_data += trans_size;
    }
    return true;
}

} // namespace gfx
