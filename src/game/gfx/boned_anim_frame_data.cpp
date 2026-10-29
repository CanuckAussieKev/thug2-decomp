// A boned animation's frame data: its keys decompressed and interpolated, its events, its frame cache entry - see
// boned_anim_frame_data.h.

#include "game/gfx/boned_anim_frame_data.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/gfx/anim_blend.h"
#include "game/gfx/anim_track.h"
#include "game/mth/math.h"
#include "game/mth/vector.h"
#include "game/obj/walk_hang.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

constexpr double kQuatUnit = 1.0 / 16384.0; // (0x006487C8)
constexpr double kTransUnit = 1.0 / 32.0;   // (0x006487CC)

int16_t* Table(uint32_t address) { return reinterpret_cast<int16_t*>(address); }

float Scaled(int16_t value, double unit) { return static_cast<float>(value * unit); }

uint16_t Word(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// The time word of a key at the head of `in`, as the original merges it (the sign bit, then the rest).
uint16_t TimeWord(uint16_t out, uint16_t in)
{
    uint16_t merged = static_cast<uint16_t>((out & 0x7FFF) | (in & 0x8000));
    return static_cast<uint16_t>((merged & 0x8000) | (in & 0x7FFF));
}

// w from x, y, z: the square root of what's left of 1 (x87 at 53 bits, in this order), negative with bit 15 of the
// time word.
float QuatW(float x, float y, float z, uint16_t time, bool nanToZero)
{
    double left = 1.0 - static_cast<double>(x) * x;
    left -= static_cast<double>(y) * y;
    left -= static_cast<double>(z) * z;
    // (0x004838D0 tests "below 0 or unordered" - NaN gives 0; the others only "below 0")
    if (nanToZero ? !(left >= 0.0) : left < 0.0)
        left = 0.0;
    double w = std::sqrt(left);
    return static_cast<float>((time & 0x8000) ? -w : w);
}

// The frame cache (0x006F92B0) and its members (not rewritten here).
void* FrameCache() { return GameFunction<void*(__cdecl*)()>(0x00480660)(); }
void CacheRemove(void* cache, CBonedAnimFrameData* data)
{
    GameFunction<void(__thiscall*)(void*, CBonedAnimFrameData*)>(0x00480DF0)(cache, data);
}
int CacheAcquire(void* cache, CBonedAnimFrameData* data)
{
    return GameFunction<int(__thiscall*)(void*, CBonedAnimFrameData*)>(0x00480E30)(cache, data);
}
void CacheRelease(void* cache, CBonedAnimFrameData* data, int index)
{
    GameFunction<void(__thiscall*)(void*, CBonedAnimFrameData*, int)>(0x00480E80)(cache, data, index);
}
// The events' arming and firing.
void ArmEvent(CAnimEvent* e, bool armed) { GameFunction<void(__thiscall*)(CAnimEvent*, bool)>(0x00486150)(e, armed); }
bool FireEvent(CAnimEvent* e, void* target)
{
    return GameFunction<bool(__thiscall*)(CAnimEvent*, void*)>(0x00486160)(e, target);
}

template <typename R, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

uint16_t ReferenceBits(uint16_t bits, int change)
{
    int count = (bits & 0xFFFE) + 2 * change;
    return static_cast<uint16_t>((bits & ~0x1FE) | (count & 0x1FE));
}

} // namespace

// --- the shared tables --------------------------------------------------------------------------------------

// 0x00482970
bool __cdecl ReadKeyTable(void* file, int16_t* table)
{
    for (int i = 0; i < 256; ++i, table += 4) {
        for (int k = 0; k < 4; ++k) {
            if (!File::Read(table + k, 2, 1, static_cast<File::Handle>(file)))
                return false;
        }
    }
    return true;
}

// 0x004829F0
bool __cdecl LoadQuatTable(const char* name)
{
    bool loaded = false;
    File::Handle file = File::Open(name, "rb");
    if (file)
        loaded = ReadKeyTable(file, Table(kQuatTable));
    File::Close(file); // (even when it didn't open)
    return loaded;
}

// 0x00482A30
bool __cdecl LoadTransTable(const char* name)
{
    bool loaded = false;
    File::Handle file = File::Open(name, "rb");
    if (file)
        loaded = ReadKeyTable(file, Table(kTransTable));
    File::Close(file);
    return loaded;
}

// --- decompressing keys -------------------------------------------------------------------------------------

// 0x00482C20
const uint8_t* __cdecl DecompressQuatKey(const uint8_t* data, CShortKey* out, uint32_t flags, const uint8_t* table)
{
    const int16_t* shared = Table(kQuatTable);
    uint16_t first = Word(data);
    uint16_t header;
    if (flags & 0x100) {
        header = Word(data + 2);
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
            uint8_t b = *data++;
            return (flags & 0x10000) ? shared[b * 4 + 3] : static_cast<int16_t>(b);
        }
        if (flags & 0x8000)
            return static_cast<int16_t>(*data++ << 8);
        int16_t v = static_cast<int16_t>(Word(data));
        data += 2;
        return v;
    };
    if (!(header & 0x4000)) {
        // Uncompressed: three shorts (or three bytes moved up).
        if (flags & 0x8000) {
            for (int i = 0; i < 3; ++i)
                out->value[i] = static_cast<int16_t>(data[i] << 8);
            data += 3;
        } else {
            for (int i = 0; i < 3; ++i)
                out->value[i] = static_cast<int16_t>(Word(data + 2 * i));
            data += 6;
        }
    } else {
        if (!(header & 0x3800)) {
            // All three from a table entry.
            uint8_t b = *data++;
            if (flags & 0x2000) {
                for (int i = 0; i < 3; ++i)
                    out->value[i] = static_cast<int16_t>(Word(table + b * 6 + 2 * i));
            } else {
                for (int i = 0; i < 3; ++i)
                    out->value[i] = shared[b * 4 + i];
            }
        } else {
            out->value[0] = value(0x2000);
            out->value[1] = value(0x1000);
            out->value[2] = value(0x0800);
        }
        // The time: 11 bits here (and the sign).
        out->time = static_cast<uint16_t>((out->time & 0x8000) | (out->time & 0x7FF));
    }
    if (flags & 0x100)
        out->time = static_cast<uint16_t>((out->time & 0x8000) | (first & 0x7FFF));
    return data;
}

