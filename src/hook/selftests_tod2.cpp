// Self-tests for the victory conditions (src/game/skate/victory_conditions.cpp) and the viewer module's pieces
// (src/game/sys/viewer.cpp, Mth::Matrix::RotateLocal(axis, angle)): the original and ours run the same calls on
// identical stand-in skaters, players, goals, viewers, handlers and objects in an arena; what they call outside
// (the skate module's skaters and scores, the network players, the goal manager, the scripts, the memory manager,
// the viewer object and the task lists) is stood in for by recording code written over the game's function and ours
// alike, and the calls and the whole arena are compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/skate/victory_conditions.h"
#include "game/skater/skater.h"
#include "game/sys/module.h"
#include "game/sys/viewer.h"
#include "game/sys/viewer_object.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/script/script_startup.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CStruct;

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

constexpr uint32_t kArenaSize = 0x10000;
ArenaPair<kArenaSize> g_vc_storage;
uint32_t g_vc_base;
uint32_t At(uint32_t offset) { return g_vc_base + offset; }
template <typename T>
T* Ptr(uint32_t offset)
{
    return reinterpret_cast<T*>(g_vc_base + offset);
}
// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_vc_storage.Get()) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_vc_storage.Get(), kArenaSize, word); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
void PushArena(std::vector<uint32_t>& out)
{
    const auto* words = reinterpret_cast<const uint32_t*>(g_vc_base);
    for (uint32_t i = 0; i < kArenaSize / 4; ++i)
        out.push_back(Rel(words[i]));
}

// Arena layout.
constexpr uint32_t kSkate = 0x0000;       // the skate module (+0x34 its skater list)
constexpr uint32_t kSkaterNodes = 0x0100; // the skater list's nodes, 0x10 apart: +4 the skater (-1: the end), +0xC next
constexpr uint32_t kSkaters = 0x0200;     // skaters, 0x40 apart: +0x14 id, +0x20 its score object
constexpr uint32_t kScores = 0x0600;      // score objects, 0x20 apart: +0 the total
constexpr uint32_t kPlayers = 0x0800;     // network players, 0x40 apart: +0x14 skater, +0x30 team, +0x34 objects,
                                          // +0x38 eliminated, +0x3C the next player (offset)
constexpr uint32_t kGoals = 0x0C00;       // goals, 0x40 apart: vtable +0, +0x2C pointer, +0x30 the pointed word,
                                          // +0x34 the 0x005542F0 answer, +0x38 the skaters (bits) that won it
constexpr uint32_t kConditions = 0x1000;  // conditions made by hand, 0x10 apart
constexpr uint32_t kStructs = 0x1100;     // stand-in structures
constexpr uint32_t kViewer = 0x1200;      // a viewer (0x58)
constexpr uint32_t kHandlers = 0x1300;    // input handlers / tasks, 0x40 apart (vtable +0)
constexpr uint32_t kData = 0x1500;        // Inp::Data
constexpr uint32_t kObject = 0x1600;      // a composite object (0x100)
constexpr uint32_t kMisc = 0x1800;
constexpr uint32_t kHeap = 0x4000;

uint32_t g_vc_heapNext;
std::vector<Saved> g_vc_saved;
std::vector<uint32_t> g_vc_calls[2];
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_vc_answers; // (structure offset, name) -> value
bool g_vc_team;
int g_vc_teams;
uint32_t g_vc_firstPlayer;  // offset or 0
uint32_t g_vc_localPlayer;
int g_vc_teamGoals[4];
int g_vc_goalsNeeded;
uint32_t g_vc_goalCount;
int g_vc_scriptGlobal[2];   // viewer_buttons_enabled, select_shift
bool g_vc_scriptExists;
void* g_vc_goalVtable[0x60 / 4];
void* g_vc_objectVtable[4];
void* g_vc_handlerVtable[4];

