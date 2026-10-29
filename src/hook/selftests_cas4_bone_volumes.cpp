// Self-tests for the bone volumes (src/game/gfx/bone_volumes.cpp): original and ours on stand-in skeletons (bones,
// matrices, inverse binds, parents) in an arena; the capsule tests, the shared table's puts and flushes, the bone
// descriptions and memory stood in by recording code (the capsules handed to the line test recorded whole); random
// sequences of every member, the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/bone_volumes.h"
#include "game/gfx/skeleton.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/script/struct.h"
#include "hook/cas4_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace Cas4Tests;
using Gfx::CBoneVolume;
using Gfx::CBoneVolumes;

Lcg* g_rand;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

void StandInBoneVolumeCalls(StandIns& s)
{
    StandInMemory(s);
    StandInStructs(s);
    using Intersects = int (Mth::Capsule::*)(const Mth::Line&, float*, float*, float*) const;
    s.At({0x004032E0, AddressOf(static_cast<Intersects>(&Mth::Capsule::IntersectsSegment))}, 16, [](Regs& r) {
        Record({0x32E0, Relocate(r.args[0]), Relocate(r.args[1]), Relocate(r.args[2]), Relocate(r.args[3])});
        const auto* capsule = reinterpret_cast<const uint32_t*>(r.ecx);
        for (int i = 0; i < 9; ++i)
            Record({capsule[i]});
        uint32_t hit = g_rand->Below(3) ? 1u : 0u;
        float values[3] = {static_cast<float>(g_rand->Below(50)) * 0.25f, static_cast<float>(g_rand->Below(50)) * 0.25f,
                           static_cast<float>(g_rand->Below(8)) * 2.5f};
        for (int i = 0; i < 3; ++i) {
            if (r.args[1 + i])
                *reinterpret_cast<float*>(r.args[1 + i]) = values[i];
        }
        return hit;
    });
    // the shared table (0x006FA308): a bucket's own slot, else an item chained after it
    s.At({0x00462740, AddressOf(&Lst::HashTable::PutItem)}, 8, [](Regs& r) {
        uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
        Record({0x2740, Relocate(r.ecx), key, Relocate(r.args[1])});
        auto* table = reinterpret_cast<Lst::HashTable*>(r.ecx);
        Lst::HashTable::Item* bucket = &table->m_buckets[key & ((1u << table->m_bits) - 1)];
        if (bucket->value) {
            auto* item = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item)));
            *item = {key, reinterpret_cast<void*>(r.args[1]), bucket->next};
            bucket->next = item;
        } else {
            bucket->key = key;
            bucket->value = reinterpret_cast<void*>(r.args[1]);
        }
        ++table->m_size;
        return 1u;
    });
    s.At({0x005B80D0, AddressOf(&Lst::HashTable::FlushItem)}, 4, [](Regs& r) {
        uint32_t key = *reinterpret_cast<uint32_t*>(r.args[0]);
        Record({0x80D0, Relocate(r.ecx), key});
        auto* table = reinterpret_cast<Lst::HashTable*>(r.ecx);
        Lst::HashTable::Item* item = &table->m_buckets[key & ((1u << table->m_bits) - 1)];
        Lst::HashTable::Item* previous = nullptr;
        while (item->key != key || !item->value) {
            previous = item;
            item = item->next;
            if (!item)
                return 0u;
        }
        if (previous) {
            previous->next = item->next;
        } else {
            item->key = 0;
            item->value = nullptr;
        }
        --table->m_size;
        return 0u;
    });
}

// A stand-in skeleton of `count` bones (names 0xB0E0 + i, each one's parent an earlier bone; random poses, inverse
// binds and scales) and its data.
struct Skeleton {
    Gfx::CSkeleton* skeleton;
    Gfx::CSkeletonData* data;
    int count;
};
Skeleton MakeSkeleton(Lcg& random, int count)
{
    Skeleton out;
    out.count = count;
    out.data = reinterpret_cast<Gfx::CSkeletonData*>(Bump(sizeof(Gfx::CSkeletonData)));
    std::memset(out.data, 0, sizeof(Gfx::CSkeletonData));
    out.skeleton = reinterpret_cast<Gfx::CSkeleton*>(Bump(sizeof(Gfx::CSkeleton)));
    std::memset(out.skeleton, 0, sizeof(Gfx::CSkeleton));
    auto* matrices = reinterpret_cast<Mth::Matrix*>(Bump(sizeof(Mth::Matrix) * count));
    auto* inverseBinds = reinterpret_cast<Mth::Matrix*>(Bump(sizeof(Mth::Matrix) * count));
    auto* bones = reinterpret_cast<Gfx::CBone*>(Bump(sizeof(Gfx::CBone) * count));
    out.data->m_count = count;
    out.data->mp_inverseBind = inverseBinds;
    for (int i = 0; i < count; ++i) {
        out.data->m_names[i] = 0xB0E0 + i;
        int parent = i == 0 ? -1 : static_cast<int>(random.Below(static_cast<uint32_t>(i)));
        out.data->m_parentNames[i] = parent < 0 ? 0 : 0xB0E0 + parent;
        for (int k = 0; k < 16; ++k) {
            (&matrices[i].row[0].x)[k] = RandomFloat(random, -2.0f, 2.0f);
            (&inverseBinds[i].row[0].x)[k] = RandomFloat(random, -2.0f, 2.0f);
        }
        bones[i].name = 0xB0E0 + i;
        bones[i].parent = &matrices[parent < 0 ? 0 : parent];
        bones[i].flags = random.Next();
        static const float kScales[] = {0.0f, -1.0f, 1.0f, 0.5f, 2.0f, 1.0f};
        for (float* scale : {&bones[i].scale.x, &bones[i].scale.y, &bones[i].scale.z})
            *scale = random.Below(4) ? RandomFloat(random, 0.25f, 3.0f) : random.Pick(kScales);
        if (random.Below(20) == 0)
            bones[i].scale.y = std::numeric_limits<float>::quiet_NaN();
        bones[i].scale.w = 1.0f;
    }
    out.skeleton->mp_matrices = matrices;
    out.skeleton->mp_data = out.data;
    out.skeleton->m_numBones = count;
    out.skeleton->mp_bones = bones;
    return out;
}

