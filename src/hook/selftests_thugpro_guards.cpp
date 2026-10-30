// Self-tests for the THUGPRO compatibility work: the trick object manager's graffiti
// log 0x005886C0 (original vs ours on stand-in clusters, players and game mode; ours with more clusters than THUG2's
// mask held).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_guards.h"
#include "game/net/thugpro_compat.h"
#include "game/obj/object.h"
#include "game/obj/trick_objects.h"
#include "game/script/struct.h"
#include "game/skate/appearance_guard.h"
#include "game/skate/skate_net.h"
#include "game/skate/thugpro_cas.h"
#include "game/skater/local_net_logic.h"
#include "game/util/bitstream.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"
#include "hook/thugpro_testkit.h"

namespace {

using namespace TestStandIns;

void Cleanup()
{
    RemoveStandIns();
    GameNet::Thugpro::SetPositionScale({});
    GameNet::Guards::SetLookups(GameNet::Guards::GameLookups());
    Obj::AppearanceGuard::SetCatalog(Obj::AppearanceGuard::GameCatalog());
}

// --- the graffiti log ------------------------------------------------------------------------------------------

constexpr uint32_t kNetGraffiti = 0x5E2EA50C;
constexpr uint32_t kTeamScoring = 0xAB80CC2F;

struct GraffitiWorld {
    // The manager (+0x20 its hash table: 2 bits, 4 buckets, chained items) and up to 24 clusters.
    alignas(16) uint8_t manager[0x40];
    Lst::HashTable::Item buckets[4];
    Lst::HashTable::Item chained[32];
    Obj::CTrickObjectCluster clusters[24];
    // Players (+0x14 skater, +0x30 team) and skaters (+0x14 object id).
    uint8_t players[8][0x40];
    uint8_t skaters[8][0x20];
    int playerCount;
    uint8_t goals[0x10];
};
GraffitiWorld g_gw;
uint32_t g_gwMode, g_gwTeamMode;
bool g_gwTeams;
int g_gwUnknownOnce = -1; // an object id the network manager doesn't know the first time it's asked
bool g_gwEmptyWalk;       // the player walk finds nobody

uint32_t Rel(uint32_t word)
{
    uint32_t base = reinterpret_cast<uint32_t>(&g_gw);
    return word - base < sizeof(g_gw) ? 0x60000000 + (word - base) : word;
}

void BuildWorld(uint32_t seed, int clusterCount)
{
    std::memset(&g_gw, 0, sizeof(g_gw));
    auto random = [&seed](uint32_t range) {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 8) % range;
    };
    auto* table = reinterpret_cast<Lst::HashTable*>(g_gw.manager + 0x20);
    table->m_bits = 2;
    table->m_buckets = g_gw.buckets;
    int chained = 0;
    for (int i = 0; i < clusterCount; ++i) {
        Obj::CTrickObjectCluster& c = g_gw.clusters[i];
        c.m_name = 0x1000 + static_cast<uint32_t>(i) * 7;
        c.m_bestScore = random(4) == 0 ? 0 : random(3000);
        for (uint32_t& s : c.m_scores)
            s = random(3) == 0 ? 0 : random(2500);
        c.m_owner = static_cast<int>(random(5));
        c.m_owned = random(3) != 0;
        if (random(7) == 0 && !c.m_owned)
            c.m_owner |= 0x10000; // (the high half: read as 16 bits in one place)
        Lst::HashTable::Item* bucket = &g_gw.buckets[c.m_name & 3];
        if (!bucket->value) {
            *bucket = {c.m_name, &c, nullptr};
        } else {
            Lst::HashTable::Item* item = &g_gw.chained[chained++];
            *item = {c.m_name, &c, bucket->next};
            bucket->next = item;
        }
    }
    // An item with the right name but no value (skipped by the lookup).
    if (chained < 32) {
        Lst::HashTable::Item* item = &g_gw.chained[chained++];
        *item = {0x9990, nullptr, g_gw.buckets[0].next};
        g_gw.buckets[0].next = item;
    }
    g_gw.playerCount = 1 + static_cast<int>(random(5));
    g_gwUnknownOnce = -1;
    g_gwEmptyWalk = false;
    for (int p = 0; p < g_gw.playerCount; ++p) {
        uint8_t* player = g_gw.players[p];
        bool hasSkater = random(6) != 0;
        *reinterpret_cast<uint8_t**>(player + 0x14) = hasSkater ? g_gw.skaters[p] : nullptr;
        *reinterpret_cast<int*>(player + 0x30) = static_cast<int>(random(2));
        *reinterpret_cast<int*>(g_gw.skaters[p] + 0x14) = p;
    }
}

uint32_t CallLog(bool original, uint32_t count, uint32_t* clusters, uint8_t* stolen, int player, uint32_t score)
{
    using Fn = uint32_t(__thiscall*)(void*, uint32_t, uint32_t*, uint8_t*, int, uint32_t);
    auto* code = reinterpret_cast<Fn>(AppMainTests::Code(original, 0x005886C0));
    return code(g_gw.manager, count, clusters, stolen, player, score);
}

void StandInGraffiti(StandIns& s)
{
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0xD0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xFFFFFFFFu;
    });
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) {
        Record({0xD1});
        return reinterpret_cast<uint32_t>(g_gw.goals);
    });
    s.At({0x00558770, AddressOf(&Game::CGoalManager::KillCluster)}, 8, [](Regs& r) {
        Record({0xD2, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Record({0xD3, r.ecx});
        return g_gwMode;
    });
    s.At({0x005535F0, AddressOf(&Game::CGameMode::IsTeamGame)}, 0, [](Regs& r) {
        Record({0xD4, r.ecx});
        return g_gwTeams ? 1u : 0u;
    });
    s.At({0x00553300, AddressOf(&Game::CGameMode::GetTeamGraffitiMode)}, 0, [](Regs& r) {
        Record({0xD5, r.ecx});
        return g_gwTeamMode;
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xD6, r.ecx, r.args[0]});
        int id = static_cast<int>(r.args[0]);
        if (id == g_gwUnknownOnce) {
            g_gwUnknownOnce = -1;
            return 0u;
        }
        return reinterpret_cast<uint32_t>(g_gw.players[static_cast<uint32_t>(id) % g_gw.playerCount]);
    });
    // The player walk: the search's node holds the next index + 1.
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0xD7, r.ecx, r.args[1]});
        auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
        Record({search[0]});
        if (g_gwEmptyWalk)
            return 0u;
        search[1] = 1;
        return reinterpret_cast<uint32_t>(g_gw.players[0]);
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0xD8, r.ecx, r.args[1]});
        auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
        uint32_t next = search[1];
        if (static_cast<int>(next) >= g_gw.playerCount)
            return 0u;
        search[1] = next + 1;
        return reinterpret_cast<uint32_t>(g_gw.players[next]);
    });
}

// Original vs ours: random worlds, lists (with missing clusters and repeats), players, scores, modes - free skate,
// graffiti, team graffiti with team or individual scoring; lists up to 200 (past THUG2's 128-byte mask, whose bytes
// after it are saved and put back around the original's run).
void TestGraffitiLog()
{
    std::vector<uint32_t> traces[2];
    std::vector<Saved> saved = {{0x007CE470, 4, {}}, {0x007CCE14, 4, {}}, {0x007CE508, 0xD0, {}}};
    SaveAll(saved);
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto* s = new StandIns();
        StandInGraffiti(*s);
        GameVariable<uint32_t>(0x007CE470) = 0x7E470000;
        GameVariable<uint32_t>(0x007CCE14) = 0x7CCE0000;
        for (uint32_t round = 0; round < 160; ++round) {
            int clusterCount = 1 + static_cast<int>(round % 24);
            BuildWorld(round * 2654435761u + 7, clusterCount);
            uint32_t seed = round * 40503u + 99;
            auto random = [&seed](uint32_t range) {
                seed = seed * 1103515245u + 12345u;
                return (seed >> 8) % range;
            };
            switch (round % 5) {
            case 0: g_gwMode = 0x1234; g_gwTeams = false; g_gwTeamMode = 0; break;
            case 1: g_gwMode = kNetGraffiti; g_gwTeams = false; g_gwTeamMode = kTeamScoring; break;
            case 2: g_gwMode = kNetGraffiti; g_gwTeams = true; g_gwTeamMode = kTeamScoring; break;
            case 3: g_gwMode = kNetGraffiti; g_gwTeams = true; g_gwTeamMode = 0xC8059823; break;
            default: g_gwMode = kNetGraffiti; g_gwTeams = true; g_gwTeamMode = kTeamScoring; g_gwEmptyWalk = true; break;
            }
            uint32_t count = round % 17 == 3 ? 0 : round % 23 == 5 ? 200 : 1 + random(30);
            std::vector<uint32_t> list(count + 1, 0xEEEEEEEE);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t pick = random(clusterCount + 3);
                list[i] = static_cast<int>(pick) < clusterCount ? g_gw.clusters[pick].m_name
                          : pick == static_cast<uint32_t>(clusterCount) ? 0x9990 : 0x5555 + pick;
            }
            int player = static_cast<int>(random(5));
            if (round % 11 == 4)
                g_gwUnknownOnce = player; // (a player the network manager doesn't know, once)
            uint32_t score = round % 13 == 6 ? 0 : random(4000);
            uint8_t stolen[8 + 0x10] = {};
            uint32_t kept = CallLog(original, count, list.data(), stolen, player, score);
            Record({0xE0, kept});
            for (uint32_t word : list)
                Record({word});
            for (uint8_t b : stolen)
                Record({b});
            for (int i = 0; i < clusterCount; ++i) {
                const Obj::CTrickObjectCluster& c = g_gw.clusters[i];
                Record({c.m_bestScore, static_cast<uint32_t>(c.m_owner), c.m_owned ? 1u : 0u});
                for (uint32_t sc : c.m_scores)
                    Record({sc});
            }
            if (original)
                std::memcpy(reinterpret_cast<void*>(0x007CE508), saved[2].bytes.data(), 0xD0);
        }
        delete s;
        traces[run] = g_trace;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "graffiti log 0x005886C0");

    // Ours only: a list longer than THUG2's mask and than ours - no overrun, the first 2048 logged.
    {
        auto* s = new StandIns();
        StandInGraffiti(*s);
        BuildWorld(12345, 8);
        g_gwMode = 0x1234;
        for (Obj::CTrickObjectCluster& c : g_gw.clusters) {
            c.m_owned = false;
            c.m_bestScore = 0;
        }
        std::vector<uint32_t> list(3000 + 1, 0xEEEEEEEE);
        for (uint32_t i = 0; i < 3000; ++i)
            list[i] = g_gw.clusters[(i < 2048 ? 0 : 4) + i % 4].m_name;
        uint8_t stolen[8] = {};
        uint32_t kept = CallLog(false, 3000, list.data(), stolen, 1, 10);
        delete s;
        // (each of the first 4 clusters is first taken once: the first 4 entries kept; the 4 named only after the
        // first 2048 entries aren't looked at; nothing past the list touched)
        Check(kept == 4 && list[3000] == 0xEEEEEEEE && list[0] == g_gw.clusters[0].m_name,
              "graffiti log: a list past the change mask's size");
    }
}

