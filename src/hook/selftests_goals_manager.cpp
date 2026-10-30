// Self-tests for the goal manager's own methods (Game::CGoalManager, 0x00557E30-0x005591BF and its table / list /
// construction code) and the lookup table (Lst::LookupTable, 0x00557F10 / 0x00561430 / 0x005628B0 / 0x00562970 /
// 0x0055B990): the original against ours on the same stand-in manager - its goal table and active list real, its
// goals stand-ins (a recording vtable; every CGoal method they call stood in for, answered from the seed), the
// structures stood in for (answered from tables), the network manager, the skater, the goal peds recorded.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/stubs.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_ped.h"
#include "game/goals/goal_skate_line.h"
#include "game/goals/goal_types.h"
#include "game/lst/lookup_table.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/object.h"
#include "game/net/gamenet_commands.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/obj/smtptr.h"
#include "game/ass/assman.h"
#include "game/mth/random.h"
#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/skate/profile_manager.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/trick_goals.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Game::CGoal;
using Game::CGoalManager;
using Game::GoalLink;
using Lst::LookupTable;

uint32_t Address(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
bool Coin() { return (Next() & 1) != 0; }
uint32_t g_salt;
uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = (a ^ 0x9E3779B9u) * 0x85EBCA6Bu ^ b * 0xC2B2AE35u ^ g_salt;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return h ^ (h >> 13);
}

// --- The lookup table ------------------------------------------------------------------------------------------------

alignas(16) uint8_t g_items[8][0x10];
void* g_itemVtable[1];

void RecordTable(const LookupTable& table)
{
    Record({static_cast<uint32_t>(table.m_count), Address(table.m_first), Address(table.m_last), Address(table.m_cursor),
            static_cast<uint32_t>(table.m_cursorIndex)});
    int n = 0;
    for (LookupTable::Node* node = table.m_first; node && n < 20; node = node->next, ++n)
        Record({Address(node), node->key, Address(node->item)});
}

