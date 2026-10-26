// Self-tests for the PC materials' loaders (src/game/gfx/pc_material_load.cpp, RFX-26): the original and ours over
// random material lists - every pass flag, specular or not, UV wibbles, colour and texture animations, textures
// found and not, repeated checksums - from memory and through a stand-in file; the heap and the table's PutItem
// stood in.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/file/file.h"
#include "game/gfx/material.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

std::vector<uint8_t> g_stream;
size_t g_position;

struct Writer {
    std::vector<uint8_t>& out;
    void U32(uint32_t v) { out.insert(out.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4); }
    void F32(float v) { U32(*reinterpret_cast<uint32_t*>(&v)); }
    void U8(uint8_t v) { out.push_back(v); }
};

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

const uint32_t kTextureNames[6] = {0, 0x100, 0x2A1, 0x3333, 0x4441, 0x5E5E}; // (0x5E5E isn't in the table)

void WriteMaterials(Writer& w, Lcg& random, uint32_t count)
{
    w.U32(count);
    for (uint32_t i = 0; i < count; ++i) {
        w.U32(random.Below(6) == 0 ? 0xC0DE : 0x10000 + random.Below(1 << 20)); // (some checksums repeated)
        w.U32(random.Next());
        uint32_t passes = random.Below(5);
        w.U32(passes);
        w.U32(random.Next()); // the alpha reference (a byte kept)
        w.U8(static_cast<uint8_t>(random.Below(2)));
        w.F32(RandomFloat(random, 0, 4));
        w.U8(static_cast<uint8_t>(random.Below(2)));
        w.U8(static_cast<uint8_t>(random.Below(2)));
        w.U32(random.Below(3) ? random.Below(20) : 0xFFFFFFF0u + random.Below(16));
        bool extra = random.Below(2);
        w.U8(extra);
        if (extra) {
            w.U32(random.Next());
            w.U32(random.Next());
        }
        float power = random.Below(3) == 0 ? 0.0f : RandomFloat(random, -2, 30);
        w.F32(power);
        if (power > 0) {
            for (int k = 0; k < 3; ++k)
                w.F32(RandomFloat(random, 0, 1));
        }
        for (uint32_t pass = 0; pass < passes; ++pass) {
            w.U32(kTextureNames[random.Below(6)]);
            uint32_t flags = random.Next() & (1 | 2 | 8 | 0x80 | 0x200 | 0x800 | 0x1000 | 0x4000);
            w.U32(flags);
            w.U8(static_cast<uint8_t>(random.Next()));
            for (int k = 0; k < 3; ++k)
                w.F32(random.Below(3) ? 0.5f : RandomFloat(random, 0, 1));
            w.U32(random.Below(3) ? 0 : random.Next() << 8 ^ random.Next());
            w.U32((random.Next() << 8 ^ random.Next()) >> random.Below(32));
            w.U32(random.Below(3));
            w.U32(random.Below(3));
            w.F32(RandomFloat(random, 0, 5));
            w.F32(RandomFloat(random, 0, 5));
            w.U32(random.Next());
            if (flags & 1)
                for (int k = 0; k < 8; ++k)
                    w.F32(RandomFloat(random, -2, 2));
            if (pass == 0 && (flags & 2)) {
                uint32_t anims = random.Below(3);
                w.U32(anims);
                for (uint32_t a = 0; a < anims; ++a) {
                    uint32_t keys = random.Below(4);
                    w.U32(keys);
                    w.U32(random.Below(100));
                    for (uint32_t k = 0; k < 2 * keys; ++k)
                        w.U32(random.Next());
                }
            }
            if (flags & 0x800) {
                uint32_t keys = random.Below(4);
                w.U32(keys);
                w.U32(random.Next());
                w.U32(random.Below(3));
                w.U32(random.Below(1000));
                for (uint32_t k = 0; k < keys; ++k) {
                    w.U32(random.Below(5000));
                    w.U32(kTextureNames[random.Below(6)]);
                }
            }
            w.U32(random.Next());
            w.U32(random.Next());
            w.F32(random.Below(4) == 0 ? -8.0f : RandomFloat(random, -12, 4));
            w.U32(random.Next());
        }
    }
}

