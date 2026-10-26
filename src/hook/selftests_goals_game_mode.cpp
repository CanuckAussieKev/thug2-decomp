// Self-tests for the game mode and the game flow (Game::CGameMode, Game::CGameFlow; 0x00552E10-0x00553DF9): the
// original against ours on the same stand-in mode (its parameters' methods stood in for, answered from tables), its
// victory conditions (stand-ins with recording vtables), the global mode list, the script the flow runs.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/skate/game_mode.h"
#include "game/skate/victory_conditions.h"
#include "game/goals/goal_manager.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Game::CGameFlow;
using Game::CGameMode;

alignas(16) uint8_t g_mode[2][0x40];
alignas(16) uint8_t g_flow[0x20];
alignas(16) uint8_t g_conditions[6][0x10];
alignas(16) uint8_t g_netManager[0x40];
alignas(16) uint8_t g_goal[0x80];
alignas(16) uint8_t g_prefs[0x10];
alignas(16) uint8_t g_viewport[0x10];
alignas(16) uint8_t g_modeEntries[3][0x10];
alignas(16) uint8_t g_saveFake[0x10]; // (the options: a stand-in structure)
void* g_conditionVtable[5];
CArray g_modeInfo, g_victoryArray;
uint32_t g_modeInfoElements[3], g_victoryElements[4];
uint32_t g_conditionAnswers, g_conditionsMade, g_goalTimeLeft;
bool g_inNetGame, g_multiplayer, g_noGoal;
int g_goalCount;
uint32_t g_flowUpdates;
bool g_allowNull;

uint32_t Address(const void* p) { return reinterpret_cast<uint32_t>(p); }
CGameMode* Mode(int i) { return reinterpret_cast<CGameMode*>(g_mode[i]); }

uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
bool Coin() { return (Next() & 1) != 0; }

