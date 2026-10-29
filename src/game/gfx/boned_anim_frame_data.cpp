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
