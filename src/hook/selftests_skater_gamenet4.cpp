// Self-tests for the network manager's server handlers (src/game/net/gamenet_server_handlers.cpp): the original and
// ours handle the same messages on the same stand-in arena (gamenet_testkit.h), everything they call outside stood in
// for by recording code; the calls, results and arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/random.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;

constexpr uint32_t kGoals = 0x16000;   // stand-in goals, 0x40 apart
constexpr uint32_t kConns = 0x16800;   // the players' connections, 0x40 apart: +0x3C the handle
constexpr uint32_t kNodes = 0x16C00;   // the new players' list nodes, 0x20 apart
constexpr uint32_t kInfos = 0x17000;   // new player infos, 0x80 apart

uint32_t g_sender;          // the connection's player (arena offset; 0 none)
uint32_t g_presentIds;
uint32_t g_local;
int g_playerCount;          // players 0 .. n-1 (observers among them when asked for)
bool g_teamGame;
uint32_t g_gameType;
int g_observers, g_maxObservers;
uint32_t g_goalCount;
uint32_t g_doneBy;          // GoalDoneBy's answer: true for this skater id
bool g_has310;
uint32_t g_nextCam;
void* g_goalVtable[0x20];
void* g_nodeVtable[2];

uint32_t PlayerAt(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }

void Install(StandIns& s)
{
    InstallCommon(s);
    Stub(s, {0x0053AE10}, 4, 0xAE10, [](Regs&) { return g_sender ? At(g_sender) : 0u; });
    s.At({0x0053AEA0}, 4, [](Regs& r) {
        uint32_t id = r.args[0] & 0xFFFF;
        Rec(0xAEA0, {r.ecx, id});
        uint32_t i = id - 0x10;
        return i < 8 && ((g_presentIds >> i) & 1) ? At(PlayerAt(static_cast<int>(i))) : 0u;
    });
    Stub(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return g_local ? At(g_local) : 0u; });
    auto first = [](Regs& r) {
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return g_playerCount > 0 ? At(PlayerAt(0)) : 0u;
    };
    s.At({0x005372C0}, 8, [first](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1] & 0xFF});
        return first(r);
    });
    s.At({0x00537010}, 8, [](Regs& r) {
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        Rec(0x7010, {r.args[1] & 0xFF, index});
        ++index;
        return static_cast<int>(index) < g_playerCount ? At(PlayerAt(static_cast<int>(index))) : 0u;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        // (bytes the original leaves uninitialised - the level change's 5-7, the goal message's 1-3 - not compared)
        uint8_t data[16] = {};
        if (desc->m_data)
            std::memcpy(data, desc->m_data, desc->m_length < 16 ? desc->m_length : 16);
        if (desc->m_id == 0x46)
            std::memset(data + 5, 0, 3);
        if (desc->m_id == 0x6E || desc->m_id == 0x6F)
            std::memset(data + 1, 0, 3);
        Rec(0x7EB0, {r.ecx, r.args[0], desc->m_id, desc->m_length, HashBytes(data, 16),
                     static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                     desc->m_singular, static_cast<uint32_t>(desc->m_delay), desc->m_compression});
        return 0u;
    });
    s.At({0x00458AB0, AddressOf(&Net::App::StreamMessage)}, 32, [](Regs& r) {
        Rec(0x8AB0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF, r.args[3], Str(r.args[4]),
                     r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    Stub(s, {0x00458CA0, AddressOf(&Net::App::SendData)}, 4, 0x8CA0);
    Stub(s, {0x00457300, AddressOf(&Net::App::TerminateConnection)}, 4, 0x7300);
    Stub(s, {0x00539C70, AddressOf(&GameNet::Manager::CountObservers)}, 0, 0x9C70, [](Regs&) { return static_cast<uint32_t>(g_observers); });
    Stub(s, {0x00532D60, AddressOf(&Manager::GetMaxObservers)}, 0, 0x2D60,
         [](Regs&) { return static_cast<uint32_t>(g_maxObservers); });
    Stub(s, {0x0053CB70}, 8, 0xCB70);
    Stub(s, {0x00565790}, 4, 0x5790);
    Stub(s, {0x00543310}, 0, 0x3310, [](Regs&) { return g_has310 ? 1u : 0u; });
    Stub(s, {0x005433A0}, 0, 0x33A0);
    Stub(s, {0x0053AAC0}, 0, 0xAAC0);
    Stub(s, {0x0053A580}, 0, 0xA580, [](Regs&) { return g_nextCam ? At(g_nextCam) : 0u; });
    Stub(s, {0x0053A910}, 4, 0xA910);
    Stub(s, {0x0058D690}, 4, 0xD690, [](Regs&) { return At(kMisc + 0x300 + 0x40); });
    StubCdecl(s, {0x004F4970}, 0x4970, 0, [](Regs&) { return At(kMisc + 0x500); });
    Stub(s, {0x004F45B0}, 0, 0x45B0, [](Regs& r) { return r.ecx + 0x34; });
    StubCdecl(s, {0x004E8470}, 0x8470, 0, [](Regs&) { return At(kMisc + 0x600); });
    Stub(s, {0x004E7E50}, 4, 0x7E50, [](Regs&) { return g_goalCount; });
    Stub(s, {0x004EBB70}, 12, 0xBB70, [](Regs&) { return 0x30u; });
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return 0x00ABCDEFu; });
    Stub(s, {0x00544670}, 4, 0x4670);
    StubCdecl(s, {0x005591B0}, 0x91B0, 0, [](Regs&) { return At(kMisc + 0x700); });
    Stub(s, {0x00557F90}, 8, 0x7F90, [](Regs& r) {
        uint32_t name = r.args[0];
        return name >= 0x60A10000 && name < 0x60A10010 ? At(kGoals + 0x40 * (name - 0x60A10000)) : 0u;
    });
    g_goalVtable[0x50 / 4] = s.Thunk(4, [](Regs& r) {
        Rec(0x60F0, {r.ecx, r.args[0]});
        return r.args[0] == g_doneBy ? 1u : 0u;
    });
    g_goalVtable[0x6C / 4] = s.Thunk(4, [](Regs& r) {
        Rec(0x610C, {r.ecx, r.args[0]});
        return 0u;
    });
    g_nodeVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x40DE, {r.ecx, r.args[0]});
        return r.ecx;
    });
    Stub(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    Stub(s, {0x005532A0}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x00543560, AddressOf(&GameNet::NewPlayerInfo::DestroyBody)}, 0, 0x3560);
    Stub(s, {0x00544340}, 4, 0x4340);
    StubCdecl(s, {0x00403DE0, AddressOf(&Mth::Rnd)}, 0x3DE0, 1, [](Regs&) { return 0xFFFFFF07u; });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_sender = PlayerAt(2);
        g_presentIds = 0xFF;
        g_local = PlayerAt(0);
        g_playerCount = 4;
        g_teamGame = false;
        g_gameType = 0;
        g_observers = 1;
        g_maxObservers = 2;
        g_goalCount = 0;
        g_doneBy = 0xFFFFFFFF;
        g_has310 = false;
        g_nextCam = 0;
        for (int i = 0; i < 8; ++i) {
            Word<uint32_t>(PlayerAt(i) + 0x18) = At(kConns + 0x40 * static_cast<uint32_t>(i));
            Word<int>(kConns + 0x40 * static_cast<uint32_t>(i) + 0x3C) = 0x20 + i;
            Word<int>(PlayerAt(i) + 0x30) = i % 2;
        }
        Word<uint32_t>(PlayerAt(0) + 0x1D0) = 0x00000001; // local
        Word<uint32_t>(PlayerAt(1) + 0x1D0) = 0x00008800; // carrying team 0's flag
        Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x00009000; // carrying team 1's flag
        Word<uint32_t>(PlayerAt(3) + 0x1D0) = 0x00000084; // an observer with the crown
        for (uint32_t g = 0; g < 16; ++g)
            Word<void**>(kGoals + 0x40 * g) = g_goalVtable;
        Word<uint32_t>(kManager + 0x04) = At(kApp + 0x400); // the server app
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