bool __fastcall StandInGoalWon(uint8_t* goal, void*, uint32_t skaterId)
{
    Rec(0x6050, {reinterpret_cast<uint32_t>(goal), skaterId});
    return (*reinterpret_cast<uint32_t*>(goal + 0x38) >> (skaterId & 31)) & 1;
}
void* __fastcall StandInDelete(void* object, void*, int flags)
{
    Rec(0xDE1, {reinterpret_cast<uint32_t>(object), static_cast<uint32_t>(flags)});
    return object;
}
void __cdecl StandInHandlerCode(Mdl::ViewerInputHandler* handler)
{
    Rec(0xC0DE, {reinterpret_cast<uint32_t>(handler)});
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_vc_saved);
    g_vc_saved.clear();
}

void Install(StandIns& s)
{
    for (void*& slot : g_vc_goalVtable)
        slot = nullptr;
    g_vc_goalVtable[0x50 / 4] = reinterpret_cast<void*>(&StandInGoalWon);
    g_vc_objectVtable[0] = reinterpret_cast<void*>(&StandInDelete);
    g_vc_handlerVtable[2] = reinterpret_cast<void*>(&StandInDelete);
    // The skate module and its skaters' scores.
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Rec(0x65C0, {r.ecx, r.args[0]});
        return r.args[0] < 6 ? At(kSkaters + 0x40 * r.args[0]) : 0u;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Rec(0x0480, {r.ecx});
        return r.ecx ? *reinterpret_cast<uint32_t*>(r.ecx + 0x20) : 0u;
    });
    s.At({0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, [](Regs& r) {
        Rec(0x1B40, {r.ecx});
        return r.ecx ? *reinterpret_cast<uint32_t*>(r.ecx) : 0u; // (a skater past the list has no score object)
    });
    s.At({0x005535F0, AddressOf(&Game::CGameMode::IsTeamGame)}, 0, [](Regs& r) {
        Rec(0x35F0, {r.ecx});
        return g_vc_team ? 1u : 0u;
    });
    s.At({0x00553130, AddressOf(&Game::CGameMode::NumTeams)}, 0, [](Regs& r) {
        Rec(0x3130, {r.ecx});
        return static_cast<uint32_t>(g_vc_teams);
    });
    // The network players (a search: +4 the next player's offset).
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Rec(0x72C0, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), r.args[1]});
        auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
        search[1] = g_vc_firstPlayer;
        if (!search[1])
            return 0u;
        uint32_t player = At(search[1]);
        search[1] = *reinterpret_cast<uint32_t*>(player + 0x3C);
        return player;
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Rec(0x7010, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), r.args[1]});
        auto* search = reinterpret_cast<uint32_t*>(r.args[0]);
        if (!search[1])
            return 0u;
        uint32_t player = At(search[1]);
        search[1] = *reinterpret_cast<uint32_t*>(player + 0x3C);
        return player;
    });
    s.At({0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, [](Regs& r) {
        Rec(0x7320, {r.ecx});
        return g_vc_localPlayer ? At(g_vc_localPlayer) : 0u;
    });
    s.At({0x00543440, AddressOf(&GameNet::PlayerInfo::IsEliminated)}, 0, [](Regs& r) {
        Rec(0x3440, {r.ecx});
        return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx + 0x38));
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Rec(0xAEA0, {r.ecx, r.args[0]});
        return At(kPlayers + 0x40 * (r.args[0] % 4));
    });
    s.At({0x005435F0, AddressOf(&GameNet::Manager::GetTeamScore)}, 4, [](Regs& r) {
        Rec(0x35F1, {r.ecx, r.args[0]});
        return static_cast<uint32_t>(g_vc_teamGoals[r.args[0] & 3]);
    });
    // The goal manager and its goals.
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) {
        Rec(0x91B0, {});
        return At(kMisc);
    });
    s.At({0x00558200, AddressOf(&Game::CGoalManager::GetNumSelectedGoals)}, 0, [](Regs& r) {
        Rec(0x8200, {r.ecx});
        return static_cast<uint32_t>(g_vc_goalsNeeded);
    });
    // (0x00469490, the goal count, is the folded getter `mov eax, [ecx]`: ours reads the goal manager's first word
    // itself - the original's real code reads the arena's, set with g_vc_goalCount)
    s.At({0x00557FB0, AddressOf(&Game::CGoalManager::GetGoalByIndex)}, 4, [](Regs& r) {
        Rec(0x7FB0, {r.ecx, r.args[0]});
        return At(kGoals + 0x40 * r.args[0]);
    });
    s.At({0x005542F0, AddressOf(&Game::CGoal::IsSelected)}, 0, [](Regs& r) {
        Rec(0x42F0, {r.ecx});
        return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx + 0x34));
    });
    // Debug output, the parameters, memory.
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0x1C30, {Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0xFFFFFFFFu;
    });
    auto named = [&](std::initializer_list<uint32_t> addresses, uint32_t tag) {
        s.At(addresses, 12, [tag](Regs& r) {
            uint32_t name = Hash(reinterpret_cast<const char*>(r.args[0]));
            Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
            auto it = g_vc_answers.find({r.ecx - g_vc_base, name});
            if (it == g_vc_answers.end())
                return 0u;
            *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
            return 1u;
        });
    };
    named({0x00476EE0, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))},
          0x6EE0);
    named({0x00476D00, AddressOf(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))},
          0x6D00);
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1]});
        uint32_t block = At(g_vc_heapNext);
        g_vc_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Rec(0xB110, {});
        return 0u;
    });
    // The scripts.
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x8F60, {r.args[0]});
        return static_cast<uint32_t>(r.args[0] == 0x89BEF647u ? g_vc_scriptGlobal[0] : g_vc_scriptGlobal[1]);
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                 bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
             return At(kMisc + 0x100);
         });
    s.At({0x00472DE0, AddressOf(&Script::ScriptExists)}, 0, [](Regs& r) {
        Rec(0x2DE0, {r.args[0]});
        return g_vc_scriptExists ? 1u : 0u;
    });
    s.At({0x005A5960, AddressOf(&Script::LoadQBFile)}, 0, [](Regs& r) {
        Rec(0x5960, {Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    // The controller data's masks.
    s.At({0x0044E840, AddressOf(&Inp::Data::MaskDigitalInput)}, 4, [](Regs& r) {
        Rec(0xE840, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0044E860, AddressOf(&Inp::Data::MaskAnalogInput)}, 4, [](Regs& r) {
        Rec(0xE860, {r.ecx, r.args[0]});
        return 0u;
    });
    // The viewer object, the task / handler lists, the module.
    s.At({0x005897B0, AddressOf(&Obj::CViewerObject::ShutDown)}, 0, [](Regs& r) {
        Rec(0x97B0, {r.ecx});
        return 0u;
    });
    s.At({0x0058ADB0, AddressOf(&Obj::CViewerObject::Construct)}, 4, [](Regs& r) {
        Rec(0xADB0, {r.ecx, r.args[0]});
        *reinterpret_cast<void***>(r.ecx) = g_vc_objectVtable;
        return r.ecx;
    });
    s.At({0x00589910, AddressOf(&Obj::CViewerObject::Load)}, 4, [](Regs& r) {
        Rec(0x9910, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, [](Regs& r) {
        Rec(0x68F0, {r.ecx});
        return 0u;
    });
    s.At({0x00406930, AddressOf(&Tsk::BaseTask::Destruct)}, 0, [](Regs& r) {
        Rec(0x6930, {r.ecx, *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0u;
    });
    s.At({0x0045A690, AddressOf(&Net::Dispatcher::Deinit)}, 0, [](Regs& r) {
        Rec(0xA690, {r.ecx});
        return 0u;
    });
    s.At({0x0044F790, AddressOf(&Mdl::Module::DestroyBody)}, 0, [](Regs& r) {
        Rec(0xF790, {r.ecx});
        return 0u;
    });
}

void SetUpArena(int run)
{
    uint8_t* arena = g_vc_storage.Get()[run];
    g_vc_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + kHeap, 0xCD, kArenaSize - kHeap);
    g_vc_heapNext = kHeap;
    g_trace.clear();
    g_vc_answers.clear();
    GameVariable<uint8_t*>(0x007CE478) = Ptr<uint8_t>(kSkate);
    GameVariable<void*>(0x007CE470) = Ptr<void>(kMisc + 0x200);
    GameVariable<void*>(0x007CCE14) = Ptr<void>(kMisc + 0x240);
    GameVariable<void*>(0x007D1800) = Ptr<void>(kMisc + 0x300);
    GameVariable<bool>(0x007CC979) = true;
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
}

void SaveGlobals()
{
    g_vc_saved = {{0x007CE478, 4, {}}, {0x007CE470, 4, {}}, {0x007CCE14, 4, {}}, {0x007D1800, 4, {}},
                  {0x007CC979, 1, {}}, {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}, {0x007CE498, 0x14, {}},
                  {0x007CEFAC, 1, {}}};
    SaveAll(g_vc_saved);
}

// `count` skaters with these scores (the list ends after them).
void SetSkaters(std::vector<int> scores)
{
    *Ptr<uint32_t>(kSkate + 0x34) = At(kSkaterNodes);
    for (size_t i = 0; i <= scores.size(); ++i) {
        uint32_t node = kSkaterNodes + 0x10 * static_cast<uint32_t>(i);
        bool end = i == scores.size();
        *Ptr<uint32_t>(node + 4) = end ? 0xFFFFFFFFu : At(kSkaters + 0x40 * static_cast<uint32_t>(i));
        *Ptr<uint32_t>(node + 0xC) = end ? 0u : At(node + 0x10);
        if (end)
            break;
        uint32_t skater = kSkaters + 0x40 * static_cast<uint32_t>(i);
        *Ptr<uint32_t>(skater + 0x14) = 0x100u + static_cast<uint32_t>(i);
        *Ptr<uint32_t>(skater + 0x20) = At(kScores + 0x20 * static_cast<uint32_t>(i));
        *Ptr<int>(kScores + 0x20 * static_cast<uint32_t>(i)) = scores[i];
    }
}
// Players (team, objects, eliminated) for the first skaters.
void SetPlayers(std::vector<std::vector<int>> players)
{
    g_vc_firstPlayer = players.empty() ? 0u : kPlayers;
    for (size_t i = 0; i < players.size(); ++i) {
        uint32_t player = kPlayers + 0x40 * static_cast<uint32_t>(i);
        *Ptr<uint32_t>(player + 0x14) = At(kSkaters + 0x40 * static_cast<uint32_t>(i));
        *Ptr<int>(player + 0x30) = players[i][0];
        *Ptr<int>(player + 0x34) = players[i][1];
        *Ptr<uint8_t>(player + 0x38) = static_cast<uint8_t>(players[i][2]);
        *Ptr<uint32_t>(player + 0x3C) = i + 1 < players.size() ? player + 0x40 : 0u;
    }
}

using Game::CVictoryCondition;

// The victory conditions: IsHighestScore (ties, the player itself, a lower score), target_score (team totals and
// the skaters), complete_goals (teams; goals that don't count, won and not; no goals; no skaters),
// last_man_standing and elimination (no local player, no skater, scores, eliminated), collect_all_objects (no
// objects; a skater with all of them), the prints, the destructors, and CreateVictoryCondition for every type (and an
// unknown one).
void TestVictoryConditions()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        auto condition = [](int n, int value) {
            auto* c = Ptr<Game::CScoreReachedVictoryCondition>(kConditions + 0x10 * n);
            c->m_vtable = reinterpret_cast<void**>(0x0064EB20);
            c->m_score = value;
            return c;
        };
        // IsHighestScore.
        for (std::vector<int> scores : {std::vector<int>{50, 20, 70}, {80, 80, 10}, {5}, {}}) {
            SetSkaters(scores);
            for (uint32_t player = 0; player < 4; ++player) {
                CVictoryCondition* c = condition(0, 0);
                t.push_back(original ? Original<bool(__thiscall*)(CVictoryCondition*, uint32_t)>(0x005769B0)(c, player)
                                     : c->IsHighestScore(player));
            }
        }
        // target_score.
        SetSkaters({30, 120, 60, 10});
        SetPlayers({{0, 3, 0}, {1, 0, 1}, {0, 5, 0}, {3, 2, 0}});
        for (bool team : {false, true}) {
            g_vc_team = team;
            for (int target : {200, 120, 121, 90, 10}) {
                auto* c = condition(1, target);
                t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576A40)(c) : c->ConditionComplete());
            }
        }
        // complete_goals.
        auto* goals = reinterpret_cast<Game::CCompleteGoalsVictoryCondition*>(condition(2, 0));
        for (int g = 0; g < 4; ++g) {
            uint32_t goal = kGoals + 0x40 * g;
            *Ptr<void**>(goal) = g_vc_goalVtable;
            *Ptr<uint32_t>(goal + 0x2C) = At(goal + 0x30);
            *Ptr<uint32_t>(goal + 0x30) = g == 2 ? 1u : 0u;      // goal 2 doesn't count
            *Ptr<uint8_t>(goal + 0x34) = g == 3 ? 0 : 1;           // nor goal 3
            *Ptr<uint32_t>(goal + 0x38) = g == 0 ? 0x6u : 0x4u;    // skaters 1, 2 won goal 0; skater 2 goal 1
        }
        auto completeGoals = [&]() {
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576B80)(goals) : goals->ConditionComplete());
        };
        g_vc_team = true;
        g_vc_teams = 3;
        g_vc_goalsNeeded = 4;
        for (int best : {2, 4, 7}) {
            g_vc_teamGoals[0] = 1;
            g_vc_teamGoals[1] = best;
            g_vc_teamGoals[2] = 3;
            completeGoals();
        }
        g_vc_team = false;
        for (uint32_t count : {4u, 2u, 0u}) {
            g_vc_goalCount = count;
            *Ptr<uint32_t>(kMisc) = count; // (the goal manager's first word: what 0x00469490 returns)
            for (std::vector<int> scores : {std::vector<int>{1, 1, 1}, {1, 1}, {}})
                SetSkaters(scores), completeGoals();
        }
        // last_man_standing / elimination.
        SetSkaters({0, 7, -3});
        SetPlayers({{0, 0, 1}, {0, 0, 0}, {0, 0, 1}});
        for (uint32_t local : {0u, kPlayers, kPlayers + 0x40, kPlayers + 0x80, kMisc + 0x400}) {
            g_vc_localPlayer = local;
            auto* last = reinterpret_cast<Game::CLastManStandingVictoryCondition*>(condition(3, 0));
            auto* elim = reinterpret_cast<Game::CEliminationVictoryCondition*>(condition(4, 0));
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576680)(last) : last->ConditionComplete());
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x005766E0)(elim) : elim->ConditionComplete());
        }
        for (uint32_t player = 0; player < 7; ++player) {
            auto* last = reinterpret_cast<Game::CLastManStandingVictoryCondition*>(condition(3, 0));
            auto* elim = reinterpret_cast<Game::CEliminationVictoryCondition*>(condition(4, 0));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x005766B0)(last, player)
                                 : last->IsWinner(player));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x00576700)(elim, player)
                                 : elim->IsWinner(player));
        }
        // collect_all_objects.
        auto* collect = reinterpret_cast<Game::CCollectAllObjectsVictoryCondition*>(condition(5, 0));
        for (std::vector<int> scores : {std::vector<int>{3, 10, 9}, {4, 4, 4}, {12, 4, 4}, {0, 0, 0}}) {
            SetSkaters(scores);
            SetPlayers({{0, scores[0] == 0 ? 0 : 3, 0}, {0, scores[0] == 0 ? 0 : 6, 0}, {0, 1, 0}});
            if (scores[0] == 0)
                *Ptr<int>(kPlayers + 0x80 + 0x34) = 0;
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576740)(collect) : collect->ConditionComplete());
        }
        SetPlayers({});
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576740)(collect) : collect->ConditionComplete());
        // The prints and destructors.
        auto* combo = reinterpret_cast<Game::CBestComboVictoryCondition*>(condition(6, 9));
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576630)(combo) : combo->Print());
        t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00576610)(condition(7, 444))
                             : condition(7, 444)->Print());
        for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
            CVictoryCondition* c = condition(8 + flags, 1);
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                original ? Original<CVictoryCondition*(__thiscall*)(void*, uint8_t)>(0x00576970)(c, flags) : c->Destroy(flags))));
            c = condition(10 + flags, 1);
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                original ? Original<CVictoryCondition*(__thiscall*)(void*, uint8_t)>(0x00576650)(c, flags)
                         : c->DestroyBase(flags))));
        }
        CVictoryCondition* body = condition(12, 1);
        if (original)
            Original<void(__thiscall*)(void*)>(0x005769A0)(body);
        else
            body->DestroyBody();
        // CreateVictoryCondition.
        const uint32_t types[] = {0x8F30FD26, 0x71CD53A2, 0x6FC19399, 0x6FAD9811, 0xCC06880F, 0xA6B8D7B0, 0x72068889, 0x1234};
        for (uint32_t type : types) {
            uint32_t params = kStructs + 0x10 * (type & 7);
            g_vc_answers[{params, Hash("type")}] = type;
            g_vc_answers[{params, Hash("score")}] = 777;
            g_vc_answers[{params, Hash("num_combos")}] = 5;
            CVictoryCondition* made = original ? Original<CVictoryCondition*(__cdecl*)(CStruct*)>(0x00576810)(Ptr<CStruct>(params))
                                               : Game::CreateVictoryCondition(Ptr<CStruct>(params));
            t.push_back(Rel(reinterpret_cast<uint32_t>(made)));
        }
        g_vc_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_vc_calls[0], g_vc_calls[1], "victory conditions (calls)");
    Compare(traces[0], traces[1], "victory conditions");
}

