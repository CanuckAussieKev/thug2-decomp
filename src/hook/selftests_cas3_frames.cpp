// Self-tests for an animation's frames (src/game/gfx/boned_anim_frame_data_pose.cpp, batch CAS3-5): each original against
// ours on stand-in frame data - plain keys sampled at many times (byte and short counts, the 15-bit rotation times,
// bones without keys), compressed keys decoded into a stand-in cache in each format (bones without keys, the frame
// limit and the vblank count), the additive pose (masks, bit arrays, the doubled translation) and loading; the
// decoders, the cache manager and its entries, the samplers, the slerp and lerp and the vblank count stood in; the
// calls, results and memory compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/anim_blend.h"
#include "game/gfx/boned_anim_frame_data.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"
#include "game/obj/walk_hang.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Gfx::CBonedAnimFrameData;

alignas(16) uint8_t g_frames[0x40];
alignas(16) uint8_t g_keys[0x400];      // plain / compressed keys
alignas(16) uint8_t g_keys2[0x400];
alignas(16) uint8_t g_counts[0x40];
alignas(16) uint16_t g_sizes[16];
alignas(16) uint16_t g_sizes2[16];
alignas(16) uint8_t g_manager[0x100];
alignas(16) uint8_t g_cache[0x200];
alignas(16) uint8_t g_cacheData[2][0x400];
alignas(16) uint16_t g_offsets[2][32];
alignas(16) uint8_t g_mask[0x220];
uint32_t g_maskBits[16];
uint32_t g_boneBits[4];
alignas(16) float g_rot[8][4];
alignas(16) float g_trans[8][4];
uint64_t g_vblank;
int g_format;
uint32_t g_room;      // 0x00480940's answer
bool g_freed;         // 0x00480ED0's
bool g_noCache;       // 0x00480980 gives none
uint32_t g_rotSize, g_transSize;
uint32_t g_seed;
std::vector<Saved> g_framesSaved;

uint32_t Rel(uint32_t address)
{
    auto in = [address](const void* base, size_t size) {
        return address >= reinterpret_cast<uint32_t>(base) && address < reinterpret_cast<uint32_t>(base) + size;
    };
    if (in(g_keys, sizeof(g_keys)))
        return 0x6B000000u + (address - reinterpret_cast<uint32_t>(g_keys));
    if (in(g_keys2, sizeof(g_keys2)))
        return 0x6C000000u + (address - reinterpret_cast<uint32_t>(g_keys2));
    if (in(g_rot, sizeof(g_rot)))
        return 0x60000000u + (address - reinterpret_cast<uint32_t>(g_rot));
    if (in(g_trans, sizeof(g_trans)))
        return 0x61000000u + (address - reinterpret_cast<uint32_t>(g_trans));
    if (address == reinterpret_cast<uint32_t>(g_frames))
        return 0xF0000000u;
    if (address == reinterpret_cast<uint32_t>(g_manager))
        return 0x3A000000u;
    if (address == reinterpret_cast<uint32_t>(g_cache))
        return 0xCA000000u;
    return address;
}
uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

