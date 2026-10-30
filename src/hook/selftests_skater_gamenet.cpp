// Self-tests for the network manager's small members (src/game/net/gamenet_state.cpp, 0x00532AA0-0x005333F0): the
// original and ours on identical stand-in managers in an arena, with network preferences built by hand; what they
// call outside (the apps' sends, the clock, the task list, the network game check, the goal editor) is stood in for by
// recording code written over the game's function and ours alike, and the calls, the results and the whole arena are
// compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/task.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/ed/goal_editor.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using GameNet::Manager;
using Script::CComponent;

constexpr uint32_t kArenaSize = 0x20000;
ArenaPair<kArenaSize> g_gs_storage;
uint32_t g_gs_base;
uint32_t At(uint32_t offset) { return g_gs_base + offset; }
template <typename T>
T& Word(uint32_t offset)
{
    return *reinterpret_cast<T*>(g_gs_base + offset);
}
uint32_t Rel(uint32_t word) { return ArenaWord(g_gs_storage.Get(), kArenaSize, word); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}

// Arena layout.
constexpr uint32_t kManager = 0x00000;   // the manager (0x10000)
constexpr uint32_t kPrefs = 0x10000;     // structures and components (0x1000)
constexpr uint32_t kApps = 0x11000;      // apps (0x100 apart)
constexpr uint32_t kTask = 0x11800;
constexpr uint32_t kEditor = 0x11900;

bool g_gs_netGame;
uint32_t g_gs_goalCount;
std::vector<Saved> g_gs_saved;

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_gs_saved);
    g_gs_saved.clear();
}

// A preference structure built by hand: the root (the preferences' structure at +0x104 + 4) holds a structure
// component per preference, each holding `checksum = value` (or, for value 0, nothing).
uint32_t g_gs_nextPref;
void AddPreference(uint32_t name, uint32_t value)
{
    uint32_t inner = g_gs_nextPref;
    g_gs_nextPref += 0x40;
    // the inner structure (8 bytes) and its component
    if (value) {
        auto* c = &Word<CComponent>(inner + 0x10);
        c->typeAndFlag = Script::ESYMBOLTYPE_NAME << 1;
        c->name = 0x21902065;
        c->raw = value;
        Word<uint32_t>(inner + 4) = At(inner + 0x10);
    }
    // the root's component, at the front of its list
    auto* c = &Word<CComponent>(inner + 0x20);
    c->typeAndFlag = Script::ESYMBOLTYPE_STRUCTURE << 1;
    c->name = name;
    c->raw = At(inner);
    c->next = reinterpret_cast<CComponent*>(Word<uint32_t>(kManager + 0x104 + 4 + 4));
    Word<uint32_t>(kManager + 0x104 + 4 + 4) = At(inner + 0x20);
}

void SetUpArena(int run)
{
    uint8_t* arena = g_gs_storage.Get()[run];
    g_gs_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    for (uint32_t i = 0; i < 0x10000; i += 4)
        Word<uint32_t>(kManager + i) = 0x5A000000u + i; // every field told apart
    std::memset(&Word<uint8_t>(kManager + 0x104), 0, 0x18); // the two preference sets: empty
    g_gs_nextPref = kPrefs;
    g_trace.clear();
}

using Fn0 = uint32_t(__thiscall*)(Manager*);
using Fn1 = uint32_t(__thiscall*)(Manager*, uint32_t);