uint32_t g_rand;
uint32_t Random(uint32_t range)
{
    g_rand = g_rand * 1103515245u + 12345u;
    return range ? (g_rand >> 8) % range : 0;
}
uint32_t RandomBits(int bits)
{
    g_rand = g_rand * 1103515245u + 12345u;
    uint32_t value = (g_rand >> 4) ^ (g_rand << 13);
    return bits >= 32 ? value : value & ((1u << bits) - 1);
}

// --- the trick objects, the manager's lookups and graffiti state (0x00588200-0x005889E0) --------------------------

struct TrickWorld {
    uint8_t manager[0x60];
    Lst::HashTable::Item buckets[2][4];
    Lst::HashTable::Item chained[2][40];
    Obj::CTrickObjectCluster clusters[12];
    uint8_t objects[24][0x18];       // the pool Mem::Calloc hands out
    uint8_t sectors[4][0x20];        // +0x14 a mesh (or none)
    void* meshes[2];                 // their vtable pointers
    void* meshVtable[8];
    int poolNext;
    uint8_t buffer[0x60];
};
TrickWorld g_tw;
int g_twCallocsLeft;
bool g_twShowAll;
uint32_t g_twSectorColor[4];

uint32_t TwRel(uint32_t word)
{
    uint32_t base = reinterpret_cast<uint32_t>(&g_tw);
    return word - base < sizeof(g_tw) ? 0x70000000 + (word - base) : word;
}

void BuildTrickWorld(uint32_t seed, int clusterCount)
{
    g_rand = seed;
    std::memset(&g_tw, 0, sizeof(g_tw));
    auto* manager = reinterpret_cast<Obj::CTrickObjectManager*>(g_tw.manager);
    Lst::HashTable* tables[2] = {&manager->m_clusters, &manager->m_objectClusters};
    int chained[2] = {};
    auto put = [&](int t, uint32_t key, void* value) {
        Lst::HashTable::Item* bucket = &g_tw.buckets[t][key & 3];
        if (!bucket->value && !bucket->next) {
            *bucket = {key, value, nullptr};
        } else {
            Lst::HashTable::Item* item = &g_tw.chained[t][chained[t]++];
            *item = {key, value, bucket->next};
            bucket->next = item;
        }
    };
    for (int t = 0; t < 2; ++t) {
        tables[t]->m_bits = 2;
        tables[t]->m_buckets = g_tw.buckets[t];
    }
    for (int i = 0; i < clusterCount; ++i) {
        Obj::CTrickObjectCluster& c = g_tw.clusters[i];
        c.m_name = 0x2000 + static_cast<uint32_t>(i) * 5;
        c.m_owner = static_cast<int>(Random(4));
        c.m_owned = Random(3) != 0;
        c.m_objects.next = c.m_objects.prev = &c.m_objects;
        put(0, c.m_name, &c);
        put(1, c.m_name, &c);
        put(1, 0x9000 + static_cast<uint32_t>(i), &c); // (an object of it)
    }
    put(0, 0x2001, nullptr); // (an item with no value: skipped)
    g_tw.meshVtable[6] = nullptr;
    for (int s = 0; s < 4; ++s)
        *reinterpret_cast<void**>(g_tw.sectors[s] + 0x14) = s % 3 == 2 ? nullptr : &g_tw.meshes[s & 1];
}

void StandInTrickObjects(StandIns& s)
{
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0xD0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0xFFFFFFFFu;
    });
    s.At({0x0048C510, AddressOf(Nx::Engine::GetSector)}, 0, [](Regs& r) {
        Record({0xA0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_tw.sectors[r.args[0] & 3]);
    });
    s.At({0x0049E670, AddressOf(&Nx::CSector::ClearColor)}, 0, [](Regs& r) {
        Record({0xA1, TwRel(r.ecx)});
        return 0u;
    });
    s.At({0x0049E980, AddressOf(&Nx::CSector::GetColor)}, 4, [](Regs& r) {
        Record({0xA2, TwRel(r.ecx)});
        int sector = static_cast<int>((r.ecx - reinterpret_cast<uint32_t>(g_tw.sectors)) / 0x20);
        *reinterpret_cast<uint32_t*>(r.args[0]) = g_twSectorColor[sector & 3];
        return r.args[0];
    });
    // graffitiColors: a stand-in array of arrays (element i of the outer: 0xC0100000 + i; of a colour: its bytes).
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0xA3, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xC0000000u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xA4, r.ecx, r.args[0]});
        if (r.ecx == 0xC0000000u)
            return 0xC0100000u + r.args[0];
        uint32_t index = r.ecx - 0xC0100000u;
        return (index * 53u + r.args[0] * 91u + 7u) & 0x1FFu; // (sometimes past a byte: the low byte is the colour)
    });
    s.At({0x004790C0, AddressOf(static_cast<int (*)(const char*)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0xA5, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_twShowAll ? 3u : 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xA6, r.args[0]});
        if (g_twCallocsLeft-- <= 0 || g_tw.poolNext >= 24)
            return 0u;
        uint8_t* block = g_tw.objects[g_tw.poolNext++];
        std::memset(block, 0xCD, 0x18);
        return reinterpret_cast<uint32_t>(block);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xA7, r.ecx, TwRel(r.args[0])});
        return 0u;
    });
    void* setColor = s.Thunk(4, [](Regs& r) {
        Record({0xA8, TwRel(r.ecx), r.args[0]});
        return 0u;
    });
    g_tw.meshVtable[6] = setColor;
    g_tw.meshes[0] = g_tw.meshes[1] = g_tw.meshVtable;
}