void StandInFrameCalls(StandIns& s)
{
    s.At({0x005BE8E0, AddressOf(&Tmr::GetVblanks)}, 0, [](Regs& r) {
        Record({0xE8E0});
        r.edx = static_cast<uint32_t>(g_vblank >> 32);
        return static_cast<uint32_t>(g_vblank);
    });
    s.At({0x00480660}, 0, [](Regs&) {
        Record({0x0660});
        return reinterpret_cast<uint32_t>(g_manager);
    });
    s.At({0x00480800}, 0, [](Regs& r) {
        Record({0x0800, Rel(r.ecx)});
        return static_cast<uint32_t>(g_format);
    });
    s.At({0x00480670}, 16, [](Regs& r) {
        Record({0x0670, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0x90u + r.args[0];
    });
    s.At({0x00480940}, 0, [](Regs& r) {
        Record({0x0940, Rel(r.ecx)});
        return g_room;
    });
    s.At({0x00480ED0}, 8, [](Regs& r) {
        Record({0x0ED0, Rel(r.ecx), r.args[0], r.args[1]});
        return g_freed ? 1u : 0u;
    });
    s.At({0x00480980}, 20, [](Regs& r) {
        Record({0x0980, Rel(r.ecx), Rel(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4]});
        return g_noCache ? 0u : reinterpret_cast<uint32_t>(g_cache);
    });
    s.At({0x00480620}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_offsets[0]); });
    s.At({0x00480630}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_offsets[1]); });
    s.At({0x004805E0}, 0, [](Regs&) { return 0xAB00u | g_rotSize; });
    s.At({0x004805F0}, 0, [](Regs&) { return 0xCD00u | g_transSize; });
    s.At({0x00480600}, 4, [](Regs& r) {
        Record({0x0600, r.args[0]});
        return reinterpret_cast<uint32_t>(g_cacheData[0]);
    });
    s.At({0x00480610}, 4, [](Regs& r) {
        Record({0x0610, r.args[0]});
        return reinterpret_cast<uint32_t>(g_cacheData[1]);
    });
    s.At({0x00480640}, 8, [](Regs& r) {
        Record({0x0640, Rel(r.ecx), r.args[0], Rel(r.args[1])});
        return 0u;
    });
    // The decoders: a rotation key 4 bytes, a translation key 6 (8 bytes out each, from the input's bytes)
    s.At({0x00482C20}, 0, [](Regs& r) {
        auto* in = reinterpret_cast<uint8_t*>(r.args[0]);
        auto* out = reinterpret_cast<uint8_t*>(r.args[1]);
        Record({0x2C20, Rel(r.args[0]), r.args[2], r.args[3]});
        for (int i = 0; i < 8; ++i)
            out[i] = static_cast<uint8_t>(in[i % 4] * (i + 3) + i);
        return r.args[0] + 4;
    });
    s.At({0x00482E70}, 0, [](Regs& r) {
        auto* in = reinterpret_cast<uint8_t*>(r.args[0]);
        auto* out = reinterpret_cast<uint8_t*>(r.args[1]);
        Record({0x2E70, Rel(r.args[0]), r.args[2]});
        for (int i = 0; i < 8; ++i)
            out[i] = static_cast<uint8_t>(in[i % 6] ^ (i * 37));
        return r.args[0] + 6;
    });
    s.At({0x00482A70}, 0, [](Regs& r) {
        auto* in = reinterpret_cast<uint8_t*>(r.args[1]);
        auto* out = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 4; ++i)
            out[i] = static_cast<float>(in[i] + in[i + 4]) * 0.01f;
        return 0u;
    });
    s.At({0x00483D00}, 0, [](Regs& r) {
        auto* in = reinterpret_cast<uint8_t*>(r.args[1]);
        auto* out = reinterpret_cast<uint8_t*>(r.args[0]);
        out[0] = in[0];
        out[1] = in[1];
        out[2] = 0x5E;
        out[3] = 0x5F;
        for (int i = 0; i < 4; ++i) {
            float value = static_cast<float>(in[i * 2] - 128) / 128.0f;
            std::memcpy(out + 4 + i * 4, &value, 4);
        }
        return 0u;
    });
    s.At({0x00484200}, 4, [](Regs& r) {
        Record({0x4200, Rel(r.ecx), r.args[0]});
        return 0x10u;
    });
    s.At({0x00484370}, 4, [](Regs& r) {
        Record({0x4370, Rel(r.ecx), r.args[0]});
        return 0x01u;
    });
    s.At({0x00484760}, 24, [](Regs& r) {
        Record({0x4760, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1]), r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0x77u;
    });
    s.At({0x00483B30}, 0, [](Regs& r) {
        Record({0x3B30, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2]), r.args[3], r.args[4]});
        return 0u;
    });
    s.At({0x00483C40}, 0, [](Regs& r) {
        Record({0x3C40, Rel(r.args[0]), Rel(r.args[1]), Rel(r.args[2]), r.args[3], r.args[4]});
        return 0u;
    });
    s.At({0x0047D470, AddressOf(&Gfx::InterpolateRotation)}, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* from = reinterpret_cast<float*>(r.args[1]);
        auto* to = reinterpret_cast<float*>(r.args[2]);
        Record({0xD470, Rel(r.args[1]), Bits(to[0]), Bits(to[1]), Bits(to[2]), Bits(to[3]), r.args[3]});
        for (int i = 0; i < 4; ++i)
            out[i] = from[i] * 0.5f + to[i];
        return r.args[0];
    });
    s.At({0x0043D990, AddressOf(&Walk::Lerp)}, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* a = reinterpret_cast<float*>(r.args[1]);
        auto* b = reinterpret_cast<float*>(r.args[2]);
        Record({0xD990, Rel(r.args[1]), Bits(b[0]), Bits(b[1]), Bits(b[2]), Bits(b[3]), r.args[3]});
        for (int i = 0; i < 4; ++i)
            out[i] = a[i] - b[i] * 0.25f;
        return r.args[0];
    });
}