// 0x00482E70
const uint8_t* __cdecl DecompressTransKey(const uint8_t* data, CShortKey* out)
{
    uint8_t head = *data++;
    if (head & 0x40) {
        out->time = static_cast<uint16_t>(head & 0x3F);
    } else {
        out->time = Word(data);
        data += 2;
    }
    if (head & 0x80) {
        const int16_t* entry = Table(kTransTable) + *data++ * 4;
        for (int i = 0; i < 3; ++i)
            out->value[i] = entry[i];
    } else {
        for (int i = 0; i < 3; ++i)
            out->value[i] = static_cast<int16_t>(Word(data + 2 * i));
        data += 6;
    }
    return data;
}

// 0x00482A70
void __cdecl ExpandQuatKey(CFloatKey* out, const CShortKey* in)
{
    out->time = TimeWord(out->time, in->time);
    for (int i = 0; i < 3; ++i)
        out->value[i] = Scaled(in->value[i], kQuatUnit);
}

// 0x00483D00
void __cdecl ExpandQuatKeyWithW(CFloatKey* out, const CShortKey* in)
{
    out->time = TimeWord(out->time, in->time);
    if (!in->value[0] && !in->value[1] && !in->value[2]) {
        out->value[0] = out->value[1] = out->value[2] = 0.0f;
        out->value[3] = 1.0f;
        return;
    }
    float x = Scaled(in->value[0], kQuatUnit), y = Scaled(in->value[1], kQuatUnit), z = Scaled(in->value[2], kQuatUnit);
    float w = QuatW(x, y, z, in->time, false);
    out->value[0] = x;
    out->value[1] = y;
    out->value[2] = z;
    out->value[3] = w;
}

// --- a key's value ------------------------------------------------------------------------------------------

// 0x004838D0
void __cdecl GetQuat(const void* key, float* out, bool floats)
{
    if (floats) {
        std::memcpy(out, &static_cast<const CFloatKey*>(key)->value[0], 3 * sizeof(float));
    } else {
        for (int i = 0; i < 3; ++i)
            out[i] = Scaled(static_cast<const CShortKey*>(key)->value[i], kQuatUnit);
    }
    out[3] = QuatW(out[0], out[1], out[2], static_cast<const CShortKey*>(key)->time, true);
}

// 0x00483990
void __cdecl GetTrans(const void* key, float* out, bool floats)
{
    if (floats) {
        std::memcpy(out, &static_cast<const CFloatKey*>(key)->value[0], 3 * sizeof(float));
    } else {
        for (int i = 0; i < 3; ++i)
            out[i] = Scaled(static_cast<const CShortKey*>(key)->value[i], kTransUnit);
    }
    out[3] = 1.0f;
}

// 0x00483A00
void __cdecl GetShortQuat(const CShortKey* key, float* out)
{
    if (!key->value[0] && !key->value[1] && !key->value[2]) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    float x = Scaled(key->value[0], kQuatUnit), y = Scaled(key->value[1], kQuatUnit), z = Scaled(key->value[2], kQuatUnit);
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = QuatW(x, y, z, key->time, false);
}

// 0x00483AE0
void __cdecl GetShortTrans(const CShortKey* key, float* out)
{
    for (int i = 0; i < 3; ++i)
        out[i] = Scaled(key->value[i], kTransUnit);
    out[3] = 1.0f;
}

// --- between two keys ---------------------------------------------------------------------------------------

// 0x00483B30
void __cdecl InterpolateQuat(float* out, const void* a, const void* b, float t, bool floats)
{
    if (t == 0.0f) {
        GetQuat(a, out, floats);
        return;
    }
    if (t == 1.0f) {
        GetQuat(b, out, floats);
        return;
    }
    float from[4] = {0.0f, 0.0f, 0.0f, 1.0f}, to[4] = {0.0f, 0.0f, 0.0f, 1.0f}, result[4];
    GetQuat(a, from, floats);
    GetQuat(b, to, floats);
    InterpolateRotation(result, from, to, t);
    std::memcpy(out, result, sizeof(result));
}

// 0x00483C40
void __cdecl InterpolateTrans(float* out, const void* a, const void* b, float t, bool floats)
{
    if (t == 0.0f) {
        GetTrans(a, out, floats);
        return;
    }
    if (t == 1.0f) {
        GetTrans(b, out, floats);
        return;
    }
    Mth::Vector from, to, result;
    GetTrans(a, &from.x, floats);
    GetTrans(b, &to.x, floats);
    const Mth::Vector* lerped = Walk::Lerp(&result, &from, &to, t);
    std::memcpy(out, lerped, sizeof(Mth::Vector));
}