void TestLookupTable()
{
    Setup();
    auto* standIns = new StandIns;
    StandInGoalMemory(*standIns);
    g_itemVtable[0] = standIns->Thunk(4, [](Regs& r) {
        Record({0x17E0, r.ecx, r.args[0] & 0xFF});
        return r.ecx;
    });
    for (auto& item : g_items)
        *reinterpret_cast<void***>(item) = g_itemVtable;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        for (uint32_t seed = 0; seed < 60; ++seed) {
            ResetMemory();
            g_seed = seed * 2654435761u + 77;
            LookupTable table = {};
            table.m_last = reinterpret_cast<LookupTable::Node*>(0x1A570000); // (not set by an empty table)
            table.m_cursorIndex = 0x55;
            Record({0xEEEE0000 | seed});
            for (int op = 0; op < 40; ++op) {
                uint32_t kind = Next() % 10;
                uint32_t key = 0x3000 + Next() % 8;
                if (kind < 3 || table.m_count == 0) {
                    void* item = g_items[Next() % 8];
                    Record({o ? Original<bool(__thiscall*)(LookupTable*, const uint32_t*, void*)>(0x005628B0)(&table, &key, item)
                              : table.PutItem(&key, item)});
                } else if (kind < 5) {
                    Record({Address(o ? Original<void*(__thiscall*)(LookupTable*, const uint32_t*)>(0x00561430)(&table, &key)
                                      : table.GetItem(&key))});
                } else if (kind < 8) {
                    int index = static_cast<int>(Next() % static_cast<uint32_t>(table.m_count));
                    uint32_t found = 0xFFFF;
                    uint32_t* out = Coin() ? &found : nullptr;
                    Record({Address(o ? Original<void*(__thiscall*)(LookupTable*, const int*, uint32_t*)>(0x00557F10)(&table, &index, out)
                                      : table.GetItemByIndex(&index, out)),
                            found});
                } else if (kind < 9) {
                    if (o)
                        Original<void(__thiscall*)(LookupTable*, const uint32_t*)>(0x00562970)(&table, &key);
                    else
                        table.FlushItem(&key);
                } else {
                    Lst::LookupTableDestroyer destroyer = {&table};
                    if (o)
                        Original<void(__thiscall*)(Lst::LookupTableDestroyer*)>(0x0055B990)(&destroyer);
                    else
                        destroyer.DeleteTableContents();
                }
                RecordTable(table);
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 5000, "lookup table: the original ran");
    CompareTraces(traces[0], traces[1], "lookup table (0x00557F10, 0x00561430, 0x005628B0, 0x00562970, 0x0055B990)");
}

void RestoreLookupTable() { Restore(); }

// --- The goal manager ------------------------------------------------------------------------------------------------

constexpr int kGoals = 6;
constexpr uint32_t kIds[kGoals] = {0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006};
constexpr uint32_t kMissing = 0x9999;

alignas(16) uint8_t g_mgr[sizeof(CGoalManager) + 0x10];
alignas(16) uint8_t g_goals[kGoals][0x80];
alignas(16) uint8_t g_goalParams[kGoals][0x10];
alignas(16) uint8_t g_peds[kGoals][0x20];
alignas(16) uint8_t g_goalData[0x10];
alignas(16) uint8_t g_mgrParams[0x10];
alignas(16) uint8_t g_saved[kGoals + 1][0x10];
alignas(16) uint8_t g_skater[0x200];
alignas(16) uint8_t g_score[0x40];
alignas(16) uint8_t g_trickComponent[0x1C00];
alignas(16) uint8_t g_client[0x40];
alignas(16) uint8_t g_net[0x40];
alignas(16) uint8_t g_mode[0x40];
alignas(16) uint8_t g_levelStructure[0x10];
alignas(16) uint8_t g_profile[0x80];
alignas(16) uint8_t g_mapping[0x10];
alignas(16) uint8_t g_trickStructure[0x10];
alignas(16) uint8_t g_trickParams[0x10];
alignas(16) uint8_t g_extraEntry[0x10];
alignas(16) uint8_t g_extraParams[0x10];
CArray g_extraArray;
CArray g_extrasList;
uint32_t g_maxSpecials;
Obj::CSkaterProfile::SpecialTrick g_specials[4];
constexpr uint32_t kTricks[] = {0x7A1C0001, 0x7A1C0002, 0xF60C9090, 0x7A1C0004};
alignas(16) uint8_t g_newGoalParams[0x10];
alignas(16) uint8_t g_proList[0x10];
alignas(16) uint8_t g_proGoals[3][0x10];
alignas(16) uint8_t g_goalInfos[6][0x10];
Script::CComponent g_proComponents[3];
Script::CComponent g_infoComponents[6];
std::map<uint32_t, std::vector<uint32_t>> g_fakeLists;
bool g_viewGoalsTest;
alignas(16) uint8_t g_screenManager[0x40];
alignas(16) uint8_t g_timeElement[0x200];
bool g_noTimeElement;
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_animBlender[0x40];
alignas(16) uint8_t g_objectManager[0x80];
alignas(16) uint8_t g_assets[0x80];
alignas(16) uint8_t g_fakeScript[0x100];
void* g_objectVtable[16];
void* g_componentVtable[16];
bool g_noObject;
uint32_t g_streamSalt;
bool g_multiplayer;
const char* g_viewText[kGoals];
LookupTable::Node g_nodes[kGoals];
GoalLink g_childLinks[kGoals][3];
GoalLink g_parentLinks[kGoals][3];
Script::CComponent g_components[kGoals + 3];
CArray g_geometry[kGoals];
uint32_t g_geometryElements[kGoals][3];
void* g_vt[30];
bool g_inNetGame;
uint32_t g_modeName, g_goalsMode, g_level, g_gameId, g_rootId;

CGoalManager* Mgr() { return reinterpret_cast<CGoalManager*>(g_mgr); }
CGoal* Goal(int i) { return reinterpret_cast<CGoal*>(g_goals[i]); }
int GoalIndex(uint32_t address)
{
    for (int i = 0; i < kGoals; ++i)
        if (address == Address(g_goals[i]))
            return i;
    return -1;
}

// (`peds`: the goal peds' methods stood in for too - not when they're the ones tested)
void StandInManagerCalls(StandIns& s, bool peds = true)
{
    StandInGoalMemory(s);
    StandInPrintf(s);
    StandInGetters(s);
    StandInContains(s);
    StandInStructChanges(s);
    StandInStructAdds(s);
    StandInArrays(s);
    StandInScripts(s);
    // The goals' vtable: each slot recorded, answered from the seed (the deleting destructor returns the goal).
    static const uint16_t kPops[30] = {4, 0, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 8, 0,
                                       0, 0, 0, 0, 12, 4, 0, 4, 0, 0, 0, 0, 4, 4, 0};
    for (uint32_t slot = 0; slot < 30; ++slot) {
        uint16_t pop = kPops[slot];
        g_vt[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xF7000000u | slot * 4, r.ecx});
            for (uint32_t i = 0; i < pop / 4u; ++i)
                g_trace.push_back(r.args[i] & (slot == 13 || slot == 0 ? 0xFFu : 0xFFFFFFFFu));
            if (slot == 0)
                return r.ecx;
            return Mix(slot, r.ecx ^ (pop ? r.args[0] : 0)) & 1;
        });
    }
    // CGoal's methods the manager calls: recorded, answered from the seed (bools), or as noted.
    struct Method {
        uint32_t address;
        uint32_t ours;
        uint16_t pop;
        uint32_t kind; // 0 none, 1 a bool, 2 the parameters, 3 the id, 4 the root's id
    };
    const Method kMethods[] = {
        {0x00554220, AddressOf(&CGoal::IsLocked), 0, 1},
        {0x005542F0, AddressOf(&CGoal::IsSelected), 0, 1},
        {0x00554540, AddressOf(&CGoal::IsNetGoal), 0, 1},
        {0x00556F90, AddressOf(&CGoal::AppendDifficultyLevelParams), 0, 0},
        {0x00557270, AddressOf(&CGoal::Init), 0, 0},
        {0x00553E90, AddressOf(&CGoal::GetParams), 0, 2},
        {0x00554510, AddressOf(&CGoal::SetQuickStart), 0, 0},
        {0x00554520, AddressOf(&CGoal::UnsetQuickStart), 0, 0},
        {0x00554F90, AddressOf(&CGoal::GotGap), 4, 1},
        {0x00555100, AddressOf(&CGoal::NextWaypoint), 0, 1},
        {0x00554250, AddressOf(&CGoal::IsGraffiti), 0, 1},
        {0x005552D0, AddressOf(&CGoal::KillCluster), 8, 0},
        {0x00554260, AddressOf(&CGoal::IsSpecial), 0, 1},
        {0x00554270, AddressOf(&CGoal::IsTetris), 0, 1},
        {0x00554A50, AddressOf(&CGoal::IsFindGaps), 0, 1},
        {0x005553C0, AddressOf(&CGoal::DidTrickString), 0, 1},
        {0x00554280, AddressOf(&CGoal::GetGoalId), 0, 3},
        {0x00557A40, AddressOf(&CGoal::Unlock), 0, 0},
        {0x00554450, AddressOf(&CGoal::Deselect), 0, 0},
        {0x00554440, AddressOf(&CGoal::Select), 0, 0},
        {0x00554430, AddressOf(&CGoal::SetHasSeen), 0, 0},
        {0x00554530, AddressOf(&CGoal::IsProGoal), 0, 1},
        {0x00554550, AddressOf(&CGoal::IsCompetition), 0, 1},
        {0x00556A00, AddressOf(&CGoal::ReplaceTrickText), 0, 1},
        {0x00554AC0, AddressOf(&CGoal::AddParent), 4, 0},
        {0x00553E00, AddressOf(&CGoal::PassInheritedFlags), 4, 0},
        {0x00553FB0, AddressOf(&CGoal::Pause), 0, 0},
        {0x00553FC0, AddressOf(&CGoal::UnPause), 0, 0},
        {0x00554BD0, AddressOf(&CGoal::Uninitialize), 8, 0},
        {0x00555540, AddressOf(&CGoal::GetRootGoalId), 0, 4},
        {0x00570F60, AddressOf(&Game::CTetrisGoal::CheckTricks), 0, 0},
        {0x00552B90, AddressOf(&Game::CFindGapsGoal::CheckGaps), 0, 0},
        {0x00576520, AddressOf(&Game::CTrickObjectsGoal::ObjectHit), 4, 0},
        {0x00553F70, AddressOf(&CGoal::EditParams), 4, 1},
        {0x00553EA0, AddressOf(&CGoal::ResetTimer), 0, 0},
        {0x00553F10, AddressOf(&CGoal::SetTimeLeft), 4, 0},
        {0x00553FA0, AddressOf(&CGoal::HasSeen), 0, 1},
        {0x00554110, AddressOf(&CGoal::AllFlagsSet), 0, 1},
        {0x005541E0, AddressOf(&CGoal::GoalFlagSet), 8, 1},
        {0x00560F90, AddressOf(&Game::CRaceGoal::NextRaceWaypoint), 4, 1},
        {0x00554320, AddressOf(&CGoal::GotCounterObject), 0, 0},
        {0x00554390, AddressOf(&CGoal::CounterGoalDone), 0, 1},
        {0x00554470, AddressOf(&CGoal::ResetCurrentTime), 0, 0},
        {0x00554480, AddressOf(&CGoal::UpdateCurrentTime), 0, 0},
        {0x005556D0, AddressOf(&CGoal::SetStartHeight), 0, 0},
        {0x00555710, AddressOf(&CGoal::CheckHeightRecord), 0, 1},
        {0x00555800, AddressOf(&CGoal::CheckDistanceRecord), 0, 1},
        {0x00554590, AddressOf(&CGoal::EndOfRunStarted), 0, 1},
        {0x00555970, AddressOf(&CGoal::EndOfRunDone), 4, 0},
        {0x005559E0, AddressOf(&CGoal::RunDone), 0, 1},
        {0x00555A20, AddressOf(&CGoal::RunStarted), 0, 1},
        {0x00557AC0, AddressOf(&CGoal::AwardSpecialTrick), 0, 1},
        {0x00555A60, AddressOf(&CGoal::RemoveRewardTrick), 0, 0},
        {0x00551880, AddressOf(&Game::CCompetitionGoal::PauseCompetition), 0, 0},
        {0x00551890, AddressOf(&Game::CCompetitionGoal::UnPauseCompetition), 0, 0},
        {0x0055F960, AddressOf(&Game::CGoalPed::PlayGoalStartStream), 4, 0},
        {0x0055FDF0, AddressOf(&Game::CGoalPed::PlayGoalWaitStream), 0, 0},
        {0x0055FA60, AddressOf(&Game::CGoalPed::PlayGoalWinStream), 4, 0},
        {0x0055F210, AddressOf(&Game::CGoalPed::StopCurrentStream), 0, 0},
        {0x00553F30, AddressOf(&CGoal::AddTime), 4, 0},
        {0x005549A0, AddressOf(&CGoal::GetNumberCollected), 0, 5},
        {0x005549D0, AddressOf(&CGoal::GetNumFlagsToWin), 0, 6},
        {0x00554070, AddressOf(&CGoal::ResetGoalFlags), 0, 0},
        {0x00555D40, AddressOf(&CGoal::ColorTrickObjects), 8, 0},
        {0x00554A00, AddressOf(&CGoal::GetTimesStarted), 0, 5},
        {0x00554A10, AddressOf(&CGoal::SetEndRunType), 4, 0},
        {0x00554A20, AddressOf(&CGoal::SetDeactivateOnExpire), 4, 0},
        {0x00556050, AddressOf(&CGoal::GetRewardPoints), 0, 5},
        {0x0056F5B0, AddressOf(&Game::CTetrisGoal::ClearTricksAndMenu), 0, 0},
        {0x00552AF0, AddressOf(&Game::CFilmGoal::Checkpoint), 0, 0},
        {0x00574960, AddressOf(&Game::CTrickBeatGoal::SetBeatPattern), 8, 0},
        {0x00574C20, AddressOf(&Game::CTrickBeatGoal::ResetClock), 0, 0},
        {0x0055F420, AddressOf(&Game::CGoalPed::SuspendGoalLogic), 4, 0},
        {0x0055F460, AddressOf(&Game::CGoalPed::UnloadLastFam), 0, 0},
        {0x0055F1B0, AddressOf(&Game::CGoalPed::StopLastStream), 0, 0},
        {0x005517C0, AddressOf(&Game::CCompetitionGoal::Construct), 4, 7},
        {0x00560AC0, AddressOf(&Game::CNetGoal::Construct), 4, 7},
        {0x00560E60, AddressOf(&Game::CRaceGoal::Construct), 4, 7},
        {0x0056F090, AddressOf(&Game::CTetrisGoal::Construct), 4, 7},
        {0x005608C0, AddressOf(&Game::CHorseGoal::Construct), 4, 7},
        {0x00552B30, AddressOf(&Game::CFindGapsGoal::Construct), 4, 7},
        {0x005527D0, AddressOf(&Game::CFilmGoal::Construct), 4, 7},
        {0x005763A0, AddressOf(&Game::CTrickObjectsGoal::Construct), 4, 7},
        {0x005691F0, AddressOf(&Game::CSkateLineGoal::Construct), 4, 7},
        {0x005710B0, AddressOf(&Game::CTimedScoreRunGoal::Construct), 4, 7},
        {0x00574C40, AddressOf(&Game::CTrickBeatGoal::Construct), 4, 7},
        {0x00556FE0, AddressOf(&CGoal::Construct), 4, 7},
        {0x00554B40, AddressOf(&CGoal::AddChild), 4, 0},
        {0x005542B0, AddressOf(&CGoal::IsEditedGoal), 0, 1},
        {0x00554300, AddressOf(&CGoal::IsEditedGoal2), 0, 1},
        {0x00554A40, AddressOf(&CGoal::IsHorse), 0, 8},
        {0x00554A70, AddressOf(&CGoal::RequiresCombo), 0, 1},
        {0x005609B0, AddressOf(&Game::CHorseGoal::Land), 0, 0},
        {0x00569220, AddressOf(&Game::CSkateLineGoal::Land), 0, 0},
        {0x00576400, AddressOf(&Game::CTrickObjectsGoal::CheckScore), 0, 0},
        {0x005761D0, AddressOf(&Game::CTrickBeatGoal::RemoveLandedTricks), 0, 0},
        {0x00574B90, AddressOf(&Game::CTrickBeatGoal::ClearLanded), 0, 0},
        {0x00561B40, 0, 0, 5}, // Score::GetTotalScore
        {0x00553F20, AddressOf(&CGoal::GetTimeLeft), 0, 9},
        {0x005543E0, AddressOf(&CGoal::State02), 0, 8},
        {0x00574AB0, AddressOf(&Game::CTrickBeatGoal::TrickLanded), 4, 0},
        {0x0055F3D0, AddressOf(&Game::CGoalPed::DestroyGoalPed), 0, 0},
        {0x0055F7A0, AddressOf(&Game::CGoalPed::PlayStream), 16, 0},
        {0x0055FE10, AddressOf(&Game::CGoalPed::PlayGoalStream), 8, 0},
        {0x0055F5B0, AddressOf(&Game::CGoalPed::Hide), 4, 0},
    };
    for (const Method& m : kMethods) {
        if (!peds && m.address >= 0x0055EB40 && m.address < 0x005600D0)
            continue;
        Method method = m;
        auto handler = [method](Regs& r) {
            Record({method.address & 0xFFFFF, r.ecx});
            for (uint32_t i = 0; i < method.pop / 4u; ++i) {
                uint32_t value = r.args[i];
                if (method.address == 0x00554BD0 && i == 0)
                    value &= 0xFF;
                if (method.address == 0x0055FE10 && i == 0)
                    value = Hash(reinterpret_cast<const char*>(value));
                if (method.address == 0x0055F5B0 || method.address == 0x0055F420 || method.address == 0x00554A20 ||
                    (method.address == 0x00555D40 && i == 1))
                    value &= 0xFF;
                g_trace.push_back(value);
            }
            int goal = GoalIndex(r.ecx);
            if (method.kind == 7) { // (a goal type's constructor: the new goal made usable)
                auto* made = reinterpret_cast<CGoal*>(r.ecx);
                made->m_node.vtable = g_vt;
                made->m_params = reinterpret_cast<CStruct*>(g_newGoalParams);
                static GoalLink links[2];
                std::memset(links, 0, sizeof(links));
                made->m_children = &links[0];
                made->m_parents = &links[1];
                made->m_ped = reinterpret_cast<Game::CGoalPed*>(g_peds[0]);
                made->m_node.data = made;
                made->m_node.next = &made->m_node;
                made->m_node.prev = &made->m_node;
                return r.ecx;
            }
            // (a graffiti goal's cluster now and then takes the table's last goal away: the manager's loops that
            // read the count once walk on)
            if (method.address == 0x005552D0 && (Mix(0x52D0, r.args[0]) & 3) == 0 && Mgr()->m_goals.m_count > 1)
                --Mgr()->m_goals.m_count;
            switch (method.kind) {
            case 1: return Mix(method.address, r.ecx ^ (method.pop ? r.args[0] : 0)) & 1;
            case 2: return goal >= 0 ? Address(g_goalParams[goal]) : *reinterpret_cast<uint32_t*>(r.ecx + 0x1C);
            case 3: return goal >= 0 ? kIds[goal] : 0u;
            case 4: return g_rootId;
            case 5: return Mix(method.address, r.ecx) % 5;
            case 8: return (Mix(method.address, r.ecx) & 7) == 0 ? 1u : 0u; // (a rare yes)
            case 9: { // a time left: none, a minute boundary, any
                uint32_t pick = Mix(method.address, r.ecx) % 5;
                return pick == 0 ? 0u : pick == 1 ? 60000u : pick == 2 ? 0xFFFFFF00u : Mix(r.ecx, 9) % 400000;
            }
            case 6: return Mix(method.address, r.ecx) % 4 == 0 ? 0xFFFFFFFFu : Mix(method.address, r.ecx) % 6;
            default: return 0u;
            }
        };
        if (method.ours)
            s.At({method.address, method.ours}, method.pop, handler);
        else
            s.At({method.address}, method.pop, handler);
    }
    // HasProsetPrefix (a string).
    s.At({0x005543F0, AddressOf(&CGoal::HasProsetPrefix)}, 4, [](Regs& r) {
        Record({0x543F0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return Mix(0x543F0, r.ecx) & 1;
    });
    // ContainsFlag by name (special_gap).
    using HasName = bool (CStruct::*)(const char*);
    s.At({0x00476BA0, AddressOf(static_cast<HasName>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        uint32_t name = Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0]));
        Record({0x6BA0, r.ecx, name});
        return g_hasFlag.count({r.ecx, name}) ? 1u : 0u;
    });
    // The structure's components (the saved goals): walked over g_components.
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Record({0x6490, r.ecx, r.args[0]});
        // (structures with fake component lists: GetViewGoalsInfo's pros and their goals)
        auto list = g_fakeLists.find(r.ecx);
        if (list != g_fakeLists.end()) {
            const std::vector<uint32_t>& items = list->second;
            if (!r.args[0])
                return items.empty() ? 0u : items[0];
            for (size_t k = 0; k + 1 < items.size(); ++k)
                if (items[k] == r.args[0])
                    return items[k + 1];
            return 0u;
        }
        if (r.ecx != Address(g_goalData))
            return 0u;
        if (!r.args[0])
            return Address(&g_components[0]);
        auto* c = reinterpret_cast<Script::CComponent*>(r.args[0]);
        int i = static_cast<int>(c - g_components);
        return i + 1 < static_cast<int>(sizeof(g_components) / sizeof(g_components[0])) ? Address(&g_components[i + 1]) : 0u;
    });
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, r.ecx, r.args[0]});
        auto* array = reinterpret_cast<CArray*>(r.ecx);
        if (array->m_size == 0) { // (an array the SetSizeAndType stand-in left empty: what SetElement was given)
            auto it = g_arrayElements.find({r.ecx, r.args[0]});
            return it == g_arrayElements.end() ? 0u : it->second;
        }
        return array->m_size == 1 ? array->m_single : array->m_elements[r.args[0]];
    });
    s.At({0x005A32A0, AddressOf(&CFuncs::CreateFromNodeName)}, 0, [](Regs& r) {
        Record({0x32A0A, r.args[0]});
        return 1u;
    });
    s.At({0x005BAB90, AddressOf(&Stubs::IgnoreArgument)}, 4, [](Regs& r) {
        Record({0xAB90, r.args[0]});
        return 0u;
    });
    // The skate module, the skater, the career, the game mode, the network.
    s.At({0x00564D90, AddressOf(&Mdl::Skate::GetGoalManager)}, 0, [](Regs& r) {
        Record({0x4D90, r.ecx});
        return Address(g_mgr);
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return Address(g_skater);
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx});
        return Address(g_score);
    });
    if (peds)
    s.At({0x0055F270, AddressOf(&Game::CGoalPed::GetGoalAnimations)}, 4, [](Regs& r) { // (a type of stream)
        uint32_t type = r.args[0] ? Hash(reinterpret_cast<const char*>(r.args[0])) : 0;
        Record({0x5F270, r.ecx, type});
        return Mix(0x5F270, r.ecx ^ type) & 1 ? 0x7A000000u + (type & 0xFF) : 0u;
    });
    using GlobalStructure = CStruct* (*)(uint32_t);
    s.At({0x00479020, AddressOf(static_cast<GlobalStructure>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        if (r.args[0] == kTricks[3])
            return 0u; // (a trick with no structure)
        return (r.args[0] & 0xFFFF0000) == 0x7A1C0000 ? Address(g_trickStructure) : Address(g_levelStructure);
    });
    using GlobalArray = CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GlobalArray>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070A, r.args[0]});
        return Address(&g_extraArray);
    });
    using GlobalInteger = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GlobalInteger>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 17u;
    });
    s.At({0x00476620}, 12, [](Regs& r) { // (the other copy of GetString)
        Record({0x6590, r.ecx, r.args[0], r.args[2] & 0xFF});
        return Answer(r, 0x6590) ? 1u : 0u;
    });
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return Address(g_profile);
    });
    s.At({0x0057B7E0, AddressOf(&Obj::CPlayerProfileManager::GetTemplate)}, 4, [](Regs& r) {
        Record({0xB7E0, r.ecx, r.args[0]});
        return Address(g_profile) + 0x10;
    });
    s.At({0x00586110, AddressOf(&Obj::CSkaterProfile::GetInfoStructure)}, 4, [](Regs& r) {
        Record({0x6110, r.ecx, r.args[0]});
        return Address(g_mapping);
    });
    s.At({0x005860A0, AddressOf(&Obj::CSkaterProfile::GetMaxSpecials)}, 0, [](Regs& r) {
        Record({0x60A0, r.ecx});
        return g_maxSpecials;
    });
    s.At({0x00586630, AddressOf(&Obj::CSkaterProfile::GetSpecialTrick)}, 8, [](Regs& r) {
        Record({0x6630A, r.ecx, r.args[1]});
        auto* out = reinterpret_cast<Obj::CSkaterProfile::SpecialTrick*>(r.args[0]);
        out->trick = g_specials[r.args[1] & 3].trick;
        out->keys = g_specials[r.args[1] & 3].keys;
        out->flag = g_specials[r.args[1] & 3].flag;
        return r.args[0];
    });
    s.At({0x005A11B0, AddressOf(&CFuncs::CFunc_InMultiplayerGame)}, 0, [](Regs& r) {
        Record({0x11B0, r.args[0], r.args[1]});
        return g_multiplayer ? 1u : 0u;
    });
    s.At({0x00538D70, AddressOf(&GameNet::CFunc_IsHost)}, 0, [](Regs& r) {
        Record({0x8D70, r.args[0], r.args[1]});
        return 1u;
    });
    using GlobalString = const char* (*)(uint32_t);
    s.At({0x00478FD0, AddressOf(static_cast<GlobalString>(&Script::GetString))}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        return Address("Judge Name");
    });
    s.At({0x00473FA0, AddressOf(&Script::KillSpawnedScriptsThatReferTo)}, 0, [](Regs& r) {
        Record({0x3FA0, r.args[0]});
        return 0u;
    });
    using AddSumName = void (CStruct::*)(const char*, uint32_t);
    s.At({0x00477DF0, AddressOf(static_cast<AddSumName>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7DF0, r.ecx, Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00554570, AddressOf(&CGoal::GetViewGoalsText)}, 4, [](Regs& r) {
        Record({0x4570, r.ecx});
        int goal = GoalIndex(r.ecx);
        if (goal >= 0 && g_viewText[goal])
            *reinterpret_cast<const char**>(r.args[0]) = g_viewText[goal];
        return goal >= 0 && g_viewText[goal] ? 1u : 0u;
    });
    using Build = bool (*)(CArray*, CArray*, CArray*, int);
    s.At({0x00555B50, AddressOf(static_cast<Build>(&CGoal::BuildTrickTexts))}, 0, [](Regs& r) {
        Record({0x5B50, r.args[0], r.args[1], r.args[2], r.args[3]});
        return Mix(0x5B50, r.args[0] ^ r.args[3]) & 1;
    });
    using Tokens = void (*)(const char*, char*, CArray*, CArray*, int);
    s.At({0x00554890, AddressOf(static_cast<Tokens>(&CGoal::ReplaceTrickTokens))}, 0, [](Regs& r) {
        // (the output is the caller's buffer - maybe on its stack: written, not recorded)
        Record({0x4890, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[2], r.args[3], r.args[4]});
        std::memcpy(reinterpret_cast<char*>(r.args[1]), "replaced text", 14);
        return 0u;
    });
    // sprintf (the game's C runtime): formatted with ours, not recorded (ours formats with its own).
    s.At({0x006234FE}, 0, [](Regs& r) {
        std::vsnprintf(reinterpret_cast<char*>(r.args[0]), 128, reinterpret_cast<const char*>(r.args[1]),
                       reinterpret_cast<va_list>(&r.args[2]));
        return 0u;
    });
    using GetElementMethod = Front::ElementPtr* (Front::CScreenElementManager::*)(Front::ElementPtr*, uint32_t, int);
    s.At({0x004AAE20, AddressOf(static_cast<GetElementMethod>(&Front::CScreenElementManager::GetElement))}, 12,
         [](Regs& r) {
             Record({0xAE20, r.ecx, r.args[1], r.args[2]});
             reinterpret_cast<uint32_t*>(r.args[0])[0] = g_noTimeElement ? 0u : Address(g_timeElement);
             return r.args[0];
         });
    s.At({0x004AF8C0, AddressOf(&Front::CTextElement::SetText)}, 4, [](Regs& r) {
        Record({0xF8C0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, r.ecx});
        return 0u;
    });
    s.At({0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, [](Regs& r) {
        Record({0x7320, r.ecx});
        return 0x91A70000u;
    });
    s.At({0x00553220, AddressOf(&Game::CGameMode::IsTrue)}, 4, [](Regs& r) {
        Record({0x3220, r.ecx, r.args[0]});
        return Mix(0x3220, r.args[0]) & 1;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        return r.args[0] == 0x7149B753 ? Address(g_animBlender) : Address(g_trickComponent);
    });
    // The goal peds' world: objects, streams, assets, the profile.
    s.At({0x00623B10}, 0, [](Regs& r) { // strstr (the game's C runtime): ours, not recorded
        return Address(std::strstr(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x00450500, AddressOf(static_cast<bool(__cdecl*)(uint32_t)>(Pcm::StreamExists))}, 0, [](Regs& r) {
        Record({0x0500, r.args[0]});
        return Mix(g_streamSalt, r.args[0]) % 3 == 0 ? 1u : 0u;
    });
    s.At({0x004506D0, AddressOf(static_cast<void(__cdecl*)(uint32_t)>(Pcm::StopStream))}, 0, [](Regs& r) {
        Record({0x06D0, r.args[0]});
        return 0u;
    });
    s.At({0x00451A50, AddressOf(&Pcm::PreLoadStream)}, 0, [](Regs& r) {
        Record({0x1A50, r.args[0], r.args[1]});
        return 0x51000000u + (r.args[0] & 0xFFF);
    });
    s.At({0x00474350, AddressOf(&Script::StopScriptsUsingObjectThatReferTo)}, 0, [](Regs& r) {
        Record({0x4350, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return g_noObject ? 0u : Address(g_object);
    });
    s.At({0x0045D990, AddressOf(&Obj::CCompositeObject::Hide)}, 4, [](Regs& r) {
        Record({0xD990, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00419AB0, AddressOf(&Obj::CAnimBlenderComponent::GetAnimSet)}, 0, [](Regs& r) {
        Record({0x9AB0, r.ecx});
        return 0xA5E70000u;
    });
    s.At({0x00407B90, AddressOf(&Ass::CAssMan::GetAsset)}, 8, [](Regs& r) {
        Record({0x7B90, r.ecx, r.args[0], r.args[1]});
        return Mix(0x7B90, r.args[0]) & 1 ? 0xA5A00000u : 0u;
    });
    s.At({0x00407B20, AddressOf(&Ass::CAssMan::UnloadAsset)}, 4, [](Regs& r) {
        Record({0x7B20, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00408870, AddressOf(&Ass::CAssMan::LoadAnim)}, 20, [](Regs& r) {
        Record({0x8870, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3] & 0xFF,
                r.args[4] & 0xFF});
        return Mix(0x8870, r.args[1]) & 1;
    });
    s.At({0x00407A70, AddressOf(&Ass::CAssMan::SetUnknown18)}, 4, [](Regs& r) {
        Record({0x7A70, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.ecx, r.args[0], r.args[1]});
        return Mix(0x7C00, r.args[0]) & 1 ? 0xA5D00000u : 0u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, r.ecx, r.args[0] - Address(g_fakeScript)});
        return 0u;
    });
    s.At({0x00586210, AddressOf(&Obj::CSkaterProfile::GetDisplayName)}, 0, [](Regs& r) {
        Record({0x6210, r.ecx});
        static const char* const kNames[] = {"Tony Hawk", "Bob", "Rodney Mullen", " Lead"};
        return Address(kNames[Mix(0x6210, g_salt) % 4]);
    });
    s.At({0x00586170, AddressOf(&Obj::CSkaterProfile::IsPro)}, 0, [](Regs& r) {
        Record({0x6170, r.ecx});
        return Mix(0x6170, g_salt) & 1;
    });
    // The objects' vtables: slot 6 (MarkAsDead) on objects, slot 12 (CallMemberFunction) on components.
    for (uint32_t slot = 0; slot < 16; ++slot) {
        g_objectVtable[slot] = s.Thunk(slot == 6 ? 0 : 0, [slot](Regs& r) {
            Record({0x0B000000u | slot, r.ecx});
            return 0u;
        });
        uint16_t pop = slot == 12 ? 12 : 0;
        g_componentVtable[slot] = s.Thunk(pop, [slot](Regs& r) {
            Record({0x0C000000u | slot, r.ecx, r.args[0], r.args[1], r.args[2]});
            return 1u;
        });
    }
    s.At({0x00584130, AddressOf(&Obj::CSkaterCareer::GetLevel)}, 0, [](Regs& r) {
        Record({0x4130, r.ecx});
        return g_level;
    });
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Record({0x32A0, r.ecx});
        return g_modeName;
    });
    s.At({0x005532D0, AddressOf(&Game::CGameMode::GetGoalsMode)}, 0, [](Regs& r) {
        Record({0x32D0, r.ecx});
        return g_goalsMode;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx});
        return g_inNetGame ? 1u : 0u;
    });
    s.At({0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.ecx, r.args[0]});
        return Address(g_client);
    });
    s.At({0x00533070, AddressOf(&GameNet::Manager::GetGameId)}, 0, [](Regs& r) {
        Record({0x3070, r.ecx});
        return 0xABCDEF00u | g_gameId;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
        Record({0x9E50, r.ecx, desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_priority),
                static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_singular, static_cast<uint32_t>(desc->m_delay),
                desc->m_forcedSequenceId, desc->m_compression});
        for (uint32_t k = 0; k < desc->m_length / 4u; ++k) // (0x6F's game id word: its padding is stack garbage)
            g_trace.push_back(reinterpret_cast<uint32_t*>(desc->m_data)[k] & (k == 0 && desc->m_id == 0x6F ? 0xFFu : ~0u));
        return 0u;
    });
}