// The viewer: the input handler (every button rule, view mode off, buttons disabled, select held), the shift
// handler, the viewer object made / removed / replaced, the stop callback, the destructors, the setters and getters,
// spawning a script that exists or not, a script file reloaded (runnow.qb, ..\runnow.qb, another), the handler
// template's methods, an object moved along its axes, and Matrix::RotateLocal(axis, angle).
void TestViewer()
{
    SaveGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        auto* viewer = Ptr<Mdl::Viewer>(kViewer);
        auto* handler = Ptr<Mdl::ViewerInputHandler>(kHandlers);
        auto* data = Ptr<Inp::Data>(kData);
        handler->m_controlData = data;
        handler->m_data = viewer;
        handler->m_code = &StandInHandlerCode;
        auto input = [&](void (*ours)(Mdl::ViewerInputHandler*), uint32_t address) {
            if (original)
                Original<void(__cdecl*)(Mdl::ViewerInputHandler*)>(address)(handler);
            else
                ours(handler);
            t.insert(t.end(), {Bits(viewer->m_rightX), Bits(viewer->m_rightY), Bits(viewer->m_leftX),
                               Bits(viewer->m_leftY), viewer->m_commands, viewer->m_shiftMakes});
        };
        const uint32_t held[] = {0, 0x8, 0xA, 0x2, 0x8 | 0x1000 | 0x4000 | 0x2000 | 0x8000 | 0x4, 0x5, 0x1, 0x100,
                                 0xFFFFFFFF, 0x1100};
        const uint32_t made[] = {0, 0x2, 0xFFFFFFFF, 0x800 | 0x10 | 0x80 | 0x20 | 0x40, 0x1000 | 0x4000 | 0x200, 0x40,
                                 0x2 | 0x10};
        int step = 0;
        for (int mode : {0, 1, 2}) {
            for (uint32_t h : held) {
                for (uint32_t m : made) {
                    ++step;
                    GameVariable<int>(0x007CE4A4) = step % 11 == 0 ? 0 : 1;
                    g_vc_scriptGlobal[0] = step % 7 == 0 ? 0 : 1;
                    g_vc_scriptGlobal[1] = step % 3;
                    viewer->m_mode = mode;
                    viewer->m_commands = 0x40000000u * (step & 1);
                    data->m_buttons = h;
                    data->m_makes = m;
                    for (int e = 0; e < 4; ++e)
                        data->m_event[0x12 + e] = static_cast<uint8_t>(step * 37 + e * 61);
                    input(Mdl::HandleViewerInput, 0x00576F00);
                    input(Mdl::HandleViewerShift, 0x00576EB0);
                }
            }
        }
        // The viewer object, the stop callback, the destructors.
        viewer->m_netHandler = Ptr<void>(kHandlers + 0x100);
        *Ptr<void**>(kHandlers + 0x100) = g_vc_objectVtable;
        for (int i = 0; i < 3; ++i)
            *Ptr<void**>(kHandlers + 0x40 * (i + 1)) = g_vc_handlerVtable;
        viewer->m_inputHandler = Ptr<Mdl::ViewerInputHandler>(kHandlers + 0x40);
        viewer->m_shiftHandler = Ptr<Mdl::ViewerInputHandler>(kHandlers + 0x80);
        viewer->m_task1C = Ptr<void>(kHandlers + 0xC0);
        viewer->m_task20 = Ptr<void>(kHandlers + 0x140);
        *Ptr<void**>(kHandlers + 0x140) = g_vc_handlerVtable;
        auto method = [&](uint32_t address, void (Mdl::Viewer::*ours)()) {
            if (original)
                Original<void(__thiscall*)(Mdl::Viewer*)>(address)(viewer);
            else
                (viewer->*ours)();
            t.push_back(Rel(reinterpret_cast<uint32_t>(viewer->m_object)));
        };
        *Ptr<uint32_t>(kMisc + 0x300 + 0x114) = 0xB0770111; // the manager's bottom-up and debug heaps, told apart
        *Ptr<uint32_t>(kMisc + 0x300 + 0x154) = 0xDEB06111;
        for (bool debugHeap : {false, true, false}) {
            GameVariable<bool>(0x007CEFAC) = debugHeap;
            if (original)
                Original<void(__thiscall*)(Mdl::Viewer*, void*)>(0x00577170)(viewer, Ptr<void>(kMisc + 0x500));
            else
                viewer->CreateViewerObject(Ptr<void>(kMisc + 0x500));
            t.push_back(Rel(reinterpret_cast<uint32_t>(viewer->m_object)));
        }
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(Mdl::Viewer*)>(0x00576E60)(viewer)
                                                             : viewer->GetViewerObject())));
        method(0x00576D40, &Mdl::Viewer::RemoveViewerObject);
        method(0x00576D40, &Mdl::Viewer::RemoveViewerObject);
        viewer->m_object = Ptr<void>(kHandlers + 0x180);
        *Ptr<void**>(kHandlers + 0x180) = g_vc_objectVtable;
        void* context = viewer;
        t.push_back(original ? Original<int(__cdecl*)(void**)>(0x00577140)(&context) : Mdl::HandleRemoveViewerObject(&context));
        viewer->m_object = Ptr<void>(kHandlers + 0x180);
        method(0x00576D70, &Mdl::Viewer::StopCallback);
        method(0x00576D70, &Mdl::Viewer::StopCallback);
        for (uint32_t commands : {0x1u, 0x80000010u}) {
            if (original)
                Original<void(__thiscall*)(Mdl::Viewer*, uint32_t)>(0x00577260)(viewer, commands);
            else
                viewer->AddCommands(commands);
            t.push_back(viewer->m_commands);
        }
        GameVariable<Mdl::Viewer*>(0x007CE4A8) = viewer;
        method(0x00576DC0, &Mdl::Viewer::DestroyBody);
        t.push_back(Rel(GameVariable<uint32_t>(0x007CE4A8)));
        viewer->m_netHandler = nullptr;
        viewer->m_task20 = nullptr;
        for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(Mdl::Viewer*, uint8_t)>(0x00577230)(viewer, flags)
                                                                 : viewer->Destroy(flags))));
        }
        // The setters and getters.
        if (original) {
            Original<void(__cdecl*)(int)>(0x00576E70)(3);
            Original<void(__cdecl*)(float)>(0x00576E80)(12.5f);
            Original<void(__cdecl*)(float)>(0x00576E90)(-7.25f);
            t.push_back(Rel(reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00576EA0)())));
        } else {
            Mdl::SetViewMode(3);
            Mdl::SetMovementVelocity(12.5f);
            Mdl::SetRotateVelocity(-7.25f);
            t.push_back(Rel(reinterpret_cast<uint32_t>(Mdl::GetViewer())));
        }
        t.insert(t.end(), {GameVariable<uint32_t>(0x007CE4A4), GameVariable<uint32_t>(0x007CE49C),
                           GameVariable<uint32_t>(0x007CE498)});
        // Scripts.
        for (bool exists : {false, true}) {
            g_vc_scriptExists = exists;
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__cdecl*)(uint32_t)>(0x00576CB0)(0x5C5C)
                                                                 : Mdl::SpawnScriptIfExists(0x5C5C))));
        }
        for (const char* file : {"runnow.qb", "..\\runnow.qb", "other.qb", "runnow.qbx", "RUNNOW.QB"}) {
            GameVariable<bool>(0x007CE4A0) = false;
            const char* name = file;
            t.push_back(original ? Original<int(__cdecl*)(const char**)>(0x00576CF0)(&name) : Mdl::ReloadScriptFile(&name));
            t.push_back(GameVariable<uint8_t>(0x007CE4A0));
        }
        // The handler template's methods.
        if (original) {
            Original<void(__thiscall*)(void*)>(0x00577270)(handler);
            Original<void(__thiscall*)(void*)>(0x00577280)(handler);
            Original<void(__thiscall*)(void*)>(0x00577290)(handler);
        } else {
            handler->Handle();
            handler->DestroyBody();
            handler->DestroyNode();
        }
        // An object moved along its axes.
        auto* object = Ptr<Obj::CCompositeObject>(kObject);
        for (int n = 0; n < 5; ++n) {
            float* m = &object->m_matrix.row[0].x;
            for (int i = 0; i < 16; ++i)
                m[i] = static_cast<float>(std::sin(i * 1.3 + n) * (1.0 + i * 0.1));
            object->m_pos = {1.5f * n, -2.0f + n, 100.25f, 1.0f};
            Mth::Vector offset = {0.75f - n, 3.0f * n, -1.125f, 0.5f};
            if (original)
                Original<void(__cdecl*)(Obj::CCompositeObject*, const Mth::Vector*)>(0x005772E0)(object, &offset);
            else
                Mdl::MoveObjectLocal(object, &offset);
            t.insert(t.end(), {Bits(object->m_pos.x), Bits(object->m_pos.y), Bits(object->m_pos.z), Bits(object->m_pos.w)});
        }
        g_vc_calls[run] = g_trace;
        PushArena(t);
    }
    Cleanup();
    Compare(g_vc_calls[0], g_vc_calls[1], "viewer (calls)");
    Compare(traces[0], traces[1], "viewer");
}

