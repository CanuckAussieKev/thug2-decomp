// Self-tests for CAS2-3: the frame cache entry's key searches and the frame data's set-up and poses
// (src/game/gfx/boned_anim_frame_data.cpp) - the original and ours on identical made-up data: cache entries of float
// keys (random bones, key counts, times - equal, out of order, past 15 bits -, cursors there or not, the entry busy)
// searched forwards and backwards at random frames (NaN, before, between, after); compressed and float-key headers set
// up (the events stood in); whole poses read from float keys and from compressed keys, through a cache entry or by
// decompressing (a frame source resuming or not, its level-of-detail mask, partial-animation bits, additive
// translations), every output compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;
using Gfx::CBonedAnimFrameData;
using Gfx::CFrameCacheEntry;
using Gfx::CFrameSource;

alignas(16) uint8_t g_keys[0x4000];     // key data
alignas(16) uint8_t g_entryBytes[0x200];
uint8_t* g_cursorArrays[4][128];
alignas(16) uint8_t g_data[0x3000];     // compressed keys, float-key poses' keys

uint32_t Offset(const void* p)
{
    auto at = reinterpret_cast<uintptr_t>(p);
    auto base = reinterpret_cast<uintptr_t>(g_keys);
    if (at >= base && at < base + sizeof(g_keys))
        return 0x6E000000u + static_cast<uint32_t>(at - base);
    auto data = reinterpret_cast<uintptr_t>(g_data);
    if (at >= data && at < data + sizeof(g_data))
        return 0x6D000000u + static_cast<uint32_t>(at - data);
    for (int i = 0; i < 4; ++i) {
        auto arrays = reinterpret_cast<uintptr_t>(g_cursorArrays[i]);
        if (at >= arrays && at < arrays + sizeof(g_cursorArrays[i]))
            return 0x6C000000u + static_cast<uint32_t>(i * 0x1000 + (at - arrays));
    }
    return OnStack(static_cast<uint32_t>(at)) ? 0x57AC0000u : static_cast<uint32_t>(at);
}

float Frame(Lcg& random, int top)
{
    switch (random.Below(10)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
        return -5.0f;
    case 2:
        return static_cast<float>(top + 10);
    case 3:
        return static_cast<float>(random.Below(top + 1)); // (on a key, often)
    default:
        return RandomFloat(random, -2.0f, static_cast<float>(top) + 2.0f);
    }
}

// A key time word: rising, sometimes equal or falling; quaternion words sometimes with bit 15 or past 14 bits.
uint16_t NextTime(Lcg& random, int& time, bool quat)
{
    switch (random.Below(10)) {
    case 0:
    case 1:
        break; // equal
    case 2:
    case 3:
        time -= 1 + static_cast<int>(random.Below(6));
        break;
    default:
        time += 1 + static_cast<int>(random.Below(12));
    }
    uint16_t word = static_cast<uint16_t>(time);
    if (quat && random.Below(8) == 0)
        word |= 0x8000;
    if (random.Below(30) == 0)
        word ^= 0x4000;
    return word;
}

