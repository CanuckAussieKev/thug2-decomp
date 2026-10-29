// Self-tests for CAS2-2: the boned animation frame data's keys and pieces (src/game/gfx/boned_anim_frame_data.cpp) -
// the original and ours on the same random inputs: compressed keys of every form decompressed (random shared tables),
// keys expanded, a key's quaternion / translation, interpolation between keys (t exactly 0 / -0 / 1 / NaN / between;
// NaN and -0 floats), the tables loaded from stood-in files, and the frame data's construction, destruction, cache
// references and events with stand-ins for the frame cache and the events.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;
using Gfx::CBonedAnimFrameData;
using Gfx::CFloatKey;
using Gfx::CShortKey;

uint32_t B(float f) { return std::bit_cast<uint32_t>(f); }

float AnyFloat(Lcg& random)
{
    switch (random.Below(12)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return std::numeric_limits<float>::quiet_NaN();
    case 3:
        return 1.0f;
    case 4:
        return RandomFloat(random, -2, 2);
    default:
        return RandomFloat(random, -0.7f, 0.7f);
    }
}
float AnyT(Lcg& random)
{
    switch (random.Below(8)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return 1.0f;
    case 3:
        return std::numeric_limits<float>::quiet_NaN();
    default:
        return RandomFloat(random, -0.2f, 1.2f);
    }
}
int16_t AnyShort(Lcg& random)
{
    switch (random.Below(6)) {
    case 0:
        return 0;
    case 1:
        return static_cast<int16_t>(random.Below(2) ? 0x7FFF : 0x8000);
    case 2:
        return static_cast<int16_t>(random.Below(0x10000));
    default:
        return static_cast<int16_t>(static_cast<int>(random.Below(20000)) - 10000);
    }
}
CShortKey ShortKey(Lcg& random)
{
    CShortKey key;
    key.time = static_cast<uint16_t>(random.Below(0x10000));
    bool zero = random.Below(6) == 0;
    for (int16_t& v : key.value)
        v = zero ? 0 : AnyShort(random);
    if (!zero && random.Below(5) == 0)
        key.value[random.Below(3)] = 0;
    return key;
}
CFloatKey FloatKey(Lcg& random)
{
    CFloatKey key;
    key.time = static_cast<uint16_t>(random.Below(0x10000));
    key.pad = static_cast<uint16_t>(random.Below(0x10000));
    bool zero = random.Below(10) == 0;
    for (float& v : key.value)
        v = zero ? (random.Below(2) ? 0.0f : -0.0f) : AnyFloat(random);
    return key;
}
void Push(std::vector<uint32_t>& trace, const void* p, size_t bytes)
{
    const auto* w = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i + 4 <= bytes; i += 4) {
        uint32_t v;
        std::memcpy(&v, w + i, 4);
        trace.push_back(v);
    }
}