// Original vs ours: the lookups (found, missing, an item without a value), the clusters each player holds, a
// cluster's name by index, the graffiti state message, trick objects added (out of memory too; show_all_trick_objects
// in and out of the 0x00684FA8 mode), coloured and uncoloured (mesh or none; the colour matching or not), the node
// destructors.
void TestTrickObjects()
{
    std::vector<Saved> saved = {{0x00684FA8, 1, {}}, {0x007D1800, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto* s = new StandIns();
        AppMainTests::Originals guards(original, {0x00588200, 0x005883C0, 0x00588120, 0x00588430, 0x00588450,
                                                  0x005884B0, 0x005B8060});
        GameVariable<uint32_t>(0x007D1800) = 0x7D180000;
        for (int round = 0; round < 40; ++round) {
            BuildTrickWorld(31337 + static_cast<uint32_t>(round), 1 + round % 12);
            StandInTrickObjects(*s);
            auto* manager = reinterpret_cast<Obj::CTrickObjectManager*>(g_tw.manager);
            using Lookup = void*(__thiscall*)(void*, uint32_t);
            using Count = int(__thiscall*)(void*, int);
            using Name = uint32_t(__thiscall*)(void*, uint8_t);
            using Write = int(__thiscall*)(void*, uint8_t*);
            using Add = bool(__thiscall*)(void*, uint32_t);
            using Color = bool(__thiscall*)(void*, int);
            using Destroy = void*(__thiscall*)(void*, unsigned int);
            using Body = void(__thiscall*)(void*);
            for (uint32_t name : {0x2000u, 0x2001u, 0x2005u, 0x9003u, 0x9000u, 0x7777u, 0x2037u}) {
                Record({0xE1, TwRel(reinterpret_cast<uint32_t>(
                                  reinterpret_cast<Lookup>(AppMainTests::Code(original, 0x00588380))(manager, name))),
                        TwRel(reinterpret_cast<uint32_t>(
                            reinterpret_cast<Lookup>(AppMainTests::Code(original, 0x005883C0))(manager, name)))});
            }
            for (int player = 0; player < 5; ++player)
                Record({0xE2, static_cast<uint32_t>(
                                  reinterpret_cast<Count>(AppMainTests::Code(original, 0x00588400))(manager, player))});
            for (int index = 0; index < 14; ++index)
                Record({0xE3, reinterpret_cast<Name>(AppMainTests::Code(original, 0x00588480))(
                                  manager, static_cast<uint8_t>(index == 13 ? 255 : index))});
            std::memset(g_tw.buffer, 0xEE, sizeof(g_tw.buffer));
            int written = reinterpret_cast<Write>(AppMainTests::Code(original, 0x00588520))(manager, g_tw.buffer);
            Record({0xE4, static_cast<uint32_t>(written), HashBytes(g_tw.buffer, sizeof(g_tw.buffer))});
            // Objects added to a cluster.
            Obj::CTrickObjectCluster& cluster = g_tw.clusters[0];
            g_twCallocsLeft = round % 5 == 4 ? 1 : 4;
            g_twShowAll = round % 3 != 0;
            GameVariable<bool>(0x00684FA8) = round % 4 == 1;
            for (int n = 0; n < 3; ++n) {
                bool added = reinterpret_cast<Add>(AppMainTests::Code(original, 0x00588630))(
                    &cluster, static_cast<uint32_t>(n + round));
                Record({0xE5, added ? 1u : 0u});
            }
            for (Mem::ListNode* node = cluster.m_objects.next; node != &cluster.m_objects; node = node->next)
                Record({0xE6, TwRel(reinterpret_cast<uint32_t>(node)), TwRel(reinterpret_cast<uint32_t>(node->vtable)),
                        TwRel(reinterpret_cast<uint32_t>(node->data)), static_cast<uint32_t>(node->priority),
                        reinterpret_cast<Obj::CTrickObject*>(node)->m_name});
            // Colours set and cleared.
            for (int k = 0; k < 4; ++k) {
                uint8_t* object = g_tw.objects[k];
                *reinterpret_cast<uint32_t*>(object + 0x14) = static_cast<uint32_t>(k + round);
                // The sector's colour: graffiti colour k, or not (all of it, or only its g or b).
                uint32_t r = (k * 53u + 7u) & 0xFF, g = (k * 53u + 91u + 7u) & 0xFF, b = (k * 53u + 182u + 7u) & 0xFF;
                switch (Random(4)) {
                case 0: break;
                case 1: r = Random(256); g = Random(256); b = Random(256); break;
                case 2: b ^= 1 + Random(255); break;
                default: g ^= 1 + Random(255); break;
                }
                g_twSectorColor[(k + round) & 3] = r | g << 8 | b << 16 | Random(256) << 24;
                Record({0xE7, reinterpret_cast<Color>(AppMainTests::Code(original, 0x00588200))(object, k) ? 1u : 0u,
                        reinterpret_cast<Color>(AppMainTests::Code(original, 0x00588270))(object, k) ? 1u : 0u});
            }
            // The node destructors.
            if (g_tw.poolNext >= 3) {
                reinterpret_cast<Body>(AppMainTests::Code(original, 0x00588590))(g_tw.objects[0]);
                reinterpret_cast<Destroy>(AppMainTests::Code(original, 0x005885F0))(g_tw.objects[1], round & 1);
                Record({TwRel(reinterpret_cast<uint32_t>(reinterpret_cast<Destroy>(AppMainTests::Code(
                    original, 0x005885B0))(g_tw.objects[2], (round >> 1) & 1)))});
            }
            Record({0xE8, HashBytes(reinterpret_cast<const uint8_t*>(&g_tw), offsetof(TrickWorld, meshes))});
            // (the pointers in the world are the same in both runs: g_tw is one static)
            delete s;
            s = new StandIns();
        }
        delete s;
        traces[run] = g_trace;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "trick objects 0x00588200-0x005889E0");
}

// --- the skaters' updates in big levels (TP-R6) -----------------------------------------------------------------

// A coordinate: mostly inside THUG2's range, some past it (wrapping there: what a big level is for).
float RandomCoordinate(bool height)
{
    float range = Random(4) == 0 ? 60000.0f : height ? 8000.0f : 16000.0f;
    return (static_cast<float>(Random(200001)) / 100000.0f - 1.0f) * range;
}

// The world: the server's player (+0x14 its skater, +0x18 its connection: +0x60 bit 0 ready), skaters (+0x14 the id,
// +0x27C the state history), histories (20 entries of 0x84 from +0x1C, the count at +0x18).
struct UpdateWorld {
    uint8_t players[8][0x40];
    uint8_t conns[8][0x80];
    uint8_t skaters[8][0x300];
    uint8_t histories[8][0xA70];
    uint8_t netManager[0x40];
    uint8_t serverApp[0x1F18];
    uint8_t sent[0x180];
    uint8_t msg[0x400];
};
UpdateWorld g_uw;
int g_uwPlayer; // the player the server's 0x0053AE10 finds (-1: none)

void BuildUpdateWorld(uint32_t seed)
{
    g_rand = seed;
    std::memset(&g_uw, 0, sizeof(g_uw));
    for (int i = 0; i < 8; ++i) {
        *reinterpret_cast<uint8_t**>(g_uw.players[i] + 0x14) = g_uw.skaters[i];
        *reinterpret_cast<uint8_t**>(g_uw.players[i] + 0x18) = g_uw.conns[i];
        g_uw.conns[i][0x60] = static_cast<uint8_t>(Random(4) == 0 ? 2 : 1 + 2 * Random(2));
        *reinterpret_cast<uint32_t*>(g_uw.skaters[i] + 0x14) = static_cast<uint32_t>(i) | (Random(2) << 8);
        *reinterpret_cast<uint8_t**>(g_uw.skaters[i] + 0x27C) = g_uw.histories[i];
        uint8_t* history = g_uw.histories[i];
        int count = static_cast<int>(Random(3) == 0 ? 0 : 1 + Random(45));
        *reinterpret_cast<int*>(history + 0x18) = count;
        for (int e = 0; e < 20; ++e) {
            uint8_t* entry = history + 0x1C + e * 0x84;
            for (int k = 0; k < 0x84; k += 4)
                *reinterpret_cast<uint32_t*>(entry + k) = RandomBits(32);
            for (int k = 0; k < 3; ++k) {
                *reinterpret_cast<float*>(entry + 0x48 + 4 * k) = RandomCoordinate(k == 1);
                // (angles as the messages carry them, in 1/4096ths: our Mth::Matrix::SetFromEulerAngles can differ
                // from the original's by an ulp for other values - its sines are taken in single precision)
                *reinterpret_cast<float*>(entry + 0x58 + 4 * k) =
                    static_cast<float>(static_cast<int>(Random(Random(5) == 0 ? 65536 : 26000)) - (Random(5) == 0 ? 32768 : 13000)) /
                    4096.0f;
            }
            if (Random(3) == 0) { // (the small values the game keeps there)
                entry[0x70] = static_cast<uint8_t>(Random(8));
                entry[0x74] = static_cast<uint8_t>(Random(2));
                entry[0x75] = static_cast<uint8_t>(Random(2));
                entry[0x76] = static_cast<uint8_t>(Random(2));
                entry[0x7C] = static_cast<uint8_t>(Random(64));
            }
        }
    }
    *reinterpret_cast<uint8_t**>(g_uw.netManager + 4) = g_uw.serverApp;
    for (int k = 0; k < 0x180; k += 4)
        *reinterpret_cast<uint32_t*>(g_uw.sent + k) = RandomBits(32);
}

void StandInUpdates(StandIns& s)
{
    s.At({0x0053AE10, AddressOf(&GameNet::Manager::PlayerByConnection)}, 4, [](Regs& r) {
        Record({0xB0, r.args[0]});
        return g_uwPlayer < 0 ? 0u : reinterpret_cast<uint32_t>(g_uw.players[g_uwPlayer]);
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xB1, r.args[0]});
        return reinterpret_cast<uint32_t>(g_uw.players[r.args[0] & 7]);
    });
}

const char* ScaleName(int mode)
{
    static const char* const names[] = {"", " (big level)", " (tall level)", " (big and tall)"};
    return names[mode];
}