int g_tableCount = 1; // (the goals MakeState put in the table)
// A goal id in the table - or, with `allowMissing`, any (one not in the table now and then).
uint32_t RandomId(bool allowMissing)
{
    if (allowMissing && Next() % 6 == 0)
        return Coin() ? kMissing : kIds[Next() % kGoals];
    return g_nodes[Next() % static_cast<uint32_t>(g_tableCount)].key;
}

// The state from the seed: the table of six goals, their links (a parent is always an earlier goal), the active
// list, the saved goals, the answers.
void MakeState(uint32_t seed)
{
    g_seed = seed * 2654435761u + 4242;
    g_salt = Next();
    g_arrayElements.clear();
    std::memset(g_mgr, 0, sizeof(g_mgr));
    for (uint32_t k = 0; k < sizeof(g_mgr); ++k)
        g_mgr[k] = static_cast<uint8_t>(Next());
    CGoalManager* m = Mgr();
    // The table (in a random order).
    int order[kGoals] = {0, 1, 2, 3, 4, 5};
    for (int i = kGoals - 1; i > 0; --i)
        std::swap(order[i], order[Next() % (i + 1)]);
    int count = 3 + static_cast<int>(Next() % 4);
    for (int k = 0; k < kGoals; ++k) {
        g_nodes[k] = {kIds[order[k]], g_goals[order[k]], k + 1 < count ? &g_nodes[k + 1] : nullptr};
    }
    m->m_goals.m_count = count;
    g_tableCount = count;
    m->m_goals.m_first = &g_nodes[0];
    m->m_goals.m_last = &g_nodes[count - 1];
    m->m_goals.m_cursor = Coin() ? &g_nodes[0] : nullptr;
    m->m_goals.m_cursorIndex = 0;
    bool inTable[kGoals] = {};
    for (int k = 0; k < count; ++k)
        inTable[order[k]] = true;
    // The goals.
    for (int i = 0; i < kGoals; ++i) {
        for (uint32_t k = 0; k < sizeof(g_goals[i]); ++k)
            g_goals[i][k] = static_cast<uint8_t>(Next());
        CGoal* goal = Goal(i);
        goal->m_node.vtable = g_vt;
        goal->m_node.data = goal;
        goal->m_node.priority = static_cast<int>(Next() % 3);
        goal->m_node.next = &goal->m_node;
        goal->m_node.prev = &goal->m_node;
        goal->m_ped = reinterpret_cast<Game::CGoalPed*>(g_peds[i]);
        goal->m_params = reinterpret_cast<CStruct*>(g_goalParams[i]);
        std::memset(g_childLinks[i], 0, sizeof(g_childLinks[i]));
        std::memset(g_parentLinks[i], 0, sizeof(g_parentLinks[i]));
        goal->m_children = &g_childLinks[i][0];
        goal->m_parents = &g_parentLinks[i][0];
    }
    // Links: each goal in the table may have a parent earlier in the table (its first parent link), and is then
    // its child (in the parent's children, maybe already linked back).
    for (int k = 1; k < count; ++k) {
        if (Next() % 3 == 0)
            continue;
        int child = order[k];
        int parent = order[Next() % k];
        GoalLink* links = g_childLinks[parent];
        int used = 0;
        while (used < 2 && links[used].m_goalId)
            ++used;
        if (used == 2)
            continue;
        links[used].m_goalId = kIds[child];
        if (used)
            links[used - 1].m_next = &links[used];
        if (Coin()) { // (the child already knows its parent)
            g_parentLinks[child][0].m_goalId = kIds[parent];
            if (Coin()) {
                g_parentLinks[child][1].m_goalId = 0x7777;
                g_parentLinks[child][0].m_next = &g_parentLinks[child][1];
            }
        }
    }
    // The active list: some of the goals, linked for real.
    Mem::ListNode& head = m->m_activeGoals.m_node;
    head.vtable = reinterpret_cast<void**>(0x0064DF78);
    head.data = reinterpret_cast<void*>(-1);
    head.priority = 0;
    head.next = &head;
    head.prev = &head;
    Mem::ListNode* last = &head;
    for (int i = 0; i < kGoals; ++i) {
        if (!Coin())
            continue;
        Mem::ListNode& node = Goal(i)->m_node;
        node.prev = last;
        node.next = &head;
        last->next = &node;
        head.prev = &node;
        last = &node;
    }
    m->m_goalData = reinterpret_cast<CStruct*>(g_goalData);
    m->m_params = reinterpret_cast<CStruct*>(g_mgrParams);
    m->m_lastGoal = Coin() ? kIds[Next() % kGoals] : 0;
    for (int& unlocked : m->m_groupUnlocked)
        unlocked = static_cast<int>(Next() % 3 == 0);
    // The answers.
    g_getAnswers.clear();
    g_typedAnswers.clear();
    g_hasFlag.clear();
    g_hasComponent.clear();
    g_emptyAnswer = Coin();
    for (int i = 0; i < kGoals; ++i) {
        uint32_t params = Address(g_goalParams[i]);
        for (uint32_t flag : {0x981D3AD0u, 0x4E863E93u, 0x8E6014F6u, 0xEFC35592u, 0x1BC2B4D7u})
            if (Next() % 3 == 0)
                g_hasFlag.insert({params, flag});
        if (Coin())
            g_hasFlag.insert({params, Crc::GenerateCRCFromString("special_gap")});
        // create_geometry
        uint32_t size = Next() % 4;
        for (uint32_t& element : g_geometryElements[i])
            element = 0x6E000000 + Next() % 0x1000;
        std::memset(&g_geometry[i], 0, sizeof(g_geometry[i]));
        g_geometry[i].m_size = size;
        if (size == 1)
            g_geometry[i].m_single = g_geometryElements[i][0];
        else
            g_geometry[i].m_elements = g_geometryElements[i];
        if (Next() % 4)
            g_getAnswers[{params, 0xF4EB5C60}] = Address(&g_geometry[i]);
        // the saved goals: by id in the saved data (hasBeaten / isLocked)
        if (Coin())
            g_getAnswers[{Address(g_goalData), kIds[i]}] = Address(g_saved[i]);
        for (uint32_t name : {0x49807745u, 0xD3E93882u})
            if (Coin())
                g_getAnswers[{Address(g_saved[i]), name}] = Next() % 3;
    }
    g_getAnswers[{Address(g_goalData), kMissing}] = Address(g_saved[kGoals]);
    g_getAnswers[{Address(g_saved[kGoals]), 0x49807745}] = Next() % 2;
    g_getAnswers[{Address(g_saved[kGoals]), 0xD3E93882}] = Next() % 2;
    // The manager's difficulty levels by game mode.
    for (uint32_t mode : {0x4DA4937Bu, 0x4C10DE52u, 0x1234u})
        if (Next() % 4)
            g_getAnswers[{Address(g_mgrParams), mode}] = Next() % 5 - (Next() % 6 == 0 ? 3 : 0);
    // The saved data's components: goals (some not in this level), GoalManager_Params, an unknown one.
    for (int i = 0; i < kGoals + 3; ++i) {
        Script::CComponent& c = g_components[i];
        std::memset(&c, 0, sizeof(c));
        c.name = i < kGoals ? kIds[i] : i == kGoals ? 0x23D4170Au : 0x5555 + i;
        c.structure = Next() % 5 ? reinterpret_cast<CStruct*>(g_saved[i % (kGoals + 1)]) : nullptr;
        if (i == kGoals)
            c.structure = reinterpret_cast<CStruct*>(g_saved[kGoals]);
        if (c.structure && Coin())
            g_hasFlag.insert({Address(c.structure), 0x0BADF1A6});
    }
    g_getAnswers[{Address(g_saved[kGoals]), 0xB13D98D3}] = 0x5D1F0000;
    // The skater's trick objects.
    *reinterpret_cast<int*>(g_score + 0x20) = static_cast<int>(Next() % 4);
    auto* clusters = reinterpret_cast<uint32_t*>(g_trickComponent + 0x13B4);
    for (int k = 0; k < 6; ++k)
        clusters[k] = 0xC1000000 + Next() % 50;
    static const int kCounts[] = {0, 1, 3, 6};
    clusters[512] = static_cast<uint32_t>(kCounts[Next() % 4]);
    g_inNetGame = Next() % 3 == 0;
    static const uint32_t kModes[] = {0x4C10DE52, 0xEC200EAA, 0x4DA4937B, 0x1234};
    g_modeName = kModes[Next() % 4];
    g_goalsMode = Coin() ? 0xE8E7DBAD : 0x4321;
    g_level = Next() % 3;
    g_gameId = Next() & 0xFF;
    g_rootId = kIds[Next() % kGoals];
    g_multiplayer = Next() % 3 == 0;
    static const char* const kTexts[] = {"Do a \\t1", "Get \\k2 points"};
    for (const char*& text : g_viewText)
        text = Coin() ? kTexts[Next() % 2] : nullptr;
    for (int i = 0; i <= kGoals; ++i)
        if (Coin())
            g_getAnswers[{Address(g_saved[i]), 0x1A4CDE06}] = Next() % 3;
    for (uint32_t flag : {0x4AF5D34Eu, 0x0D15EA00u, 0x25904450u, 0x4147922Bu, 0x9D65D0E7u, 0xC5EC08E6u, 0x7DBB41DDu,
                          0xEFC35592u, 0xF268C278u, 0xD3C2899Du, 0xD0DD047Bu, 0xC18FEF36u})
        g_hasFlag.erase({Address(g_newGoalParams), flag});
}

