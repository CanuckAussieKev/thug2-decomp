// Self-tests for the script commands 0x005B4F80-0x005B5C90 (object placements and projectiles over the network, the
// cutscene destroy list and the std::list constructor, the skaters' set-up and scores, the nearest skater): each
// original against ours on identical stand-in state; the network manager and its players, the memory manager, the
// cutscene entries, spawning and running scripts, the skate module, the skaters and their scores are stood in for
// by recording code (the game's functions and ours alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/hunt_cfuncs.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/cutscene_list_cfuncs.h"
#include "game/obj/movie_cfuncs.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/skater_score_cfuncs.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;

// --- memory: blocks handed out in order from one buffer ---------------------------------------------------------
alignas(16) uint8_t g_lHeap[0x1000];
uint32_t g_lHeapUsed;
alignas(16) uint8_t g_lManager[0x600];
uint32_t HeapId(uint32_t address)
{
    auto base = reinterpret_cast<uint32_t>(g_lHeap);
    if (address >= base && address < base + sizeof(g_lHeap))
        return 0x4EA00000u + (address - base);
    return ArrayId(address);
}

// --- the network ---------------------------------------------------------------------------------------------
alignas(16) uint8_t g_lNet[0x40];
alignas(16) uint8_t g_lServer[0x40];
alignas(16) uint8_t g_lClients[3][0x40];
alignas(16) uint8_t g_lPlayers[4][0x200];
alignas(16) uint8_t g_lPlayerSkaters[4][0x40];
bool g_lObserver[4];
bool g_lCarrying[4];
int g_lPlayerCount;
bool g_lInNetGame;
bool g_lHaveClient;

uint32_t PlayerIndex(uint32_t address)
{
    for (uint32_t i = 0; i < 4; ++i)
        if (address == reinterpret_cast<uint32_t>(g_lPlayers[i]))
            return 0x9100 + i;
    return address;
}
uint32_t PlayerFrom(uint32_t search, int start, bool observers)
{
    for (int i = start; i < g_lPlayerCount; ++i) {
        if (!observers && g_lObserver[i])
            continue;
        reinterpret_cast<uint32_t*>(search)[1] = static_cast<uint32_t>(i + 1);
        return reinterpret_cast<uint32_t>(g_lPlayers[i]);
    }
    return 0;
}
void RecordMessage(uint32_t address)
{
    auto* desc = reinterpret_cast<Net::MsgDesc*>(address);
    Record({desc->m_id, desc->m_stream, desc->m_length, static_cast<uint32_t>(desc->m_priority),
            static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_singular, desc->m_compression});
    auto* data = static_cast<const uint8_t*>(desc->m_data);
    if (!data)
        return;
    if (desc->m_id == 0x8F && desc->m_length == 0x58) { // the id byte, the count, the positions counted
        int count = *reinterpret_cast<const int*>(data + 4);
        Record({data[0], static_cast<uint32_t>(count), HashBytes(data + 8, count > 0 ? count * 16 : 0)});
    } else if (desc->m_id == 0x83) { // (the last 3 bytes are padding)
        Record({HashBytes(data, 0x35)});
    } else {
        Record({HashBytes(data, desc->m_length)});
    }
}

// --- the skaters ---------------------------------------------------------------------------------------------
alignas(16) uint8_t g_lSkate[0x100];
alignas(16) uint8_t g_lSkaters[5][0x200];
void* g_lSkaterVtable[20];
Mem::ListNode g_lSkaterNodes[5];
// The skate module's skater list (+0x28, its head; the original counts it inline): the first `count` skaters.
void SetSkaterCount(int count)
{
    auto* head = reinterpret_cast<Mem::ListNode*>(g_lSkate + 0x28);
    std::memset(head, 0, sizeof(*head));
    head->data = reinterpret_cast<void*>(-1);
    Mem::ListNode* last = head;
    for (int i = 0; i < count; ++i) {
        g_lSkaterNodes[i] = {};
        g_lSkaterNodes[i].data = g_lSkaters[i];
        last->next = &g_lSkaterNodes[i];
        g_lSkaterNodes[i].prev = last;
        last = &g_lSkaterNodes[i];
    }
    last->next = head;
    head->prev = last;
}
uint32_t g_lModeName;
uint32_t SkaterIndex(uint32_t address)
{
    for (uint32_t i = 0; i < 5; ++i)
        if (address == reinterpret_cast<uint32_t>(g_lSkaters[i]))
            return 0x5C00 + i;
    return address;
}