// 0x0056BE60 the server's reader: messages of every field mix (a 32-bit time stamp going back sometimes), from a
// player that's known and ready or not, packets late / foreign; each scale.
void TestClientObjectUpdates()
{
    std::vector<Saved> saved = {{0x007CCE14, 4, {}}};
    SaveAll(saved);
    for (int mode = 0; mode < ThugproTests::kScaleModes; ++mode) {
        std::vector<uint32_t> traces[2];
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            auto* s = new StandIns();
            StandInUpdates(*s);
            // (both runs take their matrices from ours: Mth::Matrix::SetFromEulerAngles can differ from the
            // original's by an ulp - its sines in single precision; the skate net tests do the same)
            s->Bytes(0x004048D0,
                     Branch(0xE9, 0x004048D0, reinterpret_cast<void*>(AddressOf(&Mth::Matrix::SetFromEulerAngles))));
            ThugproTests::UseScale(original, mode);
            AppMainTests::Originals guards(original, {0x00401050, 0x00401070, 0x004011B0, 0x00401260, 0x004010C0,
                                                      0x00404090});
            BuildUpdateWorld(777 + mode);
            GameVariable<uint8_t*>(0x007CCE14) = g_uw.netManager;
            using Fn = int(__cdecl*)(Net::MsgHandlerContext*);
            auto* code = reinterpret_cast<Fn>(AppMainTests::Code(original, 0x0056BE60));
            uint32_t times[8] = {};
            for (int round = 0; round < 400; ++round) {
                int who = static_cast<int>(Random(8));
                g_uwPlayer = round % 23 == 7 ? -1 : who;
                if (round % 29 == 3)
                    *reinterpret_cast<uint8_t**>(g_uw.players[who] + 0x14) = nullptr;
                if (round % 29 == 4)
                    *reinterpret_cast<uint8_t**>(g_uw.players[who] + 0x14) = g_uw.skaters[who];
                std::memset(g_uw.msg, 0, sizeof(g_uw.msg));
                Util::BitStream stream;
                stream.SetOutputData(g_uw.msg, sizeof(g_uw.msg));
                times[who] += Random(5) == 0 ? static_cast<uint32_t>(-static_cast<int>(Random(50))) : Random(100);
                if (round % 37 == 11)
                    times[who] = RandomBits(32);
                stream.WriteBits(times[who], 32);
                uint32_t fields = round % 11 == 0 ? 0x1FFu : round % 11 == 1 ? 0u : RandomBits(9);
                stream.WriteBits(fields, 9);
                for (int k = 0; k < 6; ++k) {
                    if (fields & (1u << k))
                        stream.WriteBits(RandomBits(16), 16);
                }
                if (fields & 0x40) {
                    stream.WriteBits(RandomBits(4), 4);
                    stream.WriteBits(RandomBits(6), 6);
                    stream.WriteBits(RandomBits(1), 1);
                    stream.WriteBits(RandomBits(1), 1);
                }
                if (fields & 0x80) {
                    stream.WriteBits(RandomBits(5), 5);
                    stream.WriteBits(RandomBits(3), 3);
                }
                if (fields & 0x100)
                    stream.WriteBits(RandomBits(32), 32);
                stream.Flush();
                Net::MsgHandlerContext context;
                std::memset(&context, 0, sizeof(context));
                context.m_Msg = reinterpret_cast<char*>(g_uw.msg);
                context.m_MsgId = 0x23;
                context.m_MsgLength = 0x77;
                context.m_Conn = reinterpret_cast<Net::Conn*>(0x0C000000 + who);
                context.m_PacketFlags = round % 13 == 5 ? 1u : round % 13 == 6 ? 2u : round % 13 == 7 ? 4u : 0u;
                int result = code(&context);
                Record({0xB2, static_cast<uint32_t>(result), context.m_MsgLength});
                Record({HashBytes(g_uw.histories[who], sizeof(g_uw.histories[who]))});
            }
            for (auto& history : g_uw.histories)
                Record({HashBytes(history, sizeof(history))});
            ThugproTests::EndScale();
            delete s;
            traces[run] = g_trace;
        }
        char what[96];
        std::snprintf(what, sizeof(what), "server's object update reader 0x0056BE60%s", ScaleName(mode));
        Compare(traces[0], traces[1], what);
    }
    RestoreAll(saved);
}

// 0x005696C0 / 0x005698F0 the server's relay of each skater to a client: random states and records of what was sent
// (equal or not), the server's frame near and past the 60-frame refresh both ways; each scale.
void TestRelayObjectUpdates()
{
    std::vector<Saved> saved = {{0x007CCE14, 4, {}}};
    SaveAll(saved);
    for (int mode = 0; mode < ThugproTests::kScaleModes; ++mode) {
        std::vector<uint32_t> traces[2];
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            auto* s = new StandIns();
            StandInUpdates(*s);
            ThugproTests::UseScale(original, mode);
            AppMainTests::Originals guards(original, {0x00401050, 0x00401090, 0x004010D0, 0x00401180, 0x004010C0,
                                                      0x005696C0});
            BuildUpdateWorld(4242 + mode);
            GameVariable<uint8_t*>(0x007CCE14) = g_uw.netManager;
            using Flags = uint32_t(__cdecl*)(void*, void*, uint8_t);
            using Write = void(__cdecl*)(void*, Mdl::SkateNet::RelayStream*);
            auto* flagsOf = reinterpret_cast<Flags>(AppMainTests::Code(original, 0x005696C0));
            auto* write = reinterpret_cast<Write>(AppMainTests::Code(original, 0x005698F0));
            int& frame = *reinterpret_cast<int*>(g_uw.serverApp + 0x1410);
            uint32_t* forced = reinterpret_cast<uint32_t*>(g_uw.sent + 0x50);
            for (int round = 0; round < 150; ++round) {
                int who = static_cast<int>(Random(8));
                uint8_t* history = g_uw.histories[who];
                if (*reinterpret_cast<int*>(history + 0x18) <= 0)
                    *reinterpret_cast<int*>(history + 0x18) = 1 + static_cast<int>(Random(30));
                uint8_t* entry = history + 0x1C + ((*reinterpret_cast<int*>(history + 0x18) - 1) % 20) * 0x84;
                // Sometimes what was sent is the entry as it stands: nothing changed.
                if (round % 4 == 1) {
                    uint8_t* sent = g_uw.sent;
                    sent[0x148 + who] = entry[0x70];
                    sent[0x150 + who] = entry[0x74];
                    sent[0x90 + who] = entry[0x7C];
                    sent[0x98 + who] = entry[0x75];
                    sent[0xA0 + who] = entry[0x76];
                    if (Random(3) == 0) // (only the last of the state's fields differs)
                        sent[0xA0 + who] ^= 1;
                    if (Random(2))
                        *reinterpret_cast<uint32_t*>(sent + 0x108 + 4 * who) = *reinterpret_cast<uint32_t*>(entry + 0x68);
                    *reinterpret_cast<uint32_t*>(sent + 0x128 + 4 * who) = *reinterpret_cast<uint32_t*>(entry + 0x6C);
                    *reinterpret_cast<uint32_t*>(sent + 0x160 + 4 * who) = *reinterpret_cast<uint32_t*>(entry + 0x80);
                }
                uint32_t spread[] = {0, 1, 59, 60, 61, 500};
                uint32_t delta = spread[Random(6)];
                frame = static_cast<int>(1000 + Random(100));
                forced[who] = Random(2) ? static_cast<uint32_t>(frame) - delta : static_cast<uint32_t>(frame) + delta;
                uint32_t flags = flagsOf(g_uw.sent, entry, static_cast<uint8_t>(who));
                Record({0xB3, flags, forced[who]});
                // A client's stream: a few skaters written after each other.
                uint8_t buffer[0x400] = {};
                Mdl::SkateNet::RelayStream out;
                std::memset(&out, 0, sizeof(out));
                out.m_stream.SetOutputData(buffer, sizeof(buffer));
                out.m_sent = g_uw.sent;
                for (int n = 0; n < 3; ++n) {
                    int other = static_cast<int>(Random(8) + static_cast<uint32_t>(n)) % 8;
                    if (*reinterpret_cast<int*>(g_uw.histories[other] + 0x18) <= 0)
                        *reinterpret_cast<int*>(g_uw.histories[other] + 0x18) = 1;
                    frame += static_cast<int>(Random(70));
                    write(g_uw.skaters[other], &out);
                }
                out.m_stream.Flush();
                Record({0xB4, static_cast<uint32_t>(out.m_stream.GetByteCount()), HashBytes(buffer, sizeof(buffer)),
                        HashBytes(g_uw.sent, sizeof(g_uw.sent))});
            }
            ThugproTests::EndScale();
            delete s;
            traces[run] = g_trace;
        }
        char what[96];
        std::snprintf(what, sizeof(what), "server's object update relay 0x005696C0 / 0x005698F0%s", ScaleName(mode));
        Compare(traces[0], traces[1], what);
    }
    RestoreAll(saved);
}

// 0x00512EA0 / 0x005131A0 a local skater's update to the server: on the server or a client, on the 15th frames and
// between (the send interval gone by or not), the skater's position, orientation, state, controls and board values
// changed or not; each scale.
struct LocalWorld {
    uint8_t component[0x48];
    uint8_t object[0x300];
    uint8_t core[0x120];
    uint8_t board[0x230];
    uint8_t boardValue[0x50];
    uint8_t component6C[0x20];
    uint8_t netManager[0x20];
    uint8_t client[0x1F18];
    uint8_t conn[0x80];
};
LocalWorld g_lw;
float g_lwAngles[3];