void StandInModeCalls(StandIns& s)
{
    StandInGoalMemory(s);
    StandInStructAdds(s);
    StandInPrintf(s);
    StandInArrays(s);
    StandInGetters(s);
    StandInContains(s);
    StandInStructChanges(s);
    // A condition's vtable: 0 the scalar deleting destructor, 2 terminal, 3 complete (from the condition's bits).
    for (uint32_t slot = 0; slot < 5; ++slot) {
        g_conditionVtable[slot] = s.Thunk(slot == 0 || slot == 1 ? 4 : 0, [slot](Regs& r) {
            Record({0xC0000000u | slot, r.ecx, slot == 0 ? r.args[0] & 0xFF : 0u});
            uint32_t index = (r.ecx - Address(g_conditions)) / 0x10;
            return slot == 2 ? (g_conditionAnswers >> (index * 2)) & 1 : slot == 3 ? (g_conditionAnswers >> (index * 2 + 1)) & 1
                                                                               : r.ecx;
        });
    }
    s.At({0x00576810, AddressOf(&Game::CreateVictoryCondition)}, 0, [](Regs& r) {
        Record({0x6810, r.args[0]});
        if (g_conditionsMade++ % 5 == 4 && g_allowNull)
            return 0u; // (sometimes none)
        uint8_t* condition = g_conditions[g_conditionsMade % 6];
        *reinterpret_cast<void***>(condition) = g_conditionVtable;
        return Address(condition);
    });
    using Calloc1 = void* (*)(unsigned int);
    using Calloc3 = void* (*)(unsigned int, Mem::Heap*, int);
    s.At({0x00401CC0, AddressOf(static_cast<Calloc1>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        void* block = HeapBlock(r.args[0]);
        std::memset(block, 0, r.args[0]);
        return Address(block);
    });
    s.At({0x00401D40, AddressOf(static_cast<Calloc3>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1D40, r.args[0], r.args[1], r.args[2]});
        void* block = HeapBlock(r.args[0]);
        std::memset(block, 0, r.args[0]);
        return Address(block);
    });
    using GetAr = CArray* (*)(uint32_t);
    using GetStName = CStruct* (*)(const char*);
    s.At({0x00479070, AddressOf(static_cast<GetAr>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return Address(&g_modeInfo);
    });
    s.At({0x00479040, AddressOf(static_cast<GetStName>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9040, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x5D3F0000u + (Hash(reinterpret_cast<const char*>(r.args[0])) & 0xFF);
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx});
        return g_inNetGame ? 1u : 0u;
    });
    s.At({0x00533FB0, AddressOf(&GameNet::Manager::CreateTeamFlags)}, 4, [](Regs& r) {
        Record({0x3FB0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005651F0}, 0, [](Regs& r) {
        Record({0x51F0, r.ecx});
        return g_multiplayer ? 1u : 0u;
    });
    s.At({0x005591B0, AddressOf(&Game::GetGoalManager)}, 0, [](Regs&) {
        Record({0x91B0});
        return Address(g_goalManager);
    });
    s.At({0x0055DC30}, 4, [](Regs& r) {
        Record({0xDC30, r.ecx, r.args[0]});
        return static_cast<uint32_t>(g_goalCount);
    });
    s.At({0x0055E980}, 0, [](Regs& r) {
        Record({0xE980, r.ecx});
        return g_noGoal ? 0u : Address(g_goal);
    });
    s.At({0x00557F90, AddressOf(&Game::CGoalManager::GetGoal)}, 8, [](Regs& r) {
        Record({0x7F90, r.ecx, r.args[0], r.args[1]});
        return g_noGoal ? 0u : Address(g_goal);
    });
    s.At({0x00564D80}, 0, [](Regs& r) {
        Record({0x4D80, r.ecx});
        return Address(g_prefs);
    });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Record({0xC4A0, r.ecx, r.args[0]});
        return Address(g_viewport);
    });
    // The script the game flow runs.
    using SetScript = void (Script::CScript::*)(uint32_t, CStruct*, Obj::CObject*);
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Record({0x38E0, r.ecx});
        return r.ecx;
    });
    s.At({0x00474420, AddressOf(&Script::CScript::Destroy)}, 0, [](Regs& r) {
        Record({0x4420, r.ecx});
        return 0u;
    });
    s.At({0x004747A0, AddressOf(static_cast<SetScript>(&Script::CScript::SetScript))}, 12, [](Regs& r) {
        Record({0x47A0, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Record({0x50B0, r.ecx});
        return ++g_flowUpdates % 7 ? 1u : 0u;
    });
}

void MakeModeState(uint32_t seed)
{
    g_seed = seed * 2246822519u + 77;
    g_getAnswers.clear();
    g_hasFlag.clear();
    g_hasComponent.clear();
    g_conditionAnswers = Next();
    g_inNetGame = Coin();
    g_multiplayer = Coin();
    g_noGoal = Next() % 4 == 0;
    g_goalCount = static_cast<int>(Next() % 3);
    std::memset(g_goal, 0, sizeof(g_goal));
    *reinterpret_cast<int*>(g_goal + 0x50) = static_cast<int>(Next() % 3) - 1;
    static const uint32_t kNames[] = {0xC1F5864B, 0xD57C16BA, 0x78FB9F04, 0xCA37E639, 0x47F763FF, 0xA8774BF7,
                                      0x38DBE1D0, 0x2EBE6CD1, 0x035C8073, 0x8D38A280, 0x6DC3D6F1, 0x4D42E61D,
                                      0xCE87E4E3, 0x3A1115CB, 0x48E748B5, 0x2B1304A1, 0xD4734865, 0x11223344};
    static const uint32_t kModes[] = {0x5D32129C, 0x6EF8FDA0, 0xF135ECB6, 0x1498240A, 0xE45F0CA2, 0x3D6D444F,
                                      0xBFF33600, 0x1C471C60, 0x30C2FFA3, 0x10F84276, 0x12345678};
    static const uint32_t kScreens[] = {0x06AB02F2, 0x3558D8E6, 0x9D65D0E7, 0x77777777};
    static const uint32_t kViewports[] = {0x92F5B4CB, 0x7C442511, 0x66666666};
    for (int m = 0; m < 2; ++m) {
        uint32_t params = Address(g_mode[m] + 4);
        for (uint32_t name : kNames)
            if (Coin() || name == 0x8D38A280 || name == 0x2EBE6CD1) // (asserted ones there)
                g_getAnswers[{params, name}] = Next() % 4 == 0 ? 0 : Next() % 4;
        g_getAnswers[{params, 0xA1DC81F9}] = kModes[Next() % 11];
        g_getAnswers[{params, 0x9BAB2EAE}] = kScreens[Next() % 4];
        if (Coin())
            g_getAnswers[{params, 0x94A24C5E}] = Address(&g_victoryArray);
        for (const char* name : {"time_limit_type", "victory_condition_type", "screenmode"})
            if (Coin())
                g_hasComponent.insert({Address(g_saveFake), Crc::GenerateCRCFromString(name)});
    }
    g_getAnswers[{Address(g_viewport), 0x21902065}] = kViewports[Next() % 3];
    for (uint32_t& v : g_victoryElements)
        v = 0x0F0C0000 + Next() % 16;
    std::memset(&g_victoryArray, 0, sizeof(g_victoryArray));
    g_victoryArray.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    g_victoryArray.m_size = 2 + Next() % 3;
    g_victoryArray.m_elements = g_victoryElements;
    for (int i = 0; i < 3; ++i) {
        uint32_t entry = Address(g_modeEntries[i]);
        g_modeInfoElements[i] = entry;
        g_getAnswers[{entry, 0x21902065}] = 0x0D0E0000 + i;
        static const char* const kDefinitions[] = {"freeskate_def", "career_def", "horse_def"};
        g_getAnswers[{entry, 0x97CFD027}] = Address(kDefinitions[i]);
    }
    std::memset(&g_modeInfo, 0, sizeof(g_modeInfo));
    g_modeInfo.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    g_modeInfo.m_size = 3;
    g_modeInfo.m_elements = g_modeInfoElements;
}

void RecordModes()
{
    for (auto& mode : g_mode)
        for (uint32_t k = 0; k < sizeof(mode); k += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(mode + k));
    for (uint32_t k = 0; k < g_heapUsed; k += 4)
        g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + k));
}