void RecordManager()
{
    for (uint32_t k = 0; k < sizeof(CGoalManager); k += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_mgr + k));
    for (const LookupTable::Node& node : g_nodes)
        Record({node.key, Address(node.next)});
    for (int i = 0; i < kGoals; ++i)
        Record({Address(Goal(i)->m_node.next), Address(Goal(i)->m_node.prev)});
}

template <typename R, typename... A>
R Call(bool original, uint32_t address, R (CGoalManager::*ours)(A...), A... args)
{
    CGoalManager* m = Mgr();
    return original ? Original<R(__thiscall*)(CGoalManager*, A...)>(address)(m, args...) : (m->*ours)(args...);
}

template <typename R>
uint32_t AsWord(R value)
{
    if constexpr (std::is_pointer_v<R>)
        return Address(value);
    else
        return static_cast<uint32_t>(value);
}

void TestGoalManager()
{
    Setup();
    auto* standIns = new StandIns;
    StandInManagerCalls(*standIns);
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x007CE470, 4);
    GameVariable<void*>(0x007CCE14) = g_net;
    GameVariable<void*>(0x007CE470) = g_mode;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        GameVariable<void*>(0x007CCE14) = g_net;
        GameVariable<void*>(0x007CE470) = g_mode;
        std::memset(g_nodes, 0, sizeof(g_nodes));
        std::memset(g_goals, 0, sizeof(g_goals));
        // The constructor, the list head's destructor.
        for (int k = 0; k < 2; ++k) {
            ResetMemory();
            std::memset(g_mgr, 0x6B, sizeof(g_mgr));
            Record({Address(o ? Original<CGoalManager*(__thiscall*)(void*)>(0x0055D9C0)(g_mgr) : Mgr()->Construct())});
            RecordManager();
            Game::GoalListHead& head = Mgr()->m_activeGoals;
            Record({Address(o ? Original<Game::GoalListHead*(__thiscall*)(void*, uint8_t)>(0x0055D960)(&head, static_cast<uint8_t>(k))
                              : head.Destroy(static_cast<uint8_t>(k)))});
            RecordManager();
        }
        for (uint32_t seed = 0; seed < 160; ++seed) {
            ResetMemory();
            MakeState(seed);
            Record({0xEEEE0000 | seed});
#define CALL(address, method, ...) Record({AsWord(Call(o, address, &CGoalManager::method, ##__VA_ARGS__))}); RecordManager()
#define CALLV(address, method, ...) Call(o, address, &CGoalManager::method, ##__VA_ARGS__); RecordManager()
            Record({o ? Original<bool(__thiscall*)(CGoalManager*)>(0x00557E30)(Mgr()) : Mgr()->CanStartGoal()});
            CALLV(0x00557E40, ShowGoalPoints);
            CALL(0x00557F90, GetGoal, RandomId(true), 1);
            CALL(0x00557FB0, GetGoalByIndex, static_cast<int>(Next() % static_cast<uint32_t>(Mgr()->m_goals.m_count)));
            CALL(0x00557FC0, ActivateGoal, RandomId(true), Coin());
            CALL(0x00558030, WinGoal, RandomId(true));
            CALL(0x00558030, WinGoal, RandomId(true));
            CALL(0x00558180, LoseGoal, RandomId(true));
            CALL(0x00558200, GetNumSelectedGoals);
            CALLV(0x00558250, DeactivateAllGoals, Coin());
            CALLV(0x005582B0, ActivateAllGoals);
            CALLV(0x00558320, UninitializeGoalTree, RandomId(false));
            CALLV(0x00558380, UninitializeAllGoals);
            CALLV(0x005583C0, InitializeAllGoals);
            CALLV(0x005584C0, InitializeAllSelectedGoals);
            CALLV(0x00558530, ActivateAllSelectedGoals);
            CALLV(0x00558580, DeselectAllGoals);
            CALLV(0x005585C0, PauseAllGoals);
            CALLV(0x00558600, UnPauseAllGoals);
            {
                bool exists = Coin();
                CALL(0x00558640, QuickStartGoal, RandomId(exists), exists);
            }
            CALL(0x005586A0, SetGoalFlag, RandomId(true), 0xF1A60000 + Next() % 4, static_cast<int>(Next() % 3), Next() % 3);
            CALL(0x005586E0, GotGap, 0x6A900000 + Next() % 9);
            CALL(0x00558730, GetGoalParams, RandomId(false));
            CALL(0x00558750, NextWaypoint, RandomId(false));
            CALLV(0x00558770, KillCluster, 0xC1000000 + Next() % 50, static_cast<int>(Next() % 9));
            CALLV(0x005587F0, CheckTrickGoals);
            CALLV(0x005588D0, UnlockGoal, RandomId(false));
            CALL(0x005588F0, HasBeatenGoal, RandomId(true));
            CALLV(0x00558960, ToggleGoalSelection, RandomId(false));
            {
                static const char* const kPrefixes[] = {"ProA", "prob"};
                CALL(0x005589A0, HasWonProsetGoal, kPrefixes[Next() % 2]);
            }
            CALLV(0x00558A00, RestoreGoalData);
            CALL(0x00558AB0, NumGoalsBeatenBy, static_cast<int>(Next() % 4));
            CALL(0x00558B10, NumGoalsNetBeatenBy, static_cast<int>(Next() % 4));
            CALL(0x00558B70, NumGoalsBeaten);
            CALLV(0x00558BC0, UnlockAllGoals);
            CALLV(0x00558C00, UnlockAllProGoals);
            {
                Params params;
                uint32_t p = Address(params.Struct());
                g_getAnswers[{p, 0xA1DC81F9}] = RandomId(false);
                if (Coin())
                    g_getAnswers[{p, 0x1A4E4FB9}] = 0x5EA00000 + Next() % 9;
                if (Coin())
                    g_getAnswers[{p, 0x7321A8D6}] = Address(Coin() ? "intro" : "wait");
                CALLV(0x00558C70, PlayGoalStream, params.Struct());
                for (uint32_t name : {0xA1DC81F9u, 0x1A4E4FB9u, 0x7321A8D6u})
                    g_getAnswers.erase({p, name});
            }
            CALL(0x00558D20, IsGoalLocked, RandomId(true));
            CALL(0x00558D90, GetActiveCompetition);
            CALLV(0x00558DF0, ReplaceTrickTextAll);
            CALL(0x00558E30, GoalExists, RandomId(true));
            CALLV(0x00558E50, CheckTrickObjects);
            CALLV(0x00558FD0, CreateGoalLevelObjects);
            CALL(0x00559050, GetDifficultyLevel, Coin());
            CALLV(0x005590C0, HideAllGoalPeds, Coin());
            {
                int group = Next() % 5 == 0 ? -1 : static_cast<int>(1 + Next() % 10);
                int level = Coin() ? -1 : static_cast<int>(Next() % 20);
                CALL(0x00559110, IsGroupLocked, group, level);
            }
            CALLV(0x00559150, TrickObjectHit, 0xC1000000 + Next() % 50);
            Record({Address(o ? Original<CGoalManager*(__cdecl*)()>(0x005591B0)() : Game::GetGoalManager())});
            {
                int active = 0;
                Mem::ListNode& head = Mgr()->m_activeGoals.m_node;
                for (Mem::ListNode* n = head.next; n != &head; n = n->next)
                    ++active;
                CALL(0x0055DBF0, GetActiveGoal, static_cast<int>(Next() % static_cast<uint32_t>(active ? active : 1)));
            }
            CALL(0x0055DC30, NumActiveGoals, Coin());
            {
                int index = static_cast<int>(Next() % 8);
                Record({Address(o ? Original<Mem::ListNode*(__thiscall*)(void*, int)>(0x0055D990)(&Mgr()->m_activeGoals, index)
                                  : Mgr()->m_activeGoals.NodeAt(index))});
            }
            {
                int i = static_cast<int>(Next() % kGoals);
                Mem::ListNode& head = Mgr()->m_activeGoals.m_node;
                bool inList = false;
                for (Mem::ListNode* n = head.next; n != &head; n = n->next)
                    inList |= n == &Goal(i)->m_node;
                if (!inList) {
                    Goal(i)->m_node.priority = static_cast<int>(Next() % 3);
                    CALLV(0x0055DBE0, AddActiveGoal, Goal(i));
                }
            }
            CALLV(0x00558ED0, BuildGoalTree);
            // The destructive ones last.
            switch (seed % 5) {
            case 0: CALLV(0x00557E70, ResetGoalData, 0u); break;
            case 1: CALLV(0x00557E70, ResetGoalData, 0x0BADF1A6u); break;
            case 2: CALL(0x0055BEA0, RemoveGoal, RandomId(false)); break;
            case 3: CALLV(0x0055BEF0, RemoveAllGoals); break;
            default: CALLV(0x0055DB50, DestroyBody); break;
            }
#undef CALL
#undef CALLV
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 50000, "goal manager: the original ran");
    CompareTraces(traces[0], traces[1], "goal manager (0x00557E30-0x005591BF, 0x0055B990-0x0055DD02)");
}