void TestLocalNetLogic()
{
    std::vector<Saved> saved = {{0x007CCE14, 4, {}}, {0x007CCDD0, 0x14, {}}};
    SaveAll(saved);
    for (int mode = 0; mode < ThugproTests::kScaleModes; ++mode) {
        std::vector<uint32_t> traces[2];
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            auto* s = new StandIns();
            s->At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
                Record({0xC0, r.args[0], r.ecx == reinterpret_cast<uint32_t>(g_lw.object) ? 1u : 0u});
                return reinterpret_cast<uint32_t>(g_lw.component6C);
            });
            s->At({0x00404AB0, AddressOf(&Mth::Matrix::GetEulerAngles)}, 4, [](Regs& r) {
                Record({0xC1, r.ecx == reinterpret_cast<uint32_t>(g_lw.object + 0xC0) ? 1u : 0u});
                auto* out = reinterpret_cast<float*>(r.args[0]);
                out[0] = g_lwAngles[0];
                out[1] = g_lwAngles[1];
                out[2] = g_lwAngles[2];
                out[3] = 1.0f;
                return r.args[0];
            });
            s->At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [](Regs& r) {
                auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
                Record({0xC2, r.ecx == reinterpret_cast<uint32_t>(g_lw.client) ? 1u : 0u, search[0], search[1]});
                return reinterpret_cast<uint32_t>(g_lw.conn);
            });
            s->At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
                auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
                Record({0xC3, r.ecx == reinterpret_cast<uint32_t>(g_lw.client) ? 1u : 0u, desc->m_id, desc->m_stream,
                        desc->m_length, static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue),
                        desc->m_groupId, desc->m_singular ? 1u : 0u, static_cast<uint32_t>(desc->m_delay),
                        desc->m_forcedSequenceId, desc->m_compression,
                        HashBytes(static_cast<const uint8_t*>(desc->m_data), desc->m_length)});
                return 0u;
            });
            ThugproTests::UseScale(original, mode);
            AppMainTests::Originals guards(original, {0x00401050, 0x00401090, 0x004010D0, 0x00401180, 0x004010C0,
                                                      0x004549F0, 0x00455970, 0x00512EA0});
            g_rand = 9000 + static_cast<uint32_t>(mode);
            std::memset(&g_lw, 0, sizeof(g_lw));
            g_lwAngles[0] = g_lwAngles[1] = g_lwAngles[2] = 0.0f;
            GameVariable<uint8_t*>(0x007CCE14) = g_lw.netManager;
            std::memset(reinterpret_cast<void*>(0x007CCDD0), 0, 0x14);
            auto* component = reinterpret_cast<Obj::CSkaterLocalNetLogicComponent*>(g_lw.component);
            *reinterpret_cast<uint8_t**>(g_lw.component + 0x0C) = g_lw.object;
            component->m_core = g_lw.core;
            component->m_board = g_lw.board;
            *reinterpret_cast<uint8_t**>(g_lw.netManager + 0x8) = g_lw.client;
            *reinterpret_cast<uint8_t**>(g_lw.netManager + 0xC) = g_lw.client;
            using Flags = uint32_t(__thiscall*)(void*);
            using Send = void(__thiscall*)(void*);
            auto* flagsOf = reinterpret_cast<Flags>(AppMainTests::Code(original, 0x00512EA0));
            auto* send = reinterpret_cast<Send>(AppMainTests::Code(original, 0x005131A0));
            for (int round = 0; round < 300; ++round) {
                *reinterpret_cast<uint32_t*>(g_lw.netManager) = Random(4) == 0 ? 0xF1u : 0xF0u;
                *reinterpret_cast<int*>(g_lw.object + 0x2BC) = static_cast<int>(Random(2));
                *reinterpret_cast<int*>(g_lw.client + 0x1410) = static_cast<int>(Random(3) == 0 ? 15 * Random(9) : Random(500));
                *reinterpret_cast<uint32_t*>(g_lw.client + 0x8) = 5000 + Random(2000);
                *reinterpret_cast<uint32_t*>(g_lw.conn + 0x74) = Random(200);
                if (Random(2))
                    component->m_lastSendTime = 5000 + Random(2000);
                if (round % 5 == 2) // (exactly the send interval since the last)
                    component->m_lastSendTime = *reinterpret_cast<uint32_t*>(g_lw.client + 0x8) -
                                                *reinterpret_cast<uint32_t*>(g_lw.conn + 0x74);
                if (round % 7 == 3) // (only the last of the state's fields)
                    g_lw.core[0x118] ^= 1;
                // What changes: a little of everything, often nothing.
                bool still = Random(3) == 0;
                if (!still) {
                    for (int k = 0; k < 3; ++k) {
                        if (Random(2))
                            reinterpret_cast<float*>(g_lw.object + 0x4C)[k] = RandomCoordinate(k == 1);
                        if (Random(2))
                            g_lwAngles[k] = (static_cast<float>(Random(20001)) / 10000.0f - 1.0f) * 3.2f;
                    }
                    if (Random(2)) {
                        g_lw.core[0x18] = static_cast<uint8_t>(Random(8));
                        g_lw.core[0xE0] = static_cast<uint8_t>(Random(3) == 0 ? 0x81 : Random(2));
                        g_lw.core[0xE1] = static_cast<uint8_t>(Random(5) == 0 ? 0xC0 + Random(64) : Random(64));
                        *reinterpret_cast<int*>(g_lw.core + 0x1C) = static_cast<int>(Random(3));
                        g_lw.core[0x118] = static_cast<uint8_t>(Random(2));
                    }
                    if (Random(2)) {
                        for (int k = 0; k < 24; ++k)
                            g_lw.core[0x20 + 8 * k] = static_cast<uint8_t>(Random(3) == 0 ? Random(256) : 0);
                        g_lw.core[0x21] = 0x5A; // (bytes between the controls aren't read)
                    }
                    if (Random(2))
                        *reinterpret_cast<uint32_t*>(g_lw.component6C + 0x18) = Random(4) ? Random(8) : RandomBits(32);
                    if (Random(2)) {
                        *reinterpret_cast<uint8_t**>(g_lw.board + 0x22C) = Random(4) ? g_lw.boardValue : nullptr;
                        *reinterpret_cast<uint32_t*>(g_lw.boardValue + 0x4C) = RandomBits(32);
                    }
                }
                if (round % 3 == 0)
                    Record({0xC4, flagsOf(g_lw.component)});
                send(g_lw.component);
                Record({0xC5});
                for (int k = 0x18; k < 0x48; k += 4)
                    Record({*reinterpret_cast<uint32_t*>(g_lw.component + k)});
                for (int k = 0; k < 5; ++k)
                    Record({reinterpret_cast<uint32_t*>(0x007CCDD0)[k]});
            }
            ThugproTests::EndScale();
            delete s;
            traces[run] = g_trace;
        }
        char what[96];
        std::snprintf(what, sizeof(what), "local skater's update 0x00512EA0 / 0x005131A0%s", ScaleName(mode));
        Compare(traces[0], traces[1], what);
    }
    RestoreAll(saved);
}

// --- the received-message guards (TP-R8, game/net/net_guards.h: ours only, nothing original to compare) ------------

constexpr uint32_t kBadAnim = 0xBAD00001, kBadDetails = 0xBAD00002, kBadMode = 0xBAD00003, kBadLevel = 0xBAD00004;
std::vector<uint32_t> g_guardCalls;
int g_handlerRuns;
int g_leaveResult = 3;

const GameNet::Guards::Lookups kStandInLookups = {
    [](uint32_t animation) {
        g_guardCalls.push_back(0xA0000000 | (animation & 0xFFFF));
        return animation != kBadAnim;
    },
    [](uint32_t name) {
        g_guardCalls.push_back(0xB0000000 | (name & 0xFFFF));
        return name != kBadDetails;
    },
    [](uint32_t mode) {
        g_guardCalls.push_back(0xC0000000 | (mode & 0xFFFF));
        return mode != kBadMode;
    },
    [](uint32_t name) {
        g_guardCalls.push_back(0xD0000000 | (name & 0xFFFF));
        return name != kBadLevel;
    },
    [](Net::MsgHandlerContext* context) {
        g_guardCalls.push_back(0xE0000000 | context->m_MsgId);
        return g_leaveResult;
    },
};

int __cdecl CountingHandler(Net::MsgHandlerContext*)
{
    ++g_handlerRuns;
    return 1;
}

