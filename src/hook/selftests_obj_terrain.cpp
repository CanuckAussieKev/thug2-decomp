// Self-tests for the terrain manager (sfx/terrain.cpp, 0x004442C0-0x00444EB0): the original against ours on the
// same stand-in terrain tables, script structures (hand-built: terrain.qb's shapes) and memory; the sound manager,
// the network client, the random numbers and the global script symbols are stood in for by recording code.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/lst/hashtable.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_client.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sfx/terrain.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using CFuncTests::Bits;
using CFuncTests::Params;
using Script::CArray;
using Script::CStruct;
using Terrain::SoundInfo;

// --- the stand-in memory: allocations from an arena, recorded relative to it ---
alignas(16) uint8_t g_arena[2][0x1000];
uint8_t* g_arenaBase;
uint32_t g_arenaUsed;
alignas(16) uint8_t g_manager[0x600];
uint32_t Relative(uint32_t p)
{
    uint32_t base = reinterpret_cast<uint32_t>(g_arenaBase);
    return p >= base && p < base + sizeof(g_arena[0]) ? 0xA0000000 + (p - base) : p;
}

// --- the stand-in tables (16 buckets each) ---
Lst::HashTable::Item g_typeBuckets[16];
Lst::HashTable::Item g_actionBuckets[16];
Lst::HashTable& Types() { return GameVariable<Lst::HashTable>(0x006968D0); }
Lst::HashTable& Actions() { return GameVariable<Lst::HashTable>(0x006968E8); }
void RecordTable(Lst::HashTable& table, const Lst::HashTable::Item* buckets, bool actions)
{
    Record({static_cast<uint32_t>(table.m_size), static_cast<uint32_t>(table.m_iterBucket)});
    for (int i = 0; i < 16; ++i) {
        const Lst::HashTable::Item& item = buckets[i];
        Record({item.key, Relative(reinterpret_cast<uint32_t>(item.value)), reinterpret_cast<uint32_t>(item.next)});
        if (item.value) {
            auto* words = static_cast<const uint32_t*>(item.value);
            Record({words[0] & 0xFF, words[1], actions ? words[2] : 0u});
        }
    }
}

// --- hand-built script data ---
struct TestArray {
    CArray array;
    uint32_t elements[8];
    CArray* Make(std::initializer_list<Params*> items)
    {
        std::memset(this, 0, sizeof(*this));
        array.m_type = Script::ESYMBOLTYPE_STRUCTURE;
        array.m_size = static_cast<uint32_t>(items.size());
        uint32_t i = 0;
        for (Params* p : items)
            elements[i++] = reinterpret_cast<uint32_t>(p->Struct());
        if (array.m_size == 1)
            array.m_single = elements[0];
        else
            array.m_elements = elements;
        return &array;
    }
};

uint32_t Crc(const char* name) { return Crc::GenerateCRCFromString(name); }

// The script's names and the test's checksums.
constexpr uint32_t kSoundActions = 0x5C969C50, kSoundAction = 0x25F654C8, kChance = 0xDBC4C4DB, kUseUpTo = 0x9EE9A09A,
                   kSound = 0x07713C7B, kMaxPitch = 0xFA3E14C5, kMinPitch = 0x1C5EBB24, kMaxVol = 0x0693DAAF,
                   kMinVol = 0x4391992D, kLoadPitch = 0xF573682F, kLoadVol = 0x312CCBCF, kChecksum = 0x21902065,
                   kType = 0x7321A8D6, kAction = 0xB833736D;
constexpr uint32_t kTerrainTypes = 0x533AD720, kTerrainActions = 0x828087B8, kTerrainDefault = 0x64967688,
                   kStandardDefault = 0x5B80CFCB;
constexpr uint32_t kTerrain0 = 0x10001001, kTerrain1 = 0x20002012, kTerrain2 = 0x30003023;
constexpr uint32_t kTA0 = 0x7000, kTA1 = 0x7101, kTA2 = 0x7202, kTA3 = 0x7303;