void Observers(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    // Observe: unknown, observing already, full (remote), the local player when full, a remote one (the camera's /
    // not; the camera onto itself again / another; 0x00543310 true / false).
    g_sender = 0;
    handle(0x0052E060, GameNet::HandleObserveRequest, 0x4A);
    g_sender = PlayerAt(3);
    handle(0x0052E060, GameNet::HandleObserveRequest, 0x4A);
    g_sender = PlayerAt(2);
    g_observers = 2;
    handle(0x0052E060, GameNet::HandleObserveRequest, 0x4A);
    g_sender = PlayerAt(0);
    handle(0x0052E060, GameNet::HandleObserveRequest, 0x4A);
    g_observers = 1;
    for (int pass = 0; pass < 4; ++pass) {
        g_sender = PlayerAt(4 + pass);
        Word<uint32_t>(PlayerAt(4 + pass) + 0x1D0) = 0;
        Word<uint32_t>(kManager + 0x1CC) = pass >= 1 ? At(PlayerAt(4 + pass)) : At(PlayerAt(1));
        g_nextCam = pass == 2 ? PlayerAt(4 + pass) : PlayerAt(1);
        g_has310 = pass % 2 == 1;
        handle(0x0052E060, GameNet::HandleObserveRequest, 0x4A);
    }
    // Disconnects: a remote player (observer or not), the local one, a waiting new player (found / not), none.
    g_sender = PlayerAt(2);
    handle(0x0052FCC0, GameNet::HandleDisconnect, 0x0B);
    g_sender = PlayerAt(3);
    handle(0x0052FCC0, GameNet::HandleDisconnect, 0x0B);
    g_sender = PlayerAt(0);
    handle(0x0052FCC0, GameNet::HandleDisconnect, 0x0B);
    g_sender = 0;
    // the list: head (data -1) at kNodes, then three nodes; the second's info has the context's connection
    auto node = [](int i) { return kNodes + 0x20 * static_cast<uint32_t>(i); };
    Word<uint32_t>(node(0) + 4) = 0xFFFFFFFF;
    for (int i = 1; i <= 3; ++i) {
        Word<void**>(node(i)) = g_nodeVtable;
        Word<uint32_t>(node(i) + 4) = At(kInfos + 0x80 * static_cast<uint32_t>(i));
        Word<uint32_t>(node(i) + 0xC) = At(node(i == 3 ? 0 : i + 1));
        Word<uint32_t>(kInfos + 0x80 * static_cast<uint32_t>(i) + 0x14) = i == 2 ? At(kConn) : At(kMisc + 0xF80);
    }
    Word<uint32_t>(kManager + 0x13C) = At(node(1));
    handle(0x0052FCC0, GameNet::HandleDisconnect, 0x0B);
    Word<uint32_t>(kInfos + 0x80 * 2 + 0x14) = 0;
    handle(0x0052FCC0, GameNet::HandleDisconnect, 0x0B);
    Word<uint32_t>(kManager + 0x13C) = At(node(0));
    handle(0x0052FCC0, GameNet::HandleDisconnect, 0x0B);
}

void GameData(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    Word<uint32_t>(kMsg) = 0xB0B0B0B0;
    Word<uint8_t>(kMsg + 4) = 'E';
    handle(0x0052E9E0, GameNet::HandleRequestGameData, 0x6B);
    g_goalCount = 2;
    Word<uint8_t>(kMsg + 4) = 'F';
    handle(0x0052E9E0, GameNet::HandleRequestGameData, 0x6B);
    // The end of games: unknown, another game, again, the last to end, not the last.
    Word<uint8_t>(kManager + 0x38) = 0x21;
    for (int i = 0; i < 4; ++i)
        Word<uint32_t>(PlayerAt(i) + 0x1D0) |= 0x100;
    Word<uint32_t>(PlayerAt(2) + 0x1D0) &= ~0x100u;
    Message({0x21});
    g_sender = 0;
    handle(0x0052EC40, GameNet::HandleGameEndedReport, 0x59);
    g_sender = PlayerAt(2);
    Message({0x22});
    handle(0x0052EC40, GameNet::HandleGameEndedReport, 0x59);
    Message({0x21});
    handle(0x0052EC40, GameNet::HandleGameEndedReport, 0x59);
    handle(0x0052EC40, GameNet::HandleGameEndedReport, 0x59);
    Word<uint32_t>(PlayerAt(1) + 0x1D0) &= ~0x100u;
    Word<uint32_t>(PlayerAt(2) + 0x1D0) &= ~0x100u;
    handle(0x0052EC40, GameNet::HandleGameEndedReport, 0x59);
    // Team changes: unknown player, no skater, in a game, in the lobby, after the game.
    g_presentIds = 0x07;
    Message({0x15, 0x02});
    handle(0x0052EE10, GameNet::HandleTeamChangeRequest, 0x67);
    Word<uint32_t>(PlayerAt(1) + 0x14) = 0;
    Message({0x11, 0x02});
    handle(0x0052EE10, GameNet::HandleTeamChangeRequest, 0x67);
    Message({0x12, 0xFE});
    Word<uint8_t>(kManager + 0x3A) = 0;
    handle(0x0052EE10, GameNet::HandleTeamChangeRequest, 0x67);
    g_gameType = 0x1C471C60;
    handle(0x0052EE10, GameNet::HandleTeamChangeRequest, 0x67);
    g_gameType = 0;
    Word<uint8_t>(kManager + 0x3A) = 1;
    handle(0x0052EE10, GameNet::HandleTeamChangeRequest, 0x67);
    // Checksum answers: right, wrong, wrong again (noted).
    Word<uint32_t>(kMsg) = 0x12345678;
    Word<uint32_t>(kMsg + 4) = 0x12345678 ^ 0xDEADFACE;
    Word<uint8_t>(kManager + 0x3B) = 0;
    handle(0x00532170, GameNet::HandleChecksumResponse, 0x81);
    Word<uint32_t>(kMsg + 4) = 0;
    handle(0x00532170, GameNet::HandleChecksumResponse, 0x81);
    handle(0x00532170, GameNet::HandleChecksumResponse, 0x81);
}