// Each guarded message with what it names known and not, too short, and other messages left alone; then through
// Net::Dispatcher::DispatchMsg (a stand-in dispatcher with one handler per message): a dropped message runs no
// handler, a changed one runs them with the change.
void TestNetGuards()
{
    using GameNet::Guards::CheckReceivedMessage;
    GameNet::Guards::SetLookups(kStandInLookups);
    struct Case {
        const char* what;
        uint8_t id;
        uint32_t length;
        std::vector<std::pair<uint32_t, uint32_t>> words; // offset -> value written first
        int result;
        std::vector<uint32_t> calls;
        std::vector<std::pair<uint32_t, uint32_t>> after; // offset -> value expected after
    };
    const std::vector<Case> cases = {
        {"anim loaded", 0x29, 14, {{2, 0x12345678}}, -1, {0xA0005678}, {{2, 0x12345678}}},
        {"anim missing", 0x29, 14, {{2, kBadAnim}}, -1, {0xA0000001}, {{2, 0x1CA1FF20}}},
        {"anim short", 0x29, 5, {{2, kBadAnim}}, -1, {}, {{2, kBadAnim}}},
        {"anim exactly long enough", 0x29, 6, {{2, kBadAnim}}, -1, {0xA0000001}, {{2, 0x1CA1FF20}}},
        {"special item", 0x77, 10, {{0, 0x0000AAAA}}, -1, {0xB000AAAA}, {}},
        {"special item missing", 0x77, 10, {{0, kBadDetails}}, 1, {0xB0000002}, {{0, kBadDetails}}},
        {"special item destroyed missing", 0x78, 4, {{0, kBadDetails}}, 1, {0xB0000002}, {}},
        {"special item short", 0x78, 3, {{0, kBadDetails}}, -1, {}, {}},
        {"start info", 0x33, 0x50, {{0xC, 0x00001111}, {0x10, 0x00002222}}, -1, {0xD0002222, 0xC0001111},
         {{0xC, 0x00001111}, {0x10, 0x00002222}}},
        {"start info mode missing", 0x33, 0x50, {{0xC, kBadMode}, {0x10, 0x00002222}}, -1, {0xD0002222, 0xC0000003},
         {{0xC, 0x1C471C60}, {0x10, 0x00002222}}},
        {"start info level missing", 0x33, 0x50, {{0xC, kBadMode}, {0x10, kBadLevel}}, 3, {0xD0000004, 0xE0000033},
         {{0xC, kBadMode}}},
        {"start info short", 0x33, 0x13, {{0xC, kBadMode}, {0x10, kBadLevel}}, -1, {}, {{0xC, kBadMode}}},
        {"game info", 0x34, 0x30, {{0, 0x00003333}}, -1, {0xC0003333}, {{0, 0x00003333}}},
        {"game info mode missing", 0x34, 0x30, {{0, kBadMode}, {4, 0x77777777}}, -1, {0xC0000003},
         {{0, 0x1C471C60}, {4, 0x77777777}}},
        {"change level", 0x46, 8, {{0, 0x00004444}}, -1, {0xD0004444}, {{0, 0x00004444}}},
        {"change level missing", 0x46, 8, {{0, kBadLevel}}, 3, {0xD0000004, 0xE0000046}, {}},
        {"change level short", 0x46, 3, {{0, kBadLevel}}, -1, {}, {}},
        {"another message", 0x20, 0, {{0, kBadLevel}}, -1, {}, {{0, kBadLevel}}},
        {"another message (0x23)", 0x23, 20, {{2, kBadAnim}}, -1, {}, {{2, kBadAnim}}},
    };
    for (const Case& c : cases) {
        alignas(4) uint8_t msg[0x60];
        std::memset(msg, 0x5C, sizeof(msg));
        for (auto [offset, value] : c.words)
            std::memcpy(msg + offset, &value, 4);
        uint8_t before[sizeof(msg)];
        std::memcpy(before, msg, sizeof(msg));
        Net::MsgHandlerContext context;
        std::memset(&context, 0, sizeof(context));
        context.m_Msg = reinterpret_cast<char*>(msg);
        context.m_MsgId = c.id;
        context.m_MsgLength = c.length;
        g_guardCalls.clear();
        int result = CheckReceivedMessage(&context);
        bool ok = result == c.result && g_guardCalls == c.calls && context.m_MsgId == c.id &&
                  context.m_MsgLength == c.length;
        // Only the words named in `after` may change.
        for (auto [offset, value] : c.after) {
            uint32_t now;
            std::memcpy(&now, msg + offset, 4);
            ok = ok && now == value;
            std::memcpy(before + offset, msg + offset, 4);
        }
        ok = ok && std::memcmp(before, msg, sizeof(msg)) == 0;
        char what[96];
        std::snprintf(what, sizeof(what), "network guards: %s", c.what);
        Check(ok, what);
    }
    // A null message.
    {
        Net::MsgHandlerContext context;
        std::memset(&context, 0, sizeof(context));
        context.m_MsgId = 0x29;
        context.m_MsgLength = 14;
        g_guardCalls.clear();
        Check(CheckReceivedMessage(&context) == -1 && g_guardCalls.empty(), "network guards: no message");
    }
    // Through the dispatcher.
    {
        auto* dispatcher = static_cast<Net::Dispatcher*>(::operator new(sizeof(Net::Dispatcher)));
        std::memset(static_cast<void*>(dispatcher), 0, sizeof(Net::Dispatcher));
        Net::MsgHandler handlers[3] = {};
        int index = 0;
        for (int id : {0x29, 0x77, 0x46}) {
            Mem::ListNode& head = dispatcher->List(id).m_node;
            Net::MsgHandler& handler = handlers[index++];
            head.data = reinterpret_cast<void*>(-1);
            head.next = head.prev = &handler.m_node;
            handler.m_node.data = &handler;
            handler.m_node.next = handler.m_node.prev = &head;
            handler.m_Code = CountingHandler;
        }
        auto dispatch = [&](uint8_t id, uint32_t offset, uint32_t value, uint32_t* after) {
            alignas(4) uint8_t msg[0x20] = {};
            std::memcpy(msg + offset, &value, 4);
            Net::MsgHandlerContext context;
            std::memset(&context, 0, sizeof(context));
            context.m_Msg = reinterpret_cast<char*>(msg);
            context.m_MsgId = id;
            context.m_MsgLength = 0x20;
            g_handlerRuns = 0;
            int result = dispatcher->DispatchMsg(&context);
            std::memcpy(after, msg + offset, 4);
            return result * 100 + g_handlerRuns;
        };
        uint32_t after = 0;
        Check(dispatch(0x29, 2, kBadAnim, &after) == 101 && after == 0x1CA1FF20,
              "network guards: the dispatcher runs the handlers with the animation changed");
        Check(dispatch(0x77, 0, kBadDetails, &after) == 100, "network guards: the dispatcher drops a special item");
        Check(dispatch(0x77, 0, 0x1234, &after) == 101, "network guards: the dispatcher runs a known special item");
        Check(dispatch(0x46, 0, kBadLevel, &after) == 300, "network guards: the dispatcher leaves on a missing level");
        g_leaveResult = 0; // (a handler error: the dispatch stops there too)
        Check(dispatch(0x46, 0, kBadLevel, &after) == 0, "network guards: the dispatcher stops on the leave's error");
        g_leaveResult = 3;
        ::operator delete(dispatcher);
    }
    GameNet::Guards::SetLookups(GameNet::Guards::GameLookups());
}

// --- the appearance guard (TP-R9, game/skate/appearance_guard.h: ours only) ------------------------------------------

template <typename R, typename... A>
using StructMethod = R (Script::CStruct::*)(A...);

// A structure built by hand: components in a fixed block (as the script pools' would be).
struct HandStruct {
    alignas(16) uint8_t storage[8] = {};
    Script::CComponent components[12] = {};
    int count = 0;
    Script::CStruct* Struct() { return reinterpret_cast<Script::CStruct*>(storage); }
    HandStruct& Add(Script::ESymbolType type, uint32_t name, uint32_t raw)
    {
        Script::CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.raw = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<Script::CComponent**>(storage)[1] = &c;
        ++count;
        return *this;
    }
    HandStruct& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
};

// A part list: an array of structures, each {desc_id = ...}.
struct HandList {
    Script::CArray array = {};
    HandStruct parts[4];
    uint32_t elements[4] = {};
    HandList(std::initializer_list<uint32_t> descs, Script::ESymbolType type = Script::ESYMBOLTYPE_STRUCTURE)
    {
        uint32_t i = 0;
        for (uint32_t desc : descs) {
            parts[i].Checksum(0x4BB2084E, desc);
            elements[i] = reinterpret_cast<uint32_t>(parts[i].Struct());
            ++i;
        }
        array.m_type = type;
        array.m_size = i;
        if (i == 1)
            array.m_single = elements[0];
        else
            array.m_elements = elements;
    }
};

constexpr uint32_t kDescId = 0x4BB2084E, kNoneDesc = 0x806FFF30, kBodySlot = 0x2457F44D;
HandList* g_lists[8];
uint32_t g_listSlots[8];
HandStruct* g_defaults;
std::vector<uint32_t> g_appearanceCalls;