void Install(StandIns& s)
{
    s.At({0x00457280, AddressOf(&Net::App::SendMessageTo)}, 24, [](Regs& r) {
        Rec(0x7280, {r.ecx, r.args[0] & 0xFF, r.args[1] & 0xFFFF, *reinterpret_cast<uint32_t*>(r.args[2]), r.args[3],
                     r.args[4] & 0xFFFF, r.args[5]});
        return 1u;
    });
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        Rec(0xE970, {});
        return 123456u;
    });
    s.At({0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, [](Regs& r) {
        Rec(0x68F0, {r.ecx});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, [](Regs& r) {
        Rec(0xE4F0, {r.ecx});
        return g_gs_netGame ? 1u : 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
        uint32_t data = 0;
        std::memcpy(&data, desc->m_data, desc->m_length < 4 ? desc->m_length : 4);
        Rec(0x9E50, {r.ecx, desc->m_id, desc->m_stream, desc->m_length, data, static_cast<uint32_t>(desc->m_priority),
                     static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_singular,
                     static_cast<uint32_t>(desc->m_delay), desc->m_forcedSequenceId, desc->m_compression,
                     // (where the data is: in the arena, or a local - not compared)
                     Rel(reinterpret_cast<uint32_t>(desc->m_data)) >= 0xA0000000u ? reinterpret_cast<uint32_t>(desc->m_data) : 0u});
        return 0u;
    });
    s.At({0x004E8470, AddressOf(&Ed::CGoalEditorComponent::Get)}, 0, [](Regs&) {
        Rec(0x8470, {});
        return At(kEditor);
    });
    s.At({0x004E7E50, AddressOf(&Ed::CGoalEditorComponent::CountCompleteGoals)}, 4, [](Regs& r) {
        Rec(0x7E50, {r.ecx, r.args[0]});
        return g_gs_goalCount;
    });
    s.At({0x004EBB70, AddressOf(&Ed::CGoalEditorComponent::WriteLevelGoalsToBuffer)}, 12, [](Regs& r) {
        Rec(0xBB70, {r.ecx, r.args[0], r.args[1], r.args[2]});
        std::memset(reinterpret_cast<void*>(r.args[1]), 0x6B, 40);
        return 40u;
    });
}

// Each case runs on a fresh arena: its results go into the trace, then the whole arena.
std::vector<uint32_t> Run(bool original, int scenario)
{
    SetUpArena(original ? 0 : 1);
    StandIns* s = new StandIns;
    Install(*s);
    auto* manager = reinterpret_cast<Manager*>(At(kManager));
    auto call0 = [&](uint32_t address, uint32_t (Manager::*ours)()) {
        uint32_t r = original ? Original<Fn0>(address)(manager) : (manager->*ours)();
        Rec(0xCA11, {address, r});
    };
    auto call1 = [&](uint32_t address, uint32_t (Manager::*ours)(uint32_t), uint32_t arg) {
        uint32_t r = original ? Original<Fn1>(address)(manager, arg) : (manager->*ours)(arg);
        Rec(0xCA11, {address, r});
    };
    // (members of other return types: called through the same signature, the result's low part kept)
    auto as0 = [](auto member) {
        uint32_t (Manager::*m)();
        static_assert(sizeof(member) == sizeof(m));
        std::memcpy(&m, &member, sizeof(m));
        return m;
    };
    auto as1 = [](auto member) {
        uint32_t (Manager::*m)(uint32_t);
        static_assert(sizeof(member) == sizeof(m));
        std::memcpy(&m, &member, sizeof(m));
        return m;
    };
    // (void members: only the call is recorded - EAX is whatever was left in it)
    auto callv0 = [&](uint32_t address, uint32_t (Manager::*ours)()) {
        original ? Original<Fn0>(address)(manager) : (manager->*ours)();
        Rec(0xCA10, {address});
    };
    auto callv1 = [&](uint32_t address, uint32_t (Manager::*ours)(uint32_t), uint32_t arg) {
        original ? Original<Fn1>(address)(manager, arg) : (manager->*ours)(arg);
        Rec(0xCA10, {address});
    };
    auto byte = [&](uint32_t address, auto member) {
        uint32_t r = original ? Original<Fn0>(address)(manager) : (manager->*as0(member))();
        Rec(0xCA12, {address, r & 0xFF});
    };

    if (scenario == 0) {
        // Getters and setters.
        callv1(0x00532AA0, as1(&Manager::SetJoinMode), 0x11223344);
        call0(0x00532AC0, as0(&Manager::GetJoinMode));
        call0(0x00532AD0, as0(&Manager::GetHostMode));
        call0(0x00532E40, as0(&Manager::GetJoinIp));
        call0(0x00532E50, as0(&Manager::GetJoinPrivateIp));
        call0(0x00532E60, as0(&Manager::GetJoinPort));
        callv1(0x00532E70, as1(&Manager::SetJoinIp), 0x0A000001);
        callv1(0x00532E80, as1(&Manager::SetJoinPrivateIp), 0x0A000002);
        callv1(0x00532E90, as1(&Manager::SetJoinPort), 0x0A000003);
        call0(0x00532E40, as0(&Manager::GetJoinIp));
        call0(0x00532E50, as0(&Manager::GetJoinPrivateIp));
        call0(0x00532E60, as0(&Manager::GetJoinPort));
        call1(0x00532EA0, as1(&Manager::GetClient), 0);
        call1(0x00532EA0, as1(&Manager::GetClient), 1);
        call0(0x00532EB0, as0(&Manager::GetMatchClient));
        call0(0x00532EC0, as0(&Manager::GetCamPlayer));
        call0(0x00532F70, as0(&Manager::GetNetworkPreferences));
        call0(0x00532F80, as0(&Manager::GetTauntPreferences));
        uint32_t words[8] = {1, 2, 3, 0x44444444, 5, 6, 7, 0x88888888};
        std::memcpy(&Word<uint32_t>(0x12000), words, sizeof(words));
        callv1(0x00532F90, as1(&Manager::SetStartInfoWords), At(0x12000));
        for (uint32_t i = 0; i < 8; i += 3)
            call1(0x00532FD0, as1(&Manager::GetStartInfoWord), i);
        byte(0x00533070, &Manager::GetGameId);
        callv1(0x00533060, as1(&Manager::SetGameId), 0xFFFFFF7E);
        byte(0x00533070, &Manager::GetGameId);
        byte(0x005332A0, &Manager::GameEndedSent);
        Word<uint8_t>(kManager + 0x39) = 0x7E;
        byte(0x005332A0, &Manager::GameEndedSent);
        callv0(0x00533130, as0(&Manager::TogglePlayerNames));
        callv0(0x00533130, as0(&Manager::TogglePlayerNames));
        callv0(0x00533130, as0(&Manager::TogglePlayerNames));
        callv1(0x00533180, as1(&Manager::SetLastLeader), At(0x500));
        call0(0x00533190, as0(&Manager::GetLastLeader));
        call0(0x005331A0, as0(&Manager::GetLastLeadingTeam));
        callv1(0x005331B0, as1(&Manager::SetLastScoreUpdate), 0x0B0B0B0B);
        call0(0x005331C0, as0(&Manager::GetLastScoreUpdate));
        callv1(0x005331D0, as1(&Manager::SetFinalScoresReceived), 0xFFFFFF01);
        byte(0x005331E0, &Manager::FinalScoresReceived);
        callv1(0x005331D0, as1(&Manager::SetFinalScoresReceived), 0x100);
        byte(0x005331E0, &Manager::FinalScoresReceived);
        callv1(0x005331F0, as1(&Manager::SetJoinState), 6);
        call0(0x00533200, as0(&Manager::GetJoinState));
        call0(0x00533210, as0(&Manager::GetCrown));
        callv0(0x00533220, as0(&Manager::ClearCrown));
        call0(0x00533210, as0(&Manager::GetCrown));
        callv1(0x00533230, as1(&Manager::SetCrownSpawnPoint), 0x12345679);
        callv1(0x00533230, as1(&Manager::SetCrownSpawnPoint), 0x123456F9); // -7
        callv1(0x00533240, as1(&Manager::SetLobbyId), 0x99509950);
        byte(0x005332B0, &Manager::IsGameOver);
        callv0(0x005332D0, as0(&Manager::ClearGameOver));
        byte(0x005332B0, &Manager::IsGameOver);
        callv0(0x005332C0, as0(&Manager::SetGameOver));
        byte(0x005332B0, &Manager::IsGameOver);
        byte(0x005332E0, &Manager::CheatingNoted);
        Word<uint8_t>(kManager + 0x3B) = 0;
        byte(0x005332E0, &Manager::CheatingNoted);
        callv0(0x005332F0, as0(&Manager::NoteCheating));
        byte(0x005332E0, &Manager::CheatingNoted);
        uint32_t size = 0;
        uint32_t r = original ? Original<uint32_t(__thiscall*)(Manager*, uint32_t*)>(0x00533360)(manager, &size)
                              : reinterpret_cast<uint32_t>(manager->GetPendingData(&size));
        Rec(0x3360, {r, size});
        call0(0x005333E0, as0(&Manager::GetCreatedGoalsMessage));
        call0(0x005333F0, as0(&Manager::GetCreatedGoalsSize));
        callv0(0x00532E30, as0(&Manager::RemoveHostTask));
    } else if (scenario == 1) {
        // The network preferences: each value of each, then misses.
        struct Pref {
            uint32_t address, name;
            uint32_t (Manager::*ours)();
        };
        const Pref prefs[] = {
            {0x00532AE0, 0x2120910C, as0(&Manager::GetRankTest)},
            {0x00532B30, 0xAD6D5802, as0(&Manager::GetRankLimit)},
            {0x00532BE0, 0x403DA0D0, as0(&Manager::GetSkillLevel)},
            {0x00532C50, 0x1CB2A6B0, as0(&Manager::GetMaxPlayers)},
            {0x00532CF0, 0xB9D44C2F, as0(&Manager::GetNumTeams)},
            {0x00532D60, 0xA8DA5CB7, as0(&Manager::GetMaxObservers)},
            {0x005330B0, 0xCE238F5D, as0(&Manager::GetFireballLevel)},
            {0x00533080, 0x651533EC, as0(&Manager::GetLevelPreference)},
            {0x00533140, 0x0F7DD263, as0(&Manager::ShouldDrawPlayerNames)},
        };
        const uint32_t values[] = {0x18CBB824, 0x6FCC88B2, 0xF6C5D908, 0x81C2E99E, 0x1FA67C3D, 0x68A14CAB, 0xF1A81D11,
                                   0x86AF2D87, 0x16103016, 0x61170080, 0x3C6F2B1A, 0x46D173AB, 0x6A220326, 0x768E5A1D,
                                   0x4D04DFA7, 0x85736594, 0xF81BC89B, 0x12345678};
        for (const Pref& p : prefs) {
            for (uint32_t value : values) {
                Word<uint32_t>(kManager + 0x104 + 8) = 0;
                g_gs_nextPref = kPrefs;
                std::memset(&Word<uint8_t>(kPrefs), 0, 0x1000);
                AddPreference(0x77777777, 0x16103016); // another preference before it
                AddPreference(p.name, value);
                AddPreference(0x66666666, 0x6FCC88B2);
                call0(p.address, p.ours);
            }
            // the preference without its checksum: the original's uninitialised local (the manager)
            Word<uint32_t>(kManager + 0x104 + 8) = 0;
            AddPreference(p.name, 0);
            call0(p.address, p.ours);
        }
    } else if (scenario == 2) {
        // The untimed game modes.
        const uint32_t modes[] = {0xEC200EAA, 0x5D32129C, 0x6EF8FDA0, 0x1498240A, 0xF135ECB6, 0x6C5FF266, 0x12345678};
        const uint32_t ctfTypes[] = {0x938AB24B, 0x11111111};
        const uint32_t stops[] = {0xF81BC89B, 0x22222222};
        for (uint32_t mode : modes) {
            for (int net = 0; net < 2; ++net) {
                for (uint32_t ctf : ctfTypes) {
                    for (uint32_t stop : stops) {
                        Word<uint32_t>(kManager + 0x104 + 8) = 0;
                        g_gs_nextPref = kPrefs;
                        AddPreference(0x15421B73, ctf);
                        AddPreference(0x48E748B5, stop);
                        Word<uint32_t>(kManager + 0x30) = mode;
                        g_gs_netGame = net != 0;
                        byte(0x00532FE0, &Manager::IsUntimedGame);
                    }
                }
            }
        }
    } else if (scenario == 3) {
        // The messages: LAN search (no lobby client; an address asked for; broadcast with / without the command
        // line's), ready, the observer count (one / two clients), the created goals.
        Word<uint32_t>(kManager + 0x2C) = 0;
        GameVariable<uint32_t>(0x007CCA84) = 0x0A0B0C0D;
        callv0(0x00532ED0, as0(&Manager::FindServersOnLan));
        Word<uint32_t>(kManager + 0x2C) = At(kApps);
        callv0(0x00532ED0, as0(&Manager::FindServersOnLan));
        Rec(0xA84, {GameVariable<uint32_t>(0x007CCA84)});
        GameVariable<uint32_t>(0x0067EFB8) = 0xFFFFFFFF;
        callv0(0x00532ED0, as0(&Manager::FindServersOnLan));
        GameVariable<uint32_t>(0x0067EFB8) = 0x01020304;
        callv0(0x00532ED0, as0(&Manager::FindServersOnLan));
        Word<uint32_t>(kManager + 0x08) = At(kApps + 0x100);
        Word<uint32_t>(kManager + 0x0C) = 0;
        Word<uint8_t>(kManager + 0x38) = 0xC3;
        callv0(0x00533250, as0(&Manager::SendGameEnded));
        callv0(0x00533300, as0(&Manager::SendReadyResponse));
        Word<uint32_t>(kManager + 0x08) = 0;
        Word<uint32_t>(kManager + 0x0C) = At(kApps + 0x200);
        callv0(0x00533300, as0(&Manager::SendReadyResponse));
        Word<uint32_t>(kManager + 0x08) = At(kApps + 0x100);
        callv0(0x00533300, as0(&Manager::SendReadyResponse));
        GameVariable<bool>(0x007CCA88) = true;
        callv1(0x00532AA0, as1(&Manager::SetJoinMode), 1);
        Rec(0xA88, {GameVariable<uint8_t>(0x007CCA88)});
        for (uint32_t count : {0u, 1u, 0x80000000u}) {
            g_gs_goalCount = count;
            byte(0x00533380, &Manager::HasCreatedGoals);
        }
        callv1(0x005333A0, as1(&Manager::PrepareCreatedGoals), 0xB0B0B0B0);
        call0(0x005333E0, as0(&Manager::GetCreatedGoalsMessage));
        call0(0x005333F0, as0(&Manager::GetCreatedGoalsSize));
    }

    delete s;
    std::vector<uint32_t> trace = g_trace;
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        trace.push_back(ArenaWordOrHigh(g_gs_storage.Get(), kArenaSize, Word<uint32_t>(i)));
    return trace;
}

void TestGameNetState()
{
    g_gs_saved = {{0x007CCA84, 4, {}}, {0x007CCA88, 1, {}}, {0x0067EFB8, 4, {}}};
    SaveAll(g_gs_saved);
    const char* names[] = {"GameNet::Manager: getters and setters", "GameNet::Manager: the network preferences",
                           "GameNet::Manager::IsUntimedGame", "GameNet::Manager: its messages and created goals"};
    for (int scenario = 0; scenario < 4; ++scenario) {
        g_gs_netGame = false;
        g_gs_goalCount = 0;
        std::vector<uint32_t> expected = Run(true, scenario);
        RestoreAll(g_gs_saved);
        std::vector<uint32_t> actual = Run(false, scenario);
        RestoreAll(g_gs_saved);
        Compare(expected, actual, names[scenario]);
    }
    Cleanup();
}

} // namespace

SELFTEST(Early, TestGameNetState, Cleanup);