void Goals(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    Word<uint8_t>(kManager + 0x38) = 0x31;
    auto report = [&](uint32_t address, MsgCode ours, uint8_t id, uint8_t game, uint32_t goal) {
        Message({game, 0xEE, 0xEE, 0xEE});
        Word<uint32_t>(kMsg + 4) = goal;
        handle(address, ours, id);
    };
    for (int team = 0; team < 2; ++team) {
        g_teamGame = team != 0;
        for (uint32_t address : {0x0052EF40u, 0x0052F1A0u}) {
            MsgCode ours = address == 0x0052EF40 ? GameNet::HandleGoalCompletedReport : GameNet::HandleGoalStartedReport;
            uint8_t id = address == 0x0052EF40 ? 0x6E : 0x6F;
            report(address, ours, id, 0x30, 0x60A10001); // another game
            report(address, ours, id, 0x31, 0x77777777); // no goal
            g_doneBy = 0xFFFFFFFF;
            report(address, ours, id, 0x31, 0x60A10002);
            g_doneBy = 0x10; // done by player 0's skater (team 0; the sender, player 2, is team 0)
            report(address, ours, id, 0x31, 0x60A10002);
            g_doneBy = 0x12; // by the sender's
            report(address, ours, id, 0x31, 0x60A10002);
            g_doneBy = 0x11; // by another team's
            report(address, ours, id, 0x31, 0x60A10002);
            Word<uint32_t>(PlayerAt(2) + 0x14) = 0;
            report(address, ours, id, 0x31, 0x60A10002);
            Word<uint32_t>(PlayerAt(2) + 0x14) = At(kSkaters + 0x200);
        }
    }
}

void Drops(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    // CTF: no sender, no flag, a remote carrier (local player on the flag's team / not), the local carrier.
    g_gameType = 0x6C5FF266;
    g_sender = 0;
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    g_sender = PlayerAt(3);
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    g_sender = PlayerAt(2);
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    Word<uint32_t>(PlayerAt(1) + 0x1D0) = 0x00008800;
    Word<int>(PlayerAt(0) + 0x30) = 1;
    g_sender = PlayerAt(1);
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    Word<uint32_t>(PlayerAt(0) + 0x1D0) = 0x00001001;
    g_sender = PlayerAt(0);
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    g_local = PlayerAt(3); // an observer on the flag's team
    Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x00001000;
    g_sender = PlayerAt(2);
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    g_local = 0;
    Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x00002000;
    g_sender = PlayerAt(2);
    handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    // King: net king / king / another mode; with / without the crown.
    for (uint32_t mode : {0x6EF8FDA0u, 0x5D32129Cu, 0x12345678u}) {
        g_gameType = mode;
        g_sender = PlayerAt(3);
        Word<uint32_t>(PlayerAt(3) + 0x1D0) = 0x00008084;
        handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
        g_sender = PlayerAt(1);
        handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
        g_sender = 0;
        handle(0x0052FDA0, GameNet::HandleDropCarried, 0x57);
    }
}

void TestGameNetServerHandlers()
{
    SaveGlobals();
    RunCase("GameNet server handlers: observe requests, disconnects", Observers);
    RunCase("GameNet server handlers: game data, game ended, team changes, checksums", GameData);
    RunCase("GameNet server handlers: goal attack reports", Goals);
    RunCase("GameNet::HandleDropCarried", Drops);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestGameNetServerHandlers, GameNetKit::Cleanup);