void StandInAppearance(StandIns& s)
{
    s.At({0x00477D40, AddressOf(static_cast<StructMethod<void, uint32_t, uint32_t>>(&Script::CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             g_appearanceCalls.insert(g_appearanceCalls.end(), {0xAC, r.ecx, r.args[0], r.args[1]});
             // (applied: the component replaced in place)
             auto* components = reinterpret_cast<Script::CComponent**>(r.ecx)[1];
             for (Script::CComponent* c = components; c; c = c->next) {
                 if (c->name == r.args[0])
                     c->raw = r.args[1];
             }
             return 0u;
         });
    s.At({0x004776B0, AddressOf(static_cast<StructMethod<void, uint32_t>>(&Script::CStruct::RemoveComponent))}, 4,
         [](Regs& r) {
             g_appearanceCalls.insert(g_appearanceCalls.end(), {0xAD, r.ecx, r.args[0]});
             return 0u;
         });
    s.At({0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        g_appearanceCalls.insert(g_appearanceCalls.end(), {0xAE, r.ecx});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&Script::CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        g_appearanceCalls.insert(g_appearanceCalls.end(), {0xAF, r.ecx, r.args[0]});
        return 0u;
    });
}

const Obj::AppearanceGuard::Catalog kStandInCatalog = {
    [](uint32_t slot) -> Script::CArray* {
        for (int i = 0; i < 8; ++i) {
            if (g_lists[i] && g_listSlots[i] == slot)
                return &g_lists[i]->array;
        }
        return nullptr;
    },
    []() { return g_defaults ? g_defaults->Struct() : nullptr; },
};

// Slots known and not, parts in their lists and not (falling back to None, the default skater's, the list's first),
// lists of the wrong type or empty, structures without a desc_id and other components left alone, a body we haven't,
// no defaults at all.
void TestAppearanceGuard()
{
    using Obj::AppearanceGuard::Sanitise;
    auto* s = new StandIns();
    StandInAppearance(*s);
    Obj::AppearanceGuard::SetCatalog(kStandInCatalog);
    // (None not first in its list; a list of names, not structures; an empty list whose stale block still holds a part)
    HandList a({0x11, 0x12}), b({0x21, kNoneDesc, 0x22}), c({0x31, 0x32, 0x33}), d({0x41}), h({0x81, 0x82}),
        ints({0x91, 0x92}, Script::ESYMBOLTYPE_NAME), empty({0x95}), body({0x28FA311F, 0x645F36DC});
    empty.array.m_size = 0;
    empty.array.m_elements = empty.elements;
    HandList* lists[] = {&a, &b, &c, &d, &h, &ints, &empty, &body};
    const uint32_t slots[] = {0x1001, 0x1002, 0x1003, 0x1004, 0x1008, 0x1009, 0x100A, kBodySlot};
    for (int i = 0; i < 8; ++i) {
        g_lists[i] = lists[i];
        g_listSlots[i] = slots[i];
    }
    HandStruct defaultsParts[3];
    defaultsParts[0].Checksum(kDescId, 0x32);  // C: in its list
    defaultsParts[1].Checksum(kDescId, 0x8F);  // H: not in its list (-> the first)
    defaultsParts[2].Checksum(kDescId, kNoneDesc + 1);
    HandStruct defaults;
    defaults.Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1003, reinterpret_cast<uint32_t>(defaultsParts[0].Struct()))
        .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1008, reinterpret_cast<uint32_t>(defaultsParts[1].Struct()))
        .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1002, reinterpret_cast<uint32_t>(defaultsParts[2].Struct()));
    g_defaults = &defaults;

    // A mixed appearance.
    {
        HandStruct parts[10];
        parts[0].Checksum(kDescId, 0x12);                          // A: fine
        parts[1].Checksum(kDescId, 0x99).Checksum(0x6E9E1C2B, 7);  // B: -> None (the other component kept)
        parts[2].Checksum(kDescId, 0x98);                          // C: -> the default's 0x32
        parts[3].Checksum(kDescId, 0x97);                          // D: no default -> the first, 0x41
        parts[4].Checksum(kDescId, 0x55);                          // E: no list -> taken out
        parts[5].Add(Script::ESYMBOLTYPE_INTEGER, 0x6E9E1C2B, 3);  // F: no desc_id -> left alone
        parts[6].Checksum(kDescId, 0x96);                          // H: the default's isn't in the list -> 0x81
        parts[7].Checksum(kDescId, 0x91);                          // an integer list -> taken out
        parts[8].Checksum(kDescId, 0x95);                          // an empty list -> taken out
        parts[9].Checksum(kDescId, 0x28FA311F);                    // body: fine
        HandStruct appearance;
        appearance.Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1001, reinterpret_cast<uint32_t>(parts[0].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1002, reinterpret_cast<uint32_t>(parts[1].Struct()))
            .Add(Script::ESYMBOLTYPE_INTEGER, 0x1002, 5)
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1003, reinterpret_cast<uint32_t>(parts[2].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1004, reinterpret_cast<uint32_t>(parts[3].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1005, reinterpret_cast<uint32_t>(parts[4].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1006, reinterpret_cast<uint32_t>(parts[5].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTUREPOINTER, 0x1007, reinterpret_cast<uint32_t>(parts[4].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1008, reinterpret_cast<uint32_t>(parts[6].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1009, reinterpret_cast<uint32_t>(parts[7].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x100A, reinterpret_cast<uint32_t>(parts[8].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, kBodySlot, reinterpret_cast<uint32_t>(parts[9].Struct()));
        g_appearanceCalls.clear();
        int changed = Sanitise(appearance.Struct());
        auto at = [](HandStruct& p) { return reinterpret_cast<uint32_t>(p.Struct()); };
        uint32_t me = reinterpret_cast<uint32_t>(appearance.Struct());
        std::vector<uint32_t> expected = {0xAC, at(parts[1]), kDescId, kNoneDesc, 0xAC, at(parts[2]), kDescId, 0x32,
                                          0xAC, at(parts[3]), kDescId, 0x41,      0xAC, at(parts[6]), kDescId, 0x81,
                                          0xAD, me,           0x1005,  0xAD,      me,   0x1009,       0xAD,    me,
                                          0x100A};
        Check(changed == 7 && g_appearanceCalls == expected && parts[1].components[1].raw == 7,
              "appearance guard: a mixed appearance");
    }
    // Every part fine: nothing done.
    {
        HandStruct part, appearance;
        part.Checksum(kDescId, 0x11);
        appearance.Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1001, reinterpret_cast<uint32_t>(part.Struct()));
        g_appearanceCalls.clear();
        Check(Sanitise(appearance.Struct()) == 0 && g_appearanceCalls.empty(), "appearance guard: a fine appearance");
        Check(Sanitise(nullptr) == 0 && g_appearanceCalls.empty(), "appearance guard: no appearance");
    }
    // A body we haven't: the default skater instead (what came before it isn't changed).
    {
        HandStruct parts[2], appearance;
        parts[0].Checksum(kDescId, 0x98);
        parts[1].Checksum(kDescId, 0xB0D1);
        appearance.Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1003, reinterpret_cast<uint32_t>(parts[0].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, kBodySlot, reinterpret_cast<uint32_t>(parts[1].Struct()))
            .Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1005, reinterpret_cast<uint32_t>(parts[0].Struct()));
        g_appearanceCalls.clear();
        uint32_t me = reinterpret_cast<uint32_t>(appearance.Struct());
        std::vector<uint32_t> expected = {0xAE, me, 0xAF, me, reinterpret_cast<uint32_t>(defaults.Struct())};
        Check(Sanitise(appearance.Struct()) == 1 && g_appearanceCalls == expected,
              "appearance guard: a body we haven't");
    }
    // No defaults: a part falls back to the list's first.
    {
        g_defaults = nullptr;
        HandStruct part, appearance;
        part.Checksum(kDescId, 0x98);
        appearance.Add(Script::ESYMBOLTYPE_STRUCTURE, 0x1003, reinterpret_cast<uint32_t>(part.Struct()));
        g_appearanceCalls.clear();
        std::vector<uint32_t> expected = {0xAC, reinterpret_cast<uint32_t>(part.Struct()), kDescId, 0x31};
        Check(Sanitise(appearance.Struct()) == 1 && g_appearanceCalls == expected, "appearance guard: no defaults");
    }
    for (HandList*& list : g_lists)
        list = nullptr;
    Obj::AppearanceGuard::SetCatalog(Obj::AppearanceGuard::GameCatalog());
    delete s;
}

// --- THUGPRO's parts from its install (TP-R10, game/skate/thugpro_cas.h: ours only) ---------------------------------

std::vector<std::string> g_foundAsked;
bool StandInFound(const char* path)
{
    g_foundAsked.push_back(path);
    return !std::strstr(path, "missing");
}

// The archive reader (stored and compressed files, names as stored, a file not asked for, a cut-off archive), the
// LZSS decoder against the game's encoder, the .qb walk (arrays, structures, scripts, name tables, line ends
// before '='), path keys, and the choice of entries (ours already, twice, without desc_id, every path found or not -
// nested structures and arrays too).
void TestThugproCas()
{
    using namespace Obj::ThugproCas;
    // LZSS (a stream made by hand: literals, a reference to what was just written - overlapping its own output -, one
    // into the window's initial spaces).
    const std::vector<uint8_t> packed = {0xF7, 'a', 'b', 'c', 0xEE, 0xF3, 'x', 'y', 'z', 'w', 0x00, 0x00, 0x00};
    const std::string text = "abcabcabcxyzw   ";
    const std::vector<uint8_t> payload(text.begin(), text.end());
    Check(DecodeLzss(packed.data(), packed.size(), payload.size()) == payload, "THUGPRO parts: LZSS");
    Check(DecodeLzss(packed.data(), packed.size(), 7) == std::vector<uint8_t>(payload.begin(), payload.begin() + 7) &&
              DecodeLzss(packed.data(), 5, payload.size()).size() == 3,
          "THUGPRO parts: LZSS cut off");
    // The archive.
    std::vector<uint8_t> archive(12, 0);
    auto put32 = [&archive](uint32_t value) {
        for (int i = 0; i < 4; ++i)
            archive.push_back(static_cast<uint8_t>(value >> (8 * i)));
    };
    auto entry = [&](const char* name, const std::vector<uint8_t>& data, uint32_t size, bool compressed) {
        put32(size);
        put32(compressed ? static_cast<uint32_t>(data.size()) : 0);
        uint16_t nameSize = static_cast<uint16_t>((std::strlen(name) + 4) & ~3u);
        archive.push_back(static_cast<uint8_t>(nameSize));
        archive.push_back(static_cast<uint8_t>(nameSize >> 8));
        archive.push_back(1);
        archive.push_back(0);
        put32(0x12345678);
        size_t at = archive.size();
        archive.resize(at + nameSize, 0);
        std::memcpy(archive.data() + at, name, std::strlen(name));
        archive.insert(archive.end(), data.begin(), data.end());
        while (archive.size() % 4)
            archive.push_back(0xEE);
    };
    entry("qb\\game\\decks.qb", {'a', 'b', 'c', 'd', 'e'}, 5, false);
    entry("qb\\other.qb", {'x', 'y', 'z'}, 3, false);
    entry("QB/Thugpro/THUGPRO_boards.qb", packed, static_cast<uint32_t>(payload.size()), true);
    entry("qb\\game\\cas_logos.qb", packed, static_cast<uint32_t>(payload.size()) + 5, true); // (decodes short)
    archive[8] = 4;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    bool read = ReadArchive(archive,
                            {"qb\\game\\decks.qb", "qb\\thugpro\\thugpro_boards.qb", "qb\\missing.qb",
                             "qb\\game\\cas_logos.qb"},
                            &files);
    Check(read && files.size() == 2 && files[0].first == "qb\\game\\decks.qb" &&
              files[0].second == std::vector<uint8_t>{'a', 'b', 'c', 'd', 'e'} &&
              files[1].first == "qb\\thugpro\\thugpro_boards.qb" && files[1].second == payload,
          "THUGPRO parts: the archive");
    std::vector<uint8_t> cut(archive.begin(), archive.begin() + 40);
    files.clear();
    Check(!ReadArchive(cut, {"qb\\other.qb"}, &files) && !ReadArchive({1, 2}, {}, &files),
          "THUGPRO parts: a cut-off archive");
    // (cut in the first file's data: not read past the end)
    std::vector<uint8_t> cutData(archive.begin(), archive.begin() + 12 + 16 + 20 + 2);
    files.clear();
    Check(!ReadArchive(cutData, {"qb\\game\\decks.qb"}, &files) && files.empty(),
          "THUGPRO parts: an archive cut in a file's data");
    // The .qb walk.
    std::vector<uint8_t> qb;
    auto bytes = [&qb](std::initializer_list<uint8_t> list) { qb.insert(qb.end(), list); };
    auto name = [&qb](uint32_t value) {
        for (int i = 0; i < 4; ++i)
            qb.push_back(static_cast<uint8_t>(value >> (8 * i)));
    };
    bytes({0x01, 0x16});
    name(0x11111111);
    bytes({0x07});
    size_t first = qb.size();
    bytes({0x05, 0x17, 1, 0, 0, 0, 0x03, 0x16});
    name(0xAAAA);
    bytes({0x07, 0x17, 2, 0, 0, 0, 0x04, 0x06, 0x01, 0x16});
    name(0x22222222);
    bytes({0x07, 0x03, 0x16});
    name(0xBBBB);
    bytes({0x04, 0x02, 9, 0, 0, 0, 0x16});
    name(0x66666666);
    bytes({0x17, 1, 0, 0, 0, 0x01, 0x23, 0x16});
    name(0x33333333);
    bytes({0x01, 0x16});
    name(0xCCCC);
    bytes({0x01, 0x16});
    name(0x77777777);
    bytes({0x07, 0x05, 0x06, 0x01, 0x24, 0x01, 0x2B});
    name(0xDDDD);
    bytes({'n', 'a', 'm', 'e', 0, 0x16});
    name(0x44444444);
    bytes({0x01, 0x07, 0x01});
    size_t second = qb.size();
    bytes({0x05, 0x06, 0x01, 0x16});
    name(0x55555555);
    bytes({0x07, 0x17, 5, 0, 0, 0, 0x01, 0x00, 0x00, 0x00});
    auto arrays = TopLevelArrays(qb.data(), qb.size());
    // (cut short: only what comes before the end)
    auto early = TopLevelArrays(qb.data(), first + 1);
    Check(arrays.size() == 2 && arrays[0].first == 0x11111111 && arrays[0].second == qb.data() + first &&
              arrays[1].first == 0x44444444 && arrays[1].second == qb.data() + second,
          "THUGPRO parts: the .qb's arrays");
    Check(early.size() == 1 && early[0].first == 0x11111111, "THUGPRO parts: the .qb's arrays up to its size");
    // Path keys.
    Check(PathKey("Models/Skater_Secret/Skater_eddie/Skater_eddie.skin") ==
                  "models\\skater_secret\\skater_eddie\\skater_eddie" &&
              PathKey("models\\skater_secret\\skater_eddie\\skater_eddie.skin.xbx") ==
                  "models\\skater_secret\\skater_eddie\\skater_eddie" &&
              PathKey("data/textures/X/CS_A.png") == "textures\\x\\cs_a" &&
              PathKey("textures\\x\\cs_a.img.xbx") == "textures\\x\\cs_a" && PathKey("a.b.c") == "a.b.c" &&
              PathKey("m\\a.skin.tex") == "m\\a",
          "THUGPRO parts: path keys");
    // The choice.
    HandList ours({0x11, 0x12});
    HandStruct parts[8], nested[2];
    Script::CArray strings = {};
    const char* stringElements[2] = {"textures/fine/a", "textures/missing/b"};
    strings.m_type = Script::ESYMBOLTYPE_STRING;
    strings.m_size = 2;
    strings.m_elements = reinterpret_cast<uint32_t*>(stringElements);
    parts[0].Checksum(kDescId, 0x12);                                                                   // ours
    parts[1].Checksum(kDescId, 0x13).Add(Script::ESYMBOLTYPE_STRING, 0x1, reinterpret_cast<uint32_t>("models/a/b.skin"))
        .Add(Script::ESYMBOLTYPE_STRING, 0x2, reinterpret_cast<uint32_t>("not a path"));                // taken
    parts[2].Checksum(kDescId, 0x14).Add(Script::ESYMBOLTYPE_STRING, 0x1, reinterpret_cast<uint32_t>("models/missing.skin"))
        .Add(Script::ESYMBOLTYPE_STRING, 0x6, reinterpret_cast<uint32_t>("models/never/asked.skin"));
    parts[3].Checksum(kDescId, 0x15);                                                                   // taken
    parts[4].Checksum(kDescId, 0x13);                                                                   // twice
    parts[5].Add(Script::ESYMBOLTYPE_STRING, 0x1, reinterpret_cast<uint32_t>("models/c/d.skin"));       // no desc_id
    nested[0].Add(Script::ESYMBOLTYPE_STRING, 0x3, reinterpret_cast<uint32_t>("textures\\e\\f"));
    parts[6].Checksum(kDescId, 0x16).Add(Script::ESYMBOLTYPE_STRUCTURE, 0x4, reinterpret_cast<uint32_t>(nested[0].Struct()));
    parts[7].Checksum(kDescId, 0x17).Add(Script::ESYMBOLTYPE_ARRAY, 0x5, reinterpret_cast<uint32_t>(&strings));
    uint32_t theirElements[8];
    for (int i = 0; i < 8; ++i)
        theirElements[i] = reinterpret_cast<uint32_t>(parts[i].Struct());
    Script::CArray theirs = {};
    theirs.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    theirs.m_size = 8;
    theirs.m_elements = theirElements;
    g_foundAsked.clear();
    std::vector<uint32_t> picked = PickNew(&ours.array, &theirs, StandInFound);
    std::vector<std::string> asked = {"models/a/b.skin", "models/missing.skin", "textures\\e\\f", "textures/fine/a",
                                      "textures/missing/b"};
    Check(picked == std::vector<uint32_t>{1, 3, 6} && g_foundAsked == asked, "THUGPRO parts: the entries taken");
    theirs.m_type = Script::ESYMBOLTYPE_NAME;
    Check(PickNew(&ours.array, &theirs, StandInFound).empty() && PickNew(nullptr, &theirs, StandInFound).empty(),
          "THUGPRO parts: not a list of structures");
    // Nothing is served before a load.
    char path[260];
    Check(!FilePath("models\\a\\b.skin.xbx", path), "THUGPRO parts: nothing served before the load");
}

// The user's THUGPRO install, when there is one (read only; skipped without): its CAS files read and walked as the
// game will - every file found, decompressed to its stored size, and the lists the parts go into among the arrays.
void TestThugproCasInstall()
{
    char local[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local)))
        return;
    std::string file = std::string(local) + "\\THUG Pro\\data\\pre\\thugpro_qb.prx";
    HANDLE handle = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        Log("THUGPRO parts: no THUGPRO install (%s) - its test skipped", file.c_str());
        return;
    }
    std::vector<uint8_t> archive(GetFileSize(handle, nullptr));
    DWORD read = 0;
    BOOL ok = ReadFile(handle, archive.data(), static_cast<DWORD>(archive.size()), &read, nullptr);
    CloseHandle(handle);
    Check(ok && read == archive.size(), "THUGPRO parts: the install's archive read");
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    bool parsed = Obj::ThugproCas::ReadArchive(
        archive, {"qb\\game\\cas_skater_m.qb", "qb\\thugpro\\thugpro_boards.qb", "qb\\game\\cas_skater_shared.qb"}, &files);
    Check(parsed && files.size() == 3, "THUGPRO parts: the install's CAS files found");
    // (each array named: skater_m_head in cas_skater_m, board in thugpro_boards, hat in cas_skater_shared)
    const std::pair<const char*, uint32_t> lists[] = {{"qb\\game\\cas_skater_m.qb", 0x650FAB6D},
                                                      {"qb\\thugpro\\thugpro_boards.qb", 0xA7A9D4B8},
                                                      {"qb\\game\\cas_skater_shared.qb", 0x6DF453B6}};
    for (size_t i = 0; i < files.size(); ++i) {
        std::vector<uint8_t> data = files[i].second;
        data.resize(data.size() + 16, 0);
        auto arrays = Obj::ThugproCas::TopLevelArrays(data.data(), files[i].second.size());
        uint32_t wanted = 0;
        for (auto& [name, list] : lists)
            wanted = files[i].first == name ? list : wanted;
        bool has = false;
        for (auto& array : arrays)
            has = has || array.first == wanted;
        Log("THUGPRO parts: %s: %u bytes, %u top-level arrays", files[i].first.c_str(),
            static_cast<unsigned>(files[i].second.size()), static_cast<unsigned>(arrays.size()));
        Check(has, "THUGPRO parts: the install's CAS lists among the arrays");
    }
}

} // namespace

SELFTEST(Early, TestThugproCasInstall, Cleanup);
SELFTEST(Early, TestThugproCas, Cleanup);
SELFTEST(Early, TestNetGuards, Cleanup);
SELFTEST(Early, TestAppearanceGuard, Cleanup);
SELFTEST(Early, TestGraffitiLog, Cleanup);
SELFTEST(Early, TestTrickObjects, Cleanup);
SELFTEST(Early, TestClientObjectUpdates, Cleanup);
SELFTEST(Early, TestRelayObjectUpdates, Cleanup);
SELFTEST(Early, TestLocalNetLogic, Cleanup);