void RestoreGoalManager() { Restore(); }

// --- The goal manager's goals: made, restarted, saved, listed (0x0055BA10-0x0055C7D6) ------------------------------

void TestGoalManagerGoals()
{
    Setup();
    auto* standIns = new StandIns;
    StandInManagerCalls(*standIns);
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x007CE470, 4);
    static const uint32_t kTypeFlags[] = {0x4AF5D34E, 0x0D15EA00, 0x25904450, 0x4147922B, 0x9D65D0E7, 0xC5EC08E6,
                                          0x7DBB41DD, 0xEFC35592, 0xF268C278, 0xD3C2899D, 0xD0DD047B, 0};
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        GameVariable<void*>(0x007CCE14) = g_net;
        GameVariable<void*>(0x007CE470) = g_mode;
        std::memset(g_nodes, 0, sizeof(g_nodes));
        std::memset(g_goals, 0, sizeof(g_goals));
        for (uint32_t seed = 0; seed < 150; ++seed) {
            ResetMemory();
            MakeState(seed);
            Record({0xEEEE0000 | seed});
            // AddGoal: each type, the early outs, children.
            {
                Params params;
                uint32_t p = Address(params.Struct());
                uint32_t type = kTypeFlags[Next() % 12];
                if (type)
                    g_hasFlag.insert({p, type});
                for (uint32_t flag : {0x981D3AD0u, 0xC18FEF36u})
                    if (Next() % 3 == 0)
                        g_hasFlag.insert({p, flag});
                uint32_t n = Address(g_newGoalParams);
                g_getAnswers.erase({n, 0x5E684E45});
                g_getAnswers.erase({n, 0xDD4CABD6});
                static uint32_t children[3] = {0x2002, 0x2004, 0x2005};
                static CArray childArray;
                std::memset(&childArray, 0, sizeof(childArray));
                childArray.m_size = 1 + Next() % 3;
                if (childArray.m_size == 1)
                    childArray.m_single = children[0];
                else
                    childArray.m_elements = children;
                uint32_t kind = Next() % 3;
                if (kind == 1)
                    g_getAnswers[{n, 0x5E684E45}] = Address(&childArray);
                else if (kind == 2)
                    g_getAnswers[{n, 0xDD4CABD6}] = 0x2003;
                uint32_t id = 0x3000 + seed;
                Record({o ? Original<bool(__thiscall*)(CGoalManager*, uint32_t, CStruct*)>(0x0055BA10)(Mgr(), id, params.Struct())
                          : Mgr()->AddGoal(id, params.Struct())});
                RecordManager();
                for (uint32_t flag : kTypeFlags)
                    g_hasFlag.erase({p, flag});
            }
            Mgr()->m_lastGoal = Next() % 4 == 0 ? 0 : RandomId(false); // (one not in the table is read through null)
            static CArray keyCombos;
            static uint32_t keyElements[2] = {0x4B000001, 0x4B000002};
            std::memset(&keyCombos, 0, sizeof(keyCombos));
            keyCombos.m_size = 1 + Next() % 2;
            if (keyCombos.m_size == 1)
                keyCombos.m_single = keyElements[0];
            else
                keyCombos.m_elements = keyElements;
            for (int i = 0; i < kGoals; ++i)
                g_getAnswers[{Address(g_goalParams[i]), 0x79704516}] = Address(&keyCombos);
            if (o)
                Original<void(__thiscall*)(CGoalManager*)>(0x0055BF40)(Mgr());
            else
                Mgr()->RestartLastGoal();
            RecordManager();
            {
                int level = static_cast<int>(Next() % 3);
                Record({static_cast<uint32_t>(o ? Original<int(__thiscall*)(CGoalManager*, int, int)>(0x0055C250)(Mgr(), level, 7)
                                                : Mgr()->NumGoalsBeatenInLevel(level, 7))});
                RecordManager();
            }
            if (o)
                Original<void(__thiscall*)(CGoalManager*)>(0x0055C090)(Mgr());
            else
                Mgr()->LevelUnload();
            RecordManager();
            for (int k = 0; k < 2; ++k) {
                bool a = Coin(), b = Coin(), c = Coin();
                if (o)
                    Original<void(__thiscall*)(CGoalManager*, bool, bool, bool)>(0x0055C2F0)(Mgr(), a, b, c);
                else
                    Mgr()->AddGoalChoices(a, b, c);
                RecordManager();
            }
            {
                uint32_t id = RandomId(false);
                char out[64];
                std::memset(out, 0x5A, sizeof(out));
                static const char* const kTexts[] = {"Land a \\t1", "\\k1 and \\t2"};
                const char* text = kTexts[Next() % 2];
                int size = static_cast<int>(Next() % 100);
                Record({o ? Original<bool(__thiscall*)(CGoalManager*, uint32_t, const char*, char*, int)>(0x0055C4D0)(Mgr(), id, text, out, size)
                          : Mgr()->ReplaceTrickText(id, text, out, size)});
                RecordManager();
            }
            Record({Address(o ? Original<CStruct*(__thiscall*)(CGoalManager*)>(0x0055C6E0)(Mgr()) : Mgr()->GetGoalAttemptInfo())});
            RecordManager();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal manager goals: the original ran");
    CompareTraces(traces[0], traces[1], "goal manager goals (0x0055BA10-0x0055C7D6)");
}

// --- The goal manager's land, bail and view goals info (0x0055C7E0-0x0055D3E2) -----------------------------------

void TestGoalManagerEvents()
{
    Setup();
    auto* standIns = new StandIns;
    StandInManagerCalls(*standIns);
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x007CE470, 4);
    static const char* const kStrings[] = {"Ollie the bum", "Grind the rail", "Tip!"};
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        GameVariable<void*>(0x007CCE14) = g_net;
        GameVariable<void*>(0x007CE470) = g_mode;
        std::memset(g_nodes, 0, sizeof(g_nodes));
        std::memset(g_goals, 0, sizeof(g_goals));
        for (uint32_t seed = 0; seed < 200; ++seed) {
            ResetMemory();
            MakeState(seed);
            Record({0xEEEE0000 | seed});
            // The skater's score.
            *reinterpret_cast<int*>(g_score + 0x14) = static_cast<int>(Next() % 6);
            *reinterpret_cast<float*>(g_score + 0x60) = static_cast<float>(Next() % 7) * 0.5f;
            *reinterpret_cast<int*>(g_score + 0x20) = static_cast<int>(Next() % 4) - 1;
            for (int i = 0; i < kGoals; ++i) {
                uint32_t p = Address(g_goalParams[i]);
                // land's branches (one or none of the flags), the scores
                static const uint32_t kKinds[] = {0xC63432A7, 0x293B95A7, 0xEFC35592, 0xD0DD047B, 0xF268C278, 0};
                for (uint32_t flag : kKinds)
                    g_hasFlag.erase({p, flag});
                if (uint32_t kind = kKinds[Next() % 6])
                    g_hasFlag.insert({p, kind});
                if (Coin())
                    g_hasFlag.insert({p, 0x7100155D});
                if (Next() % 3 == 0)
                    g_hasComponent.insert({p, 0x655F4F3F});
                g_getAnswers[{p, 0xCD66C8AE}] = Next() % 8;
                g_getAnswers[{p, 0x655F4F3F}] = Next() % 8;
                if (Coin())
                    g_getAnswers[{p, 0x60B827D5}] = Next() % 2;
                // the view goals texts
                g_getAnswers[{p, 0x4BC5229D}] = Address(kStrings[Next() % 3]);
                g_typedAnswers.erase({p, 0x0C5D7E6B});
                uint32_t description = Next() % 3;
                if (description == 1)
                    g_typedAnswers[{p, 0x0C5D7E6B}] = {{0x6590, Address(kStrings[Next() % 3])}};
                else if (description == 2)
                    g_typedAnswers[{p, 0x0C5D7E6B}] = {{0x6860, 0x5E300000 + Next() % 8}};
                if (Coin())
                    g_getAnswers[{p, 0x63981D6F}] = Address(kStrings[2]);
                if (Coin())
                    g_getAnswers[{p, 0x22A555F7}] = 0x5E400000 + Next() % 8;
                Goal(i)->m_skater = 0x9E000000 + Next() % 3;
            }
            // GetViewGoalsInfo's own structures: the pros (its first new structure, at the start of the heap) and
            // their goals - fake lists.
            uint32_t byPro = Address(g_heap);
            g_fakeLists.clear();
            std::vector<uint32_t>& pros = g_fakeLists[byPro];
            uint32_t numPros = Next() % 4;
            for (uint32_t k = 0; k < numPros; ++k) {
                Script::CComponent& c = g_proComponents[k];
                std::memset(&c, 0, sizeof(c));
                c.name = 0x9E000000 + k;
                c.structure = reinterpret_cast<CStruct*>(g_proGoals[k]);
                pros.push_back(Address(&c));
                std::vector<uint32_t>& goals = g_fakeLists[Address(g_proGoals[k])];
                uint32_t numGoals = Next() % 3;
                for (uint32_t j = 0; j < numGoals; ++j) {
                    Script::CComponent& e = g_infoComponents[k * 2 + j];
                    std::memset(&e, 0, sizeof(e));
                    e.name = 0x2001 + j;
                    e.structure = reinterpret_cast<CStruct*>(g_goalInfos[k * 2 + j]);
                    goals.push_back(Address(&e));
                }
                g_getAnswers[{Address(g_proGoals[k]), 0x5607E8CC}] = numGoals;
            }
            for (uint32_t k = 0; k < 3; ++k) {
                if (Coin())
                    g_hasComponent.insert({byPro, 0x9E000000 + k});
                else
                    g_hasComponent.erase({byPro, 0x9E000000 + k});
                g_getAnswers[{byPro, 0x9E000000 + k}] = Address(g_proGoals[k]);
            }
            {
                uint32_t skater = Coin() ? 0 : 0x9E000000 + Next() % 3;
                Params out;
                if (o)
                    Original<void(__thiscall*)(CGoalManager*, CStruct*, uint32_t)>(0x0055CD70)(Mgr(), out.Struct(), skater);
                else
                    Mgr()->GetViewGoalsInfo(out.Struct(), skater);
                RecordManager();
            }
            g_fakeLists.clear();
            if (o)
                Original<void(__thiscall*)(CGoalManager*)>(0x0055C7E0)(Mgr());
            else
                Mgr()->Land();
            RecordManager();
            if (o)
                Original<void(__thiscall*)(CGoalManager*)>(0x0055CBB0)(Mgr());
            else
                Mgr()->Bail();
            RecordManager();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal manager events: the original ran");
    CompareTraces(traces[0], traces[1], "goal manager land / bail / view goals info (0x0055C7E0-0x0055D3E2)");
}