// The scene's texture table (8 buckets, some chained): entries with flag 1 hold their texture behind a pointer.
Lst::HashTable* MakeTextures()
{
    auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
    std::memset(table, 0, sizeof(Lst::HashTable));
    table->m_bits = 3;
    table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(8 * sizeof(Lst::HashTable::Item)));
    std::memset(table->m_buckets, 0, 8 * sizeof(Lst::HashTable::Item));
    for (int i = 1; i < 5; ++i) {
        uint32_t name = kTextureNames[i];
        auto* entry = reinterpret_cast<uint8_t*>(Bump(0x20));
        std::memset(entry, 0, 0x20);
        entry[8] = static_cast<uint8_t>(i & 1);
        uint32_t texture = i == 3 ? 0 : 0x7E000000 + i; // (one entry without a texture)
        if (entry[8] & 1) {
            auto* holder = reinterpret_cast<uint32_t*>(Bump(4));
            *holder = texture;
            *reinterpret_cast<uint32_t**>(entry + 0x10) = holder;
        } else {
            *reinterpret_cast<uint32_t*>(entry + 0x10) = texture;
        }
        Lst::HashTable::Item* item = &table->m_buckets[name & 7];
        if (item->value) { // (chained)
            auto* chained = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item)));
            *chained = {name, entry, nullptr};
            item->next = chained;
        } else {
            *item = {name, entry, nullptr};
        }
    }
    return table;
}

void TestPcMaterialLoad()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
        auto* table = reinterpret_cast<Lst::HashTable*>(r.ecx);
        uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
        Record({0x2740, Relocate(r.ecx), key, Relocate(r.args[1])});
        Lst::HashTable::Item* bucket = &table->m_buckets[key & ((1u << table->m_bits) - 1)];
        if (!bucket->value)
            *bucket = {key, reinterpret_cast<void*>(r.args[1]), nullptr}; // (else dropped: no item pool here)
        return 1u;
    });
    s.At({0x005B9EE0, AddressOf(&File::Read)}, 0, [](Regs& r) {
        Record({0x9EE0, r.args[1], r.args[2], r.args[3]});
        size_t bytes = static_cast<size_t>(r.args[1]) * r.args[2];
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_stream.data() + g_position, bytes);
        g_position += bytes;
        return static_cast<uint32_t>(bytes);
    });
    s.At({0x005B9FA0, AddressOf(&File::Seek)}, 0, [](Regs& r) {
        Record({0x9FA0, r.args[0], r.args[1], r.args[2]});
        g_position += static_cast<int>(r.args[1]);
        return static_cast<uint32_t>(g_position);
    });
    g_relocate = [](uint32_t word) { return ArenaRelocate(word); };
    bool ok = true, reported = false;
    for (int round = 0; round < 24; ++round) {
        ok &= CompareTwoRuns("pc material load", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x2CB0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x2CB00 + round);
            std::vector<uint32_t> trace;
            g_stream.clear();
            Writer writer{g_stream};
            uint32_t count = round == 0 ? 0 : 1 + random.Below(round < 12 ? 4 : 20);
            WriteMaterials(writer, random, count);
            g_stream.resize(g_stream.size() + 64, 0xCD);
            Lst::HashTable* textures = MakeTextures();
            uint8_t* cursor = reinterpret_cast<uint8_t*>(Bump(static_cast<uint32_t>(g_stream.size())));
            std::memcpy(cursor, g_stream.data(), g_stream.size());
            uint8_t* start = cursor;
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint8_t**, const void*)>(original, 0x004D2CB0)(
                &cursor, textures)));
            trace.push_back(static_cast<uint32_t>(cursor - start));
            g_position = 0;
            trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(void*, const void*)>(original, 0x004D3390)(
                reinterpret_cast<void*>(0xF11E0000 + round), textures)));
            trace.push_back(static_cast<uint32_t>(g_position));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc material load: material lists from memory and from a file");
}

} // namespace

SELFTEST(Early, TestPcMaterialLoad, RemoveStandIns);