// A cache entry of float keys: quaternion keys of `quatStride`, translation keys of 16 bytes.
CFrameCacheEntry* MakeEntry(Lcg& random, int bones, int& top)
{
    std::memset(g_entryBytes, 0xEE, sizeof(g_entryBytes));
    std::memset(g_keys, 0xCC, sizeof(g_keys));
    auto* e = reinterpret_cast<CFrameCacheEntry*>(g_entryBytes);
    e->m_boneCount = static_cast<uint16_t>(bones);
    e->m_flags = static_cast<uint16_t>(random.Below(8) == 0 ? 0x8000 | random.Below(0x100) : random.Below(0x4000));
    e->m_quatStride = 0x14;
    e->m_transStride = 0x10;
    e->mp_quatKeys = g_keys;
    uint32_t at = 0;
    top = 0;
    for (int pass = 0; pass < 2; ++pass) {
        uint8_t* base = g_keys + (pass ? at : 0);
        if (pass)
            e->mp_transKeys = base;
        uint32_t used = 0;
        int stride = pass ? 0x10 : 0x14;
        for (int bone = 0; bone < 60; ++bone) {
            (pass ? e->m_transOffsets : e->m_quatOffsets)[bone] = static_cast<uint16_t>(used);
            if (bone >= bones)
                continue;
            int count = static_cast<int>(random.Below(6));
            int time = static_cast<int>(random.Below(6)) - (pass ? 3 : 0);
            for (int k = 0; k < count; ++k) {
                uint16_t word = NextTime(random, time, !pass);
                std::memcpy(base + used, &word, 2);
                for (int f = 0; f < (pass ? 3 : 4); ++f) {
                    float v = RandomFloat(random, -1, 1);
                    std::memcpy(base + used + 4 + 4 * f, &v, 4);
                }
                used += stride;
            }
            if (time > top)
                top = time;
        }
        if (!pass)
            at = used;
        else
            e->m_size = at + used;
    }
    for (int c = 0; c < 18; ++c)
        e->mp_cursors[c] = nullptr;
    for (int c = 0; c < 4; ++c) {
        if (random.Below(4) == 0)
            continue;
        e->mp_cursors[c] = g_cursorArrays[c];
        for (int b = 0; b < 120; ++b) {
            int bone = b % 60;
            if (bone >= bones || random.Below(3) == 0) {
                g_cursorArrays[c][b] = nullptr;
                continue;
            }
            // A key of the bone (or any key of the block).
            bool trans = b >= 60;
            uint16_t* offsets = trans ? e->m_transOffsets : e->m_quatOffsets;
            int stride = trans ? 0x10 : 0x14;
            uint8_t* base = trans ? e->mp_transKeys : e->mp_quatKeys;
            int keys = (offsets[bone + 1] - offsets[bone]) / stride;
            g_cursorArrays[c][b] = keys > 0 ? base + offsets[bone] + stride * static_cast<int>(random.Below(keys))
                                            : base + offsets[bone];
        }
    }
    return e;
}

// A key time of `bone` (as the search reads it), or a random frame.
float SearchFrame(Lcg& random, const CFrameCacheEntry* e, int bone, bool trans, int top)
{
    const uint16_t* offsets = trans ? e->m_transOffsets : e->m_quatOffsets;
    int stride = trans ? 0x10 : 0x14;
    int keys = (offsets[bone + 1] - offsets[bone]) / stride;
    if (bone == e->m_boneCount - 1 || keys <= 0 || random.Below(5) < 3)
        return Frame(random, top);
    const uint8_t* key = (trans ? e->mp_transKeys : e->mp_quatKeys) + offsets[bone] + stride * random.Below(keys);
    int16_t word = static_cast<int16_t>(key[0] | (key[1] << 8));
    return static_cast<float>(trans ? word : static_cast<int16_t>(static_cast<uint16_t>(word << 1)) >> 1);
}

void DumpEntry(std::vector<uint32_t>& trace, const CFrameCacheEntry* e)
{
    trace.push_back(e->m_flags);
    trace.push_back(e->m_unknown04[0]);
    for (int c = 0; c < 4; ++c)
        for (int b = 0; b < 120; ++b)
            trace.push_back(Offset(g_cursorArrays[c][b]));
}