void TestGoalManagerCommands4();

// --- The goal manager's update, timer, unbeating, view goals list (0x0055DD10-0x0055EB3F) ------------------------

void TestGoalManagerUpdate()
{
    Setup();
    auto* standIns = new StandIns;
    StandInManagerCalls(*standIns);
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x007CE470, 4);
    SaveGlobal(0x00701440, 4);
    SaveGlobal(0x00690FA4, 32); // (FormatWithSeparators' buffer)
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        GameVariable<void*>(0x007CCE14) = g_net;
        GameVariable<void*>(0x007CE470) = g_mode;
        GameVariable<void*>(0x00701440) = g_screenManager;
        std::memset(g_nodes, 0, sizeof(g_nodes));
        std::memset(g_goals, 0, sizeof(g_goals));
        for (uint32_t seed = 0; seed < 200; ++seed) {
            ResetMemory();
            MakeState(seed);
            Record({0xEEEE0000 | seed});
            g_noTimeElement = Next() % 4 == 0;
            for (int i = 0; i < kGoals; ++i) {
                uint32_t p = Address(g_goalParams[i]);
                // the view goals list's places (1..), records, pro goals
                if (Next() % 3)
                    g_getAnswers[{p, 0x09B048FC}] = 1 + Next() % 4;
                else
                    g_getAnswers.erase({p, 0x09B048FC});
                if (g_getAnswers.count({p, 0x09B048FC}))
                    g_hasComponent.insert({p, 0x09B048FC});
                else
                    g_hasComponent.erase({p, 0x09B048FC});
                if (Coin())
                    g_hasFlag.insert({p, 0xD303A1A3});
                static const uint32_t kWins[] = {1, 2, 3, 4, 1234567, 0, 61234, 125990};
                g_getAnswers[{p, 0xC22A2B72}] = kWins[Next() % 8];
                static const uint32_t kTypes[] = {0xCD66C8AE, 0x906B67BA, 0x12345678};
                if (Next() % 4)
                    g_getAnswers[{p, 0x96963902}] = kTypes[Next() % 3];
                else
                    g_getAnswers.erase({p, 0x96963902});
                if (Coin())
                    g_getAnswers[{p, 0x52D4489E}] = 0x5E500000 + Next() % 4;
                else
                    g_getAnswers.erase({p, 0x52D4489E});
                if (Coin())
                    g_hasFlag.insert({p, 0xD0DD047B});
                else
                    g_hasFlag.erase({p, 0xD0DD047B});
            }
            Mgr()->m_allUnlocked = Coin();
            auto call = [&](uint32_t address, void (CGoalManager::*ours)()) {
                if (o)
                    Original<void(__thiscall*)(CGoalManager*)>(address)(Mgr());
                else
                    (Mgr()->*ours)();
                RecordManager();
            };
            call(0x0055DD10, &CGoalManager::UpdateTimeDisplay);
            Record({o ? Original<bool(__thiscall*)(CGoalManager*)>(0x0055DEA0)(Mgr()) : Mgr()->HasActiveGapGoal()});
            call(0x0055E050, &CGoalManager::AddViewGoalsList);
            {
                uint32_t trick = 0x7E000000 + Next() % 9;
                if (o)
                    Original<void(__thiscall*)(CGoalManager*, CStruct*)>(0x0055E790)(Mgr(), reinterpret_cast<CStruct*>(trick));
                else
                    Mgr()->TrickLanded(reinterpret_cast<CStruct*>(trick));
                RecordManager();
            }
            Record({Address(o ? Original<CGoal*(__thiscall*)(CGoalManager*)>(0x0055E980)(Mgr()) : Mgr()->GetActiveNetGoal())});
            call(0x0055DF90, &CGoalManager::UnBeatAllGoals);
            call(0x0055EA40, &CGoalManager::Update);
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal manager update: the original ran");
    CompareTraces(traces[0], traces[1], "goal manager update / timer / unbeat / view goals list (0x0055DD10-0x0055EB3F)");
}

void TestGoalManagerCommands5();

// --- The goals' pros (Game::CGoalPed, 0x0055EB40-0x005600CF) ------------------------------------------------------

uint32_t g_rndCalls;

void StandInPedCalls(StandIns& s)
{
    using GlobalArrayNamed = CArray* (*)(const char*);
    s.At({0x00479090, AddressOf(static_cast<GlobalArrayNamed>(&Script::GetArray))}, 0, [](Regs& r) {
        uint32_t name = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x9090, name});
        return Mix(0x9090, name) % 3 == 0 ? 0x5E600000u : 0u;
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        return Mix(0x3DE0, ++g_rndCalls) % r.args[0];
    });
}

void TestGoalPed()
{
    Setup();
    auto* standIns = new StandIns;
    StandInManagerCalls(*standIns, false);
    StandInPedCalls(*standIns);
    for (uint32_t address : {0x007CCE14u, 0x007CE470u, 0x00701440u, 0x006F0FB8u, 0x006918DCu})
        SaveGlobal(address, 4);
    uint32_t savedSpawn = g_spawnResult;
    static const char* const kPros[] = {"Tony", "Bob", "Rodney", "Lead"};
    static const char* const kGoalNames[] = {"Ber_goal_trick", "hangar_combo"};
    static const char* const kSpeakers[] = {"skater", "tony", "x"};
    static const char* const kTypes[] = {"midgoal", "wait", "intro", "Far"};
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        GameVariable<void*>(0x007CCE14) = g_net;
        GameVariable<void*>(0x007CE470) = g_mode;
        GameVariable<void*>(0x00701440) = g_screenManager;
        GameVariable<void*>(0x006F0FB8) = g_objectManager;
        GameVariable<void*>(0x006918DC) = g_assets;
        g_spawnResult = Address(g_fakeScript);
        std::memset(g_nodes, 0, sizeof(g_nodes));
        std::memset(g_goals, 0, sizeof(g_goals));
        for (uint32_t seed = 0; seed < 300; ++seed) {
            ResetMemory();
            MakeState(seed);
            g_rndCalls = 0;
            g_streamSalt = Next();
            g_noObject = Next() % 4 == 0;
            Record({0xEEEE0000 | seed});
            std::memset(g_object, 0, sizeof(g_object));
            std::memset(g_animBlender, 0, sizeof(g_animBlender));
            std::memset(g_fakeScript, 0, sizeof(g_fakeScript));
            std::memset(g_assets, 0, sizeof(g_assets));
            *reinterpret_cast<void***>(g_object) = g_objectVtable;
            *reinterpret_cast<void***>(g_animBlender) = g_componentVtable;
            *reinterpret_cast<uint32_t*>(g_assets + 0x18) = 0xA5180000 + Next() % 4;
            for (int i = 0; i < kGoals; ++i) {
                auto* ped = reinterpret_cast<Game::CGoalPed*>(g_peds[i]);
                ped->m_goalId = kIds[i];
                ped->m_suspended = Next() % 5 == 0;
                ped->m_fam = Coin() ? 0xFA000000 + Next() % 9 : 0;
                static const uint32_t kObjects[] = {0, 0x5B8AB877, 0x0B1EC700};
                ped->m_animObject = kObjects[Next() % 3];
                ped->m_stream = Coin() ? 0x51000000 + Next() % 9 : 0;
                uint32_t p = Address(g_goalParams[i]);
                for (uint32_t name : {0x944B2900u, 0x878CCC12u, 0x02D7E03Du, 0xDC14D454u})
                    g_getAnswers.erase({p, name});
                if (Next() % 5)
                    g_getAnswers[{p, 0x944B2900}] = Address(kPros[Next() % 4]);
                if (Coin())
                    g_getAnswers[{p, 0x878CCC12}] = Address("Hawk");
                g_getAnswers[{p, 0xBFECC45B}] = Address(kGoalNames[Next() % 2]);
                if (Next() % 3)
                    g_getAnswers[{p, 0x02D7E03D}] = Next() % 4 == 0 ? 0x5B8AB877 : 0x0B1EC700 + Next() % 3;
                if (Coin())
                    g_getAnswers[{p, 0xDC14D454}] = Next() % 4 == 0 ? 0 : 0x51000000 + Next() % 9;
                for (uint32_t flag : {0x3D1CAB0Bu, 0x102A8591u})
                    if (Next() % 4 == 0)
                        g_hasFlag.insert({p, flag});
            }
            g_getAnswers[{Address(g_profile) + 0x5C, 0x3F813177}] = Next() % 2;
            int i = GoalIndex(Address(g_nodes[Next() % static_cast<uint32_t>(g_tableCount)].item)); // (a goal in the table)
            auto* ped = reinterpret_cast<Game::CGoalPed*>(g_peds[i]);
            auto recordPed = [&] {
                for (int k = 0; k < kGoals; ++k)
                    for (uint32_t w = 0; w < sizeof(Game::CGoalPed); w += 4)
                        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_peds[k] + w));
                RecordManager();
            };