// Volumes made, registered, found, filled, tested against lines and measured, released (both runs alike).
void TestBoneVolumes()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FA308, 0x18}, {0x006FA320, 0x400}});
    StandIns& s = *new StandIns();
    StandInBoneVolumeCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareRuns("bone volumes", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            ClearStructs();
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x7B00 + round);
            Lcg answers(0x8C00 + round);
            g_rand = &answers;
            auto& table = GameVariable<Lst::HashTable>(0x006FA308);
            table.m_bits = 2;
            table.m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
            std::memset(table.m_buckets, 0, 4 * sizeof(Lst::HashTable::Item));
            std::vector<uint32_t> out;
            Skeleton skeleton = MakeSkeleton(random, 1 + static_cast<int>(random.Below(6)));
            // the bone descriptions (by name, some with a radius; some names twice, some not bones)
            auto* descriptions = reinterpret_cast<Script::CArray*>(Bump(sizeof(Script::CArray)));
            auto* elements = reinterpret_cast<uint32_t*>(Bump(8 * 4));
            uint32_t described = random.Below(8);
            for (uint32_t j = 0; j < described; ++j) {
                uint32_t description = Bump(8);
                if (random.Below(6))
                    SetValue(reinterpret_cast<void*>(description), 0xCAB94088, 0xB0E0 + random.Below(8));
                if (random.Below(2))
                    SetFloat(reinterpret_cast<void*>(description), 0xC48391A5, RandomFloat(random, 0.0f, 9.0f));
                elements[j] = description;
            }
            descriptions->m_unknown0 = 0;
            descriptions->m_type = Script::ESYMBOLTYPE_STRUCTURE;
            descriptions->m_unknown2 = 0;
            descriptions->m_size = described;
            if (described == 1)
                descriptions->m_single = elements[0];
            else
                descriptions->m_elements = elements;
            g_globalArrays[0x00E7085E] = Ptr(descriptions);
            // a set of volumes for the skeleton, made by hand (radii random, some bones without one)
            auto* volumes = reinterpret_cast<CBoneVolumes*>(Bump(sizeof(CBoneVolumes)));
            volumes->m_count = static_cast<uint32_t>(skeleton.count);
            volumes->m_users = 0;
            volumes->mp_volumes = reinterpret_cast<CBoneVolume**>(Bump(4 * 8));
            for (int i = 0; i < 8; ++i) {
                CBoneVolume* volume = nullptr;
                if (i < skeleton.count && random.Below(4)) {
                    volume = reinterpret_cast<CBoneVolume*>(Bump(sizeof(CBoneVolume)));
                    volume->m_vtable = reinterpret_cast<void**>(0x00648988);
                    std::memset(&volume->m_capsule, 0, sizeof(volume->m_capsule));
                    static const float kRadii[] = {0.0f, -1.5f, 3.0f};
                    volume->m_capsule.radius = random.Below(4) ? RandomFloat(random, 0.1f, 6.0f) : random.Pick(kRadii);
                }
                volumes->mp_volumes[i] = volume;
            }
            auto* world = reinterpret_cast<Mth::Matrix*>(Bump(sizeof(Mth::Matrix)));
            for (int k = 0; k < 16; ++k)
                (&world->row[0].x)[k] = RandomFloat(random, -3.0f, 3.0f);
            auto* segment = reinterpret_cast<Mth::Line*>(Bump(sizeof(Mth::Line)));
            auto* results = reinterpret_cast<float*>(Bump(16));
            std::vector<CBoneVolumes*> made;
            for (int step = 0; step < 24; ++step) {
                uint32_t op = random.Below(9);
                out.push_back(0xC0DE0000 + op);
                switch (op) {
                case 0: { // made
                    auto* fresh = reinterpret_cast<CBoneVolumes*>(Bump(sizeof(CBoneVolumes)));
                    out.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048B350)(
                        fresh, random.Below(5))));
                    made.push_back(fresh);
                    break;
                }
                case 1: { // registered
                    CBoneVolumes* which = made.empty() || random.Below(3) == 0 ? volumes : made[random.Below(
                                                                                              static_cast<uint32_t>(made.size()))];
                    uint32_t count = random.Below(3) ? static_cast<uint32_t>(skeleton.count) : random.Below(9);
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0048BEA0)(which, count);
                    if (which == volumes) { // (its array replaced: given back its volumes for the other tests)
                        for (uint32_t i = 0; i < count && i < 8; ++i)
                            volumes->mp_volumes[i] = random.Below(3) ? reinterpret_cast<CBoneVolume*>(
                                                                           Bump(sizeof(CBoneVolume)))
                                                                     : nullptr;
                        for (uint32_t i = 0; i < count && i < 8; ++i) {
                            if (CBoneVolume* v = volumes->mp_volumes[i]) {
                                std::memset(v, 0, sizeof(CBoneVolume));
                                v->m_capsule.radius = RandomFloat(random, 0.1f, 6.0f);
                            }
                        }
                    }
                    break;
                }
                case 2: // found
                    out.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048B3C0)(random.Below(9))));
                    break;
                case 3: { // released (only what's there)
                    uint32_t count = random.Below(9);
                    if (Fn<uint32_t(__cdecl*)(uint32_t)>(original, 0x0048B3C0)(count))
                        Fn<void(__cdecl*)(uint32_t)>(original, 0x0048B400)(count);
                    break;
                }
                case 4: { // filled
                    CBoneVolumes* which = made.empty() || random.Below(2) ? volumes : made.back();
                    if (which->m_count && !which->mp_volumes)
                        break;
                    if (which->m_count > 8)
                        break;
                    Fn<void(__thiscall*)(void*, Gfx::CSkeletonData*)>(original, 0x0048B430)(which, skeleton.data);
                    break;
                }
                case 5: { // a line through one bone or every bone
                    if (volumes->m_count < static_cast<uint32_t>(skeleton.count) || !volumes->mp_volumes)
                        break;
                    uint32_t bone = random.Below(3) ? 0 : 0xB0E0 + random.Below(static_cast<uint32_t>(skeleton.count));
                    static const float kScales[] = {0.0f, -1.0f, 1.5f, 0.75f};
                    float scale = random.Below(6) ? random.Pick(kScales) : std::numeric_limits<float>::quiet_NaN();
                    float extra = RandomFloat(random, -0.5f, 2.0f);
                    float* distance = random.Below(4) ? &results[0] : nullptr;
                    float* t = random.Below(4) ? &results[1] : nullptr;
                    out.push_back(Fn<uint32_t(__thiscall*)(void*, void*, void*, void*, uint32_t, float, float, float*,
                                                           float*)>(original, 0x0048B580)(
                        volumes, segment, skeleton.skeleton, world, bone, scale, extra, distance, t));
                    break;
                }
                case 6: { // a bone's volume
                    if (volumes->m_count < static_cast<uint32_t>(skeleton.count) || !volumes->mp_volumes)
                        break;
                    uint32_t bone = 0xB0E0 + random.Below(static_cast<uint32_t>(skeleton.count));
                    out.push_back(Fn<uint8_t(__thiscall*)(void*, void*, void*, uint32_t, float*)>(original, 0x0048BC40)(
                        volumes, skeleton.skeleton, world, bone, &results[2]));
                    break;
                }
                case 7: { // a volume's own test (its vtable slot)
                    CBoneVolume* volume = nullptr;
                    for (uint32_t i = 0; i < 8 && i < volumes->m_count && !volume; ++i)
                        volume = volumes->mp_volumes ? volumes->mp_volumes[i] : nullptr;
                    if (volume)
                        out.push_back(Fn<uint32_t(__thiscall*)(void*, void*, float*)>(original, 0x0048B330)(
                            volume, segment, random.Below(2) ? &results[3] : nullptr));
                    break;
                }
                default: // a new skeleton
                    skeleton = MakeSkeleton(random, 1 + static_cast<int>(random.Below(6)));
                    break;
                }
                out.insert(out.end(), g_trace.begin(), g_trace.end());
                g_trace.clear();
            }
            DumpArena(out);
            memory.Dump(out);
            return out;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "bone volumes: made, registered, found, released, filled from their descriptions, lines through one "
              "bone and every bone (scales, radii, NaN), a bone's volume, a volume's own test");
}

} // namespace

SELFTEST(Early, TestBoneVolumes, RemoveStandIns);