void TestFrameCacheSearch()
{
    bool ok = true, reported = false;
    for (int round = 0; round < 150; ++round) {
        ok &= CompareTwoRuns("frame cache key searches", reported, [&](bool original) {
            std::vector<uint32_t> trace;
            Lcg random(0xCAC5 + round);
            int top;
            CFrameCacheEntry* e = MakeEntry(random, 1 + static_cast<int>(random.Below(5)), top);
            for (int i = 0; i < 300; ++i) {
                if (random.Below(20) == 0)
                    e->m_flags = static_cast<uint16_t>(random.Below(4) ? e->m_flags & 0x7FFF : e->m_flags | 0x8000);
                int bone = static_cast<int>(random.Below(e->m_boneCount));
                int cursor = random.Below(4) == 0 ? -1 : static_cast<int>(random.Below(4));
                uint32_t which = random.Below(4);
                float frame = SearchFrame(random, e, bone, which & 1, top);
                const uint32_t addresses[] = {0x00482FB0, 0x00483210, 0x00483470, 0x004836C0};
                if (which == 2 && cursor != -1 && !e->mp_cursors[cursor])
                    cursor = -1; // (the backwards quaternion search writes through a missing cursor array)
                uint8_t* first = reinterpret_cast<uint8_t*>(1);
                uint8_t* second = reinterpret_cast<uint8_t*>(2);
                int result = Fn<int(__thiscall*)(void*, int, float, uint8_t**, uint8_t**, int)>(
                    original, addresses[which])(e, bone, frame, &first, &second, cursor);
                trace.insert(trace.end(), {which, static_cast<uint32_t>(result), Offset(first), Offset(second)});
                if (i % 10 == 9)
                    DumpEntry(trace, e);
            }
            DumpEntry(trace, e);
            return trace;
        });
    }
    Check(ok, "frame cache key searches: forwards and backwards, quaternions and translations, cursors kept / "
              "stepped / missing, one-key bones, the last bone, the entry busy");
}

// --- set-up and poses ------------------------------------------------------------------------------------------

uint32_t g_events;

void StandInPoseCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Record({0xBAE0, r.args[0]});
        return Bump(r.args[0]);
    });
    s.At({0x00486680}, 0, [](Regs& r) {
        auto* at = reinterpret_cast<uint8_t**>(r.args[0]);
        Record({0x6680, Offset(*at)});
        uint32_t step = (Offset(*at) >> 2) % 5;
        *at += 4 + 4 * step;
        ++g_events;
        return (Offset(*at) >> 2) % 5 == 0 ? 0u : 0xE7000000u + g_events;
    });
    s.At({0x00480590}, 8, [](Regs& r) {
        Record({0x0590, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0], r.args[1]});
        return (r.args[1] & 1) ? 0x0002FFFFu : 0x00010001u; // (a short: -1 or 1 - the high word isn't part of it)
    });
}

void DumpFrameData(std::vector<uint32_t>& trace, const CBonedAnimFrameData* d)
{
    const auto* words = reinterpret_cast<const uint32_t*>(d);
    for (int i = 0; i < 15; ++i)
        trace.push_back(Offset(reinterpret_cast<void*>(words[i])));
    trace.push_back(Relocate(reinterpret_cast<uint32_t>(d->mp_events)));
}