#define PED(R, address, ...) (o ? Original<R(__thiscall*)(Game::CGoalPed*, ##__VA_ARGS__)>(address) : nullptr)
            // The constructor, the name helpers.
            {
                alignas(16) uint8_t made[0x20];
                std::memset(made, 0x6B, sizeof(made));
                Params params;
                g_getAnswers[{Address(params.Struct()), 0x9982E501}] = 0x2002;
                auto* m = reinterpret_cast<Game::CGoalPed*>(made);
                Record({Address(o ? Original<Game::CGoalPed*(__thiscall*)(void*, CStruct*)>(0x0055EB40)(made, params.Struct())
                                  : m->Construct(params.Struct()))});
                for (uint32_t w = 0; w < 0x18; w += 4)
                    g_trace.push_back(*reinterpret_cast<uint32_t*>(made + w));
                static const char* const kNames[] = {"Tony Hawk", "Bob", " Lead", "Rodney Mullen Jr"};
                const char* name = kNames[seed % 4];
                char out[0x40];
                std::memset(out, 0x5A, sizeof(out));
                if (o)
                    Original<void(__cdecl*)(char*, const char**, int)>(0x0055EB70)(out, &name, 0x40);
                else
                    Game::CGoalPed::ParseFirstName(out, &name, 0x40);
                Record({Hash(out), Address(name) - Address(kNames[seed % 4])});
                char text[0x100] = "goal_text";
                bool add = Coin();
                if (o)
                    Original<void(__cdecl*)(bool, char*, int)>(0x0055EBC0)(add, text, 0x100);
                else
                    Game::CGoalPed::AddSuccessSuffix(add, text, 0x100);
                Record({Hash(text)});
            }
            Record({Hash(o ? Original<const char*(__thiscall*)(Game::CGoalPed*)>(0x0055EBF0)(ped) : ped->GetProFirstName())});
            recordPed();
            {
                uint32_t out = 0xDEAD0000;
                int index = static_cast<int>(Next() % 4) - 1;
                bool success = Coin(), noCam = Next() % 3 == 0;
                const char* suffix = Next() % 3 ? kSpeakers[Next() % 3] : nullptr;
                Record({o ? Original<bool(__thiscall*)(Game::CGoalPed*, uint32_t*, int, bool, const char*, bool)>(0x0055ED00)(
                                ped, &out, index, success, suffix, noCam)
                          : ped->GetStreamChecksum(&out, index, success, suffix, noCam),
                        out});
            }
            {
                char out[0x80];
                std::memset(out, 0x5A, sizeof(out));
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, char*, int)>(0x0055F170)(ped, out, 0x80);
                else
                    ped->GetPlayerFirstName(out, 0x80);
                Record({Hash(out)});
            }
            auto plain = [&](uint32_t address, void (Game::CGoalPed::*ours)()) {
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*)>(address)(ped);
                else
                    (ped->*ours)();
                recordPed();
            };
            plain(0x0055F1B0, &Game::CGoalPed::StopLastStream);
            plain(0x0055F210, &Game::CGoalPed::StopCurrentStream);
            {
                const char* type = Next() % 4 ? kTypes[Next() % 4] : nullptr;
                Record({o ? Original<uint32_t(__thiscall*)(Game::CGoalPed*, const char*)>(0x0055F270)(ped, type)
                          : ped->GetGoalAnimations(type)});
            }
            plain(0x0055F3D0, &Game::CGoalPed::DestroyGoalPed);
            {
                bool flag = Coin();
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, bool)>(0x0055F420)(ped, flag);
                else
                    ped->SuspendGoalLogic(flag);
                recordPed();
                ped->m_suspended = Next() % 5 == 0;
            }
            plain(0x0055F460, &Game::CGoalPed::UnloadLastFam);
            {
                bool flag = Coin();
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, bool)>(0x0055F5B0)(ped, flag);
                else
                    ped->Hide(flag);
                recordPed();
            }
            {
                uint32_t stream = 0x57000000 + Next() % 9, set = 0xA5E70000;
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, uint32_t, uint32_t)>(0x0055F710)(ped, stream, set);
                else
                    ped->LoadFam(stream, set);
                recordPed();
            }
            {
                uint32_t stream = 0x57000000 + Next() % 9;
                bool noAnim = Coin(), fam = Coin();
                static const uint32_t kObjects[] = {0, 0x5B8AB877, 0x0B1EC701};
                uint32_t object = kObjects[Next() % 3];
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, uint32_t, bool, uint32_t, bool)>(0x0055F7A0)(ped, stream, noAnim, object, fam);
                else
                    ped->PlayStream(stream, noAnim, object, fam);
                recordPed();
            }
            {
                Params params;
                uint32_t p = Address(params.Struct());
                if (Coin())
                    g_getAnswers[{p, 0x64EEE2E6}] = Next() % 4;
                if (Coin())
                    g_getAnswers[{p, 0x82CBE8D1}] = Address(kSpeakers[Next() % 3]);
                if (Coin())
                    g_hasFlag.insert({p, 0x0E7F5FF8});
                if (Coin())
                    g_getAnswers[{p, 0xF0C598FA}] = Coin() ? 0x5B8AB877 : 0x0B1EC702;
                for (uint32_t flag : {0x2BE2056Au, 0x9F38B332u})
                    if (Coin())
                        g_hasFlag.insert({p, flag});
                if (Next() % 4 == 0)
                    g_getAnswers[{p, 0x1A4E4FB9}] = 0x57100000 + Next() % 9;
                // (the start stream's slot holds garbage when no stream name can be made: always a pro here)
                g_getAnswers[{Address(g_goalParams[i]), 0x944B2900}] = Address(kPros[Next() % 4]);
                g_getAnswers[{Address(g_goalParams[i]), 0x878CCC12}] = Address("Hawk");
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, CStruct*)>(0x0055F960)(ped, params.Struct());
                else
                    ped->PlayGoalStartStream(params.Struct());
                recordPed();
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, CStruct*)>(0x0055FA60)(ped, params.Struct());
                else
                    ped->PlayGoalWinStream(params.Struct());
                recordPed();
                const char* type = kTypes[Next() % 4];
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, const char*, CStruct*)>(0x0055FE10)(ped, type, params.Struct());
                else
                    ped->PlayGoalStream(type, params.Struct());
                recordPed();
            }
            {
                const char* kind = kTypes[Next() % 4];
                int count = static_cast<int>(Next() % 6);
                bool byName = Coin(), noAnim = Coin();
                if (o)
                    Original<void(__thiscall*)(Game::CGoalPed*, const char*, int, bool, bool)>(0x0055FB00)(ped, kind, count, byName, noAnim);
                else
                    ped->PlayGoalProStream(kind, count, byName, noAnim);
                recordPed();
            }
            plain(0x0055FDF0, &Game::CGoalPed::PlayGoalWaitStream);
#undef PED
        }
        traces[run] = g_trace;
    }
    g_spawnResult = savedSpawn;
    Restore();
    Check(traces[0].size() > 20000, "goal ped: the original ran");
    CompareTraces(traces[0], traces[1], "goal ped (0x0055EB40-0x005600CF)");
}

// --- The goal manager's commands -------------------------------------------------------------------------------------

constexpr uint32_t kCommandFlags[] = {0xB394C01C, 0x1A4E0EF9, 0x54585419, 0xE022C12C, 0x806FFF30,
                                      0x68977CD7, 0x977FE2CF, 0x531FF702, 0x5685C1B7, 0xBF6C452E,
                                      0x185133BB, 0x8690A9CF};

// The trick mapping's answers for `keyCombo` (a created trick's number, a trick, Unassigned, nothing), the trick's
// structure, its ExtraTricks (a checksum, an array, none), the special slots.
void MakeTrickAnswers(uint32_t keyCombo)
{
    uint32_t m = Address(g_mapping);
    std::map<uint32_t, uint32_t> answers;
    if (Next() % 4 == 0)
        answers[0x6630] = Next() % 10;
    if (Next() % 5)
        answers[0x6950] = kTricks[Next() % 3];
    g_typedAnswers[{m, keyCombo}] = answers;
    g_getAnswers[{Address(g_trickStructure), 0x7031F10C}] = Address(g_trickParams);
    static const char* const kTrickNames[] = {"Kickflip", "Heelflip"};
    g_getAnswers[{Address(g_trickParams), 0xA1DC81F9}] = Address(kTrickNames[0]);
    g_getAnswers[{Address(g_extraParams), 0xA1DC81F9}] = Address(kTrickNames[1]);
    g_getAnswers[{Address(g_extraEntry), 0x7031F10C}] = Address(g_extraParams);
    g_typedAnswers.erase({Address(g_trickParams), 0x6E855102});
    uint32_t extras = Next() % 3;
    if (extras == 1)
        g_typedAnswers[{Address(g_trickParams), 0x6E855102}] = {{0x6950, 0x3E000000 + Next() % 4}};
    else if (extras == 2)
        g_typedAnswers[{Address(g_trickParams), 0x6E855102}] = {{0x6860, Address(&g_extrasList)}};
    std::memset(&g_extrasList, 0, sizeof(g_extrasList));
    g_extrasList.m_size = 1;
    g_extrasList.m_single = Next() % 3 ? 0x3E000000 + Next() % 4 : 0;
    std::memset(&g_extraArray, 0, sizeof(g_extraArray));
    g_extraArray.m_size = 1;
    g_extraArray.m_single = Address(g_extraEntry);
    g_maxSpecials = Next() % 5;
    for (auto& special : g_specials) {
        special.trick = kTricks[Next() % 4];
        special.keys = 0x4B000000 + Next() % 4;
        special.flag = Coin();
    }
}

struct Command {
    uint32_t address;
    CFunc ours;
    bool needsGoal; // (one not in the level is read through null)
};

void RunCommands(const Command* commands, size_t count, uint32_t seeds, const char* what)
{
    Setup();
    auto* standIns = new StandIns;
    StandInManagerCalls(*standIns);
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x007CE470, 4);
    SaveGlobal(0x00701440, 4);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        GameVariable<void*>(0x007CCE14) = g_net;
        GameVariable<void*>(0x007CE470) = g_mode;
        GameVariable<void*>(0x00701440) = g_screenManager;
        std::memset(g_nodes, 0, sizeof(g_nodes));
        std::memset(g_goals, 0, sizeof(g_goals));
        for (uint32_t seed = 0; seed < seeds; ++seed) {
            ResetMemory();
            MakeState(seed);
            Record({0xEEEE0000 | seed});
            static CArray keyCombos;
            static uint32_t keyElements[2] = {0x4B000001, 0x4B000002};
            std::memset(&keyCombos, 0, sizeof(keyCombos));
            keyCombos.m_size = 2;
            keyCombos.m_elements = keyElements;
            for (int i = 0; i < kGoals; ++i) {
                g_getAnswers[{Address(g_goalParams[i]), 0x79704516}] = Address(&keyCombos);
                g_getAnswers[{Address(g_goalParams[i]), 0x4BC5229D}] = Address("View me");
            }
            for (size_t c = 0; c < count; ++c) {
                const Command& command = commands[c];
                Params params;
                uint32_t p = Address(params.Struct());
                g_getAnswers[{p, 0xA1DC81F9}] = RandomId(!command.needsGoal);
                for (uint32_t flag : {0x9AC8D54Fu, 0x687DE558u})
                    if (Coin())
                        g_hasFlag.insert({p, flag});
                if (Coin())
                    g_hasComponent.insert({p, 0xCC365298});
                g_getAnswers[{p, 0x7031F10C}] = 0x5E100000 + Next() % 16;
                if (Coin())
                    g_getAnswers[{p, 0x906B67BA}] = Next() % 3 ? Next() % 200 : 0xFFFFFFFF;
                g_getAnswers[{p, 0}] = 0xF1A60000 + Next() % 4;
                if (Coin())
                    g_getAnswers[{p, 0x5B8AB877}] = Next() % 3;
                g_getAnswers[{p, 0x2E0B1465}] = 0xF1A60000 + Next() % 4;
                static const char* const kStrings[] = {"ProA_", "x", "Bc", "trick_type"};
                g_typedAnswers[{p, 0}] = {{0x6590, Address(kStrings[Next() % 4])}, {0x6630, Next() % 7},
                                          {0x6950, 0xF1A60000 + Next() % 4}};
                if (Next() % 4 == 0 && (command.address == 0x00559B40 || command.address == 0x0055AE30)) // (not asserted)
                    g_typedAnswers[{p, 0}].erase(0x6630);
                g_getAnswers[{p, 0x6D11ED74}] = Address(kStrings[Next() % 4]);
                if (Coin())
                    g_getAnswers[{p, 0x7321A8D6}] = Address(kStrings[Next() % 4]);
                for (uint32_t flag : {0x32D771EEu, 0x3DD01EA1u})
                    if (Coin())
                        g_hasFlag.insert({p, flag});
                // GetTrickFromKeyCombo's and the others' parameters
                uint32_t keyCombo = 0x4B000000 + Next() % 4;
                g_getAnswers[{p, 0xACFDB27A}] = keyCombo;
                if (Coin()) {
                    g_hasComponent.insert({p, 0x7EA855F0});
                    g_getAnswers[{p, 0x7EA855F0}] = 0x9E0F0000 + Next() % 3;
                }
                for (uint32_t flag : kCommandFlags)
                    if (Coin())
                        g_hasFlag.insert({p, flag});
                for (uint32_t name : {0x5B8AB877u, 0xCE0CA665u})
                    if (Coin())
                        g_getAnswers[{p, name}] = Next() % 4;
                g_getAnswers[{p, 0x8B4A1490}] = Next() % 300;
                g_getAnswers[{p, 0x407CCE70}] = 0x5E200000 + Next() % 16;
                g_getAnswers[{p, 0x944B2900}] = 0x9E000000 + Next() % 5;
                g_getAnswers[{p, 0x9982E501}] = RandomId(false);
                if (Coin())
                    g_getAnswers[{p, 0x5D82C486}] = Next() % 4;
                MakeTrickAnswers(keyCombo);
                static const char* const kLevels[] = {"Ber", "TrainingLevel"};
                g_getAnswers[{Address(g_levelStructure), 0x651533EC}] = Address(kLevels[Next() % 2]);
                if (command.address == 0x0055AA40 && !g_hasFlag.count({p, 0x9AC8D54F})) // (asserting on a missing goal
                    g_getAnswers[{p, 0xA1DC81F9}] = RandomId(false);                        // reads it through null)
                if ((command.address == 0x00559CC0 || command.address == 0x0055D7D0) && Next() % 4 == 0) // (not asserted)
                    g_getAnswers.erase({p, 0xA1DC81F9});
                Mgr()->m_lastGoal = Next() % 4 == 0 ? 0 : RandomId(false); // (one not in the table is read through null)
                if (Coin())
                    g_getAnswers[{p, 0xC4745838}] = Address(Coin() ? "Do a \\t1" : "\\k1!");
                if (Coin())
                    g_hasFlag.insert({p, 0xC4E78E22});
                Mgr()->m_goalPoints = static_cast<int>(Next() % 7);
                Mgr()->m_totalGoalPoints = static_cast<int>(Next() % 9);
                CallCommand(o, command.address, command.ours, params);
                RecordManager();
                for (uint32_t name : {0xA1DC81F9u, 0x7031F10Cu, 0x906B67BAu, 0u, 0x5B8AB877u, 0x2E0B1465u})
                    g_getAnswers.erase({p, name});
                g_hasFlag.erase({p, 0x9AC8D54F});
                g_hasFlag.erase({p, 0x32D771EE});
                g_hasFlag.erase({p, 0x3DD01EA1});
                g_typedAnswers.erase({p, 0});
                for (uint32_t flag : kCommandFlags)
                    g_hasFlag.erase({p, flag});
                g_hasComponent.erase({p, 0x7EA855F0});
                g_getAnswers.erase({p, 0xC4745838});
                g_hasFlag.erase({p, 0xC4E78E22});
                for (uint32_t name : {0xACFDB27Au, 0x7EA855F0u, 0x5B8AB877u, 0xCE0CA665u, 0x8B4A1490u, 0x407CCE70u,
                                      0x944B2900u, 0x9982E501u, 0x5D82C486u})
                    g_getAnswers.erase({p, name});
                g_getAnswers.erase({p, 0x6D11ED74});
                g_getAnswers.erase({p, 0x7321A8D6});
                g_hasFlag.erase({p, 0x687DE558});
                g_hasComponent.erase({p, 0xCC365298});
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "goal manager commands: the original ran");
    CompareTraces(traces[0], traces[1], what);
}