void TestBonedAnimKeys()
{
    GameMemory memory({{Gfx::kQuatTable, 0x800}, {Gfx::kTransTable, 0x800}});
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("boned animation keys", reported, [&](bool original) {
            std::vector<uint32_t> trace;
            Lcg random(0xF4A3 + round);
            memory.Fill(0x1357 + round);
            for (int i = 0; i < 120; ++i) {
                // Compressed keys: random bytes, random flags.
                uint8_t data[16], table[256 * 6];
                for (uint8_t& b : data)
                    b = static_cast<uint8_t>(random.Below(256));
                for (int k = 0; k < 64; ++k)
                    table[random.Below(sizeof(table))] = static_cast<uint8_t>(random.Below(256));
                uint32_t flags = random.Next() & 0x1A100;
                flags |= random.Below(4) == 0 ? 0x80000000u : 0u;
                CShortKey out;
                std::memset(&out, 0xA5, sizeof(out));
                out.time = static_cast<uint16_t>(random.Below(0x10000));
                auto end = Fn<const uint8_t*(__cdecl*)(const uint8_t*, CShortKey*, uint32_t, const uint8_t*)>(
                    original, 0x00482C20)(data, &out, flags, table);
                trace.push_back(static_cast<uint32_t>(end - data));
                Push(trace, &out, sizeof(out));
                std::memset(&out, 0x5A, sizeof(out));
                end = Fn<const uint8_t*(__cdecl*)(const uint8_t*, CShortKey*)>(original, 0x00482E70)(data, &out);
                trace.push_back(static_cast<uint32_t>(end - data));
                Push(trace, &out, sizeof(out));

                // Expanded keys.
                CShortKey in = ShortKey(random);
                CFloatKey expanded = FloatKey(random);
                Fn<void(__cdecl*)(CFloatKey*, const CShortKey*)>(original, 0x00482A70)(&expanded, &in);
                Push(trace, &expanded, sizeof(expanded));
                expanded = FloatKey(random);
                Fn<void(__cdecl*)(CFloatKey*, const CShortKey*)>(original, 0x00483D00)(&expanded, &in);
                Push(trace, &expanded, sizeof(expanded));

                // A key's value.
                CShortKey a = ShortKey(random), b = ShortKey(random);
                CFloatKey fa = FloatKey(random), fb = FloatKey(random);
                if (random.Below(4) == 0)
                    fb = fa;
                float result[4];
                uint32_t floats = random.Below(3) == 0 ? 0x100u : random.Below(2);
                const void* ka = floats & 0xFF ? static_cast<const void*>(&fa) : &a;
                const void* kb = floats & 0xFF ? static_cast<const void*>(&fb) : &b;
                for (uint32_t address : {0x004838D0u, 0x00483990u}) {
                    std::memset(result, 0x77, sizeof(result));
                    Fn<void(__cdecl*)(const void*, float*, uint32_t)>(original, address)(ka, result, floats);
                    Push(trace, result, sizeof(result));
                }
                for (uint32_t address : {0x00483A00u, 0x00483AE0u}) {
                    std::memset(result, 0x77, sizeof(result));
                    Fn<void(__cdecl*)(const CShortKey*, float*)>(original, address)(&a, result);
                    Push(trace, result, sizeof(result));
                }
                // Between keys.
                float t = AnyT(random);
                for (uint32_t address : {0x00483B30u, 0x00483C40u}) {
                    std::memset(result, 0x77, sizeof(result));
                    Fn<void(__cdecl*)(float*, const void*, const void*, float, uint32_t)>(original, address)(
                        result, ka, kb, t, floats);
                    Push(trace, result, sizeof(result));
                }
                for (uint32_t address : {0x00483E10u, 0x00484080u}) {
                    std::memset(result, 0x77, sizeof(result));
                    Fn<void(__cdecl*)(float*, const CShortKey*, const CShortKey*, float)>(original, address)(
                        result, &a, &b, t);
                    Push(trace, result, sizeof(result));
                }
                for (uint32_t address : {0x00483F10u, 0x00484170u}) {
                    CFloatKey copy = fa;
                    std::memset(result, 0x77, sizeof(result));
                    Fn<void(__cdecl*)(float*, CFloatKey*, const CFloatKey*, float)>(original, address)(
                        result, &copy, &fb, t);
                    Push(trace, result, sizeof(result));
                    Push(trace, &copy, sizeof(copy));
                }
            }
            return trace;
        });
    }
    Check(ok, "boned animation keys: compressed quaternion / translation keys of every form, expanded keys, a key's "
              "values, interpolation (0 / 1 / NaN / between, float and short keys)");
}

// --- the frame data and the tables -----------------------------------------------------------------------------

std::vector<uint8_t> g_fileBytes; // what the stood-in file holds
size_t g_fileAt;
uint32_t g_eventVtable[3];

void StandInFrameCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x005B9E60, AddressOf(&File::Open)}, 0, [](Regs& r) {
        Record({0x9E60, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        g_fileAt = 0;
        return g_fileBytes.empty() ? 0u : 0xF11Eu;
    });
    s.At({0x005B9EA0, AddressOf(&File::Close)}, 0, [](Regs& r) {
        Record({0x9EA0, r.args[0]});
        return 0u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        Record({0x9EE0, r.args[1], r.args[2], r.args[3]});
        uint32_t bytes = r.args[1] * r.args[2];
        if (g_fileAt + bytes > g_fileBytes.size())
            return 0u;
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_fileBytes.data() + g_fileAt, bytes);
        g_fileAt += bytes;
        return bytes;
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0xBB60, Relocate(r.args[0])});
        return 0u;
    });
    s.At({0x00480660}, 0, [](Regs&) {
        Record({0x0660});
        return GameVariable<uint32_t>(0x006F92B0);
    });
    StandInThis(s, {0x00480DF0}, 1);
    s.At({0x00480E30}, 4, [](Regs& r) {
        Record({0x0E30, Relocate(r.ecx), Relocate(r.args[0])});
        return static_cast<uint32_t>(r.args[0] >> 4) % 7 - 1;
    });
    StandInThis(s, {0x00480E80}, 2);
    StandInThis(s, {0x00486150}, 1);
    s.At({0x00486160}, 4, [](Regs& r) {
        Record({0x6160, Relocate(r.ecx), Relocate(r.args[0])});
        return (r.ecx >> 4) & 1;
    });
    // The events' vtable: 0 the deleting destructor, 1 whether it lies between two times (by its address).
    g_eventVtable[0] = reinterpret_cast<uint32_t>(s.Thunk(4, [](Regs& r) {
        Record({0xE000, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    }));
    g_eventVtable[1] = reinterpret_cast<uint32_t>(s.Thunk(12, [](Regs& r) {
        Record({0xE001, Relocate(r.ecx), r.args[0], r.args[1], r.args[2]});
        return (r.ecx >> 5) & 1;
    }));
}

void TestBonedAnimFrameData()
{
    GameMemory memory({{0x007D1800, 4}, {0x006F92B0, 4}, {Gfx::kQuatTable, 0x800}, {Gfx::kTransTable, 0x800}});
    StandIns& s = *new StandIns();
    StandInFrameCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("boned animation frame data", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            Lcg random(0x3F2A + round);
            memory.Fill(0x2468 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            GameVariable<uint32_t>(0x006F92B0) = random.Below(4) ? Bump(0x10) : 0;
            // The tables: a whole file, a short one, none.
            uint32_t size = random.Below(3) == 0 ? random.Below(0x800) : random.Below(4) ? 0x800 : 0;
            g_fileBytes.resize(size);
            for (uint8_t& b : g_fileBytes)
                b = static_cast<uint8_t>(random.Below(256));
            Record({Fn<uint32_t(__cdecl*)(const char*)>(original, random.Below(2) ? 0x004829F0 : 0x00482A30)(
                        "anims\\table.bin") & 0xFF});
            g_fileAt = 0;
            Record({Fn<uint32_t(__cdecl*)(void*, int16_t*)>(original, 0x00482970)(
                        reinterpret_cast<void*>(0xF11E), reinterpret_cast<int16_t*>(Bump(0x800))) & 0xFF});

            // A frame data.
            auto* data = reinterpret_cast<CBonedAnimFrameData*>(Bump(sizeof(CBonedAnimFrameData)));
            for (uint32_t i = 0; i < sizeof(CBonedAnimFrameData); ++i)
                reinterpret_cast<uint8_t*>(data)[i] = static_cast<uint8_t>(random.Below(256));
            Record({Relocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00482AF0)(data))});
            for (uint32_t i = 0; i < sizeof(CBonedAnimFrameData); i += 4)
                Record({*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(data) + i)});
            int events = static_cast<int>(random.Below(5));
            data->m_eventCount = static_cast<int16_t>(random.Below(8) == 0 ? -1 : events);
            data->mp_events = events || random.Below(2) ? reinterpret_cast<Gfx::CAnimEvent**>(Bump(4 * 8)) : nullptr;
            for (int i = 0; i < events; ++i) {
                uint32_t event = random.Below(5) ? Bump(0x20 + 0x10 * random.Below(3)) : 0;
                if (event)
                    *reinterpret_cast<uint32_t*>(event) = reinterpret_cast<uint32_t>(g_eventVtable);
                data->mp_events[i] = reinterpret_cast<Gfx::CAnimEvent*>(event);
            }
            bool nullEvents = random.Below(2) != 0; // (then never fired)
            if (data->mp_events && !nullEvents) {
                for (int i = 0; i < events; ++i) {
                    if (!data->mp_events[i]) {
                        uint32_t event = Bump(0x20);
                        *reinterpret_cast<uint32_t*>(event) = reinterpret_cast<uint32_t>(g_eventVtable);
                        data->mp_events[i] = reinterpret_cast<Gfx::CAnimEvent*>(event);
                    }
                }
            }
            data->mp_boneNames = reinterpret_cast<uint32_t*>(Bump(0x40));
            for (int i = 0; i < 16; ++i)
                data->mp_boneNames[i] = random.Next();
            for (int step = 0; step < 16; ++step) {
                Record({0x57E9, static_cast<uint32_t>(step)});
                switch (random.Below(7)) {
                case 0:
                    data->m_bits = static_cast<uint16_t>(random.Below(0x10000));
                    Record({Fn<uint32_t(__thiscall*)(void*)>(original, 0x00482BA0)(data)});
                    break;
                case 1:
                    if (random.Below(2))
                        data->m_bits = static_cast<uint16_t>(random.Below(0x10000));
                    Fn<void(__thiscall*)(void*, int)>(original, 0x00482BD0)(data, static_cast<int>(random.Below(9)) - 1);
                    break;
                case 2:
                    Record({Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x00482F00)(data, random.Below(16))});
                    break;
                case 3:
                    if (data->mp_events)
                        Record({Fn<uint32_t(__thiscall*)(void*)>(original, 0x00482F10)(data) & 0xFF});
                    break;
                case 4:
                    if (data->mp_events && !nullEvents)
                        Record({Fn<uint32_t(__thiscall*)(void*, float, float, void*, int)>(original, 0x00482F40)(
                                    data, AnyFloat(random), AnyFloat(random),
                                    reinterpret_cast<void*>(0x7A00 + random.Below(16)), static_cast<int>(random.Below(3)))
                                & 0xFF});
                    break;
                case 5:
                    Fn<void(__cdecl*)(void*)>(original, 0x00482B90)(random.Below(4) ? data : nullptr);
                    break;
                default:
                    data->mp_cacheEntry = random.Below(2) ? reinterpret_cast<uint8_t*>(Bump(8)) : nullptr;
                    break;
                }
            }
            if (data->m_eventCount < 0)
                data->m_eventCount = static_cast<int16_t>(events);
            if (data->mp_events || !data->m_eventCount)
                Fn<void(__thiscall*)(void*)>(original, 0x00482B30)(data);
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "boned animation frame data: the key tables loaded (whole, short, missing files), constructed, cache "
              "references taken / let go, bone names, events armed / fired, the cache's callback, destroyed");
}

} // namespace

SELFTEST(Early, TestBonedAnimKeys);
SELFTEST(Early, TestBonedAnimFrameData, RemoveStandIns);