// The same D3DXMatrixMultiply for both (the original's statically linked one and ours).
Mth::Matrix* __stdcall SameMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}

// Matrix::RotateLocal(axis, angle) (0x005772A0): several matrices, axes and angles.
void TestMatrixRotateLocalAxis()
{
    Patch multiply{0x005BFA8C, Branch(0xE9, 0x005BFA8C, reinterpret_cast<void*>(&SameMultiply))};
    // (the rotation built the same way for both: the original's builder uses the x87's sine and cosine)
    auto* build = static_cast<Mth::Matrix& (*)(Mth::Matrix&, const Mth::Vector&, float)>(&Mth::CreateRotateMatrix);
    Patch rotate{0x00404C60, Branch(0xE9, 0x00404C60, reinterpret_cast<void*>(build))};
    std::vector<uint32_t> results[2];
    for (int run = 0; run < 2; ++run) {
        for (int n = 0; n < 6; ++n) {
            Mth::Matrix m;
            float* f = &m.row[0].x;
            for (int i = 0; i < 16; ++i)
                f[i] = static_cast<float>(std::cos(i * 0.7 + n * 1.9) * (2.0 - i * 0.05));
            Mth::Vector axis = {0.25f * n - 0.5f, 1.0f, -0.75f + n, 0.0f};
            float angle = -1.0f + 0.7f * n;
            Mth::Matrix* r = run == 0 ? Original<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*, float)>(0x005772A0)(
                                            &m, &axis, angle)
                                      : &m.RotateLocal(axis, angle);
            results[run].push_back(r == &m);
            const auto* words = reinterpret_cast<const uint32_t*>(&m);
            results[run].insert(results[run].end(), words, words + 16);
        }
    }
    Compare(results[0], results[1], "Matrix::RotateLocal(axis, angle)");
}

} // namespace

SELFTEST(Early, TestVictoryConditions, Cleanup);
SELFTEST(Early, TestViewer, Cleanup);
SELFTEST(Early, TestMatrixRotateLocalAxis);