void StandInListCalls(StandIns& s)
{
    using GameNet::Manager;
    // memory
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        uint32_t block = reinterpret_cast<uint32_t>(g_lHeap) + g_lHeapUsed;
        g_lHeapUsed += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        uint32_t block = reinterpret_cast<uint32_t>(g_lHeap) + g_lHeapUsed;
        g_lHeapUsed += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, HeapId(r.args[0])});
        return 0u;
    });
    s.At({0x005ADF60, AddressOf(&CFuncs::CutsceneDestroyEntry::Construct)}, 0, [](Regs& r) {
        Record({0xDF60, HeapId(r.ecx)});
        auto* entry = reinterpret_cast<CFuncs::CutsceneDestroyEntry*>(r.ecx);
        entry->m_name = 0;
        entry->m_destroyScript = 0;
        entry->m_params = reinterpret_cast<Script::CStruct*>(0x57A00000u + (HeapId(r.ecx) & 0xFFFF));
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, r.ecx, r.args[0]});
        return 0u;
    });
    using SpawnFn = Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnFn>(&Script::SpawnScript))}, 0, [](Regs& r) {
        Record({0x5420, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        static uint8_t spawned[0x100];
        reinterpret_cast<uint32_t*>(spawned)[0xB0 / 4] = 0x0ABC0000u + (r.args[0] & 0xFFFF);
        return reinterpret_cast<uint32_t>(spawned);
    });
    using RunScriptFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunScriptFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x004ECC30}, 12, [](Regs& r) { // std::list::erase(first, last)
        Record({0xCC30, r.ecx, HeapId(r.args[1]), HeapId(r.args[2])});
        auto* list = reinterpret_cast<CFuncs::PointerList*>(r.ecx);
        list->m_head->next = list->m_head;
        list->m_head->prev = list->m_head;
        list->m_size = 0;
        *reinterpret_cast<uint32_t*>(r.args[0]) = r.args[2];
        return r.args[0];
    });
    // the network
    s.At({0x0053AEA0, AddressOf(&Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xAEA0, r.args[0] & 0xFFFF});
        uint32_t id = r.args[0] & 0xFFFF;
        return id >= 0x10 && id < 0x14 ? reinterpret_cast<uint32_t>(g_lPlayers[id - 0x10]) : 0u;
    });
    s.At({0x005372C0, AddressOf(&Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x72C0, r.args[1] & 0xFF});
        return PlayerFrom(r.args[0], 0, (r.args[1] & 0xFF) != 0);
    });
    s.At({0x00537010, AddressOf(&Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0x7010, r.args[1] & 0xFF});
        return PlayerFrom(r.args[0], static_cast<int>(reinterpret_cast<uint32_t*>(r.args[0])[1]), (r.args[1] & 0xFF) != 0);
    });
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        Record({0x34A0, PlayerIndex(r.ecx)});
        return 0x4A00u + (PlayerIndex(r.ecx) & 0xFF);
    });
    s.At({0x005433B0, AddressOf(&GameNet::PlayerInfo::IsCarryingFlag)}, 0, [](Regs& r) {
        uint32_t index = PlayerIndex(r.ecx);
        Record({0x33B0, index});
        return g_lCarrying[index & 3] ? 1u : 0u;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        Record({0x7EB0, r.ecx == reinterpret_cast<uint32_t>(g_lServer), r.args[0]});
        RecordMessage(r.args[1]);
        return 0u;
    });
    s.At({0x00532EA0, AddressOf(&Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.args[0]});
        return g_lHaveClient ? reinterpret_cast<uint32_t>(g_lClients[r.args[0] % 3]) : 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        Record({0x9E50, r.ecx - reinterpret_cast<uint32_t>(g_lClients)});
        RecordMessage(r.args[0]);
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, [](Regs&) {
        Record({0xE4F0});
        return g_lInNetGame ? 1u : 0u;
    });
    // the skaters
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.args[0]});
        return r.args[0] < 5 ? reinterpret_cast<uint32_t>(g_lSkaters[r.args[0]]) : 0u;
    });
    g_lSkaterVtable[14] = s.Thunk(0, [](Regs& r) {
        Record({0xF14, SkaterIndex(r.ecx)});
        return 0u;
    });
    s.At({0x00564C60}, 8, [](Regs& r) {
        Record({0x4C60, r.ecx == reinterpret_cast<uint32_t>(g_lSkate), SkaterIndex(r.args[0]), r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0057FEC0, AddressOf(&Obj::CSkater::GetSkaterNumber)}, 0, [](Regs& r) {
        Record({0xFEC0, SkaterIndex(r.ecx)});
        return (SkaterIndex(r.ecx) & 0xF) ^ 3;
    });
    s.At({0x00581550, AddressOf(&Obj::CSkater::ShowSkater)}, 0, [](Regs& r) {
        Record({0x1550, SkaterIndex(r.ecx)});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, SkaterIndex(r.ecx)});
        return r.ecx + 0x100;
    });
    for (uint32_t address : {0x00564A10u, 0x00561B40u}) {
        s.At({address}, 0, [address](Regs& r) {
            Record({address, SkaterIndex(r.ecx - 0x100)});
            return address == 0x00561B40 ? 1000u + (SkaterIndex(r.ecx - 0x100) & 0xF) : 0u;
        });
    }
    for (uint32_t address : {0x00561A80u, 0x00564860u}) {
        s.At({address}, 4, [address](Regs& r) {
            Record({address, SkaterIndex(r.ecx - 0x100), address == 0x00564860 ? r.args[0] & 0xFF : r.args[0]});
            return 0u;
        });
    }
    s.At({0x005532A0}, 0, [](Regs&) {
        Record({0x32A0});
        return g_lModeName;
    });
    s.At({0x004AB910, AddressOf(&Front::SetScoreText)}, 0, [](Regs& r) {
        Record({0xB910, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
}

void SetUpPlayers()
{
    std::memset(g_lNet, 0, sizeof(g_lNet));
    reinterpret_cast<void**>(g_lNet)[1] = g_lServer;
    std::memset(g_lPlayers, 0, sizeof(g_lPlayers));
    for (int i = 0; i < 4; ++i) {
        reinterpret_cast<void**>(g_lPlayers[i])[0x14 / 4] = g_lPlayerSkaters[i];
        reinterpret_cast<uint32_t*>(g_lPlayerSkaters[i])[0x14 / 4] = 0x0C0 + i;
        reinterpret_cast<int*>(g_lPlayers[i])[0x30 / 4] = i & 1; // the team
        reinterpret_cast<int*>(g_lPlayers[i])[0x34 / 4] = i == 2 ? 0 : i + 1; // placed
        auto* positions = reinterpret_cast<float*>(g_lPlayers[i] + 0x180);
        for (int j = 0; j < 20; ++j)
            positions[j] = static_cast<float>(i * 100 + j) + 0.25f;
        g_lObserver[i] = i == 3;
        g_lCarrying[i] = i == 1;
    }
    g_lPlayerCount = 4;
}

void TestNetListScoreCFuncs()
{
    SaveGlobals({{0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x007CCE14, 4, {}}, {0x007CEDD8, 8, {}}, {0x007CED50, 1, {}},
                 {0x007CE478, 4, {}}, {0x007CE470, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInListCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        std::memset(g_lHeap, 0, sizeof(g_lHeap));
        g_lHeapUsed = 0;
        UseStandInArrayPool();
        std::memset(g_lManager, 0, sizeof(g_lManager));
        for (int i = 0; i < 8; ++i)
            reinterpret_cast<uint32_t*>(g_lManager)[0x180 / 4 + i] = 0x4EA80000u + i;
        GameVariable<void*>(0x007D1800) = g_lManager;
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<void*>(0x007CCE14) = g_lNet;
        GameVariable<bool>(0x007CED50) = false;
        GameVariable<void*>(0x007CE478) = g_lSkate;
        GameVariable<void*>(0x007CE470) = reinterpret_cast<void*>(0x6A3E0000);
        SetUpPlayers();
        g_lInNetGame = false;
        g_lHaveClient = true;
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params, void* object = nullptr) {
            Call(original, address, ours, params, object);
        };
        auto heapImage = [&]() {
            std::vector<uint32_t> image(reinterpret_cast<uint32_t*>(g_lHeap), reinterpret_cast<uint32_t*>(g_lHeap + g_lHeapUsed));
            for (uint32_t& word : image)
                word = HeapId(word);
            g_trace.insert(g_trace.end(), image.begin(), image.end());
            auto& list = GameVariable<CFuncs::PointerList>(0x007CEDD8);
            Record({HeapId(reinterpret_cast<uint32_t>(list.m_head)), list.m_size});
        };
        using namespace CFuncs;
        TestParams none;

        // BroadcastObjectPlacements, ReportObjectPlacement, BroadcastProjectile
        {
            call(0x005B4F80, CFunc_BroadcastObjectPlacements, none);
            g_lPlayerCount = 1;
            call(0x005B4F80, CFunc_BroadcastObjectPlacements, none);
            g_lPlayerCount = 4;
            alignas(16) static float pos[4] = {0.0f, 1.5f, -2.5f, 9.0f};
            alignas(16) static float vel[4] = {3.0f, 4.5f, 5.5f, 0.0f};
            TestParams a; a.Checksum(0x0830ECAF, 2).Vector(0x7F261953, pos);
            call(0x005B50F0, CFunc_ReportObjectPlacement, a);
            g_lInNetGame = true;
            call(0x005B50F0, CFunc_ReportObjectPlacement, a);
            g_lHaveClient = false;
            call(0x005B50F0, CFunc_ReportObjectPlacement, a);
            g_lHaveClient = true;
            g_lInNetGame = false;
            TestParams b; b.Checksum(0x0830ECAF, 0x123456A7).Checksum(0x7321A8D6, 0xB0B0B0B0).Checksum(0x1D1A8B59, 0x0A770001)
                .Vector(0x7F261953, pos).Vector(0x0C4C809E, vel).Float(0x13B9DA7B, 1.75f).Integer(0xC48391A5, 48);
            call(0x005B51B0, CFunc_BroadcastProjectile, b);
            g_lHaveClient = false;
            call(0x005B51B0, CFunc_BroadcastProjectile, b);
            g_lHaveClient = true;
        }
        // the cutscene destroy list
        {
            UseStandInStructPool();
            reinterpret_cast<CFuncs::PointerList*>(&GameVariable<uint8_t>(0x007CEDD8))->m_head = nullptr;
            if (original)
                Original<void*(__thiscall*)(void*)>(0x005B4650)(&GameVariable<uint8_t>(0x007CEDD8));
            else
                GameVariable<CFuncs::PointerList>(0x007CEDD8).Construct();
            heapImage();
            static TestParams destroyParams;
            destroyParams.Reset();
            destroyParams.Integer(0x0BAD0001, 5);
            for (uint32_t name : {0xD1000001u, 0xD1000002u, 0xD1000003u}) {
                TestParams p; p.Checksum(0xA1DC81F9, name).Checksum(0x589B2282, name ^ 0x00FF0000);
                if (name != 0xD1000002u)
                    p.Structure(0x9C2E3A57, destroyParams.Struct());
                call(0x005B5320, CFunc_AddToCutsceneDestroyList, p);
                heapImage();
            }
            for (uint32_t name : {0xD1000002u, 0xD1000009u}) {
                TestParams p; p.Checksum(0xA1DC81F9, name);
                call(0x005B5450, CFunc_RemoveFromCutsceneDestroyList, p);
                heapImage();
            }
            GameVariable<bool>(0x007CED50) = true;
            TestParams r; r.Checksum(0xA1DC81F9, 0xD1000001u);
            call(0x005B5450, CFunc_RemoveFromCutsceneDestroyList, r);
            GameVariable<bool>(0x007CED50) = false;
            call(0x005B5BE0, CFunc_FlushCutsceneDestroyList, none);
            Record({GameVariable<uint8_t>(0x007CED50)});
            heapImage();
        }
        // the skaters' set-up and scores
        {
            std::memset(g_lSkaters, 0, sizeof(g_lSkaters));
            for (int i = 0; i < 5; ++i) {
                reinterpret_cast<void**>(g_lSkaters[i])[0] = g_lSkaterVtable;
                g_lSkaters[i][0xBC] = i == 0 || i == 3 ? 1 : 0;
            }
            SetSkaterCount(4);
            call(0x005B5770, CFunc_InitializeSkaters, none);
            call(0x005B57E0, CFunc_ReinsertSkaters, none);
            for (uint32_t mode : {0x6EF8FDA0u, 0x5D32129Cu, 0x11223344u}) {
                g_lModeName = mode;
                call(0x005B5860, CFunc_ResetScore, none);
                TestParams all; all.Integer(0x81740BB6, 0);
                call(0x005B5860, CFunc_ResetScore, all);
            }
            call(0x005B5950, CFunc_UpdateScore, none);
            call(0x005B59C0, CFunc_ResetScorePot, none);
            TestParams bail; bail.Flag(0x929EB4D5);
            call(0x005B59C0, CFunc_ResetScorePot, bail);
            SetSkaterCount(0);
            call(0x005B5770, CFunc_InitializeSkaters, none);
        }
        // GetCollidingPlayerAndTeam (skaters 0-3 are the players' 0x10-0x13; skater 4 has no player)
        {
            alignas(16) static uint8_t object[0x100];
            reinterpret_cast<float*>(object + 0x4C)[0] = 10.0f;
            reinterpret_cast<float*>(object + 0x4C)[1] = 0.0f;
            reinterpret_cast<float*>(object + 0x4C)[2] = -5.0f;
            float where[5][3] = {{30.0f, 2.0f, -5.0f}, {12.0f, 1.0f, -4.0f}, {11.0f, 7.0f, -6.0f},
                                 {10.0f, 0.0f, 50.0f}, {10.5f, 0.0f, -5.0f}};
            for (int i = 0; i < 5; ++i) {
                reinterpret_cast<uint32_t*>(g_lSkaters[i])[0x14 / 4] = i < 4 ? 0x10u + i : 0x77u;
                std::memcpy(g_lSkaters[i] + 0x4C, where[i], 12);
            }
            SetSkaterCount(5);
            for (int exclude : {0, 1, 7}) {
                for (float radius : {0.5f, 2.0f, 6.0f}) {
                    TestParams p; p.Integer(0x6C2A140F, exclude).Float(0xC48391A5, radius);
                    call(0x005B5A40, CFunc_GetCollidingPlayerAndTeam, p, object);
                }
            }
            // the radius squared before rounding: a skater just inside it (0x3EEF9DB2 squared lies between the two
            // roundings of (0.039 * 12) squared)
            std::memset(object + 0x4C, 0, 12);
            const float edge[3] = {0.468f, 0.0f, 0.0f};
            std::memcpy(g_lSkaters[0] + 0x4C, edge, 12);
            TestParams p; p.Integer(0x6C2A140F, 1).Float(0xC48391A5, 0.039f);
            call(0x005B5A40, CFunc_GetCollidingPlayerAndTeam, p, object);
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "placements, projectiles, the cutscene destroy list, skater set-up and scores (0x005B4F80-0x005B5C90)");
}

// Cutscene_SpawnScript, with AddToCutsceneDestroyList stood in for (recording the entry it's given: the kit's
// structure stand-ins only record, so the entry is told by the calls that built it).
void TestCutsceneSpawnScriptCFunc()
{
    SaveGlobals({{0x007D1800, 4, {}}, {0x007CC979, 1, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInListCalls(*standIns);
    standIns->At({0x005B5320, AddressOf(&CFuncs::CFunc_AddToCutsceneDestroyList)}, 0, [](Regs& r) {
        Record({0x5320, ArrayId(r.args[0]), r.args[1]});
        return 1u;
    });
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_lHeapUsed = 0;
        UseStandInArrayPool();
        UseStandInStructPool();
        GameVariable<void*>(0x007D1800) = g_lManager;
        GameVariable<bool>(0x007CC979) = true;
        g_own.Reset();
        using namespace CFuncs;
        static TestParams spawnParams, callbackParams;
        TestParams s; s.Checksum(0, 0x5C417001).Structure(0x7031F10C, spawnParams.Struct()).Checksum(0x86068BD9, 0xCA11BAC0)
            .Structure(0x0DD85BDA, callbackParams.Struct()).Flag(0x20209C31).Integer(0x2EBFF4BC, 3);
        // the calling scripts' symbols (their .qb files at +8), in the stand-in symbol table
        static Script::CSymbolTableEntry symbols[2];
        for (int i = 0; i < 2; ++i) {
            std::memset(&symbols[i], 0, sizeof(symbols[i]));
            symbols[i].unknown0 = 1;
            symbols[i].type = Script::ESYMBOLTYPE_QSCRIPT;
            symbols[i].name = 0x5C417777u + i;
            symbols[i].unknown8 = 0x0F11E000u + i;
            g_table[symbols[i].name & 0xFFF] = &symbols[i];
        }
        auto* script = NewScript();
        reinterpret_cast<uint32_t*>(g_script)[0xC8 / 4] = 0x5C417777;
        reinterpret_cast<int*>(g_script)[0xB8 / 4] = 42;
        bool result = original ? Original<CFunc>(0x005B5510)(s.Struct(), script) : CFunc_Cutscene_SpawnScript(s.Struct(), script);
        Record({result});
        TestParams s2; s2.Checksum(0, 0x5C417002).Checksum(0x40C698AF, 0x1D1D1D1D).Flag(0x23627FD7);
        script = NewScript();
        reinterpret_cast<uint32_t*>(g_script)[0xC8 / 4] = 0x5C417778;
        result = original ? Original<CFunc>(0x005B5510)(s2.Struct(), script) : CFunc_Cutscene_SpawnScript(s2.Struct(), script);
        Record({result});
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "Cutscene_SpawnScript (0x005B5510)");
}

} // namespace

SELFTEST(Early, TestNetListScoreCFuncs, Gs4Tests::RestoreGlobals);
SELFTEST(Early, TestCutsceneSpawnScriptCFunc, Gs4Tests::RestoreGlobals);