// 0x00483E10
void __cdecl InterpolateShortQuat(float* out, const CShortKey* a, const CShortKey* b, float t)
{
    if (t == 0.0f) {
        GetShortQuat(a, out);
        return;
    }
    if (t == 1.0f) {
        GetShortQuat(b, out);
        return;
    }
    float from[4] = {0.0f, 0.0f, 0.0f, 1.0f}, to[4] = {0.0f, 0.0f, 0.0f, 1.0f}, result[4];
    GetShortQuat(a, from);
    GetShortQuat(b, to);
    InterpolateRotation(result, from, to, t);
    std::memcpy(out, result, sizeof(result));
}

// 0x00484080
void __cdecl InterpolateShortTrans(float* out, const CShortKey* a, const CShortKey* b, float t)
{
    if (t == 0.0f) {
        GetShortTrans(a, out);
        return;
    }
    if (t == 1.0f) {
        GetShortTrans(b, out);
        return;
    }
    Mth::Vector from, to, result;
    GetShortTrans(a, &from.x);
    GetShortTrans(b, &to.x);
    const Mth::Vector* lerped = Walk::Lerp(&result, &from, &to, t);
    std::memcpy(out, lerped, sizeof(Mth::Vector));
}

// 0x00483F10
void __cdecl InterpolateFloatQuat(float* out, CFloatKey* a, const CFloatKey* b, float t)
{
    if (t == 0.0f) {
        std::memcpy(out, a->value, 4 * sizeof(float));
        return;
    }
    if (t == 1.0f) {
        std::memcpy(out, b->value, 4 * sizeof(float));
        return;
    }
    const float* p = a->value;
    const float* q = b->value;
    // (x87 at 53 bits, in the original's order: w, x, z, y)
    double dot = static_cast<double>(p[3]) * q[3] + static_cast<double>(p[0]) * q[0];
    dot += static_cast<double>(q[2]) * p[2];
    dot += static_cast<double>(p[1]) * q[1];
    if (dot < 0.0) {
        for (int i = 0; i < 4; ++i)
            a->value[i] = -b->value[i];
    }
    // x and w stay unrounded; y and z are rounded to floats on the way.
    double x = (static_cast<double>(q[0]) - p[0]) * t + p[0];
    float y = static_cast<float>((static_cast<double>(q[1]) - p[1]) * t + p[1]);
    float z = static_cast<float>((static_cast<double>(q[2]) - p[2]) * t + p[2]);
    double w = (static_cast<double>(q[3]) - p[3]) * t + p[3];
    double sum = w * w + static_cast<double>(z) * z;
    sum += static_cast<double>(y) * y;
    sum += x * x;
    double length = std::sqrt(sum);
    if (length == 0.0)
        length = static_cast<double>(GameVariable<float>(0x006453B8)); // 0.00001f
    double scale = 1.0 / length;
    out[0] = static_cast<float>(scale * x);
    out[1] = static_cast<float>(y * scale);
    out[2] = static_cast<float>(z * scale);
    out[3] = static_cast<float>(scale * w);
}