void TestFrameDataSetUp()
{
    GameMemory memory({{0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInPoseCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("frame data set-up", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            g_events = 0;
            Lcg random(0x5E70 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            uint8_t* data = g_keys + random.Below(4); // (misaligned starts too)
            for (int i = 0; i < 0x400; ++i)
                data[i] = static_cast<uint8_t>(random.Below(256));
            auto* header = reinterpret_cast<uint32_t*>(data);
            header[0] = 1 + random.Below(40);
            header[1] = random.Below(0x30000);
            header[2] = random.Below(0x30000);
            header[3] = random.Below(3) ? random.Below(5) : 0;
            header[4] = random.Below(0x200);
            header[5] = random.Below(0x200);
            CBonedAnimFrameData frames;
            std::memset(&frames, 0xAB, sizeof(frames));
            frames.m_flags = ((random.Next() << 8) ^ random.Next()) & 0x09682000;
            bool compressed = random.Below(2) != 0;
            Record({Fn<uint32_t(__thiscall*)(void*, uint8_t*)>(original, compressed ? 0x00484200 : 0x00484370)(
                        &frames, data) & 0xFF});
            std::vector<uint32_t> trace = g_trace;
            DumpFrameData(trace, &frames);
            if (frames.mp_events && frames.m_eventCount > 0) {
                for (int i = 0; i < frames.m_eventCount; ++i)
                    trace.push_back(reinterpret_cast<uint32_t>(frames.mp_events[i]));
            }
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "frame data set-up: compressed and float-key headers, own tables, partial bits, bone names, key counts, "
              "events (one failing), misaligned data");
}

// Float keys (16 bytes) for `bones` bones: `counts` filled in (bytes or shorts).
void MakeFloatKeys(Lcg& random, CBonedAnimFrameData& d, int bones, int& top)
{
    uint8_t* counts = g_data;
    uint8_t* keys = g_data + 0x200;
    d.mp_keyCounts = counts;
    d.mp_quatKeys = keys;
    int quatTotal = 0;
    std::vector<int> quat(bones), trans(bones);
    for (int b = 0; b < bones; ++b) {
        quat[b] = static_cast<int>(random.Below(5));
        trans[b] = static_cast<int>(random.Below(5));
        quatTotal += quat[b];
        if (d.m_flags & 0x400000) {
            reinterpret_cast<int16_t*>(counts)[2 * b] = static_cast<int16_t>(quat[b]);
            reinterpret_cast<int16_t*>(counts)[2 * b + 1] = static_cast<int16_t>(trans[b]);
        } else {
            counts[2 * b] = static_cast<uint8_t>(quat[b]);
            counts[2 * b + 1] = static_cast<uint8_t>(trans[b]);
        }
    }
    d.mp_transKeys = keys + 16 * quatTotal;
    top = 0;
    uint8_t* at = keys;
    for (int pass = 0; pass < 2; ++pass) {
        for (int b = 0; b < bones; ++b) {
            int time = static_cast<int>(random.Below(4)) - (pass ? 2 : 0);
            for (int k = 0; k < (pass ? trans[b] : quat[b]); ++k) {
                uint16_t word = NextTime(random, time, !pass);
                std::memcpy(at, &word, 2);
                std::memset(at + 2, 0, 2);
                for (int f = 0; f < 3; ++f) {
                    float v = RandomFloat(random, -0.6f, 0.6f);
                    std::memcpy(at + 4 + 4 * f, &v, 4);
                }
                at += 16;
                if (time > top)
                    top = time;
            }
        }
    }
}

void TestFloatKeyPoses()
{
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("float-key poses", reported, [&](bool original) {
            std::vector<uint32_t> trace;
            Lcg random(0xF10A + round);
            CBonedAnimFrameData d;
            std::memset(&d, 0, sizeof(d));
            const uint32_t keyFlags[] = {0, 0x8000000, 0x1000000, 0x9000000};
            d.m_flags = (random.Below(2) ? 0x400000u : 0u) | keyFlags[random.Below(4)];
            int bones = static_cast<int>(random.Below(12));
            d.m_boneCount = bones;
            int top;
            MakeFloatKeys(random, d, bones, top);
            for (int call = 0; call < 6; ++call) {
                float rotations[12 * 4], translations[12 * 4];
                for (float& f : rotations)
                    f = RandomFloat(random, -9, 9);
                std::memcpy(translations, rotations, sizeof(translations));
                float time = random.Below(3) == 0 ? 0.25f * random.Below(top / 15 + 2) : Frame(random, top) / 60.0f;
                trace.push_back(Fn<uint32_t(__thiscall*)(void*, float*, float*, void*, float, int)>(original, 0x004844C0)(
                                    &d, rotations, translations, nullptr, time, 7) & 0xFF);
                for (int i = 0; i < bones * 4; ++i)
                    trace.insert(trace.end(), {std::bit_cast<uint32_t>(rotations[i]), std::bit_cast<uint32_t>(translations[i])});
            }
            return trace;
        });
    }
    Check(ok, "float-key poses: byte and short key counts, float and short keys, empty bones, frames before / on / "
              "between / past the keys and NaN");
}

// Compressed keys for one bone: quaternion keys (random forms; times rising) and translation keys.
void CompressedQuatKeys(Lcg& random, std::vector<uint8_t>& out, uint32_t flags, int& time)
{
    int count = static_cast<int>(random.Below(5));
    for (int k = 0; k < count; ++k) {
        time += random.Below(2) ? 15 : static_cast<int>(random.Below(8)); // (often on quarter seconds)
        uint16_t first = static_cast<uint16_t>(time & 0x7FF) | (random.Below(4) == 0 ? 0x8000 : 0);
        uint16_t header = first;
        uint32_t form = random.Below(3);
        if (form == 0)
            header &= ~0x4000; // uncompressed
        else
            header = static_cast<uint16_t>((header & 0x87FF) | 0x4000 | (form == 1 ? 0 : (random.Below(7) + 1) << 11));
        if (flags & 0x100) {
            out.insert(out.end(), {static_cast<uint8_t>(time), static_cast<uint8_t>((time >> 8) & 0x7F)});
            out.insert(out.end(), {static_cast<uint8_t>(header), static_cast<uint8_t>(header >> 8)});
        } else {
            out.insert(out.end(), {static_cast<uint8_t>(header), static_cast<uint8_t>(header >> 8)});
        }
        // (exactly what the key's form takes, so the next key's time is where it should be)
        int bytes;
        if (!(header & 0x4000)) {
            bytes = (flags & 0x8000) ? 3 : 6;
        } else if (!(header & 0x3800)) {
            bytes = 1;
        } else {
            bytes = 0;
            for (int bit : {0x2000, 0x1000, 0x0800})
                bytes += (header & bit) || (flags & 0x8000) ? 1 : 2;
        }
        for (int i = 0; i < bytes; ++i)
            out.push_back(static_cast<uint8_t>(random.Below(256)));
    }
}
void CompressedTransKeys(Lcg& random, std::vector<uint8_t>& out, int& time)
{
    int count = static_cast<int>(random.Below(5));
    for (int k = 0; k < count; ++k) {
        time += random.Below(2) ? 15 : static_cast<int>(random.Below(8)); // (often on quarter seconds)
        bool small = time < 64 && random.Below(2);
        uint8_t head = static_cast<uint8_t>((random.Below(3) == 0 ? 0x80 : 0) | (small ? 0x40 | time : 0));
        out.push_back(head);
        if (!small)
            out.insert(out.end(), {static_cast<uint8_t>(time), static_cast<uint8_t>(time >> 8)});
        int bytes = head & 0x80 ? 1 : 6;
        for (int i = 0; i < bytes; ++i)
            out.push_back(static_cast<uint8_t>(random.Below(256)));
    }
}

void TestCompressedPoses()
{
    GameMemory memory({{0x006F92B4, 4}, {Gfx::kQuatTable, 0x800}, {Gfx::kTransTable, 0x800}});
    StandIns& s = *new StandIns();
    StandInPoseCalls(s);
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("compressed poses", reported, [&](bool original) {
            g_trace.clear();
            Lcg random(0xC0E5 + round);
            memory.Fill(0x77 + round);
            CBonedAnimFrameData d;
            std::memset(&d, 0, sizeof(d));
            d.m_flags = random.Next() & 0x0003E100;
            int bones = 1 + static_cast<int>(random.Below(10));
            d.m_boneCount = bones;
            // The compressed keys (in g_data), each bone's sizes.
            std::vector<uint8_t> quat, trans;
            static uint16_t quatSizes[16], transSizes[16];
            int top = 0;
            for (int b = 0; b < bones; ++b) {
                size_t q = quat.size(), t = trans.size();
                int time = static_cast<int>(random.Below(4));
                CompressedQuatKeys(random, quat, d.m_flags, time);
                int ttime = static_cast<int>(random.Below(4));
                CompressedTransKeys(random, trans, ttime);
                quatSizes[b] = static_cast<uint16_t>(quat.size() - q);
                transSizes[b] = static_cast<uint16_t>(trans.size() - t);
                top = time > top ? time : top;
                top = ttime > top ? ttime : top;
            }
            std::memset(g_data, 0, sizeof(g_data));
            std::memcpy(g_data, quat.data(), quat.size());
            std::memcpy(g_data + 0x1000, trans.data(), trans.size());
            for (int i = 0; i < 0x600; ++i)
                g_data[0x2000 + i] = static_cast<uint8_t>(random.Below(256));
            d.mp_quatKeys = g_data;
            d.mp_transKeys = g_data + 0x1000;
            d.mp_keyTable = g_data + 0x2000;
            d.mp_quatSizes = quatSizes;
            d.mp_transSizes = transSizes;
            static uint32_t partial[4];
            partial[0] = 64;
            partial[1] = random.Next();
            d.mp_partialBones = partial;
            if (random.Below(4) == 0)
                d.m_flags |= 0x80000;
            // A cache entry, sometimes.
            int entryTop;
            CFrameCacheEntry* entry = MakeEntry(random, bones, entryTop);
            entry->m_flags &= random.Below(4) ? 0x7FFF : 0xFFFF;
            // (MakeEntry fills g_keys only: g_data is the compressed keys')
            d.mp_cacheEntry = random.Below(2) ? reinterpret_cast<uint8_t*>(entry) : nullptr;
            // A frame source, sometimes.
            static CFrameSource source;
            static uint32_t masks[16];
            std::memset(&source, 0, sizeof(source));
            source.m_resume = random.Below(2) != 0;
            source.m_maskBit = static_cast<int>(random.Below(40));
            for (uint32_t& m : masks)
                m = random.Below(2) ? 0 : random.Next();
            source.mp_boneMasks = masks;
            uint32_t quatAt = 0, transAt = 0;
            for (int b = 0; b < bones; ++b) {
                if (random.Below(3) == 0 && quatSizes[b])
                    source.mp_quatAt[b] = g_data + quatAt + (random.Below(2) ? 0 : quatSizes[b] - 2);
                if (random.Below(3) == 0 && transSizes[b])
                    source.mp_transAt[b] = g_data + 0x1000 + transAt;
                quatAt += quatSizes[b];
                transAt += transSizes[b];
            }
            CFrameSource* sourceArg = random.Below(4) ? &source : nullptr;
            for (int call = 0; call < 4; ++call) {
                float rotations[16 * 4], translations[16 * 4];
                uint8_t valid[16];
                for (float& f : rotations)
                    f = RandomFloat(random, -9, 9);
                std::memcpy(translations, rotations, sizeof(translations));
                std::memset(valid, 0x55, sizeof(valid));
                int most = top > entryTop ? top : entryTop;
                float time = random.Below(3) == 0 ? 0.25f * random.Below(most / 15 + 2) : Frame(random, most) / 60.0f;
                int cursor = random.Below(3) == 0 ? -1 : static_cast<int>(random.Below(4));
                if (cursor != -1 && !entry->mp_cursors[cursor])
                    cursor = -1; // (the backwards search writes through a missing cursor array)
                g_trace.push_back(Fn<uint32_t(__thiscall*)(void*, float*, float*, uint8_t*, float, int, void*)>(
                                      original, 0x00484760)(&d, rotations, translations, valid, time, cursor, sourceArg) &
                                  0xFF);
                for (int i = 0; i < bones * 4; ++i)
                    g_trace.insert(g_trace.end(),
                                   {std::bit_cast<uint32_t>(rotations[i]), std::bit_cast<uint32_t>(translations[i])});
                for (int i = 0; i < bones; ++i)
                    g_trace.push_back(valid[i]);
                for (int b = 0; b < bones; ++b)
                    g_trace.insert(g_trace.end(), {Offset(source.mp_quatAt[b]), Offset(source.mp_transAt[b])});
                g_trace.push_back(GameVariable<uint32_t>(0x006F92B4));
            }
            std::vector<uint32_t> trace = g_trace;
            DumpEntry(trace, entry);
            return trace;
        });
    }
    delete &s;
    Check(ok, "compressed poses: decompressed (resuming or not, every key form, additive translations) or through a "
              "cache entry (forwards / backwards, clamped frames, busy entries), level-of-detail masks, partial bits");
}

} // namespace

SELFTEST(Early, TestFrameCacheSearch);
SELFTEST(Early, TestFrameDataSetUp, RemoveStandIns);
SELFTEST(Early, TestFloatKeyPoses);
SELFTEST(Early, TestCompressedPoses, RemoveStandIns);
