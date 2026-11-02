// A boned animation's frame data: its keys decompressed and interpolated, its events, its frame cache entry - see
// boned_anim_frame_data.h.

#include "engine/gfx/boned_anim_frame_data.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/gfx/anim_blend.h"
#include "formats/anim_format.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/custom_anim_key.h"
#include "engine/gfx/anim_track.h"
#include "engine/maths/maths.h"
#include "engine/maths/vec4.h"
#include "formats/byte_stream.h"
#include "engine/objects/walk_hang.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace gfx {

namespace {
// The shared key tables (256 entries of four shorts), loaded from files at start-up.
std::array<int16_t, 1024> g_quat_table;
std::array<int16_t, 1024> g_trans_table;

constexpr double kQuatUnit = 1.0 / 16384.0; // (0x006487C8)
constexpr double kTransUnit = 1.0 / 32.0;   // (0x006487CC)

float get_scaled(int16_t value, double unit) { return static_cast<float>(value * unit); }

uint16_t get_word(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// The time word of a key at the head of `in`, as the original merges it (the sign bit, then the rest).
uint16_t time_word(uint16_t out, uint16_t in)
{
    auto merged = static_cast<uint16_t>((out & 0x7FFF) | (in & 0x8000));
    return static_cast<uint16_t>((merged & 0x8000) | (in & 0x7FFF));
}

// w from x, y, z: the square root of what's left of 1 (x87 at 53 bits, in this order), negative with bit 15 of the
// time word.
float quat_w(float x, float y, float z, uint16_t time, bool nan_to_zero)
{
    double left = 1.0 - static_cast<double>(x) * x;
    left -= static_cast<double>(y) * y;
    left -= static_cast<double>(z) * z;
    // (0x004838D0 tests "below 0 or unordered" - NaN gives 0; the others only "below 0")
    if (nan_to_zero ? !(left >= 0.0) : left < 0.0)
        left = 0.0;
    const double w = std::sqrt(left);
    return static_cast<float>((time & 0x8000) ? -w : w);
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
void arm_event(UserClipKey* e, bool armed) { e->change_live(armed); }
bool fire_event(UserClipKey* e, void* target) { return e->process_if_active(target); }

uint16_t reference_bits(uint16_t bits, int change)
{
    const int count = (bits & 0xFFFE) + 2 * change;
    return static_cast<uint16_t>((bits & ~0x1FE) | (count & 0x1FE));
}

} // namespace

// --- the shared tables --------------------------------------------------------------------------------------

// 0x00482970
bool read_key_table(void* file, int16_t* table)
{
    for (int i = 0; i < 256; ++i, table += 4) {
        for (int k = 0; k < 4; ++k) {
            if (!file::get_take_in(table + k, 2, 1, static_cast<file::Hdl>(file)))
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
    if (file)
        loaded = read_key_table(file, g_quat_table.data());
    file::shut(file); // (even when it didn't open)
    return loaded;
}

// 0x00482A30
bool load_trans_table(const char* name)
{
    bool loaded = false;
    file::Hdl file = file::unseal(name, "rb");
    if (file)
        loaded = read_key_table(file, g_trans_table.data());
    file::shut(file);
    return loaded;
}

// --- decompressing keys -------------------------------------------------------------------------------------

// 0x00482C20
const uint8_t* decompress_quat_key(const uint8_t* data, ShortKey* out, uint32_t flags, const uint8_t* table)
{
    const int16_t* shared = g_quat_table.data();
    const uint16_t first = get_word(data);
    uint16_t header = 0;
    if (flags & 0x100) {
        header = get_word(data + 2);
        out->time = static_cast<uint16_t>((header & 0x8000) | (first & 0x7FFF));
        data += 4;
    } else {
        header = first;
        out->time = first;
        data += 2;
    }
    // A short, a byte moved up, or (with the header's bit) a byte - in the 0x10000 form the w of that table entry.
    auto value = [&](uint16_t bit) -> int16_t {
        if (header & bit) {
            const uint8_t b = *data++;
            return (flags & 0x10000) ? shared[b * 4 + 3] : static_cast<int16_t>(b);
        }
        if (flags & 0x8000)
            return static_cast<int16_t>(*data++ << 8);
        auto v = static_cast<int16_t>(get_word(data));
        data += 2;
        return v;
    };
    if (!(header & 0x4000)) {
        // Uncompressed: three shorts (or three bytes moved up).
        if (flags & 0x8000) {
            for (int i = 0; i < 3; ++i)
                core::at(out->value, i) = static_cast<int16_t>(data[i] << 8);
            data += 3;
        } else {
            for (int i = 0; i < 3; ++i)
                core::at(out->value, i) = static_cast<int16_t>(get_word(data + 2 * i));
            data += 6;
        }
    } else {
        if (!(header & 0x3800)) {
            // All three from a table entry.
            const uint8_t b = *data++;
            if (flags & 0x2000) {
                for (int i = 0; i < 3; ++i)
                    core::at(out->value, i) = static_cast<int16_t>(get_word(table + b * 6 + 2 * i));
            } else {
                for (int i = 0; i < 3; ++i)
                    core::at(out->value, i) = shared[b * 4 + i];
            }
        } else {
            core::at(out->value, 0) = value(0x2000);
            core::at(out->value, 1) = value(0x1000);
            core::at(out->value, 2) = value(0x0800);
        }
        // The time: 11 bits here (and the sign).
        out->time = static_cast<uint16_t>((out->time & 0x8000) | (out->time & 0x7FF));
    }
    if (flags & 0x100)
        out->time = static_cast<uint16_t>((out->time & 0x8000) | (first & 0x7FFF));
    return data;
}

// 0x00482E70
const uint8_t* decompress_trans_key(const uint8_t* data, ShortKey* out)
{
    const uint8_t head = *data++;
    if (head & 0x40) {
        out->time = static_cast<uint16_t>(head & 0x3F);
    } else {
        out->time = get_word(data);
        data += 2;
    }
    if (head & 0x80) {
        const int16_t* entry = g_trans_table.data() + *data++ * 4;
        for (int i = 0; i < 3; ++i)
            core::at(out->value, i) = entry[i];
    } else {
        for (int i = 0; i < 3; ++i)
            core::at(out->value, i) = static_cast<int16_t>(get_word(data + 2 * i));
        data += 6;
    }
    return data;
}

// 0x00482A70
void expand_quat_key(FloatKey* out, const ShortKey* in)
{
    out->time = time_word(out->time, in->time);
    for (int i = 0; i < 3; ++i)
        core::at(out->value, i) = get_scaled(core::at(in->value, i), kQuatUnit);
}

// 0x00483D00
void expand_quat_key_with_w(FloatKey* out, const ShortKey* in)
{
    out->time = time_word(out->time, in->time);
    if (!core::at(in->value, 0) && !core::at(in->value, 1) && !core::at(in->value, 2)) {
        core::at(out->value, 0) = core::at(out->value, 1) = core::at(out->value, 2) = 0.0f;
        core::at(out->value, 3) = 1.0f;
        return;
    }
    const float x = get_scaled(core::at(in->value, 0), kQuatUnit);
    const float y = get_scaled(core::at(in->value, 1), kQuatUnit);
    const float z = get_scaled(core::at(in->value, 2), kQuatUnit);
    const float w = quat_w(x, y, z, in->time, false);
    core::at(out->value, 0) = x;
    core::at(out->value, 1) = y;
    core::at(out->value, 2) = z;
    core::at(out->value, 3) = w;
}

// --- a key's value ------------------------------------------------------------------------------------------

// 0x004838D0
void get_quat(const void* key, float* out, bool floats)
{
    if (floats) {
        std::memcpy(out, static_cast<const FloatKey*>(key)->value.data(), 3 * sizeof(float));
    } else {
        const ShortKey& short_key = *static_cast<const ShortKey*>(key);
        for (int i = 0; i < 3; ++i)
            out[i] = get_scaled(core::at(short_key.value, i), kQuatUnit);
    }
    out[3] = quat_w(out[0], out[1], out[2], static_cast<const ShortKey*>(key)->time, true);
}

// 0x00483990
void get_trans(const void* key, float* out, bool floats)
{
    if (floats) {
        std::memcpy(out, static_cast<const FloatKey*>(key)->value.data(), 3 * sizeof(float));
    } else {
        const ShortKey& short_key = *static_cast<const ShortKey*>(key);
        for (int i = 0; i < 3; ++i)
            out[i] = get_scaled(core::at(short_key.value, i), kTransUnit);
    }
    out[3] = 1.0f;
}

// 0x00483A00
void get_short_quat(const ShortKey* key, float* out)
{
    if (!core::at(key->value, 0) && !core::at(key->value, 1) && !core::at(key->value, 2)) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    const float x = get_scaled(core::at(key->value, 0), kQuatUnit);
    const float y = get_scaled(core::at(key->value, 1), kQuatUnit);
    const float z = get_scaled(core::at(key->value, 2), kQuatUnit);
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = quat_w(x, y, z, key->time, false);
}

// 0x00483AE0
void get_short_trans(const ShortKey* key, float* out)
{
    for (int i = 0; i < 3; ++i)
        out[i] = get_scaled(core::at(key->value, i), kTransUnit);
    out[3] = 1.0f;
}

// --- between two keys ---------------------------------------------------------------------------------------

// 0x00483B30
void interpolate_quat(float* out, const void* a, const void* b, float t, bool floats)
{
    if (t == 0.0f) {
        get_quat(a, out, floats);
        return;
    }
    if (t == 1.0f) {
        get_quat(b, out, floats);
        return;
    }
    std::array<float, 4> from = {{0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 4> to = {{0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 4> result{};
    get_quat(a, from.data(), floats);
    get_quat(b, to.data(), floats);
    interpolate_rotation(result.data(), from.data(), to.data(), t);
    std::memcpy(out, result.data(), sizeof(result));
}

// 0x00483C40
void interpolate_trans(float* out, const void* a, const void* b, float t, bool floats)
{
    if (t == 0.0f) {
        get_trans(a, out, floats);
        return;
    }
    if (t == 1.0f) {
        get_trans(b, out, floats);
        return;
    }
    maths::Vec4 from{}, to{}, result{};
    get_trans(a, &from.x, floats);
    get_trans(b, &to.x, floats);
    const maths::Vec4* lerped = walk::get_lerp(&result, &from, &to, t);
    std::memcpy(out, lerped, sizeof(maths::Vec4));
}

// 0x00483E10
void interpolate_short_quat(float* out, const ShortKey* a, const ShortKey* b, float t)
{
    if (t == 0.0f) {
        get_short_quat(a, out);
        return;
    }
    if (t == 1.0f) {
        get_short_quat(b, out);
        return;
    }
    std::array<float, 4> from = {{0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 4> to = {{0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 4> result{};
    get_short_quat(a, from.data());
    get_short_quat(b, to.data());
    interpolate_rotation(result.data(), from.data(), to.data(), t);
    std::memcpy(out, result.data(), sizeof(result));
}

// 0x00484080
void interpolate_short_trans(float* out, const ShortKey* a, const ShortKey* b, float t)
{
    if (t == 0.0f) {
        get_short_trans(a, out);
        return;
    }
    if (t == 1.0f) {
        get_short_trans(b, out);
        return;
    }
    maths::Vec4 from{}, to{}, result{};
    get_short_trans(a, &from.x);
    get_short_trans(b, &to.x);
    const maths::Vec4* lerped = walk::get_lerp(&result, &from, &to, t);
    std::memcpy(out, lerped, sizeof(maths::Vec4));
}

float g_tiny_length{0.00001f};
namespace {
uint32_t g_cache_use_counter;
} // namespace

// 0x00483F10
void interpolate_float_quat(float* out, FloatKey* a, const FloatKey* b, float t)
{
    if (t == 0.0f) {
        std::memcpy(out, a->value.data(), 4 * sizeof(float));
        return;
    }
    if (t == 1.0f) {
        std::memcpy(out, b->value.data(), 4 * sizeof(float));
        return;
    }
    const auto& p = a->value;
    const auto& q = b->value;
    // (x87 at 53 bits, in the original's order: w, x, z, y)
    double dot = static_cast<double>(core::at(p, 3)) * core::at(q, 3) + static_cast<double>(core::at(p, 0)) * core::at(q, 0);
    dot += static_cast<double>(core::at(q, 2)) * core::at(p, 2);
    dot += static_cast<double>(core::at(p, 1)) * core::at(q, 1);
    if (dot < 0.0) {
        for (int i = 0; i < 4; ++i)
            core::at(a->value, i) = -core::at(b->value, i);
    }
    // x and w stay unrounded; y and z are rounded to floats on the way.
    const double x = (static_cast<double>(core::at(q, 0)) - core::at(p, 0)) * t + core::at(p, 0);
    auto y = static_cast<float>((static_cast<double>(core::at(q, 1)) - core::at(p, 1)) * t + core::at(p, 1));
    auto z = static_cast<float>((static_cast<double>(core::at(q, 2)) - core::at(p, 2)) * t + core::at(p, 2));
    const double w = (static_cast<double>(core::at(q, 3)) - core::at(p, 3)) * t + core::at(p, 3);
    double sum = w * w + static_cast<double>(z) * z;
    sum += static_cast<double>(y) * y;
    sum += x * x;
    double length = std::sqrt(sum);
    if (length == 0.0)
        length = static_cast<double>(g_tiny_length); // 0.00001f
    const double scale = 1.0 / length;
    out[0] = static_cast<float>(scale * x);
    out[1] = static_cast<float>(y * scale);
    out[2] = static_cast<float>(z * scale);
    out[3] = static_cast<float>(scale * w);
}

// 0x00484170
void interpolate_float_trans(float* out, const FloatKey* a, const FloatKey* b, float t)
{
    if (t == 0.0f || t == 1.0f) {
        const FloatKey* key = t == 0.0f ? a : b;
        std::memcpy(out, key->value.data(), 3 * sizeof(float));
        out[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<float>((static_cast<double>(core::at(b->value, i)) - core::at(a->value, i)) * t + core::at(a->value, i));
    out[3] = 1.0f;
}

// --- the frame data -----------------------------------------------------------------------------------------

// 0x00482AF0
JointClipFrames::JointClipFrames() { bits_ &= 0xFE00; }

// 0x00482B30
JointClipFrames::~JointClipFrames()
{
    for (int i = 0; i < event_count_; ++i) {
        gsl::owner<UserClipKey*> key = memory::owned_element(events_ptr_[i]); // (each the frames' own)
        delete key;
    }
    if (events_ptr_)
        memory::free_malloc(static_cast<void*>(events_ptr_));
    if (cache_slot_) {
        if (void* cache = frame_cache())
            cache_remove(cache, this);
    }
}

// 0x00482B90
void JointClipFrames::forget_cache_entry(JointClipFrames* data)
{
    if (data)
        data->cache_slot_ = nullptr;
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
    if (static_cast<int8_t>((bits_ >> 1) & 0xFF) <= 0)
        return;
    bits_ = reference_bits(bits_, -1);
    cache_release(frame_cache(), this, index);
}

// 0x00482F00
uint32_t JointClipFrames::fetch_joint_name(int index) { return bone_labels_[index]; }

// 0x00482F10
bool JointClipFrames::arm_events()
{
    const int count = event_count_;
    for (int i = 0; i < count; ++i)
        arm_event(events_ptr_[i], true);
    return true;
}

// 0x00482F40
bool JointClipFrames::fire_events(float from, float to, void* target, int flag)
{
    auto from_frame = static_cast<float>(static_cast<double>(from) * 60.0);
    auto to_frame = static_cast<float>(static_cast<double>(to) * 60.0);
    const int count = event_count_;
    for (int i = 0; i < count; ++i) {
        UserClipKey* event = events_ptr_[i];
        if (event->inside_range(from_frame, to_frame, flag != 0))
            fire_event(event, target);
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

} // namespace

// 0x00482FB0
int AnimCacheEntry::find_quat_keys(int bone, float frame, uint8_t** first, uint8_t** second, int slot)
{
    if (flag_bits_ & 0x8000)
        return -2;
    flag_bits_ |= 0x4000;
    const int stride = rotation_key_size_;
    uint8_t* start = rotation_keys_ptr_ + core::at(rotation_offsets_, bone);
    const uint8_t* end = nullptr;
    if (bone == bones_ - 1) {
        if (!(start + stride < translation_keys_ptr_)) {
            *second = *first = start;
            flag_bits_ &= ~0x4000;
            return slot;
        }
        end = translation_keys_ptr_;
    } else {
        if (!(core::at(rotation_offsets_, bone) + stride < core::at(rotation_offsets_, bone + 1))) {
            *second = *first = start;
            flag_bits_ &= ~0x4000;
            return slot;
        }
        end = rotation_keys_ptr_ + core::at(rotation_offsets_, bone + 1);
    }
    // From where the slot's search stopped: its pair still around the frame, or the next one.
    if (slot != -1 && core::at(slots_ptr_, slot) && core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone)) {
        uint8_t* key = core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone);
        uint8_t* next = key + stride;
        if (static_cast<float>(quat_key_time(key)) <= frame) {
            if (static_cast<float>(quat_key_time(next)) >= frame) {
                *first = key;
                *second = next;
                flag_bits_ &= ~0x4000;
                return slot;
            }
            uint8_t* after = next + stride;
            if (after < end && static_cast<float>(quat_key_time(after)) >= frame) {
                core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone) = next;
                *first = next;
                *second = after;
                flag_bits_ &= ~0x4000;
                return slot;
            }
        }
    }
    // From the bone's first key.
    uint8_t* key = start;
    uint8_t* next = start + stride;
    if (static_cast<float>(quat_key_time(next)) <= frame) {
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                next = key; // (past the last: the last alone)
                break;
            }
            if (!(static_cast<float>(quat_key_time(next)) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (slot != -1 && core::at(slots_ptr_, slot))
        core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone) = key;
    flag_bits_ &= ~0x4000;
    return slot;
}

// 0x00483210
int AnimCacheEntry::find_trans_keys(int bone, float frame, uint8_t** first, uint8_t** second, int slot)
{
    if (flag_bits_ & 0x8000)
        return -2;
    flag_bits_ |= 0x4000;
    const int stride = translation_key_size_;
    uint8_t* start = translation_keys_ptr_ + core::at(translation_offsets_, bone);
    const uint8_t* end = nullptr;
    if (bone == bones_ - 1) {
        end = rotation_keys_ptr_ + extent_;
        if (!(start + stride < end)) {
            *second = *first = start;
            flag_bits_ &= ~0x4000;
            return slot;
        }
    } else {
        if (!(core::at(translation_offsets_, bone) + stride < core::at(translation_offsets_, bone + 1))) {
            *second = *first = start;
            flag_bits_ &= ~0x4000;
            return slot;
        }
        end = translation_keys_ptr_ + core::at(translation_offsets_, bone + 1);
    }
    if (slot != -1 && core::at(slots_ptr_, slot) && core::at(core::at(slots_ptr_, slot)->translation_keys_, bone)) {
        uint8_t* key = core::at(core::at(slots_ptr_, slot)->translation_keys_, bone);
        uint8_t* next = key + stride;
        if (static_cast<float>(trans_key_time(key)) <= frame) {
            if (static_cast<float>(trans_key_time(next)) >= frame) {
                *first = key;
                *second = next;
                flag_bits_ &= ~0x4000;
                return slot;
            }
            uint8_t* after = next + stride;
            if (after < end && static_cast<float>(trans_key_time(after)) >= frame) {
                core::at(core::at(slots_ptr_, slot)->translation_keys_, bone) = next;
                *first = next;
                *second = after;
                flag_bits_ &= ~0x4000;
                return slot;
            }
        }
    }
    uint8_t* key = start;
    uint8_t* next = start + stride;
    if (static_cast<float>(trans_key_time(next)) <= frame) {
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                // Past the last: the last alone, and (unlike the rotations') the slot's place left as it was.
                *first = key;
                *second = key;
                flag_bits_ &= ~0x4000;
                return slot;
            }
            if (!(static_cast<float>(trans_key_time(next)) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (slot != -1 && core::at(slots_ptr_, slot))
        core::at(core::at(slots_ptr_, slot)->translation_keys_, bone) = key;
    flag_bits_ &= ~0x4000;
    return slot;
}

// 0x00483470
int AnimCacheEntry::find_quat_keys_backwards(int bone, float frame, uint8_t** first, uint8_t** second, int slot)
{
    if (flag_bits_ & 0x8000)
        return -2;
    flag_bits_ |= 0x4000;
    // (the last bone's keys end where they start here: always one key)
    const int offset = core::at(rotation_offsets_, bone);
    const int end_offset = core::at(rotation_offsets_, bone == bones_ - 1 ? bone : bone + 1);
    const int stride = rotation_key_size_;
    if (!(offset + stride < end_offset)) {
        *second = *first = rotation_keys_ptr_ + offset;
        flag_bits_ &= ~0x4000;
        return slot;
    }
    // From where the slot's search stopped: its pair still around the frame, or the one before.
    if (slot != -1 && core::at(slots_ptr_, slot) && core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone)) {
        uint8_t* key = core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone);
        uint8_t* next = key + stride;
        if (static_cast<float>(quat_key_time(next)) >= frame) {
            if (static_cast<float>(quat_key_time(key)) <= frame) {
                *first = key;
                *second = next;
                flag_bits_ &= ~0x4000;
                return slot;
            }
            uint8_t* before = key - stride;
            if (!(before < rotation_keys_ptr_ + offset) && static_cast<float>(quat_key_time(before)) <= frame &&
                static_cast<float>(quat_key_time(key)) >= frame) {
                core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone) = before;
                *first = before;
                *second = key;
                flag_bits_ &= ~0x4000;
                return slot;
            }
        }
    }
    uint8_t* key = rotation_keys_ptr_ + core::at(rotation_offsets_, bone);
    uint8_t* next = key + stride;
    if (static_cast<float>(quat_key_time(next)) <= frame) {
        const uint8_t* end = rotation_keys_ptr_ + end_offset;
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                *first = key;
                *second = key;
                if (slot != -1)
                    core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone) = key; // (no check for a slot here, as the original)
                flag_bits_ &= ~0x4000;
                return slot;
            }
            if (!(static_cast<float>(quat_key_time(next)) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (slot != -1 && core::at(slots_ptr_, slot))
        core::at(core::at(slots_ptr_, slot)->rotation_keys_, bone) = key;
    flag_bits_ &= ~0x4000;
    return slot;
}

// 0x004836C0
int AnimCacheEntry::find_trans_keys_backwards(int bone, float frame, uint8_t** first, uint8_t** second, int slot)
{
    if (flag_bits_ & 0x8000)
        return -2;
    flag_bits_ |= 0x4000;
    const int offset = core::at(translation_offsets_, bone);
    const int end_offset = core::at(translation_offsets_, bone == bones_ - 1 ? bone : bone + 1);
    const int stride = translation_key_size_;
    if (!(offset + stride < end_offset)) {
        *second = *first = translation_keys_ptr_ + offset;
        flag_bits_ &= ~0x4000;
        return slot;
    }
    if (slot != -1 && core::at(slots_ptr_, slot) && core::at(core::at(slots_ptr_, slot)->translation_keys_, bone)) {
        uint8_t* key = core::at(core::at(slots_ptr_, slot)->translation_keys_, bone);
        uint8_t* next = key + stride;
        if (static_cast<float>(trans_key_time(next)) >= frame) {
            if (static_cast<float>(trans_key_time(key)) <= frame) {
                *first = key;
                *second = next;
                flag_bits_ &= ~0x4000;
                return slot;
            }
            uint8_t* before = key - stride;
            if (!(before < translation_keys_ptr_ + core::at(translation_offsets_, bone)) && static_cast<float>(trans_key_time(before)) <= frame &&
                static_cast<float>(trans_key_time(key)) >= frame) {
                core::at(core::at(slots_ptr_, slot)->translation_keys_, bone) = before;
                *first = before;
                *second = key;
                flag_bits_ &= ~0x4000;
                return slot;
            }
        }
    }
    uint8_t* key = translation_keys_ptr_ + core::at(translation_offsets_, bone);
    uint8_t* next = key + stride;
    if (static_cast<float>(trans_key_time(next)) <= frame) {
        const uint8_t* end = translation_keys_ptr_ + end_offset;
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                *first = key;
                *second = key;
                flag_bits_ &= ~0x4000;
                return slot;
            }
            if (!(static_cast<float>(trans_key_time(next)) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (slot != -1 && core::at(slots_ptr_, slot))
        core::at(core::at(slots_ptr_, slot)->translation_keys_, bone) = key;
    flag_bits_ &= ~0x4000;
    return slot;
}

// --- setting up and reading the frame data ------------------------------------------------------------------

// The custom keys after the keys (the header's count), then their count - not when one fails. Not original - read
// through anim_format's reader within the animation (create_custom_anim_key's reader form).
bool JointClipFrames::read_events(const anim_format::AnimHeader& header, const anim_format::AnimLayout& layout)
{
    if (!header.custom_key_count) {
        events_ptr_ = nullptr;
        event_count_ = static_cast<int16_t>(header.custom_key_count);
        return true;
    }
    events_ptr_ = memory::adopt<UserClipKey**>(memory::alloc_block(header.custom_key_count * 4));
    util::ByteReader reader = anim_format::custom_key_reader(layout);
    for (uint32_t i = 0; i < header.custom_key_count; ++i) {
        gsl::owner<UserClipKey*> event = create_custom_anim_key(reader);
        if (!event)
            return false;
        events_ptr_[i] = event;
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
    if (layout.key_table)
        key_table_ = anim_format::key_table_bytes(layout);
    if (layout.partial_bones)
        partial_bones_ = anim_format::partial_bone_bits(layout);
    if (layout.rotation_keys) {
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
    if (layout.partial_bones)
        partial_bones_ = anim_format::partial_bone_bits(layout);
    key_counts_ = anim_format::key_count_bytes(layout);
    quat_keys_ = anim_format::rotation_key_bytes(layout);
    trans_keys_ = anim_format::translation_key_bytes(layout);
    return read_events(header, layout);
}

// 0x004844C0
bool JointClipFrames::get_float_key_pose(float* rotations, float* translations, void*, float time, int)
{
    auto quat_frame = static_cast<float>(static_cast<double>(time) * 60.0);
    const float trans_frame = quat_frame;
    // (kept from bone to bone when a bone's search finds nothing)
    const uint8_t* quat_from = quat_keys_;
    const uint8_t* quat_to = quat_keys_;
    float quat_t = 0.0f;
    const uint8_t* trans_from = trans_keys_;
    const uint8_t* trans_to = trans_keys_;
    float trans_t = 0.0f;
    const uint8_t* quat_keys = quat_keys_;
    const uint8_t* trans_keys = trans_keys_;
    for (int bone = 0; bone < bone_count_; ++bone) {
        int quat_count = 0, trans_count = 0;
        if (flag_bits_ & 0x400000) {
            quat_count = anim_format::wide_key_count(key_counts_, 2 * bone);
            trans_count = anim_format::wide_key_count(key_counts_, 2 * bone + 1);
        } else {
            quat_count = key_counts_[2 * bone];
            trans_count = key_counts_[2 * bone + 1];
        }
        for (int k = 0; k < quat_count; ++k) {
            const uint8_t* key = quat_keys + 16 * k;
            if (k == quat_count - 1) {
                quat_from = quat_to = key;
                quat_t = 0.0f;
                break;
            }
            const int from = quat_key_time(key);
            const int to = quat_key_time(key + 16);
            if (static_cast<float>(from) <= quat_frame && static_cast<float>(to) >= quat_frame) {
                quat_from = key;
                quat_to = key + 16;
                quat_t =
                    static_cast<float>((static_cast<double>(quat_frame) - from) / (static_cast<double>(to) - from));
                break;
            }
        }
        interpolate_quat(rotations, quat_from, quat_to, quat_t, (flag_bits_ & 0x9000000) != 0);
        for (int k = 0; k < trans_count; ++k) {
            const uint8_t* key = trans_keys + 16 * k;
            if (k == trans_count - 1) {
                trans_from = trans_to = key;
                trans_t = 0.0f;
                break;
            }
            const int from = trans_key_time(key);
            const int to = trans_key_time(key + 16);
            if (static_cast<float>(from) <= trans_frame && static_cast<float>(to) >= trans_frame) {
                trans_from = key;
                trans_to = key + 16;
                trans_t =
                    static_cast<float>((static_cast<double>(trans_frame) - from) / (static_cast<double>(to) - from));
                break;
            }
        }
        interpolate_trans(translations, trans_from, trans_to, trans_t, (flag_bits_ & 0x9000000) != 0);
        quat_keys += 16 * quat_count;
        trans_keys += 16 * trans_count;
        rotations += 4;
        translations += 4;
    }
    return true;
}

namespace {

// The frame cache's time for a cursor (0x00480590 on the entry): its direction - negative when time went back.

} // namespace

// 0x00484760
bool JointClipFrames::fetch_pose(float* rotations, float* translations, uint8_t* valid, float time, int cursor,
                                 FrameSource* source)
{
    auto quat_frame = static_cast<float>(static_cast<double>(time) * 60.0);
    float trans_frame = quat_frame; // (each clamped in turn by the cache's keys - kept for the later bones)
    const uint8_t* quat_data = quat_keys_;
    const uint8_t* trans_data = trans_keys_;
    const uint16_t* quat_size = quat_sizes_;
    const uint16_t* trans_size = trans_sizes_;
    bool forwards = true;
    const uint32_t mask = source && source->resume_ ? 1u << (source->mask_bit_ & 31) : 0u;
    if (cache_slot_) {
        uint32_t& counter = g_cache_use_counter;
        cache_slot_->last_used_ = counter++;
        if (cursor != -1 &&
            cache_slot_->set_slot_time(cursor, static_cast<int16_t>(static_cast<int>(static_cast<int64_t>(time)))) < 0)
            forwards = false;
    }
    AnimCacheEntry* entry = cache_slot_;
    for (int bone = 0; bone < bone_count_; ++bone) {
        bool skip = mask ? (source->bone_masks_[bone] & mask) != 0 : false;
        if (flag_bits_ & 0x80000)
            skip = !(partial_bones_[1 + bone / 32] & (1u << (bone % 32)));
        if (skip) {
            valid[bone] = 0;
        } else {
            valid[bone] = 1;
            // The rotation.
            if (*quat_size == 0) {
                rotations[0] = rotations[1] = rotations[2] = 0.0f;
                rotations[3] = 1.0f;
            } else {
                bool done = false;
                if (cache_slot_) {
                    uint8_t* first = nullptr;
                    uint8_t* second = nullptr;
                    const int found = forwards
                                          ? entry->find_quat_keys(bone, quat_frame, &first, &second, cursor)
                                          : entry->find_quat_keys_backwards(bone, quat_frame, &first, &second, cursor);
                    if (found != -2) {
                        const int from = quat_key_time(first);
                        const int to = quat_key_time(second);
                        FloatKey key_a = anim_format::float_key_at(first);
                        const FloatKey key_b = anim_format::float_key_at(second);
                        if (from == to) {
                            interpolate_float_quat(rotations, &key_a, &key_b, 0.0f);
                        } else {
                            if (quat_frame < static_cast<float>(from))
                                quat_frame = static_cast<float>(from);
                            else if (quat_frame > static_cast<float>(to))
                                quat_frame = static_cast<float>(to);
                            auto t = static_cast<float>((static_cast<double>(quat_frame) - from) /
                                                        (static_cast<double>(to) - from));
                            interpolate_float_quat(rotations, &key_a, &key_b, t);
                        }
                        anim_format::store_float_key(first, key_a); // (its values flipped in place when needed)
                        done = true;
                    }
                }
                if (!done) {
                    // Decompressing: from the bone's start (or where the source left it) to the pair around the frame.
                    const uint8_t* end = quat_data + *quat_size;
                    const uint8_t* at = quat_data;
                    if (source && source->resume_ && core::at(source->quat_at_, bone))
                        at = core::at(source->quat_at_, bone);
                    ShortKey a{}, b{};
                    float t = 0.0f;
                    for (;;) {
                        if (source)
                            core::at(source->quat_at_, bone) = at;
                        at = decompress_quat_key(at, &a, flag_bits_, key_table_);
                        if (at >= end) {
                            b = a;
                            t = 0.0f;
                            break;
                        }
                        decompress_quat_key(at, &b, flag_bits_, key_table_);
                        auto from = static_cast<float>(quat_key_time(a.time));
                        auto to = static_cast<float>(quat_key_time(b.time));
                        if (quat_frame >= from && quat_frame <= to) {
                            t = static_cast<float>((static_cast<double>(quat_frame) - from) /
                                                   (static_cast<double>(to) - from));
                            break;
                        }
                    }
                    interpolate_short_quat(rotations, &a, &b, t);
                }
            }
            // The translation (none for a bone without keys when flag 0x20000 says so).
            if (!((flag_bits_ & 0x20000) && *trans_size == 0)) {
                bool done = false;
                if (cache_slot_) {
                    uint8_t* first = nullptr;
                    uint8_t* second = nullptr;
                    const int found =
                        forwards ? entry->find_trans_keys(bone, trans_frame, &first, &second, cursor)
                                 : entry->find_trans_keys_backwards(bone, trans_frame, &first, &second, cursor);
                    if (found != -2) {
                        auto from = static_cast<float>(trans_key_time(first));
                        auto to = static_cast<float>(trans_key_time(second));
                        const bool add = (flag_bits_ & 0x4000) && (flag_bits_ & 0x20000);
                        const std::array<float, 4> before{translations[0], translations[1], translations[2], translations[3]};
                        const FloatKey key_a = anim_format::float_key_at(first);
                        const FloatKey key_b = anim_format::float_key_at(second);
                        auto add_before = [&] {
                            for (int i = 0; i < 4; ++i)
                                translations[i] += core::at(before, i);
                        };
                        if (from == to) {
                            interpolate_float_trans(translations, &key_a, &key_b, 0.0f);
                            if (add)
                                add_before();
                        } else if (trans_frame >= from) { // (before the first key: left as it was)
                            trans_frame = maths::confine(trans_frame, from, to);
                            auto t = static_cast<float>((static_cast<double>(trans_frame) - from) /
                                                        (static_cast<double>(to) - from));
                            interpolate_float_trans(translations, &key_a, &key_b, t);
                            if (add)
                                add_before();
                        }
                        done = true;
                    }
                }
                if (!done) {
                    const uint8_t* end = trans_data + *trans_size;
                    const uint8_t* at = trans_data;
                    if (source && source->resume_ && core::at(source->trans_at_, bone))
                        at = core::at(source->trans_at_, bone);
                    ShortKey a{}, b{};
                    float t = 0.0f;
                    for (;;) {
                        if (source)
                            core::at(source->trans_at_, bone) = at;
                        at = decompress_trans_key(at, &a);
                        if (at >= end) {
                            b = a;
                            break;
                        }
                        decompress_trans_key(at, &b);
                        const int from = trans_key_time(a.time);
                        if (!(static_cast<float>(from) <= trans_frame)) { // (the frame before this key: this key alone)
                            b = a;
                            break;
                        }
                        const int to = trans_key_time(b.time);
                        if (static_cast<float>(to) >= trans_frame) {
                            t = static_cast<float>((static_cast<double>(trans_frame) - from) /
                                                   (static_cast<double>(to) - from));
                            break;
                        }
                    }
                    std::array<float, 4> before{};
                    std::memcpy(before.data(), translations, sizeof(before));
                    interpolate_short_trans(translations, &a, &b, t);
                    if ((flag_bits_ & 0x4000) && (flag_bits_ & 0x20000)) {
                        for (int i = 0; i < 4; ++i)
                            translations[i] = core::at(before, i) + translations[i];
                    }
                }
            }
        }
        quat_data += *quat_size++;
        trans_data += *trans_size++;
        rotations += 4;
        translations += 4;
    }
    return true;
}

} // namespace gfx