int Random(int low, int high)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return low + static_cast<int>((g_seed >> 8) % static_cast<uint32_t>(high - low + 1));
}

using This = void*;

void TestAnimFrameData()
{
    g_framesSaved = {{0x006FA2D8, 4, {}}, {0x006FA2E0, 8, {}}};
    SaveAll(g_framesSaved);
    auto* standIns = new StandIns;
    StandInFrameCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_seed = 0x5EED5;
        GameVariable<uint32_t>(0x006FA2D8) = 0;
        GameVariable<uint64_t>(0x006FA2E0) = 0;
        g_vblank = 0x100000005ull;
        std::memset(g_manager, 0, sizeof(g_manager));
        g_room = 0;
        g_freed = false;
        auto* frames = reinterpret_cast<CBonedAnimFrameData*>(g_frames);
        auto dumpFrames = [] {
            for (int i = 0; i < 0x40; i += 4)
                g_trace.push_back(Rel(*reinterpret_cast<uint32_t*>(g_frames + i)));
        };
        auto fillPose = [] {
            for (int i = 0; i < 8; ++i) {
                for (int k = 0; k < 4; ++k) {
                    g_rot[i][k] = static_cast<float>(Random(-100, 100)) / 64.0f;
                    g_trans[i][k] = static_cast<float>(Random(-1000, 1000)) / 16.0f;
                }
            }
        };
        auto dumpPose = [] {
            for (int i = 0; i < 8; ++i)
                for (int k = 0; k < 4; ++k)
                    g_trace.push_back(Bits(g_rot[i][k])), g_trace.push_back(Bits(g_trans[i][k]));
        };

        // Plain keys sampled: byte counts / short counts, rotation times with their 16th bit set, a bone without
        // keys, times before, between, on and after the keys, NaN
        for (int layout = 0; layout < 2; ++layout) {
            std::memset(g_frames, 0, sizeof(g_frames));
            frames->m_boneCount = 5;
            frames->m_flags = layout ? 0x400000u | 0x1000000u : 0u;
            frames->mp_quatKeys = g_keys;
            frames->mp_transKeys = g_keys2;
            frames->mp_keyCounts = g_counts;
            const int rotCounts[5] = {3, 1, 0, 4, 2};
            const int transCounts[5] = {2, 0, 3, 1, 4};
            std::memset(g_counts, 0, sizeof(g_counts));
            for (int b = 0; b < 5; ++b) {
                if (layout) {
                    reinterpret_cast<int16_t*>(g_counts)[b * 2] = static_cast<int16_t>(rotCounts[b]);
                    reinterpret_cast<int16_t*>(g_counts)[b * 2 + 1] = static_cast<int16_t>(transCounts[b]);
                } else {
                    g_counts[b * 2] = static_cast<uint8_t>(rotCounts[b]);
                    g_counts[b * 2 + 1] = static_cast<uint8_t>(transCounts[b]);
                }
            }
            int rotKey = 0, transKey = 0;
            for (int b = 0; b < 5; ++b) {
                for (int k = 0; k < rotCounts[b]; ++k, ++rotKey) {
                    uint16_t time = static_cast<uint16_t>(k * 10 + b);
                    if (k == 1)
                        time |= 0x8000; // (not part of the time)
                    if (b == 3 && k == 2)
                        time = 0x7FF0;  // (15 bits: negative)
                    std::memcpy(g_keys + rotKey * 8, &time, 2);
                }
                for (int k = 0; k < transCounts[b]; ++k, ++transKey) {
                    int16_t time = static_cast<int16_t>(b == 2 && k == 1 ? 0x4010 : k * 12 - 3); // (bit 14: all 16 count)
                    std::memcpy(g_keys2 + transKey * 8, &time, 2);
                }
            }
            for (float time : {0.0f, 0.05f, 0.1f, 1.0f / 6.0f, 0.2f, 0.3f, 0.45f, 1.0f, -0.5f, 0.00001f}) {
                bool r = original ? Original<bool(__thiscall*)(This, void*, void*, uint8_t*, float, int, void*)>(
                                        0x00485730)(frames, g_rot, g_trans, nullptr, time, 7, nullptr)
                                  : frames->GetFrames(reinterpret_cast<Mth::Quat*>(g_rot),
                                                      reinterpret_cast<Mth::Vector*>(g_trans), nullptr, time, 7, nullptr);
                Record({0x5730, r ? 1u : 0u});
            }
        }
        // Compressed frames sampled: cached first (or not: flag 0x1000, a cache already) - the cache manager has none
        g_noCache = true;
        for (int k = 0; k < 3; ++k) {
            std::memset(g_frames, 0, sizeof(g_frames));
            frames->m_flags = 0x800000u | (k == 1 ? 0x1000u : 0u);
            frames->m_boneCount = 0;
            frames->mp_cacheEntry = k == 2 ? g_cache : nullptr;
            g_format = 2;
            bool r = original ? Original<bool(__thiscall*)(This, void*, void*, uint8_t*, float, int, void*)>(0x00485730)(
                                    frames, g_rot, g_trans, g_keys, 0.5f, 9, g_mask)
                              : frames->GetFrames(reinterpret_cast<Mth::Quat*>(g_rot),
                                                  reinterpret_cast<Mth::Vector*>(g_trans), g_keys, 0.5f, 9, g_mask);
            Record({0x5731, r ? 1u : 0u});
            dumpFrames();
        }

        // The cache built: each format, sizes with empty bones (first, middle, last), the manager short of room or
        // not, no cache, the limit of ten a frame
        const uint16_t rotSizes[6] = {0, 8, 4, 0, 0, 12};
        const uint16_t transSizes[6] = {6, 0, 12, 6, 0, 0};
        for (int round = 0; round < 16; ++round) {
            std::memset(g_frames, 0, sizeof(g_frames));
            frames->m_boneCount = round == 9 ? 3 : 5;
            frames->m_flags = round % 2 ? 0x800u : 0x0u;
            frames->mp_keyTable = g_counts;
            frames->mp_quatKeys = g_keys;
            frames->mp_transKeys = g_keys2;
            frames->mp_quatSizes = g_sizes;
            frames->mp_transSizes = g_sizes2;
            frames->m_quatKeyCount = static_cast<int16_t>(20 + round);
            frames->m_transKeyCount = static_cast<int16_t>(-3 - round);
            frames->m_bits = static_cast<int16_t>(0x5A3 + round * 0x111);
            std::memcpy(g_sizes, rotSizes, sizeof(rotSizes));
            std::memcpy(g_sizes2, transSizes, sizeof(transSizes));
            for (int i = 0; i < 0x400; ++i) {
                g_keys[i] = static_cast<uint8_t>(i * 7 + round);
                g_keys2[i] = static_cast<uint8_t>(i * 11 + 3 * round);
            }
            std::memset(g_cacheData, 0xEE, sizeof(g_cacheData));
            std::memset(g_offsets, 0xEE, sizeof(g_offsets));
            std::memset(g_cache, 0, sizeof(g_cache));
            *reinterpret_cast<uint16_t*>(g_cache + 0x164) = static_cast<uint16_t>(0xFFFF - round * 0x1234);
            *reinterpret_cast<int*>(g_manager + 0xC8) = round % 3 == 0 ? 0x32 : 7;
            g_room = round % 4 < 2 ? 0x10u : round % 4 == 2 ? 0x95u : 0x1000u; // (0x95: exactly the size)
            g_freed = round % 5 != 0;
            g_noCache = round == 6;
            if (round == 12)
                g_vblank += 1; // (a new frame: counting from 0 again)
            int format = round % 5 + 1;
            bool converted = format == 3 || format == 4; // (as many bytes as each format's buffer has)
            g_rotSize = converted ? (round % 3 == 2 ? 0x10u : 0xCu) : (round % 3 == 2 ? 8u : 6u);
            g_transSize = converted ? (round % 4 == 3 ? 0x10u : 0xEu) : (round % 4 == 3 ? 6u : 8u);
            if (original)
                Original<void(__thiscall*)(This, int)>(0x00485270)(frames, format);
            else
                frames->BuildCache(format);
            dumpFrames();
            Record({GameVariable<uint32_t>(0x006FA2D8), static_cast<uint32_t>(GameVariable<uint64_t>(0x006FA2E0) >> 32),
                    static_cast<uint32_t>(GameVariable<uint64_t>(0x006FA2E0)), *reinterpret_cast<uint16_t*>(g_cache + 0x164)});
            for (auto& offsets : g_offsets)
                for (uint16_t o : offsets)
                    g_trace.push_back(o);
            for (auto& data : g_cacheData)
                for (int i = 0; i < 0x100; i += 4)
                    g_trace.push_back(*reinterpret_cast<uint32_t*>(data + i));
        }

        // Loading: each kind of keys, cached or not
        for (uint32_t flags : {0x10000000u, 0x800000u, 0x0u, 0x10800800u, 0x800800u}) {
            std::memset(g_frames, 0, sizeof(g_frames));
            g_noCache = true;
            uint32_t data[8] = {0x12, flags, 0x34, 1, 2, 3, 4, 5};
            bool r = original ? Original<bool(__thiscall*)(This, const uint8_t*, uint32_t, bool)>(0x004856C0)(
                                    frames, reinterpret_cast<uint8_t*>(data), 0x20, true)
                              : frames->Load(reinterpret_cast<uint8_t*>(data), 0x20, true);
            Record({0x56C0, r ? 1u : 0u});
            dumpFrames();
        }
        g_noCache = false;

        // The additive pose: the skeleton's mask on / off, the bit array, the doubled translation, keys or not
        for (int round = 0; round < 8; ++round) {
            std::memset(g_frames, 0, sizeof(g_frames));
            frames->m_boneCount = 6;
            frames->m_flags = (round & 1 ? 0x80000u : 0u) | (round & 2 ? 0x4000u : 0u) | (round & 4 ? 0x20000u : 0u) |
                              0x800000u;
            frames->mp_quatKeys = g_keys;
            frames->mp_transKeys = g_keys2;
            frames->mp_quatSizes = g_sizes;
            frames->mp_transSizes = g_sizes2;
            frames->mp_keyTable = g_counts;
            frames->mp_partialBones = g_boneBits;
            g_boneBits[1] = 0x2D ^ round;
            std::memset(g_mask, 0, sizeof(g_mask));
            g_mask[0x208] = round == 3 ? 0 : 1;
            *reinterpret_cast<uint32_t**>(g_mask + 0x20C) = g_maskBits;
            *reinterpret_cast<uint32_t*>(g_mask + 0x210) = static_cast<uint32_t>(round + 1);
            for (int i = 0; i < 16; ++i)
                g_maskBits[i] = static_cast<uint32_t>(Random(0, 0xFFFF));
            fillPose();
            float weight = 0.25f * round - 0.3f;
            uint8_t* mask = round == 5 ? nullptr : g_mask;
            uint32_t address = round == 6 ? 0x00485250u : 0x00484EB0u;
            if (round == 7)
                frames->m_flags &= ~0x800000u; // (the jump's test)
            bool r;
            if (original)
                r = Original<bool(__thiscall*)(This, void*, void*, void*, float, uint8_t*)>(round >= 6 ? 0x00485250u
                                                                                                      : address)(
                    frames, g_rot, g_trans, nullptr, weight, mask);
            else if (round >= 6)
                r = frames->ApplyAdditiveIfCompressed(reinterpret_cast<Mth::Quat*>(g_rot),
                                                      reinterpret_cast<Mth::Vector*>(g_trans), nullptr, weight, mask);
            else
                r = frames->ApplyAdditive(reinterpret_cast<Mth::Quat*>(g_rot), reinterpret_cast<Mth::Vector*>(g_trans),
                                          nullptr, weight, mask);
            Record({0x4EB0, r ? 1u : 0u});
            dumpPose();
        }
        for (auto& word : g_trace)
            word = Rel(word);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(g_framesSaved);
    Check(traces[0].size() > 3000, "animation frames: the original reached its calls");
    Compare(traces[0], traces[1], "an animation's frames (0x00484EB0-0x004859E8)");
}

void RestoreFrames()
{
    RemoveStandIns();
    if (!g_framesSaved.empty())
        RestoreAll(g_framesSaved);
}

} // namespace

SELFTEST(Early, TestAnimFrameData, RestoreFrames);
