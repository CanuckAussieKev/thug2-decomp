// Self-tests for the network manager's script commands (src/game/net/gamenet_commands.cpp): the original and ours run
// on the same stand-in arena (gamenet_testkit.h), everything they call outside stood in for by recording code; the
// calls, results and arena are compared, case by case. (The bytes the original leaves uninitialised in what it sends
// aren't compared.)

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/task.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Script::CScript;
using Script::CStruct;

constexpr uint32_t kScript = 0x14000;   // a stand-in CScript (+0x14: its params, a stand-in pointer)
constexpr uint32_t kLobby = 0x14200;

using CFunc = bool(__cdecl*)(CStruct*, CScript*);

bool g_netGame, g_peerConnected, g_haveLobbyPeer, g_inList;
uint8_t g_inGroupRoom;
uint32_t g_time;
int g_numTeams;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answer; // (structure, name) -> value

uint32_t Answer(Regs& r, uint32_t tag, uint32_t name)
{
    Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
    auto it = g_answer.find({r.ecx, name});
    if (it == g_answer.end())
        return 0u;
    *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
    return 1u;
}

void Install(StandIns& s)
{
    InstallCommon(s);
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_netGame ? 1u : 0u; });
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_time += 1000; });
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    Stub(s, {0x00406850, AddressOf(&Tsk::BaseTask::InList)}, 0, 0x6850, [](Regs&) { return g_inList ? 1u : 0u; });
    Stub(s, {0x00553130}, 0, 0x3130, [](Regs&) { return static_cast<uint32_t>(g_numTeams); });
    Stub(s, {0x0053F880, AddressOf(&GameNet::LobbyMan::GetPeer)}, 0, 0xF880, [](Regs&) { return g_haveLobbyPeer ? At(kLobby + 0x100) : 0u; });
    StubCdecl(s, {0x005FC730}, 0xC730, 1, [](Regs&) { return g_peerConnected ? 1u : 0u; });
    Stub(s, {0x0053F890}, 0, 0xF890, [](Regs&) { return static_cast<uint32_t>(g_inGroupRoom); });
    Stub(s, {0x005453E0, AddressOf(&GameNet::Manager::SetServiceState)}, 4, 0x53E0);
    Stub(s, {0x005451B0}, 4, 0x51B0);
    Stub(s, {0x00534AF0, AddressOf(&Manager::ReattemptJoin)}, 4, 0x4AF0);
    Stub(s, {0x00564D80}, 0, 0x4D80, [](Regs&) { return At(kMisc + 0x600); });
    Stub(s, {0x0058D690}, 4, 0xD690, [](Regs&) { return At(kMisc + 0x680); });
    StubCdecl(s, {0x004F4970}, 0x4970, 0, [](Regs&) { return At(kMisc + 0x6C0); });
    Stub(s, {0x004F45B0}, 0, 0x45B0, [](Regs&) { return At(kMisc + 0x700); });
    // Preferences and parameters.
    Stub(s, {0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, 0xC4A0, [](Regs& r) { return r.args[0]; });
    Stub(s, {0x0046C590, AddressOf(&Prefs::Preferences::GetPreferenceChecksum)}, 8, 0xC590,
         [](Regs& r) { return r.args[0] ^ 0x11111111u; });
    s.At({0x00476EC0, 0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) { return Answer(r, 0x6950, r.args[0]); });
    s.At({0x00476A00, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return Answer(r, 0x6A00, Str(r.args[0])); });
    s.At({0x00476CB0, 0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         12, [](Regs& r) { return Answer(r, 0x6630, r.args[0]); });
    s.At({0x00476680, AddressOf(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) { return Answer(r, 0x6680, Str(r.args[0])); });
    s.At({0x00477C30, AddressOf(static_cast<void (CStruct::*)(const char*, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Rec(0x7C30, {r.ecx, Str(r.args[0]), r.args[1]});
        return 0u;
    });
    StubCdecl(s, {0x004790C0, AddressOf(static_cast<int (*)(const char*)>(&Script::GetInteger))}, 0x90C0, 0,
              [](Regs& r) {
                  Rec(0x90C1, {Str(r.args[0])});
                  return 40u;
              });
    // Messages.
    s.At({0x00459DC0, AddressOf(&Net::Client::StreamMessageToServer)}, 28, [](Regs& r) {
        Rec(0x9DC0, {r.ecx, r.args[0] & 0xFF, r.args[1] & 0xFFFF, r.args[2], Str(r.args[3]), r.args[4] & 0xFF,
                     r.args[5] & 0xFF, r.args[6] & 0xFF});
        return 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[0]);
        uint8_t data[0x40] = {};
        if (desc->m_data)
            std::memcpy(data, desc->m_data, desc->m_length < 0x40 ? desc->m_length : 0x40);
        if (desc->m_id == 0x60) // (+0x18: never written by the original)
            std::memset(data + 0x18, 0, 4);
        if (desc->m_id == 0x63) // (the bit's three bytes after it)
            std::memset(data + 5, 0, 3);
        for (int i = 0; i < 0x40; i += 4)
            g_trace.push_back(Rel(*reinterpret_cast<uint32_t*>(data + i)));
        Rec(0x9E50, {r.ecx, desc->m_id, desc->m_stream, desc->m_length, static_cast<uint32_t>(desc->m_priority),
                     static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_singular,
                     static_cast<uint32_t>(desc->m_delay), desc->m_forcedSequenceId, desc->m_compression});
        return 0u;
    });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_answer.clear();
        g_netGame = g_peerConnected = g_haveLobbyPeer = g_inList = false;
        g_inGroupRoom = 0;
        g_time = 0x10000;
        g_numTeams = 3;
        Word<uint32_t>(kManager + 0x10) = At(kLobby);
        Word<uint32_t>(kManager + 0x08) = At(kApp);
        Word<uint32_t>(kScript + 0x14) = 0x0BAD0014; // the script's params (a stand-in pointer)
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

// A command, original or ours, on `params` (a stand-in pointer answered by g_answer) and the stand-in script.
void Command(bool original, uint32_t address, CFunc ours, uint32_t params = 0x0BAD0000)
{
    auto* p = reinterpret_cast<CStruct*>(params);
    auto* script = &Word<CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(p, script) : ours(p, script);
    Rec(0xC0DE, {address, result ? 1u : 0u});
    // (AL as the original leaves it, for those returning a byte they read)
}
uint32_t Byte(bool original, uint32_t address, CFunc ours)
{
    // (AL as each leaves it: some return a byte of the manager or the lobby as it is)
    using Raw = uint32_t(__cdecl*)(CStruct*, CScript*);
    auto* script = &Word<CScript>(kScript);
    Raw code = original ? Original<Raw>(address) : reinterpret_cast<Raw>(ours);
    return code(nullptr, script) & 0xFF;
}

void Simple(bool original)
{
    using namespace GameNet;
    Word<uint32_t>(kManager + 0xF8) = At(kMisc + 0x500);
    Word<uint32_t>(kManager + 0xEC) = At(kMisc + 0x540);
    Command(original, 0x00535DA0, CFunc_StartCTFGame);
    Command(original, 0x00535DC0, CFunc_EndCTFGame);
    // The flag base warning: the time moves 1000 a call (the first call: 0x10000 + 1000 after 0).
    GameVariable<uint32_t>(0x007CCE24) = 0;
    for (int i = 0; i < 6; ++i) {
        Command(original, 0x00535DE0, CFunc_DisplayFlagBaseWarning);
        Rec(0xF001, {GameVariable<uint32_t>(0x007CCE24)});
        g_time += 700 * static_cast<uint32_t>(i);
    }
    GameVariable<uint32_t>(0x007CCE24) = g_time - 2000; // exactly 3000 at the next call: not yet
    Command(original, 0x00535DE0, CFunc_DisplayFlagBaseWarning);
    Rec(0xF002, {GameVariable<uint32_t>(0x007CCE24)});
    for (int pending : {0, 1}) {
        Word<uint8_t>(kManager + 0x1B4) = static_cast<uint8_t>(pending);
        Rec(0xF003, {Byte(original, 0x00535E20, CFunc_NetworkGamePending)});
    }
    Word<uint8_t>(kManager + 0x3A) = 0;
    Command(original, 0x00535E30, CFunc_MarkGameOver);
    Rec(0xF00C, {Word<uint8_t>(kManager + 0x3A)});
    // Join modes: none given, 0, 1, 7, -1.
    GameVariable<uint8_t>(0x007CCA88) = 1;
    Command(original, 0x00535E40, CFunc_SetJoinMode, 0x0BAD0040);
    Rec(0xF004, {Word<uint32_t>(kManager + 0x3C), GameVariable<uint8_t>(0x007CCA88)});
    for (int mode : {0, 1, 7, -1}) {
        g_answer[{0x0BAD0040, 0}] = static_cast<uint32_t>(mode);
        GameVariable<uint8_t>(0x007CCA88) = 1;
        Command(original, 0x00535E40, CFunc_SetJoinMode, 0x0BAD0040);
        Rec(0xF004, {Word<uint32_t>(kManager + 0x3C), GameVariable<uint8_t>(0x007CCA88)});
    }
    for (int teams : {0, 4})
        g_numTeams = teams, Command(original, 0x00535E90, CFunc_GetNumTeams);
    // The lobby's peer: none, not connected, connected.
    Command(original, 0x00535EB0, CFunc_ConnectedToPeer);
    g_haveLobbyPeer = true;
    Command(original, 0x00535EB0, CFunc_ConnectedToPeer);
    g_peerConnected = true;
    Command(original, 0x00535EB0, CFunc_ConnectedToPeer);
    for (int room : {0, 1, 2}) {
        g_inGroupRoom = static_cast<uint8_t>(room);
        Rec(0xF005, {Byte(original, 0x00535EE0, CFunc_InGroupRoom)});
    }
    // Auto serving: net game x on the server x host mode.
    for (int net = 0; net < 2; ++net)
        for (int flags : {0, 1, 0xFE, 0xFF})
            for (int mode : {0, 1, 2}) {
                g_netGame = net != 0;
                Word<uint8_t>(kManager) = static_cast<uint8_t>(flags);
                Word<int>(kManager + 0x40) = mode;
                Rec(0xF006, {Byte(original, 0x00535EF0, CFunc_IsAutoServing)});
            }
    for (int in = 0; in < 2; ++in) {
        g_inList = in != 0;
        Rec(0xF007, {Byte(original, 0x00535F20, CFunc_ChangeLevelPending)});
    }
    Command(original, 0x00535F30, CFunc_CancelMotdDownload);
    Command(original, 0x00535F40, CFunc_DownloadMotd);
    // Services: available (MOTD in or not), not (0 / 2).
    for (int state : {1, 0, 2})
        for (int motd : {0, 1}) {
            Word<uint8_t>(kManager + 0xFE0A) = static_cast<uint8_t>(state);
            Word<uint8_t>(kManager + 0xFE09) = static_cast<uint8_t>(motd);
            Command(original, 0x00535F50, CFunc_ServicesAreAvailable);
            Rec(0xF008, {Byte(original, 0x00535FA0, CFunc_AlreadyGotMotd)});
        }
    for (int ready : {0, 1}) {
        Word<uint8_t>(kManager + 0x18C) = static_cast<uint8_t>(ready);
        Rec(0xF009, {Byte(original, 0x00535FB0, CFunc_JoinServerComplete)});
    }
    // Just started: 4000, 5000 (not), and a start time in the future (wraps: not).
    for (uint32_t ago : {4000u - 1000u, 5000u - 1000u, 0u - 3000u}) {
        GameVariable<uint32_t>(0x007CCDF4) = g_time - ago;
        Rec(0xF00A, {Byte(original, 0x00535FC0, CFunc_JustStartedNetGame)});
    }
    Command(original, 0x00536240, CFunc_ReattemptJoinServer);
    Command(original, 0x00536460, CFunc_HasSignedDisclaimer);
    Word<uint8_t>(kManager + 0x3A) = 0;
}

void Requests(bool original)
{
    using namespace GameNet;
    // The start request, in a network game and not (the game type's preferences). (Without a game type the original
    // sends its uninitialised local: not compared.)
    for (int net = 0; net < 2; ++net) {
            g_netGame = net != 0;
            g_answer.clear();
            g_answer[{0x9834C4FA, 0x21902065}] = 0x12340000u + static_cast<uint32_t>(net);
            Word<uint8_t>(kManager + 0x1B4) = 0;
            Command(original, 0x00535FE0, CFunc_FCFSRequestStartGame);
            Rec(0xF00B, {Word<uint8_t>(kManager + 0x1B4)});
        }
    // The level: none, a level, the created park.
    g_answer.clear();
    Command(original, 0x00536170, CFunc_FCFSRequestChangeLevel, 0x0BAD0100);
    g_answer[{0x0BAD0100, 0x651533EC}] = 0x5150AAAA;
    Command(original, 0x00536170, CFunc_FCFSRequestChangeLevel, 0x0BAD0100);
    g_answer[{0x0BAD0100, 0x651533EC}] = 0xB664035D;
    Command(original, 0x00536170, CFunc_FCFSRequestChangeLevel, 0x0BAD0100);
    // Pro set and goal toggles.
    g_answer[{0x0BAD0200, Hash("bit")}] = 0x1FF; // (only its byte goes)
    g_answer[{0x0BAD0200, Hash("param_id")}] = 0xBEEF0001;
    Command(original, 0x00536260, CFunc_FCFSRequestToggleProSet, 0x0BAD0200);
    g_answer[{0x0BAD0200, Hash("bit")}] = 3;
    Command(original, 0x00536260, CFunc_FCFSRequestToggleProSet, 0x0BAD0200);
    g_answer[{0x0BAD0300, 0xA1DC81F9}] = 0x60A10007;
    Command(original, 0x005362F0, CFunc_FCFSRequestToggleGoalSelection, 0x0BAD0300);
    // The pro set flags.
    for (uint32_t flags : {0u, 0x7Fu, 0x55u, 0xFFFFFF80u, 0x2Au}) {
        Word<uint32_t>(kManager + 0x194) = flags;
        Command(original, 0x00536360, CFunc_ResetProSetFlags);
    }
}

void TestNetCommands()
{
    SaveGlobals({{0x007CCE24, 4, {}}, {0x007CCDF4, 4, {}}, {0x007CCA88, 1, {}}});
    RunCase("GameNet commands: CTF, state, join mode, teams, lobby, MOTD", Simple);
    RunCase("GameNet commands: the first-come-first-served requests, pro set flags", Requests);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetCommands, GameNetKit::Cleanup);