template <typename R, typename... A>
R Call(bool original, uint32_t address, R (CGameMode::*ours)(A...), CGameMode* mode, std::type_identity_t<A>... args)
{
    return original ? Original<R(__thiscall*)(CGameMode*, A...)>(address)(mode, args...) : (mode->*ours)(args...);
}

void TestGoalsGameMode()
{
    Setup();
    SaveGlobal(0x007CCE14, 4);
    SaveGlobal(0x006F3548, 16); // (the scripts' pool pair: emptied - the manager's Allocate)
    SaveGlobal(0x007CE474, 4);  // the game mode's references
    std::memset(reinterpret_cast<void*>(0x006F3548), 0, 16);
    auto* standIns = new StandIns;
    StandInModeCalls(*standIns);
    GameVariable<uint8_t*>(0x007CCE14) = g_netManager;

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        g_conditionsMade = 0;
        g_flowUpdates = 0;
        MakeModeState(999); // (the stand-ins' answers the same for the constructors in both runs)
        GameVariable<uint8_t*>(0x007CE470) = g_mode[0];
        std::memset(g_mode, 0, sizeof(g_mode));
        using Make = CGameMode*(__thiscall*)(CGameMode*);
        Record({Address(o ? Original<Make>(0x00553D30)(Mode(0)) : Mode(0)->Construct())});
        Record({Address(o ? Original<Make>(0x00553D30)(Mode(1)) : Mode(1)->Construct())});
        RecordModes();
        for (uint32_t seed = 0; seed < 200; ++seed) {
            MakeModeState(seed);
            CGameMode* m = Mode(seed & 1);
            g_allowNull = seed % 3 == 0;
            Record({0xEEEE0000 | seed});
            Record({Call(o, 0x00553AD0, &CGameMode::LoadGameType, m, 0x0D0E0000u + seed % 4)});
            Call(o, 0x005537D0, &CGameMode::BuildVictoryConditions, m);
            RecordModes();
            if (!g_allowNull)
                Record({Call(o, 0x00553910, &CGameMode::EndConditionsMet, m)}); // (a null condition crashes it)
            Record({static_cast<uint32_t>(Call(o, 0x00553A60, &CGameMode::GetFirstConditionValue, m))});
            Record({Call(o, 0x00553BE0, &CGameMode::OverrideOptions, m, reinterpret_cast<CStruct*>(g_saveFake))});
            Record({Call(o, 0x00552FB0, &CGameMode::ShouldAccumulateScore, m),
                    Call(o, 0x00552FD0, &CGameMode::ShouldTrackBestCombo, m),
                    static_cast<uint32_t>(Call(o, 0x005530C0, &CGameMode::IsScoreFrozen, m)),
                    Call(o, 0x005530F0, &CGameMode::ShouldTrackTrickScore, m),
                    Call(o, 0x00553110, &CGameMode::ShouldDegradeScore, m),
                    static_cast<uint32_t>(Call(o, 0x00553130, &CGameMode::NumTeams, m)),
                    Call(o, 0x00553150, &CGameMode::ShouldTimerBeep, m),
                    static_cast<uint32_t>(Call(o, 0x00553170, &CGameMode::GetTimeLimit, m)),
                    static_cast<uint32_t>(Call(o, 0x00553190, &CGameMode::GetInitialNumberOfPlayers, m)),
                    static_cast<uint32_t>(Call(o, 0x005531D0, &CGameMode::GetMaximumNumberOfPlayers, m)),
                    static_cast<uint32_t>(Call(o, 0x005531F0, &CGameMode::GetFireballLevel, m)),
                    Call(o, 0x00553220, &CGameMode::IsTrue, m, 0x11223344u),
                    static_cast<uint32_t>(Call(o, 0x00553270, &CGameMode::GetRound, m)),
                    Call(o, 0x005532A0, &CGameMode::GetNameChecksum, m), Call(o, 0x005532D0, &CGameMode::GetGoalsMode, m),
                    Call(o, 0x00553300, &CGameMode::GetTeamGraffitiMode, m)});
            for (uint32_t name : {0x30C2FFA3u, 0x5E2EA50Cu, 0xC50AFFD0u, 0xF9D5D933u, 0x6EF8FDA0u, 0xBFF33600u,
                                  0x6C5FF266u, 0x10F84276u, 0x12345678u})
                Record({Call(o, 0x00553330, &CGameMode::ConvertNetNameChecksum, m, name)});
            Record({static_cast<uint32_t>(Call(o, 0x005533B0, &CGameMode::GetGoalTimeLeft, m))});
            Call(o, 0x00553400, &CGameMode::SetGoalTimeLeft, m, static_cast<int>(seed % 90));
            Record({Call(o, 0x00553460, &CGameMode::ShouldUseClock, m), Call(o, 0x00553550, &CGameMode::ShouldStopAtZero, m),
                    static_cast<uint32_t>(Call(o, 0x005535F0, &CGameMode::IsTeamGame, m)),
                    static_cast<uint32_t>(Call(o, 0x00553630, &CGameMode::GetScreenMode, m))});
            Call(o, 0x005535A0, &CGameMode::SetNumTeams, m, static_cast<int>(seed % 4));
            Call(o, 0x00553000, &CGameMode::SetScoreAccumulation, m, (seed & 1) != 0);
            Call(o, 0x00553020, &CGameMode::SetScoreFrozen, m, (seed & 2) != 0);
            Call(o, 0x00553040, &CGameMode::SetScoreDegradation, m, (seed & 4) != 0);
            Call(o, 0x00553060, &CGameMode::SetTrickScoreTracking, m, (seed & 8) != 0);
            Call(o, 0x00553080, &CGameMode::SetTeamGraffitiMode, m, 0x7EA00000u + seed);
            Call(o, 0x005530A0, &CGameMode::SetGoalsMode, m, 0x60A50000u + seed);
            Call(o, 0x005531B0, &CGameMode::SetMaximumNumberOfPlayers, m, static_cast<int>(seed % 9));
            Call(o, 0x00553250, &CGameMode::SetRound, m, static_cast<int>(seed));
            Call(o, 0x005536D0, &CGameMode::IncrementRound, m);
            if (seed % 5 == 0)
                Call(o, 0x00553D10, &CGameMode::Reset, m);
            RecordModes();
            // (the conditions let go, so the stand-in heap can start again)
            Call(o, 0x00553770, &CGameMode::ClearVictoryConditions, Mode(0));
            Call(o, 0x00553770, &CGameMode::ClearVictoryConditions, Mode(1));
            RecordModes();
            ResetMemory();
        }
        // The command, then the destructors.
        Params none;
        Record({o ? Original<bool(__cdecl*)(CStruct*, Script::CScript*)>(0x00553C90)(none.Struct(), nullptr)
                  : Game::CFunc_OverrideGameModeOptions(none.Struct(), nullptr)});
        Call(o, 0x00553890, &CGameMode::DestroyBody, Mode(0));
        Record({Address(Call(o, 0x00553CE0, &CGameMode::Destroy, Mode(1), static_cast<uint8_t>(1)))});
        RecordModes();
        // The singleton.
        for (int k = 0; k < 4; ++k) {
            GameVariable<uint8_t*>(0x007CE470) = k & 1 ? g_mode[1] : nullptr;
            GameVariable<uint32_t>(0x007CE474) = static_cast<uint32_t>(k);
            Record({Address(o ? Original<CGameMode*(__cdecl*)(bool)>(0x00553DB0)(k < 2)
                              : CGameMode::Instance(k < 2)),
                    GameVariable<uint32_t>(0x007CE474), Address(GameVariable<uint8_t*>(0x007CE470))});
            if (k >= 1) {
                std::memset(g_mode[1], 0, 4);
                *reinterpret_cast<void***>(g_mode[1]) = g_conditionVtable; // (its destructor: a stand-in)
                for (int twice = 0; twice < 2; ++twice) {
                    if (GameVariable<uint8_t*>(0x007CE470))
                        *reinterpret_cast<void***>(GameVariable<uint8_t*>(0x007CE470)) = g_conditionVtable;
                    if (o)
                        Original<void(__cdecl*)()>(0x00552F70)();
                    else
                        CGameMode::Release();
                    Record({GameVariable<uint32_t>(0x007CE474), Address(GameVariable<uint8_t*>(0x007CE470))});
                }
            }
        }
        // The game flow.
        auto* flow = reinterpret_cast<CGameFlow*>(g_flow);
        std::memset(g_flow, 0x5A, sizeof(g_flow));
        Record({Address(o ? Original<CGameFlow*(__thiscall*)(CGameFlow*)>(0x00552E10)(flow) : flow->Construct())});
        for (uint32_t k = 0; k < 24; ++k) {
            if (k % 5 == 0) {
                if (o)
                    Original<void(__thiscall*)(CGameFlow*, uint32_t)>(0x00552E30)(flow, 0xF10E0000 + k);
                else
                    flow->Request(0xF10E0000 + k);
            }
            if (o)
                Original<void(__thiscall*)(CGameFlow*)>(0x00552E40)(flow);
            else
                flow->Update();
            for (uint32_t w = 0; w < sizeof(g_flow); w += 4)
                g_trace.push_back(*reinterpret_cast<uint32_t*>(g_flow + w));
        }
        Record({Address(o ? Original<CGameFlow*(__thiscall*)(CGameFlow*, uint8_t)>(0x00552F30)(flow, 1)
                          : flow->Destroy(1))});
        RecordModes();
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 10000, "game mode: the original reached its calls");
    CompareTraces(traces[0], traces[1], "game mode and game flow (0x00552E10-0x00553DF9)");
}

void RestoreGameMode() { Restore(); }

} // namespace

SELFTEST(Early, TestGoalsGameMode, RestoreGameMode);