// The data (built once per test).
struct Data {
    Params types[3], actions[4];
    TestArray typeArray, actionArray;
    Params L1, L2, L3, L4, C1, C2, C3, D0, D1, D2, Ea, Eb, Ec, Ed, Ee, Ef, Eg;
    TestArray EbChoices, EeChoices, SA0List, StdList, DefList2, DefList1, DefList0;
    Params SA0, SAstd, SAdef, T0, T2, Std, Def;
};
Data* g_data;

// --- what the stand-ins answer ---
bool g_noSound;
bool g_noStandard; // STANDARD_TERRAIN_DEFAULT missing
bool g_silent;
uint32_t g_rnd;
alignas(16) uint8_t g_sfx[0x10];
alignas(16) uint8_t g_client[0x10];

// The dropoff distance (a float in ST0): a thiscall stand-in of its own.
struct FakeSfx {
    float Dropoff(uint32_t checksum)
    {
        Record({0xB760, reinterpret_cast<uint32_t>(this) == reinterpret_cast<uint32_t>(g_sfx), checksum});
        return 250.25f + static_cast<float>(checksum % 7);
    }
};

void StandInCalls(StandIns& s)
{
    using GetStructureFn = CStruct* (*)(uint32_t);
    s.At({0x00479020, AddressOf(static_cast<GetStructureFn>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        Data& d = *g_data;
        switch (r.args[0]) {
        case kTerrain0: return reinterpret_cast<uint32_t>(d.T0.Struct());
        case kTerrain2: return reinterpret_cast<uint32_t>(d.T2.Struct());
        case kStandardDefault: return g_noStandard ? 0u : reinterpret_cast<uint32_t>(d.Std.Struct());
        case kTerrainDefault: return reinterpret_cast<uint32_t>(d.Def.Struct());
        default: return 0u;
        }
    });
    using GetArrayFn = CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        if (r.args[0] == kTerrainTypes)
            return reinterpret_cast<uint32_t>(&g_data->typeArray.array);
        return reinterpret_cast<uint32_t>(&g_data->actionArray.array);
    });
    s.At({0x0047B010, AddressOf(&Sfx::NoSound)}, 0, [](Regs&) { Record({0xB010}); return g_noSound ? 1u : 0u; });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.ecx == reinterpret_cast<uint32_t>(g_manager), r.args[0], r.args[1], r.args[2]});
        uint8_t* p = g_arenaBase + g_arenaUsed;
        g_arenaUsed += (r.args[0] + 15) & ~15u;
        return reinterpret_cast<uint32_t>(p);
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) { Record({0x3DE0, r.args[0]}); return g_rnd; });
    using Sfx::CSfxManager;
    s.At({0x0047B360, AddressOf(&CSfxManager::LoadSound)}, 28, [](Regs& r) {
        Record({0xB360, r.ecx == reinterpret_cast<uint32_t>(g_sfx), Hash(reinterpret_cast<const char*>(r.args[0])),
                r.args[1] & 0xFFFF, r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        return 1u;
    });
    s.Bytes(0x0047B760, Branch(0xE9, 0x0047B760, reinterpret_cast<void*>(AddressOf(&FakeSfx::Dropoff))));
    s.Bytes(AddressOf(&CSfxManager::GetDropoffDistance),
            Branch(0xE9, AddressOf(&CSfxManager::GetDropoffDistance), reinterpret_cast<void*>(AddressOf(&FakeSfx::Dropoff))));
    s.At({0x0047BE10, AddressOf(&CSfxManager::GetVolume)}, 24, [](Regs& r) {
        Record({0xBE10, r.ecx == reinterpret_cast<uint32_t>(g_sfx), r.args[1], r.args[2], r.args[3], r.args[4],
                r.args[5]});
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[0]);
        Record({Bits(volume->m_channels[0]), static_cast<uint32_t>(volume->m_type)});
        volume->m_channels[0] = 0.5f;
        volume->m_channels[1] = 0.25f;
        return 1u;
    });
    s.At({0x0047B030, AddressOf(&Sfx::sVolume::Init)}, 0, [](Regs& r) {
        Record({0xB030});
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.ecx);
        std::memset(volume, 0, sizeof(*volume));
        volume->m_type = 7;
        return r.ecx;
    });
    s.At({0x0047AC50, AddressOf(&Sfx::sVolume::PercentageAdjustment)}, 4, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.ecx);
        Record({0xAC50, r.args[0], Bits(volume->m_channels[0]), Bits(volume->m_channels[1])});
        return 0u;
    });
    s.At({0x0047ACF0, AddressOf(&Sfx::sVolume::IsSilent)}, 0, [](Regs&) { Record({0xACF0}); return g_silent ? 1u : 0u; });
    s.At({0x0047BD20, AddressOf(&CSfxManager::PlaySound)}, 24, [](Regs& r) {
        auto* volume = reinterpret_cast<Sfx::sVolume*>(r.args[1]);
        Record({0xBD20, r.ecx == reinterpret_cast<uint32_t>(g_sfx), r.args[0], Bits(volume->m_channels[0]),
                static_cast<uint32_t>(volume->m_type), r.args[2], r.args[3], r.args[4],
                Hash(reinterpret_cast<const char*>(r.args[5]))});
        return 0x77u;
    });
    s.At({0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_client);
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* desc = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x9E50, r.ecx == reinterpret_cast<uint32_t>(g_client), desc[0], desc[2], desc[3], desc[4] & 0xFFFF,
                desc[5], desc[6], desc[7] & 0xFF}); // (not the padding)
        auto* message = reinterpret_cast<const uint8_t*>(desc[1]);
        Record({HashBytes(message, 11)});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Relative(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7090, Relative(r.ecx)}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9071, Relative(r.args[0])});
        return 0u;
    });
}