void TestGoalManagerCommands()
{
    using namespace Game;
    static const Command kCommands[] = {
        {0x005591C0, CFunc_GoalManager_EditGoal, false},
        {0x00559230, CFunc_GoalManager_ActivateGoal, false},
        {0x00559290, CFunc_GoalManager_DeactivateGoal, false},
        {0x005592F0, CFunc_GoalManager_ClearLastGoal, false},
        {0x00559310, CFunc_GoalManager_WinGoal, false},
        {0x00559350, CFunc_GoalManager_LoseGoal, false},
        {0x00559390, CFunc_GoalManager_DeactivateAllGoals, false},
        {0x005593D0, CFunc_GoalManager_ActivateAllGoals, false},
        {0x005593F0, CFunc_GoalManager_UninitializeAllGoals, false},
        {0x00559410, CFunc_GoalManager_UninitializeGoal, true},
        {0x00559490, CFunc_GoalManager_InitializeAllGoals, false},
        {0x005594B0, CFunc_GoalManager_InitializeAllSelectedGoals, false},
        {0x005594D0, CFunc_GoalManager_ActivateAllSelectedGoals, false},
        {0x005594F0, CFunc_GoalManager_GoalIsActive, false},
        {0x00559540, CFunc_GoalManager_SetGoalTimer, false},
        {0x005595D0, CFunc_GoalManager_ZeroGoalTimer, false},
        {0x00559630, CFunc_GoalManager_PauseAllGoals, false},
        {0x00559650, CFunc_GoalManager_UnPauseAllGoals, false},
        {0x00559670, CFunc_GoalManager_SetGoalFlag, false},
        {0x00559720, CFunc_GoalManager_HasSeenGoal, false},
        {0x00559770, CFunc_GoalManager_AllFlagsSet, true},
        {0x005597C0, CFunc_GoalManager_GoalFlagSet, true},
        {0x00559850, CFunc_GoalManager_CanRetryGoal, false},
        {0x00559870, CFunc_GoalManager_GetGoalParams, true},
        {0x005598D0, CFunc_GoalManager_CanStartGoal, false},
        {0x005598E0, CFunc_GoalManager_NextRaceWaypoint, true},
        {0x00559930, CFunc_GoalManager_NextWaypoint, true},
    };
    RunCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), 120,
                "goal manager commands (0x005591C0-0x0055997F)");
}

void TestGoalManagerCommands2()
{
    using namespace Game;
    static const Command kCommands[] = {
        {0x00559980, CFunc_GoalManager_CreateGoalName, false},
        {0x00559AF0, CFunc_GoalManager_GetLevelPrefix, false},
        {0x00559B40, CFunc_GoalManager_SetGraffitiMode, false},
        {0x00559B80, CFunc_GoalManager_HasWonGoal, false},
        {0x00559BC0, CFunc_GoalManager_GoalIsSelected, true},
        {0x00559C10, CFunc_GoalManager_ToggleGoalSelection, true},
        {0x00559C80, CFunc_GoalManager_DeselectAllGoals, false},
        {0x00559CA0, CFunc_GoalManager_GoalsAreSelected, false},
        {0x00559CC0, CFunc_GoalManager_GotCounterObject, true},
        {0x00559D20, CFunc_GoalManager_CounterGoalDone, true},
        {0x00559D70, CFunc_GoalManager_AddGoalPoint, false},
        {0x00559DB0, CFunc_GoalManager_SpendGoalPoints, false},
        {0x00559DF0, CFunc_GoalManager_HasGoalPoints, false},
        {0x00559E30, CFunc_GoalManager_ClearGoalPoints, false},
        {0x00559E50, CFunc_GoalManager_GetNumberOfGoalPoints, false},
        {0x00559E90, CFunc_GoalManager_HasBeatenGoalWithProset, false},
        {0x00559EC0, CFunc_GoalManager_GetProsetNotPrefix, false},
        {0x00559F70, CFunc_GoalManager_LevelLoad, false},
        {0x00559F90, CFunc_GoalManager_UnlockAllGoals, false},
        {0x00559FB0, CFunc_GoalManager_DeactivateCurrentGoal, false},
        {0x00559FD0, CFunc_GoalManager_SetStartTime, true},
        {0x0055A020, CFunc_GoalManager_UpdateComboTimer, true},
        {0x0055A070, CFunc_GoalManager_SetStartHeight, true},
        {0x0055A0C0, CFunc_GoalManager_CheckHeightRecord, true},
        {0x0055A110, CFunc_GoalManager_CheckDistanceRecord, true},
        {0x0055A160, CFunc_GoalManager_PlayGoalStartStream, true},
        {0x0055A1B0, CFunc_GoalManager_PlayGoalWaitStream, true},
        {0x0055A200, CFunc_GoalManager_PlayGoalWinStream, true},
        {0x0055A250, CFunc_GoalManager_StopCurrentStream, true},
        {0x0055A2A0, CFunc_GoalManager_TurnPro, false},
        {0x0055A2C0, CFunc_GoalManager_PauseGoal, true},
        {0x0055A310, CFunc_GoalManager_UnPauseGoal, true},
        {0x0055A360, CFunc_GoalManager_PauseCompetition, true},
        {0x0055A3B0, CFunc_GoalManager_UnPauseCompetition, true},
        {0x0055A400, CFunc_GoalManager_GoalIsLocked, false},
        {0x0055A430, CFunc_GoalManager_IsInCompetition, false},
        {0x0055A470, CFunc_GoalManager_GetGoalAnimations, true},
        {0x0055A500, CFunc_GoalManager_PlayGoalStream, true},
        {0x0055A520, CFunc_GoalManager_EndRunCalled, true},
        {0x0055A570, CFunc_GoalManager_ClearEndRun, true},
        {0x0055A5C0, CFunc_GoalManager_FinishedEndOfRun, true},
        {0x0055A610, CFunc_GoalManager_StartedEndOfRun, true},
        {0x0055A660, CFunc_GoalManager_AddTempSpecialTrick, true},
        {0x0055A6B0, CFunc_GoalManager_RemoveTempSpecialTrick, true},
    };
    RunCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), 100,
                "goal manager commands (0x00559980-0x0055A6FF)");
}

void TestGoalManagerCommands3()
{
    using namespace Game;
    static const Command kCommands[] = {
        {0x0055A700, CFunc_GoalManager_GetTrickFromKeyCombo, false},
        {0x0055A9F0, CFunc_GoalManager_UnlockGoal, true},
        {0x0055AA40, CFunc_GoalManager_QuickStartGoal, false},
        {0x0055AAA0, CFunc_GoalManager_InitializeGoal, true},
        {0x0055AAF0, CFunc_GoalManager_GoalIsExpired, true},
        {0x0055AB40, CFunc_GoalManager_ExpireGoal, true},
        {0x0055AB90, CFunc_GoalManager_AddTime, true},
        {0x0055ABF0, CFunc_GoalManager_GetNumberCollected, true},
        {0x0055AC50, CFunc_GoalManager_GetNumberOfFlags, true},
        {0x0055ACB0, CFunc_GoalManager_IsPro, false},
        {0x0055ACC0, CFunc_GoalManager_ResetGoalFlags, true},
        {0x0055AD10, CFunc_GoalManager_ColorTrickObjects, true},
        {0x0055AD90, CFunc_GoalManager_GetNumberOfTimesGoalStarted, true},
        {0x0055ADF0, CFunc_GoalManager_GoalExists, false},
        {0x0055AE30, CFunc_GoalManager_SetCanStartGoal, false},
        {0x0055AE70, CFunc_GoalManager_GetLastGoalId, false},
        {0x0055AEA0, CFunc_GoalManager_ClearTetrisTricks, true},
        {0x0055AEF0, CFunc_GoalManager_AnnounceGoalStarted, true},
        {0x0055AFB0, CFunc_GoalManager_SetEndRunType, true},
        {0x0055B040, CFunc_GoalManager_SetShouldDeactivateOnExpire, true},
        {0x0055B0B0, CFunc_GoalManager_ResetCareer, false},
        {0x0055B0F0, CFunc_GoalManager_UpdateFamilyTrees, false},
        {0x0055B110, CFunc_GoalManager_SuspendGoalPedLogic, true},
        {0x0055B1A0, CFunc_GoalManager_SetDifficultyLevel, false},
        {0x0055B1F0, CFunc_GoalManager_GetDifficultyLevel, false},
        {0x0055B260, CFunc_GoalManager_RestartStage, false},
        {0x0055B280, CFunc_GoalManager_CanRestartStage, false},
        {0x0055B2A0, CFunc_GoalManager_SetGoalChaptersAndStages, false},
        {0x0055B2C0, CFunc_GoalManager_AdvanceStage, false},
        {0x0055B310, CFunc_GoalManager_FilmGoalCheckpoint, true},
        {0x0055B370, CFunc_GoalManager_StartFilming, true},
        {0x0055B3D0, CFunc_GoalManager_SetSponsor, false},
        {0x0055B400, CFunc_GoalManager_GetSponsor, false},
        {0x0055B430, CFunc_GoalManager_GetTeam, false},
        {0x0055B490, CFunc_GoalManager_SetTeamMember, false},
        {0x0055B520, CFunc_GoalManager_KillTeamMembers, false},
        {0x0055B540, CFunc_GoalManager_SetTeamName, false},
        {0x0055B580, CFunc_GoalManager_RunLastStageScript, false},
        {0x0055B590, CFunc_GoalManager_GoalInitialized, true},
        {0x0055B5E0, CFunc_GoalManager_UnloadLastFam, true},
        {0x0055B630, CFunc_GoalManager_StopLastStream, true},
        {0x0055B680, CFunc_GoalManager_HideAllGoalPeds, false},
        {0x0055B6C0, CFunc_GoalManager_SetTrickBeatPattern, true},
        {0x0055B740, CFunc_GoalManager_ResetTrickBeatTimer, true},
        {0x0055B790, CFunc_GoalManager_GetGoalManagerParams, false},
        {0x0055B7C0, CFunc_GoalManager_GetActiveRewardPoints, false},
        {0x0055B8B0, CFunc_GoalManager_GetGoalRewardPoints, true},
        {0x0055B930, CFunc_Goal_CollectActive, false},
    };
    RunCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), 100,
                "goal manager commands (0x0055A700-0x0055B98F)");
}

void TestGoalManagerCommands4()
{
    using namespace Game;
    static const Command kCommands[] = {
        {0x0055D3F0, CFunc_GoalManager_AddGoal, false},
        {0x0055D4F0, CFunc_GoalManager_RestartLastGoal, false},
        {0x0055D510, CFunc_GoalManager_LevelUnload, false},
        {0x0055D530, CFunc_GoalManager_NumGoalsBeatenInLevel, false},
        {0x0055D590, CFunc_GoalManager_AddGoalChoices, false},
        {0x0055D600, CFunc_GoalManager_GetGoalList, false},
        {0x0055D7D0, CFunc_GoalManager_ReplaceTrickText, true},
        {0x0055D8E0, CFunc_GoalManager_GetGoalAttemptInfo, false},
        {0x0055D910, CFunc_GoalManager_GetViewGoalsInfo, false},
        {0x0055D450, CFunc_GoalManager_RemoveGoal, true}, // (the table changes: last)
        {0x0055D4D0, CFunc_GoalManager_RemoveAllGoals, false},
    };
    RunCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), 150,
                "goal manager commands (0x0055D3F0-0x0055D954)");
}

void TestGoalManagerCommands5()
{
    using namespace Game;
    static const Command kCommands[] = {
        {0x0055E860, CFunc_GoalManager_HasActiveGoals, false},
        {0x0055E8A0, CFunc_GoalManager_UnBeatAllGoals, false},
        {0x0055E8C0, CFunc_GoalManager_AddViewGoalsList, false},
        {0x0055E8E0, CFunc_GoalManager_GoalShouldExpire, false},
        {0x0055EAE0, CFunc_GoalManager_GetTimeLeftInNetGame, false},
        {0x0055EB20, CFunc_GoalManager_GetUnlimitedTimeInNetGame, false},
        {0x0055EAC0, CFunc_GoalManager_UpdateAllGoals, false}, // (goals may go: last)
    };
    RunCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), 150,
                "goal manager commands (0x0055E860-0x0055EB3F)");
}

} // namespace

SELFTEST(Early, TestLookupTable, RestoreLookupTable);
SELFTEST(Early, TestGoalManager, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerGoals, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerEvents, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerCommands4, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerUpdate, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerCommands5, RestoreGoalManager);
SELFTEST(Early, TestGoalPed, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerCommands, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerCommands2, RestoreGoalManager);
SELFTEST(Early, TestGoalManagerCommands3, RestoreGoalManager);