// 0x00484170
void __cdecl InterpolateFloatTrans(float* out, const CFloatKey* a, const CFloatKey* b, float t)
{
    if (t == 0.0f || t == 1.0f) {
        const CFloatKey* key = t == 0.0f ? a : b;
        std::memcpy(out, key->value, 3 * sizeof(float));
        out[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<float>((static_cast<double>(b->value[i]) - a->value[i]) * t + a->value[i]);
    out[3] = 1.0f;
}

// --- the frame data -----------------------------------------------------------------------------------------

// 0x00482AF0
CBonedAnimFrameData* CBonedAnimFrameData::Construct()
{
    m_bits &= 0xFE00;
    m_unknown00 = 0;
    m_boneCount = 0;
    mp_keyTable = nullptr;
    mp_quatKeys = nullptr;
    mp_transKeys = nullptr;
    mp_keyCounts = nullptr;
    mp_boneNames = nullptr;
    mp_quatSizes = nullptr;
    mp_transSizes = nullptr;
    m_eventCount = 0;
    mp_events = nullptr;
    mp_cacheEntry = nullptr;
    mp_partialBones = nullptr;
    return this;
}

// 0x00482B30
void CBonedAnimFrameData::DestroyBody()
{
    for (int i = 0; i < m_eventCount; ++i) {
        if (CAnimEvent* event = mp_events[i])
            Virtual<void*>(event, 0, 1);
    }
    if (mp_events)
        Mem::FreeMalloc(mp_events);
    if (mp_cacheEntry) {
        if (void* cache = FrameCache())
            CacheRemove(cache, this);
    }
}

// 0x00482B90
void __cdecl CBonedAnimFrameData::ForgetCacheEntry(CBonedAnimFrameData* data)
{
    if (data)
        data->mp_cacheEntry = nullptr;
}

// 0x00482BA0
int CBonedAnimFrameData::AcquireCacheIndex()
{
    m_bits = ReferenceBits(m_bits, 1);
    return CacheAcquire(FrameCache(), this);
}

// 0x00482BD0
void CBonedAnimFrameData::ReleaseCacheIndex(int index)
{
    // (the count as a signed byte)
    if (static_cast<int8_t>((m_bits >> 1) & 0xFF) <= 0)
        return;
    m_bits = ReferenceBits(m_bits, -1);
    CacheRelease(FrameCache(), this, index);
}

// 0x00482F00
uint32_t CBonedAnimFrameData::GetBoneName(int index) { return mp_boneNames[index]; }

// 0x00482F10
bool CBonedAnimFrameData::ArmEvents()
{
    int count = m_eventCount;
    for (int i = 0; i < count; ++i)
        ArmEvent(mp_events[i], true);
    return true;
}

// 0x00482F40
bool CBonedAnimFrameData::FireEvents(float from, float to, void* target, int flag)
{
    float fromFrame = static_cast<float>(static_cast<double>(from) * 60.0);
    float toFrame = static_cast<float>(static_cast<double>(to) * 60.0);
    int count = m_eventCount;
    for (int i = 0; i < count; ++i) {
        CAnimEvent* event = mp_events[i];
        if (Virtual<bool>(event, 1, fromFrame, toFrame, flag))
            FireEvent(event, target);
    }
    return true;
}

// --- the frame cache entry's key searches -------------------------------------------------------------------

namespace {

// A key's time: 15 bits, sign-extended (quaternion keys - bit 15 is w's sign), or the whole short (translations).
int QuatKeyTime(const uint8_t* key)
{
    uint16_t word = static_cast<uint16_t>(key[0] | (key[1] << 8));
    return static_cast<int16_t>(static_cast<uint16_t>(word << 1)) >> 1;
}
int TransKeyTime(const uint8_t* key) { return static_cast<int16_t>(static_cast<uint16_t>(key[0] | (key[1] << 8))); }

} // namespace

// 0x00482FB0
int CFrameCacheEntry::FindQuatKeys(int bone, float frame, uint8_t** first, uint8_t** second, int cursor)
{
    if (m_flags & 0x8000)
        return -2;
    m_flags |= 0x4000;
    int stride = m_quatStride;
    uint8_t* start = mp_quatKeys + m_quatOffsets[bone];
    uint8_t* end;
    if (bone == m_boneCount - 1) {
        if (!(start + stride < mp_transKeys)) {
            *second = *first = start;
            m_flags &= ~0x4000;
            return cursor;
        }
        end = mp_transKeys;
    } else {
        if (!(m_quatOffsets[bone] + stride < m_quatOffsets[bone + 1])) {
            *second = *first = start;
            m_flags &= ~0x4000;
            return cursor;
        }
        end = mp_quatKeys + m_quatOffsets[bone + 1];
    }
    // From the cursor: its pair still around the frame, or the next one.
    if (cursor != -1 && mp_cursors[cursor] && mp_cursors[cursor][bone]) {
        uint8_t* key = mp_cursors[cursor][bone];
        uint8_t* next = key + stride;
        if (QuatKeyTime(key) <= frame) {
            if (QuatKeyTime(next) >= frame) {
                *first = key;
                *second = next;
                m_flags &= ~0x4000;
                return cursor;
            }
            uint8_t* after = next + stride;
            if (after < end && QuatKeyTime(after) >= frame) {
                mp_cursors[cursor][bone] = next;
                *first = next;
                *second = after;
                m_flags &= ~0x4000;
                return cursor;
            }
        }
    }
    // From the bone's first key.
    uint8_t* key = start;
    uint8_t* next = start + stride;
    if (QuatKeyTime(next) <= frame) {
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                next = key; // (past the last: the last alone)
                break;
            }
            if (!(QuatKeyTime(next) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (cursor != -1 && mp_cursors[cursor])
        mp_cursors[cursor][bone] = key;
    m_flags &= ~0x4000;
    return cursor;
}

// 0x00483210
int CFrameCacheEntry::FindTransKeys(int bone, float frame, uint8_t** first, uint8_t** second, int cursor)
{
    if (m_flags & 0x8000)
        return -2;
    m_flags |= 0x4000;
    int stride = m_transStride;
    uint8_t* start = mp_transKeys + m_transOffsets[bone];
    uint8_t* end;
    if (bone == m_boneCount - 1) {
        end = mp_quatKeys + m_size;
        if (!(start + stride < end)) {
            *second = *first = start;
            m_flags &= ~0x4000;
            return cursor;
        }
    } else {
        if (!(m_transOffsets[bone] + stride < m_transOffsets[bone + 1])) {
            *second = *first = start;
            m_flags &= ~0x4000;
            return cursor;
        }
        end = mp_transKeys + m_transOffsets[bone + 1];
    }
    if (cursor != -1 && mp_cursors[cursor] && mp_cursors[cursor][60 + bone]) {
        uint8_t* key = mp_cursors[cursor][60 + bone];
        uint8_t* next = key + stride;
        if (TransKeyTime(key) <= frame) {
            if (TransKeyTime(next) >= frame) {
                *first = key;
                *second = next;
                m_flags &= ~0x4000;
                return cursor;
            }
            uint8_t* after = next + stride;
            if (after < end && TransKeyTime(after) >= frame) {
                mp_cursors[cursor][60 + bone] = next;
                *first = next;
                *second = after;
                m_flags &= ~0x4000;
                return cursor;
            }
        }
    }
    uint8_t* key = start;
    uint8_t* next = start + stride;
    if (TransKeyTime(next) <= frame) {
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                // Past the last: the last alone, and (unlike the quaternions') the cursor left as it was.
                *first = key;
                *second = key;
                m_flags &= ~0x4000;
                return cursor;
            }
            if (!(TransKeyTime(next) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (cursor != -1 && mp_cursors[cursor])
        mp_cursors[cursor][60 + bone] = key;
    m_flags &= ~0x4000;
    return cursor;
}

// 0x00483470
int CFrameCacheEntry::FindQuatKeysBackwards(int bone, float frame, uint8_t** first, uint8_t** second, int cursor)
{
    if (m_flags & 0x8000)
        return -2;
    m_flags |= 0x4000;
    // (the last bone's keys end where they start here: always one key)
    int offset = m_quatOffsets[bone];
    int endOffset = m_quatOffsets[bone == m_boneCount - 1 ? bone : bone + 1];
    int stride = m_quatStride;
    if (!(offset + stride < endOffset)) {
        *second = *first = mp_quatKeys + offset;
        m_flags &= ~0x4000;
        return cursor;
    }
    // From the cursor: its pair still around the frame, or the one before.
    if (cursor != -1 && mp_cursors[cursor] && mp_cursors[cursor][bone]) {
        uint8_t* key = mp_cursors[cursor][bone];
        uint8_t* next = key + stride;
        if (QuatKeyTime(next) >= frame) {
            if (QuatKeyTime(key) <= frame) {
                *first = key;
                *second = next;
                m_flags &= ~0x4000;
                return cursor;
            }
            uint8_t* before = key - stride;
            if (!(before < mp_quatKeys + offset) && QuatKeyTime(before) <= frame && QuatKeyTime(key) >= frame) {
                mp_cursors[cursor][bone] = before;
                *first = before;
                *second = key;
                m_flags &= ~0x4000;
                return cursor;
            }
        }
    }
    uint8_t* key = mp_quatKeys + m_quatOffsets[bone];
    uint8_t* next = key + stride;
    if (QuatKeyTime(next) <= frame) {
        uint8_t* end = mp_quatKeys + endOffset;
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                *first = key;
                *second = key;
                if (cursor != -1)
                    mp_cursors[cursor][bone] = key; // (no check for a cursor array here, as the original)
                m_flags &= ~0x4000;
                return cursor;
            }
            if (!(QuatKeyTime(next) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (cursor != -1 && mp_cursors[cursor])
        mp_cursors[cursor][bone] = key;
    m_flags &= ~0x4000;
    return cursor;
}

// 0x004836C0
int CFrameCacheEntry::FindTransKeysBackwards(int bone, float frame, uint8_t** first, uint8_t** second, int cursor)
{
    if (m_flags & 0x8000)
        return -2;
    m_flags |= 0x4000;
    int offset = m_transOffsets[bone];
    int endOffset = m_transOffsets[bone == m_boneCount - 1 ? bone : bone + 1];
    int stride = m_transStride;
    if (!(offset + stride < endOffset)) {
        *second = *first = mp_transKeys + offset;
        m_flags &= ~0x4000;
        return cursor;
    }
    if (cursor != -1 && mp_cursors[cursor] && mp_cursors[cursor][60 + bone]) {
        uint8_t* key = mp_cursors[cursor][60 + bone];
        uint8_t* next = key + stride;
        if (TransKeyTime(next) >= frame) {
            if (TransKeyTime(key) <= frame) {
                *first = key;
                *second = next;
                m_flags &= ~0x4000;
                return cursor;
            }
            uint8_t* before = key - stride;
            if (!(before < mp_transKeys + m_transOffsets[bone]) && TransKeyTime(before) <= frame &&
                TransKeyTime(key) >= frame) {
                mp_cursors[cursor][60 + bone] = before;
                *first = before;
                *second = key;
                m_flags &= ~0x4000;
                return cursor;
            }
        }
    }
    uint8_t* key = mp_transKeys + m_transOffsets[bone];
    uint8_t* next = key + stride;
    if (TransKeyTime(next) <= frame) {
        uint8_t* end = mp_transKeys + endOffset;
        for (;;) {
            key = next;
            next += stride;
            if (next >= end) {
                *first = key;
                *second = key;
                m_flags &= ~0x4000;
                return cursor;
            }
            if (!(TransKeyTime(next) <= frame))
                break;
        }
    }
    *first = key;
    *second = next;
    if (cursor != -1 && mp_cursors[cursor])
        mp_cursors[cursor][60 + bone] = key;
    m_flags &= ~0x4000;
    return cursor;
}

// --- setting up and reading the frame data ------------------------------------------------------------------

namespace {

uint8_t* Aligned(uint8_t* p)
{
    uintptr_t misaligned = reinterpret_cast<uintptr_t>(p) & 3;
    return misaligned ? p + (4 - misaligned) : p;
}
// Past the partial-animation bits at `p` (a count, then a word per 32 bones - one too many, as the original).
uint8_t* PastBoneBits(uint8_t* p)
{
    int count = *reinterpret_cast<int32_t*>(p);
    return p + ((count - 1) / 32) * 4 + 8;
}
// An event made from the data at `*at` (0x00486680, not rewritten here), which it moves past.
CAnimEvent* ReadEvent(uint8_t** at) { return GameFunction<CAnimEvent*(__cdecl*)(uint8_t**)>(0x00486680)(at); }

} // namespace

// The events after the keys, then their count (not when one fails).
bool CBonedAnimFrameData::ReadEvents(const uint32_t* header, uint8_t* at)
{
    if (!header[3]) {
        mp_events = nullptr;
        m_eventCount = static_cast<int16_t>(header[3]);
        return true;
    }
    mp_events = static_cast<CAnimEvent**>(Mem::Malloc(header[3] * 4));
    for (uint32_t i = 0; i < header[3]; ++i) {
        CAnimEvent* event = ReadEvent(&at);
        if (!event)
            return false;
        mp_events[i] = event;
    }
    m_eventCount = static_cast<int16_t>(header[3]);
    return true;
}

// 0x00484200
bool CBonedAnimFrameData::SetUpCompressed(uint8_t* data)
{
    const auto* header = reinterpret_cast<const uint32_t*>(data);
    m_boneCount = static_cast<int>(header[0]);
    m_quatKeyCount = static_cast<int16_t>(header[1]);
    m_transKeyCount = static_cast<int16_t>(header[2]);
    uint32_t quatSize = header[4];
    uint32_t transSize = header[5];
    uint8_t* p = data + 0x18;
    mp_quatSizes = reinterpret_cast<uint16_t*>(p);
    p += m_boneCount * 2;
    mp_transSizes = reinterpret_cast<uint16_t*>(p);
    p = Aligned(p + m_boneCount * 2);
    uint32_t flags = m_flags;
    mp_boneNames = nullptr;
    if (flags & 0x2000) {
        mp_keyTable = p;
        p += 0x600;
    }
    if (flags & 0x80000) {
        mp_partialBones = reinterpret_cast<uint32_t*>(p);
        p = PastBoneBits(p);
    }
    p = Aligned(p);
    if (!(flags & 0x8000000) && !(flags & 0x1000000)) {
        mp_quatKeys = p;
        mp_transKeys = p + quatSize;
        p += quatSize + transSize;
    }
    return ReadEvents(header, Aligned(p));
}

// 0x00484370
bool CBonedAnimFrameData::SetUpFloatKeys(uint8_t* data)
{
    const auto* header = reinterpret_cast<const uint32_t*>(data);
    m_boneCount = static_cast<int>(header[0]);
    uint32_t flags = m_flags;
    m_quatKeyCount = static_cast<int16_t>(header[1]);
    m_transKeyCount = static_cast<int16_t>(header[2]);
    uint8_t* p = data + 0x10;
    if (flags & 0x1000000) {
        mp_boneNames = reinterpret_cast<uint32_t*>(p);
        p += m_boneCount * 4;
    } else {
        mp_boneNames = nullptr;
    }
    if (flags & 0x80000) {
        mp_partialBones = reinterpret_cast<uint32_t*>(p);
        p = PastBoneBits(p);
    }
    mp_keyCounts = p;
    p = Aligned(p + m_boneCount * ((flags & 0x400000) ? 4 : 2));
    int keySize = (flags & 0x8000000) || (flags & 0x1000000) ? 16 : 8;
    mp_quatKeys = p;
    mp_transKeys = p + m_quatKeyCount * keySize;
    p = mp_transKeys + m_transKeyCount * keySize;
    return ReadEvents(header, Aligned(p));
}

// 0x004844C0
bool CBonedAnimFrameData::GetFloatKeyPose(float* rotations, float* translations, void*, float time, int)
{
    float quatFrame = static_cast<float>(static_cast<double>(time) * 60.0);
    float transFrame = quatFrame;
    // (kept from bone to bone when a bone's search finds nothing)
    uint8_t* quatFrom = mp_quatKeys;
    uint8_t* quatTo = mp_quatKeys;
    float quatT = 0.0f;
    uint8_t* transFrom = mp_transKeys;
    uint8_t* transTo = mp_transKeys;
    float transT = 0.0f;
    uint8_t* quatKeys = mp_quatKeys;
    uint8_t* transKeys = mp_transKeys;
    for (int bone = 0; bone < m_boneCount; ++bone) {
        int quatCount, transCount;
        if (m_flags & 0x400000) {
            quatCount = reinterpret_cast<const int16_t*>(mp_keyCounts)[2 * bone];
            transCount = reinterpret_cast<const int16_t*>(mp_keyCounts)[2 * bone + 1];
        } else {
            quatCount = mp_keyCounts[2 * bone];
            transCount = mp_keyCounts[2 * bone + 1];
        }
        for (int k = 0; k < quatCount; ++k) {
            uint8_t* key = quatKeys + 16 * k;
            if (k == quatCount - 1) {
                quatFrom = quatTo = key;
                quatT = 0.0f;
                break;
            }
            int from = QuatKeyTime(key), to = QuatKeyTime(key + 16);
            if (from <= quatFrame && to >= quatFrame) {
                quatFrom = key;
                quatTo = key + 16;
                quatT = static_cast<float>((static_cast<double>(quatFrame) - from) / (static_cast<double>(to) - from));
                break;
            }
        }
        InterpolateQuat(rotations, quatFrom, quatTo, quatT, (m_flags & 0x9000000) != 0);
        for (int k = 0; k < transCount; ++k) {
            uint8_t* key = transKeys + 16 * k;
            if (k == transCount - 1) {
                transFrom = transTo = key;
                transT = 0.0f;
                break;
            }
            int from = TransKeyTime(key), to = TransKeyTime(key + 16);
            if (from <= transFrame && to >= transFrame) {
                transFrom = key;
                transTo = key + 16;
                transT = static_cast<float>((static_cast<double>(transFrame) - from) / (static_cast<double>(to) - from));
                break;
            }
        }
        InterpolateTrans(translations, transFrom, transTo, transT, (m_flags & 0x9000000) != 0);
        quatKeys += 16 * quatCount;
        transKeys += 16 * transCount;
        rotations += 4;
        translations += 4;
    }
    return true;
}

namespace {

// The frame cache's time for a cursor (0x00480590 on the entry): its direction - negative when time went back.
int16_t CacheDirection(uint8_t* entry, int cursor, int time)
{
    return GameFunction<int16_t(__thiscall*)(uint8_t*, int, int)>(0x00480590)(entry, cursor, time);
}

} // namespace

// 0x00484760
bool CBonedAnimFrameData::GetPose(float* rotations, float* translations, uint8_t* valid, float time, int cursor,
                                  CFrameSource* source)
{
    float quatFrame = static_cast<float>(static_cast<double>(time) * 60.0);
    float transFrame = quatFrame; // (each clamped in turn by the cache's keys - kept for the later bones)
    uint8_t* quatData = mp_quatKeys;
    uint8_t* transData = mp_transKeys;
    const uint16_t* quatSize = mp_quatSizes;
    const uint16_t* transSize = mp_transSizes;
    bool forwards = true;
    uint32_t mask = source && source->m_resume ? 1u << (source->m_maskBit & 31) : 0u;
    if (mp_cacheEntry) {
        uint32_t& counter = GameVariable<uint32_t>(0x006F92B4);
        *reinterpret_cast<uint32_t*>(mp_cacheEntry + 4) = counter++;
        if (cursor != -1 && CacheDirection(mp_cacheEntry, cursor, static_cast<int>(static_cast<int64_t>(time))) < 0)
            forwards = false;
    }
    auto* entry = reinterpret_cast<CFrameCacheEntry*>(mp_cacheEntry);
    for (int bone = 0; bone < m_boneCount; ++bone) {
        bool skip = mask ? (source->mp_boneMasks[bone] & mask) != 0 : false;
        if (m_flags & 0x80000)
            skip = !(mp_partialBones[1 + bone / 32] & (1u << (bone % 32)));
        if (skip) {
            valid[bone] = 0;
        } else {
            valid[bone] = 1;
            // The rotation.
            if (*quatSize == 0) {
                rotations[0] = rotations[1] = rotations[2] = 0.0f;
                rotations[3] = 1.0f;
            } else {
                bool done = false;
                if (mp_cacheEntry) {
                    uint8_t* first;
                    uint8_t* second;
                    int found = forwards ? entry->FindQuatKeys(bone, quatFrame, &first, &second, cursor)
                                         : entry->FindQuatKeysBackwards(bone, quatFrame, &first, &second, cursor);
                    if (found != -2) {
                        int from = QuatKeyTime(first), to = QuatKeyTime(second);
                        if (from == to) {
                            InterpolateFloatQuat(rotations, reinterpret_cast<CFloatKey*>(first),
                                                 reinterpret_cast<CFloatKey*>(second), 0.0f);
                        } else {
                            if (quatFrame < from)
                                quatFrame = static_cast<float>(from);
                            else if (quatFrame > to)
                                quatFrame = static_cast<float>(to);
                            float t = static_cast<float>((static_cast<double>(quatFrame) - from) /
                                                         (static_cast<double>(to) - from));
                            InterpolateFloatQuat(rotations, reinterpret_cast<CFloatKey*>(first),
                                                 reinterpret_cast<CFloatKey*>(second), t);
                        }
                        done = true;
                    }
                }
                if (!done) {
                    // Decompressing: from the bone's start (or where the source left it) to the pair around the frame.
                    const uint8_t* end = quatData + *quatSize;
                    const uint8_t* at = quatData;
                    if (source && source->m_resume && source->mp_quatAt[bone])
                        at = source->mp_quatAt[bone];
                    CShortKey a, b;
                    float t;
                    for (;;) {
                        if (source)
                            source->mp_quatAt[bone] = at;
                        at = DecompressQuatKey(at, &a, m_flags, mp_keyTable);
                        if (at >= end) {
                            b = a;
                            t = 0.0f;
                            break;
                        }
                        DecompressQuatKey(at, &b, m_flags, mp_keyTable);
                        float from = static_cast<float>(QuatKeyTime(reinterpret_cast<const uint8_t*>(&a.time)));
                        float to = static_cast<float>(QuatKeyTime(reinterpret_cast<const uint8_t*>(&b.time)));
                        if (quatFrame >= from && quatFrame <= to) {
                            t = static_cast<float>((static_cast<double>(quatFrame) - from) / (static_cast<double>(to) - from));
                            break;
                        }
                    }
                    InterpolateShortQuat(rotations, &a, &b, t);
                }
            }
            // The translation (none for a bone without keys when flag 0x20000 says so).
            if (!((m_flags & 0x20000) && *transSize == 0)) {
                bool done = false;
                if (mp_cacheEntry) {
                    uint8_t* first;
                    uint8_t* second;
                    int found = forwards ? entry->FindTransKeys(bone, transFrame, &first, &second, cursor)
                                         : entry->FindTransKeysBackwards(bone, transFrame, &first, &second, cursor);
                    if (found != -2) {
                        float from = static_cast<float>(TransKeyTime(first));
                        float to = static_cast<float>(TransKeyTime(second));
                        bool add = (m_flags & 0x4000) && (m_flags & 0x20000);
                        Mth::Vector before = *reinterpret_cast<Mth::Vector*>(translations);
                        if (from == to) {
                            InterpolateFloatTrans(translations, reinterpret_cast<CFloatKey*>(first),
                                                  reinterpret_cast<CFloatKey*>(second), 0.0f);
                            if (add)
                                *reinterpret_cast<Mth::Vector*>(translations) += before;
                        } else if (transFrame >= from) { // (before the first key: left as it was)
                            transFrame = Mth::Clamp(transFrame, from, to);
                            float t = static_cast<float>((static_cast<double>(transFrame) - from) /
                                                         (static_cast<double>(to) - from));
                            InterpolateFloatTrans(translations, reinterpret_cast<CFloatKey*>(first),
                                                  reinterpret_cast<CFloatKey*>(second), t);
                            if (add)
                                *reinterpret_cast<Mth::Vector*>(translations) += before;
                        }
                        done = true;
                    }
                }
                if (!done) {
                    const uint8_t* end = transData + *transSize;
                    const uint8_t* at = transData;
                    if (source && source->m_resume && source->mp_transAt[bone])
                        at = source->mp_transAt[bone];
                    CShortKey a, b;
                    float t = 0.0f;
                    for (;;) {
                        if (source)
                            source->mp_transAt[bone] = at;
                        at = DecompressTransKey(at, &a);
                        if (at >= end) {
                            b = a;
                            break;
                        }
                        DecompressTransKey(at, &b);
                        int from = TransKeyTime(reinterpret_cast<const uint8_t*>(&a.time));
                        if (!(from <= transFrame)) { // (the frame before this key: this key alone)
                            b = a;
                            break;
                        }
                        int to = TransKeyTime(reinterpret_cast<const uint8_t*>(&b.time));
                        if (to >= transFrame) {
                            t = static_cast<float>((static_cast<double>(transFrame) - from) / (static_cast<double>(to) - from));
                            break;
                        }
                    }
                    float before[4];
                    std::memcpy(before, translations, sizeof(before));
                    InterpolateShortTrans(translations, &a, &b, t);
                    if ((m_flags & 0x4000) && (m_flags & 0x20000)) {
                        for (int i = 0; i < 4; ++i)
                            translations[i] = before[i] + translations[i];
                    }
                }
            }
        }
        quatData += *quatSize++;
        transData += *transSize++;
        rotations += 4;
        translations += 4;
    }
    return true;
}

} // namespace Gfx

REPLACE(0x00482970, Gfx::ReadKeyTable);
REPLACE(0x004829F0, Gfx::LoadQuatTable);
REPLACE(0x00482A30, Gfx::LoadTransTable);
REPLACE(0x00482A70, Gfx::ExpandQuatKey);
REPLACE(0x00482AF0, &Gfx::CBonedAnimFrameData::Construct);
REPLACE(0x00482B30, &Gfx::CBonedAnimFrameData::DestroyBody);
REPLACE(0x00482B90, Gfx::CBonedAnimFrameData::ForgetCacheEntry);
REPLACE(0x00482BA0, &Gfx::CBonedAnimFrameData::AcquireCacheIndex);
REPLACE(0x00482BD0, &Gfx::CBonedAnimFrameData::ReleaseCacheIndex);
REPLACE(0x00482C20, Gfx::DecompressQuatKey);
REPLACE(0x00482E70, Gfx::DecompressTransKey);
REPLACE(0x00482F00, &Gfx::CBonedAnimFrameData::GetBoneName);
REPLACE(0x00482F10, &Gfx::CBonedAnimFrameData::ArmEvents);
REPLACE(0x00482F40, &Gfx::CBonedAnimFrameData::FireEvents);
REPLACE(0x004838D0, Gfx::GetQuat);
REPLACE(0x00483990, Gfx::GetTrans);
REPLACE(0x00483A00, Gfx::GetShortQuat);
REPLACE(0x00483AE0, Gfx::GetShortTrans);
REPLACE(0x00483B30, Gfx::InterpolateQuat);
REPLACE(0x00483C40, Gfx::InterpolateTrans);
REPLACE(0x00483D00, Gfx::ExpandQuatKeyWithW);
REPLACE(0x00483E10, Gfx::InterpolateShortQuat);
REPLACE(0x00483F10, Gfx::InterpolateFloatQuat);
REPLACE(0x00484080, Gfx::InterpolateShortTrans);
REPLACE(0x00484170, Gfx::InterpolateFloatTrans);
REPLACE(0x00482FB0, &Gfx::CFrameCacheEntry::FindQuatKeys);
REPLACE(0x00483210, &Gfx::CFrameCacheEntry::FindTransKeys);
REPLACE(0x00483470, &Gfx::CFrameCacheEntry::FindQuatKeysBackwards);
REPLACE(0x004836C0, &Gfx::CFrameCacheEntry::FindTransKeysBackwards);
REPLACE(0x00484200, &Gfx::CBonedAnimFrameData::SetUpCompressed);
REPLACE(0x00484370, &Gfx::CBonedAnimFrameData::SetUpFloatKeys);
REPLACE(0x004844C0, &Gfx::CBonedAnimFrameData::GetFloatKeyPose);
REPLACE(0x00484760, &Gfx::CBonedAnimFrameData::GetPose);