void BuildData(Data& d)
{
    d.types[0].Checksum(kChecksum, kTerrain0).String(Crc("desc"), "Zero");
    d.types[1].String(Crc("desc"), "One").Checksum(kChecksum, kTerrain1);
    d.types[2].Checksum(kChecksum, kTerrain2);
    d.typeArray.Make({&d.types[0], &d.types[1], &d.types[2]});
    const uint32_t tableTypes[4] = {0xA0, 0xA1, 0xA2, 0xA3};
    const uint32_t tableActions[4] = {kTA0, kTA1, kTA2, kTA3};
    for (int i = 0; i < 4; ++i)
        d.actions[i].Checksum(kType, tableTypes[i]).Checksum(kAction, tableActions[i]);
    d.actionArray.Make({&d.actions[0], &d.actions[1], &d.actions[2], &d.actions[3]});

    d.L1.String(kSound, "snd\\roll1.wav").Float(kMaxPitch, 120.0f).Float(kMinPitch, 30.0f);
    d.L2.String(kSound, "snd/ollie").Float(kMaxVol, 175.0f).Float(kMinVol, 120.0f).Float(kLoadPitch, 90.0f)
        .Float(kLoadVol, 80.5f);
    d.L3.String(kSound, "land3").Float(kMaxVol, 80.0f).Float(kMaxPitch, 150.0f);
    d.L4.Float(kMinPitch, 5.0f);
    d.C1.Integer(kChance, 3).String(kSound, "c1");
    d.C2.Integer(kChance, 5).String(kSound, "dir\\c2/x").Float(kMaxPitch, 50.0f).Float(kMinPitch, 50.0f);
    d.C3.Integer(kChance, 0).String(kSound, "c3");
    d.Ea.Float(kUseUpTo, 10.0f).Structure(kSoundAction, d.L2.Struct());
    d.Eb.Float(kUseUpTo, 20.0f).Array(kSoundAction, d.EbChoices.Make({&d.C1, &d.C2, &d.C3}));
    d.Ec.Structure(kSoundAction, d.L3.Struct());
    d.SA0.Structure(kTA0, d.L1.Struct()).Array(kTA1, d.SA0List.Make({&d.Ea, &d.Eb, &d.Ec}));
    d.Ed.Float(kUseUpTo, 5.0f).Structure(kSoundAction, d.L4.Struct());
    d.Ee.Float(kUseUpTo, 50.0f).Array(kSoundAction, d.EeChoices.Make({&d.C2}));
    d.SAstd.Array(kTA2, d.StdList.Make({&d.Ed, &d.Ee}));
    d.D0.String(kSound, "def0").Float(kMinVol, 10.0f);
    d.D1.String(kSound, "def3").Integer(kTA3, 1);
    d.D2.String(kSound, "def2");
    d.Ef.Structure(kSoundAction, d.L3.Struct());
    d.Eg.Structure(kSoundAction, d.L1.Struct());
    d.SAdef.Structure(kTA3, d.D1.Struct()).Structure(kTA2, d.D2.Struct()).Array(kTA2, d.DefList2.Make({&d.Ef}))
        .Structure(kTA0, d.D0.Struct()).Array(kTA0, d.DefList0.Make({&d.Eg})).Array(kTA1, d.DefList1.Make({&d.Eg, &d.Ef}));
    // (an action whose default structure doesn't name it again needs the array: without one the original reads an
    // uninitialised array)
    d.T0.Structure(kSoundActions, d.SA0.Struct());
    d.T2.Integer(0x1234, 1);
    d.Std.Structure(kSoundActions, d.SAstd.Struct());
    d.Def.Structure(kSoundActions, d.SAdef.Struct());
}

uint32_t Id(CStruct* s)
{
    // which of the test's structures (by address: the same in both runs)
    return reinterpret_cast<uint32_t>(s);
}

void RecordInfo(const SoundInfo& info)
{
    Record({HashBytes(reinterpret_cast<const uint8_t*>(&info), sizeof(info))});
}
void RecordDouble(double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    Record({static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32)});
}

template <typename F>
F Pick(bool original, uint32_t address, F ours)
{
    return original ? Original<F>(address) : ours;
}

void TestTerrainManager()
{
    std::vector<Saved> saved = {{0x006F3624, 4, {}}, {0x006968D0, 0x30, {}}, {0x007CC979, 1, {}},
                                {0x007D1800, 4, {}}, {0x006F3614, 16, {}}, {0x006F85A8, 4, {}},
                                {0x007CCE14, 4, {}}};
    SaveAll(saved);
    CFuncTests::UseEmptySymbolTable();
    auto* data = new Data;
    g_data = data;
    BuildData(*data);
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    using namespace Terrain;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_arenaBase = g_arena[run];
        g_arenaUsed = 0;
        std::memset(g_arenaBase, 0xCD, sizeof(g_arena[0]));
        std::memset(g_manager, 0, sizeof(g_manager));
        std::memset(g_typeBuckets, 0, sizeof(g_typeBuckets));
        std::memset(g_actionBuckets, 0, sizeof(g_actionBuckets));
        std::memset(reinterpret_cast<void*>(0x006968D0), 0, 0x30);
        Types().m_bits = 4;
        Types().m_buckets = g_typeBuckets;
        Types().m_iterBucket = 9;
        Actions().m_bits = 4;
        Actions().m_buckets = g_actionBuckets;
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<void*>(0x007D1800) = g_manager;
        std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
        GameVariable<void*>(0x006F85A8) = g_sfx;
        GameVariable<void*>(0x007CCE14) = reinterpret_cast<void*>(0x4E7000);
        g_noSound = false;
        g_silent = false;
        g_rnd = 0;

        // the tables
        Pick(original, 0x004444A0, &LoadTerrainTypes)();
        RecordTable(Types(), g_typeBuckets, false);
        RecordTable(Actions(), g_actionBuckets, true);
        for (uint32_t checksum : {kTerrain1, kTerrain2, kTerrain0})
            Record({Pick(original, 0x004442C0, &GetTerrainIndex)(checksum)});
        for (uint8_t table : std::initializer_list<uint8_t>{3, 0, 2, 1})
            Record({Pick(original, 0x00444310, &GetTableAction)(table)});
        for (uint8_t terrain : std::initializer_list<uint8_t>{2, 0, 7, 1})
            Record({Pick(original, 0x00444360, &GetTerrainChecksum)(terrain), static_cast<uint32_t>(Types().m_iterBucket)});
        for (uint32_t type : {0xA2u, 0xA0u, 0xFFu, 0xA3u})
            Record({Pick(original, 0x004443B0, &GetTableIndex)(type), static_cast<uint32_t>(Actions().m_iterBucket)});

        // a terrain's structures
        for (uint8_t terrain : std::initializer_list<uint8_t>{0, 1, 2, 3}) {
            for (uint32_t name : {kSoundActions, 0x1234u, 0x999u}) {
                CStruct* out = nullptr;
                bool found = Pick(original, 0x00444400, &GetTerrainStructure)(terrain, &out, name);
                Record({found, Id(out)});
                out = nullptr;
                found = Pick(original, 0x00444480, &GetTerrainStructureInto)(&out, terrain, name);
                Record({found, Id(out)});
            }
        }

        // sound infos
        for (Params* p : {&data->L1, &data->L2, &data->L3, &data->L4, &data->C2}) {
            SoundInfo info;
            std::memset(&info, 0xCD, sizeof(info));
            Record({Pick(original, 0x004445B0, &GetSoundInfo)(p->Struct(), &info)});
            RecordInfo(info);
        }
        {
            g_noSound = true;
            SoundInfo info;
            std::memset(&info, 0xCD, sizeof(info));
            Record({Pick(original, 0x004445B0, &GetSoundInfo)(data->L1.Struct(), &info)});
            RecordInfo(info);
            g_noSound = false;
        }
        // scaled volumes
        {
            const float ranges[][2] = {{100.0f, 0.0f}, {175.0f, 120.0f}, {100.0f, 20.0f}, {80.0f, 0.0f},
                                       {-0.0f, -0.0f}, {100.0f, -0.0f}};
            for (const float* range : ranges) {
                SoundInfo info{};
                info.maxVol = range[0];
                info.minVol = range[1];
                for (float percent : {0.0f, 37.5f, 100.0f, 250.0f, -10.0f, std::numeric_limits<float>::quiet_NaN()}) {
                    for (bool clamp : {false, true}) {
                        RecordDouble(original
                                         ? Original<double(__cdecl*)(const SoundInfo*, float, bool)>(0x00444710)(
                                               &info, percent, clamp)
                                         : GetScaledVolume(&info, percent, clamp));
                    }
                }
            }
        }
        // picking and finding sound actions
        for (uint32_t rnd : {0u, 4u, 5u, 7u, 8u, 0x80000000u}) {
            g_rnd = rnd;
            Record({Id(Pick(original, 0x00444760, &PickSoundAction)(data->Eb.Struct()))});
            Record({Id(Pick(original, 0x00444760, &PickSoundAction)(data->Ea.Struct()))});
            Record({Id(Pick(original, 0x00444760, &PickSoundAction)(data->Ee.Struct()))});
        }
        g_rnd = 6;
        for (float value : {1.0f, 10.0f, 10.5f, 20.0f, 25.0f, std::numeric_limits<float>::quiet_NaN()}) {
            for (uint8_t table : std::initializer_list<uint8_t>{0, 1})
                Record({Id(Pick(original, 0x00444830, &FindSoundAction)(data->SA0.Struct(), table, value))});
            Record({Id(Pick(original, 0x00444830, &FindSoundAction)(data->SAstd.Struct(), 2, value * 4.0f))});
        }
        // loading
        for (Params* p : {&data->L1, &data->L2, &data->L4, &data->C2})
            Pick(original, 0x004448E0, &LoadSoundAction)(p->Struct());
        for (uint8_t table : std::initializer_list<uint8_t>{0, 1, 2, 3}) {
            Pick(original, 0x00444950, &LoadTableSounds)(data->SA0.Struct(), table);
            Pick(original, 0x00444950, &LoadTableSounds)(data->SAdef.Struct(), table);
        }
        Pick(original, 0x00444950, &LoadTableSounds)(data->SAstd.Struct(), 2);
        // a terrain's sound
        for (bool noSound : {false, true}) {
            g_noSound = noSound;
            for (uint8_t terrain : std::initializer_list<uint8_t>{0, 1, 2}) {
                for (uint8_t table : std::initializer_list<uint8_t>{0, 1, 2, 3}) {
                    for (float value : {5.0f, 15.0f, 30.0f}) {
                        SoundInfo info;
                        std::memset(&info, 0xCD, sizeof(info));
                        Record({Pick(original, 0x00444B10, &GetTerrainSound)(&info, terrain, table, value)});
                        RecordInfo(info);
                    }
                }
            }
        }
        g_noSound = false;
        // playing one
        {
            Mth::Vector position;
            position.x = 1234.75f;
            position.y = -77.5f;
            position.z = 40000.0f;
            position.w = 1.0f;
            struct Case {
                uint8_t table, terrain;
                float volume, pitch, value;
                bool propagate, silent, noSound;
            };
            const Case cases[] = {
                {0, 0, 50.0f, 75.0f, 1.0f, false, false, false},  // L1: pitch range
                {1, 0, 33.3f, 99.9f, 5.0f, true, false, false},   // L2: volume range, sent
                {1, 0, 120.0f, -20.0f, 15.0f, true, true, false}, // a chance pick, silent
                {1, 0, 300.5f, 10.0f, 22.0f, false, false, false}, // L3
                {0, 1, 60.0f, 60.0f, 2.0f, true, false, false},   // the default's D0 (minVol)
                {2, 1, 60.0f, 60.0f, 2.0f, true, false, false},   // L4: no sound name
                {2, 2, 70.0f, 70.0f, 60.0f, false, false, false}, // nothing fits
                {3, 1, 255.9f, 256.7f, 257.8f, true, false, false}, // D1, bytes wrapping
                {0, 0, 50.0f, 50.0f, 1.0f, true, false, true},    // no sound
            };
            g_rnd = 3;
            for (const Case& c : cases) {
                g_silent = c.silent;
                g_noSound = c.noSound;
                Pick(original, 0x00444BB0, &PlayTerrainSound)(c.table, c.terrain, &position, c.volume, c.pitch, c.value,
                                                              c.propagate);
                Record({0xCA5E});
            }
            g_silent = false;
            g_noSound = false;
        }
        // loading a terrain's sounds
        for (uint32_t checksum : {kTerrain0, kTerrain1, kTerrain2}) {
            Record({Pick(original, 0x00444EB0, &LoadTerrainSounds)(checksum)});
            g_noSound = checksum == kTerrain1;
            Record({Pick(original, 0x00444EB0, &LoadTerrainSounds)(checksum)});
            g_noSound = false;
        }
        g_noStandard = true; // (no SoundActions anywhere: nothing loaded, still true)
        Record({Pick(original, 0x00444EB0, &LoadTerrainSounds)(kTerrain2)});
        g_noStandard = false;
        // emptied
        Pick(original, 0x00444460, &ClearTerrainTypes)();
        RecordTable(Types(), g_typeBuckets, false);
        RecordTable(Actions(), g_actionBuckets, true);
        for (uint32_t& word : g_trace)
            word = Relative(word);
        std::vector<uint32_t> image(sizeof(g_arena[0]) / 4);
        std::memcpy(image.data(), g_arenaBase, sizeof(g_arena[0]));
        for (uint32_t& word : image)
            word = Relative(word);
        g_trace.insert(g_trace.end(), image.begin(), image.end());
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    delete data;
    g_data = nullptr;
    Check(traces[0].size() > 1500, "terrain manager: the original reached its calls");
    Compare(traces[0], traces[1], "terrain manager (0x004442C0-0x00444EB0)");
}

} // namespace

SELFTEST(Early, TestTerrainManager, RemoveStandIns);
