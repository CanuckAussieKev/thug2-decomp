// Self-tests for the network manager's script commands, second part (src/game/net/gamenet_game_commands.cpp): the
// original and ours on the same stand-in arena (gamenet_testkit.h) with real player lists, everything they call
// outside stood in for by recording code (the players' flag getters and the list walks run as they are); the calls,
// results and arena are compared, case by case. (The bytes the original leaves uninitialised in what it sends aren't
// compared.)

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into the test's own arena)

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/task.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Mem::ListNode;
using Script::CScript;
using Script::CStruct;

using CFunc = bool(__cdecl*)(CStruct*, CScript*);

constexpr uint32_t kNodes = 0x14000;   // list nodes, 0x20 apart
constexpr uint32_t kConns = 0x14400;   // players' connections, 0x80 apart (+0x60: ready)
constexpr uint32_t kGoals = 0x14C00;   // goals (a vtable pointer), 0x20 apart
constexpr uint32_t kScript = 0x15000;  // the calling script (+0x14: its params, a stand-in pointer)
constexpr uint32_t kCrownNodes = 0x15100; // stand-in node structures, 0x10 apart

bool g_netGame, g_teamGame, g_inList1, g_inList2, g_loggedIn, g_peer;
uint32_t g_time, g_gameType;
int g_goalCount, g_crownNodes;
uint32_t g_localSkater;
void* g_goalVtable[32];
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answer;

uint32_t Node(int i) { return kNodes + 0x20 * static_cast<uint32_t>(i); }
uint32_t Player(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
void Players(std::initializer_list<int> which)
{
    auto* h = &Word<ListNode>(kManager + 0x11C);
    h->data = reinterpret_cast<void*>(-1);
    ListNode* last = h;
    for (int i : which) {
        auto* n = &Word<ListNode>(Node(i));
        n->data = reinterpret_cast<void*>(At(Player(i)));
        last->next = n;
        n->prev = last;
        last = n;
    }
    last->next = h;
    h->prev = last;
}
void Flags(int player, uint32_t flags) { Word<uint32_t>(Player(player) + 0x1D0) = flags; }

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
    Stub(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_time += 100; });
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    s.At({0x00406850, AddressOf(&Tsk::BaseTask::InList)}, 0, [](Regs& r) {
        Rec(0x6850, {r.ecx});
        return (r.ecx == At(kMisc + 0x500) ? g_inList1 : g_inList2) ? 1u : 0u;
    });
    Stub(s, {0x0053F880}, 0, 0xF880, [](Regs&) { return g_peer ? At(kMisc + 0x480) : 0u; });
    StubCdecl(s, {0x005FC730}, 0xC730, 1, [](Regs&) { return 1u; });
    Stub(s, {0x0053F810}, 0, 0xF810, [](Regs&) { return 1u; });
    StubCdecl(s, {0x005FCCF0}, 0xCCF0, 1);
    StubCdecl(s, {0x005FBAD0}, 0xBAD0, 1);
    Stub(s, {0x00535B70, AddressOf(&Manager::GetGameTypePreference)}, 0, 0x5B70, [](Regs&) { return g_gameType; });
    Stub(s, {0x00536A40, AddressOf(&Manager::GetGameTypeName)}, 0, 0x6A40, [](Regs&) { return At(kMisc + 0x4C0); });
    s.At({0x005376F0, AddressOf(&Manager::CreateNetPanelMessage)}, 32, [](Regs& r) {
        Rec(0x76F0, {r.ecx, r.args[0] & 0xFF, r.args[1], r.args[2], r.args[3], r.args[4], Str(r.args[5]),
                     r.args[6] & 0xFF, r.args[7]});
        return 0u;
    });
    s.At({0x005434A0}, 0, [](Regs& r) {
        Rec(0x34A0, {r.ecx});
        return (r.ecx - g_base) / 0x10;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[1]);
        uint16_t defined = desc->m_id == 0x70 ? 5 : desc->m_length; // (0x70: the bit's three bytes after it are the stack's)
        uint32_t data = desc->m_data ? HashBytes(static_cast<const uint8_t*>(desc->m_data), defined) : 0;
        Rec(0x7EB0, {r.ecx, r.args[0], desc->m_id, desc->m_stream, desc->m_length, data,
                     static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                     desc->m_singular, static_cast<uint32_t>(desc->m_delay)});
        return 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        RecordDesc(0x9E50, r.ecx, reinterpret_cast<const Net::MsgDesc*>(r.args[0]));
        return 0u;
    });
    // Goals.
    StubCdecl(s, {0x005591B0}, 0x91B0, 0, [](Regs&) { return At(kMisc + 0x700); });
    Stub(s, {0x00469490}, 0, 0x9490, [](Regs&) { return static_cast<uint32_t>(g_goalCount); });
    Stub(s, {0x00557FB0}, 4, 0x7FB0, [](Regs& r) { return At(kGoals + 0x20 * r.args[0]); });
    Stub(s, {0x005542F0}, 0, 0x42F0, [](Regs& r) { return ((r.ecx - At(kGoals)) / 0x20) % 3 != 1 ? 1u : 0u; });
    Stub(s, {0x00554280}, 0, 0x4280, [](Regs& r) { return 0x60A10000u + (r.ecx - At(kGoals)) / 0x20; });
    g_goalVtable[29] = s.Thunk(0, [](Regs& r) { Rec(0x60A9, {r.ecx}); return 0u; });
    // Teams.
    Stub(s, {0x005535A0}, 4, 0x35A0);
    Stub(s, {0x00566520}, 0, 0x6520, [](Regs&) { return g_localSkater ? At(g_localSkater) : 0u; });
    Stub(s, {0x00566370}, 12, 0x6370);
    s.At({0x005433D0}, 0, [](Regs& r) { // (the flag a player carries)
        Rec(0x33D0, {r.ecx});
        return ((r.ecx - At(kPlayers)) / kPlayerSize) % 3;
    });
    // GameSpy presence.
    Stub(s, {0x005480B0}, 0, 0x80B0, [](Regs&) { return g_loggedIn ? 1u : 0u; });
    Stub(s, {0x00549CB0}, 4, 0x9CB0, [](Regs& r) { return r.args[0] % 2; });
    s.At({0x00626EFB}, 0, [](Regs& r) { // _stricmp (the game's C runtime; ours uses the DLL's: not recorded)
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    // Parameters.
    s.At({0x00476EC0, 0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) { return Answer(r, 0x6950, r.args[0]); });
    s.At({0x00476A00, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return Answer(r, 0x6A00, Str(r.args[0])); });
    s.At({0x00476CB0, 0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         12, [](Regs& r) { return Answer(r, 0x6630, r.args[0]); });
    s.At({0x00476680, AddressOf(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) { return Answer(r, 0x6680, Str(r.args[0])); });
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) { return Answer(r, 0x6590, r.args[0]); });
    s.At({0x00477C30, AddressOf(static_cast<void (CStruct::*)(const char*, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Rec(0x7C30, {r.ecx, Str(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArray))}, 8,
         [](Regs& r) {
             auto* a = reinterpret_cast<Script::CArray*>(r.args[1]);
             Rec(0x8B00, {r.ecx, r.args[0], a->m_type, a->m_size, a->m_elements[0], a->m_elements[1], a->m_elements[2],
                          a->m_elements[3]});
             return 0u;
         });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8,
         [](Regs& r) {
             Rec(0x8670, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    // Arrays: the elements from the arena's heap (the real ones free through the stood-in heap's headers).
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        auto* a = reinterpret_cast<Script::CArray*>(r.ecx);
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        a->m_type = static_cast<Script::ESymbolType>(r.args[1] & 0xFF);
        a->m_size = r.args[0];
        a->m_elements = &Word<uint32_t>(g_heapNext);
        std::memset(a->m_elements, 0, 4 * r.args[0]);
        g_heapNext += (4 * r.args[0] + 15) & ~15u;
        return 0u;
    });
    Stub(s, {0x0046CAB0, AddressOf(&Script::CArray::Clear)}, 0, 0xCAB0);
    StubCdecl(s, {0x00408FD0, AddressOf(&Script::DeleteArray)}, 0x8FD0, 1);
    // The crown and nodes.
    Stub(s, {0x00578240, AddressOf(&Obj::CCrownObject::Construct)}, 0, 0x8240, [](Regs& r) { return r.ecx; });
    Stub(s, {0x00578490, AddressOf(&Obj::CCrownObject::InitFromNode)}, 8, 0x8490);
    Stub(s, {0x005782B0, AddressOf(&Obj::CCrownObject::PutOn)}, 4, 0x82B0);
    StubCdecl(s, {0x0045C1D0, AddressOf(&SkateScript::StartNodeWalk)}, 0xC1D0, 1, [](Regs&) {
        g_crownNodes = 0;
        return 0u;
    });
    StubCdecl(s, {0x0045C1F0, AddressOf(&SkateScript::NextNode)}, 0xC1F0, 0, [](Regs&) {
        return g_crownNodes < 7 ? At(kCrownNodes + 0x10 * static_cast<uint32_t>(g_crownNodes++)) : 0u;
    });
    StubCdecl(s, {0x0045C200, AddressOf(&SkateScript::EndNodeWalk)}, 0xC200, 0);
    StubCdecl(s, {0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0xBE40, 1,
              [](Regs& r) { return 0x4E0D0000u + (r.args[0] - At(kCrownNodes)) / 0x10; });
    StubCdecl(s, {0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0xC050, 1, [](Regs&) { return 1u; });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        Log("self-test: %s (%s)", what, original ? "original" : "ours");
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_answer.clear();
        g_netGame = g_teamGame = g_inList1 = g_inList2 = g_loggedIn = g_peer = false;
        g_time = 0x10000;
        g_gameType = 0;
        g_goalCount = 0;
        g_localSkater = 0;
        GameVariable<uint8_t>(0x007CCA82) = 0;
        GameVariable<uint8_t>(0x007CCE1C) = 0;
        GameVariable<uint8_t>(0x007CCE1E) = 0;
        GameVariable<uint32_t>(0x007CCDF4) = 0;
        GameVariable<uint32_t>(0x007CCE28) = 0;
        Word<uint32_t>(kManager + 0x00) = 0x5A000000;
        Word<uint32_t>(kManager + 0x04) = At(kApp);          // the server
        Word<uint32_t>(kManager + 0x08) = At(kApp + 0x400);  // client 0
        Word<uint32_t>(kManager + 0x10) = At(kMisc + 0x440); // the lobby
        Word<uint32_t>(kManager + 0x18) = At(kMisc + 0x460); // the presence
        Word<uint32_t>(kManager + 0xE8) = At(kMisc + 0x500);
        Word<uint32_t>(kManager + 0xEC) = At(kMisc + 0x540);
        Word<uint32_t>(kScript + 0x14) = 0x0BAD0014;
        for (int i = 0; i < 8; ++i)
            Word<uint32_t>(Player(i) + 0x18) = At(kConns + 0x80 * static_cast<uint32_t>(i));
        for (int i = 0; i < 6; ++i)
            Word<void**>(kGoals + 0x20 * static_cast<uint32_t>(i)) = g_goalVtable;
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] != actual[i]) {
            for (size_t j = i > 12 ? i - 12 : 0; j < i + 3 && j < expected.size(); ++j)
                Log("  [%u] %08X %08X", static_cast<unsigned>(j), expected[j], actual[j]);
            break;
        }
    }
}

void Command(bool original, uint32_t address, CFunc ours, uint32_t params = 0x0BAD0000)
{
    auto* p = reinterpret_cast<CStruct*>(params);
    auto* script = &Word<CScript>(kScript);
    using Raw = uint32_t(__cdecl*)(CStruct*, CScript*);
    Raw code = original ? Original<Raw>(address) : reinterpret_cast<Raw>(ours);
    Rec(0xC0DE, {address, code(p, script) & 0xFF});
}

void Games(bool original)
{
    using namespace GameNet;
    Players({0, 1, 2, 3});
    Flags(0, 1);    // local
    Flags(1, 4);    // an observer
    Flags(2, 0x200);
    Flags(3, 0);
    // Start: a task busy (each), then free: offline, a net game (goal attack / other, host modes, team game).
    GameVariable<uint8_t>(0x007CCE1E) = 0;
    g_inList1 = true;
    Command(original, 0x00538280, CFunc_StartNetworkGame);
    g_inList1 = false;
    g_inList2 = true;
    Command(original, 0x00538280, CFunc_StartNetworkGame);
    g_inList2 = false;
    Command(original, 0x00538280, CFunc_StartNetworkGame);
    Rec(0xF001, {GameVariable<uint32_t>(0x007CCDF4), GameVariable<uint8_t>(0x007CCE1E)});
    g_netGame = true;
    g_peer = true;
    GameVariable<uint8_t>(0x007CCE1E) = 2;
    GameVariable<uint8_t>(0x007CCA82) = 1;
    GameVariable<uint8_t>(0x007CCE1C) = 1;
    for (int step = 0; step < 4; ++step) {
        g_gameType = step % 2 ? 0xEC200EAA : 0x12345678;
        g_teamGame = step >= 2;
        Word<int>(kManager + 0x40) = step == 1 ? 1 : 0;
        g_goalCount = step == 3 ? 0 : 5;
        Command(original, 0x00538280, CFunc_StartNetworkGame);
        Rec(0xF001, {GameVariable<uint32_t>(0x007CCDF4), GameVariable<uint8_t>(0x007CCE1E)});
    }
    // End: on the server (reported or not), on a client.
    for (int step = 0; step < 3; ++step) {
        GameVariable<uint8_t>(0x007CCE1E) = static_cast<uint8_t>(step == 1 ? 0 : 3);
        Word<uint8_t>(kManager) = step == 2 ? 0 : 1;
        Command(original, 0x00538720, CFunc_EndNetworkGame);
        Rec(0xF002, {GameVariable<uint8_t>(0x007CCE1E)});
    }
    // Game over to observers: on a client (nothing), on the server.
    Word<uint8_t>(kManager) = 0xFE;
    Command(original, 0x00538620, CFunc_SendGameOverToObservers);
    Word<uint8_t>(kManager) = 1;
    Command(original, 0x00538620, CFunc_SendGameOverToObservers);
}

void Teams(bool original)
{
    using namespace GameNet;
    Players({0, 1, 2, 3, 4});
    Flags(0, 1);
    Flags(1, 4);
    Flags(2, 0);
    Flags(3, 0x200);
    Flags(4, 0);
    const int teams[] = {2, 3, 1, 0, 4};
    auto resetTeams = [&]() {
        for (int i = 0; i < 5; ++i)
            Word<int>(Player(i) + 0x30) = teams[i];
    };
    resetTeams();
    // The flag taken: each team, and team missing (0).
    for (int team : {0, 1, 2, 5}) {
        g_answer[{0x0BAD0010, Hash("team")}] = static_cast<uint32_t>(team);
        Command(original, 0x00538050, CFunc_TeamFlagTaken, 0x0BAD0010);
    }
    // My team: none local, local.
    Players({1, 2});
    Command(original, 0x005388C0, CFunc_GetMyTeam);
    Players({0, 1, 2, 3, 4});
    Command(original, 0x005388C0, CFunc_GetMyTeam);
    // The team count: a client; the server - 0 teams, 2 (team game already / not, with and without a local skater),
    // no server app.
    Word<uint8_t>(kManager) = 0;
    g_answer[{0x0BAD0020, 0}] = 2;
    Command(original, 0x005388F0, CFunc_SetNumTeams, 0x0BAD0020);
    Word<uint8_t>(kManager) = 1;
    for (int step = 0; step < 5; ++step) {
        resetTeams();
        g_answer[{0x0BAD0020, 0}] = step == 0 ? 0u : 2u;
        g_teamGame = step == 1;
        g_localSkater = step == 3 ? 0 : kSkaters + 0x100;
        if (step == 4)
            Word<uint32_t>(kManager + 4) = 0;
        Command(original, 0x005388F0, CFunc_SetNumTeams, 0x0BAD0020);
        for (int i = 0; i < 5; ++i)
            Rec(0xF003, {Word<uint32_t>(Player(i) + 0x30)});
    }
    Word<uint32_t>(kManager + 4) = At(kApp);
    // Joining a team: each colour, too soon, no local player, no skater.
    const uint32_t colours[] = {0x61C9354B, 0x059EA070, 0x2F6511DE, 0x12345678};
    GameVariable<uint32_t>(0x007CCE28) = 0;
    for (uint32_t colour : colours) {
        g_answer[{0x0BAD0030, 0}] = colour;
        Command(original, 0x00538C90, CFunc_JoinTeam, 0x0BAD0030);
        g_time += 200;
    }
    Command(original, 0x00538C90, CFunc_JoinTeam, 0x0BAD0030); // (100 ms later: too soon)
    GameVariable<uint32_t>(0x007CCE28) = g_time + 100 - 150; // exactly 150 ms at the next call: sent
    Command(original, 0x00538C90, CFunc_JoinTeam, 0x0BAD0030);
    Rec(0xF004, {GameVariable<uint32_t>(0x007CCE28)});
    Word<uint32_t>(Player(0) + 0x14) = 0;
    Command(original, 0x00538C90, CFunc_JoinTeam, 0x0BAD0030);
    Players({1, 2});
    Command(original, 0x00538C90, CFunc_JoinTeam, 0x0BAD0030);
    // The host: offline; on the server (host modes 0 / 1); on a client (the local player the server's or not).
    Players({0, 1, 2, 3, 4});
    Command(original, 0x00538D70, CFunc_IsHost);
    g_netGame = true;
    for (int mode : {0, 1, 2}) {
        Word<int>(kManager + 0x40) = mode;
        Command(original, 0x00538D70, CFunc_IsHost);
    }
    Word<uint8_t>(kManager) = 0;
    Command(original, 0x00538D70, CFunc_IsHost);
    Flags(0, 0x201);
    Command(original, 0x00538D70, CFunc_IsHost);
    Players({1, 2});
    Command(original, 0x00538D70, CFunc_IsHost);
    // Ready: a client; the server with all ready, one not, none.
    Players({0, 1, 2, 3});
    Command(original, 0x00538DC0, CFunc_AllPlayersAreReady);
    Word<uint8_t>(kManager) = 1;
    for (int i = 0; i < 4; ++i)
        Word<uint8_t>(kConns + 0x80 * static_cast<uint32_t>(i) + 0x60) = 0x81;
    Command(original, 0x00538DC0, CFunc_AllPlayersAreReady);
    Word<uint8_t>(kConns + 0x80 * 2 + 0x60) = 0xFE;
    Command(original, 0x00538DC0, CFunc_AllPlayersAreReady);
    Players({});
    Command(original, 0x00538DC0, CFunc_AllPlayersAreReady);
}

void PlayerMenu(bool original)
{
    using namespace GameNet;
    Players({0, 1, 2, 3});
    Flags(0, 1);
    Flags(1, 0x404);
    Flags(2, 0);
    Flags(3, 0);
    std::strcpy(&Word<char>(Player(2) + 0x38), "Bob");
    std::strcpy(&Word<char>(Player(3) + 0x38), "Alice");
    Word<int>(Player(1) + 0x28) = 0;
    Word<int>(Player(2) + 0x28) = 1001; // (a buddy: odd)
    Word<int>(Player(3) + 0x28) = 1002;
    static const char kBob[] = "BOB";
    static const char kCarol[] = "Carol";
    // Options: the index'th player by name (a different name; beyond the list; on the internet logged in or not).
    auto options = [&](int index, const char* name, uint32_t address) {
        g_answer[{0x0BAD0040, 0x7F8C98FE}] = static_cast<uint32_t>(index);
        g_answer[{0x0BAD0040, 0xA1DC81F9}] = reinterpret_cast<uint32_t>(name);
        Command(original, address, address == 0x00539470 ? CFunc_RemovePlayer : CFunc_CreatePlayerOptions, 0x0BAD0040);
        for (int i = 0; i < 4; ++i)
            Rec(0xF008, {Word<uint32_t>(Player(i) + 0x1D0)});
    };
    options(2, kBob, 0x00538E70);
    options(2, kCarol, 0x00538E70);
    options(2, "Xob", 0x00538E70); // (only the first letter differs)
    options(9, kBob, 0x00538E70);
    Word<uint8_t>(kManager) = 4;
    options(2, kBob, 0x00539470);
    g_loggedIn = true;
    options(2, kBob, 0x00538E70);
    options(3, "alice", 0x00538E70);
    Word<int>(Player(3) + 0x28) = 0;
    options(3, "ALICE", 0x00538E70);
    // The menu: logged in or not (the heaps told apart).
    Word<uint32_t>(kMisc + 0x800 + 0x11C) = At(kMisc + 0xC00);
    Word<uint32_t>(kMisc + 0x800 + 0x124) = At(kMisc + 0xC40);
    Word<int>(Player(3) + 0x28) = 1002;
    for (int logged = 0; logged < 2; ++logged) {
        g_loggedIn = logged != 0;
        Command(original, 0x00539080, CFunc_FillPlayerListMenu);
    }
    // Cancel the removal marks (no observers).
    Command(original, 0x00539480, CFunc_CancelRemovePlayer);
    for (int i = 0; i < 4; ++i)
        Rec(0xF005, {Word<uint32_t>(Player(i) + 0x1D0)});
    // Pro set toggle.
    g_answer[{0x0BAD0050, Hash("bit")}] = 5;
    g_answer[{0x0BAD0050, Hash("param_id")}] = 0xBEEF0005;
    Word<uint32_t>(kManager + 0x194) = 0x11;
    Command(original, 0x00539570, CFunc_ToggleProSet, 0x0BAD0050);
    Rec(0xF006, {Word<uint32_t>(kManager + 0x194)});
}

void Crown(bool original)
{
    using namespace GameNet;
    // Seven nodes, the 2nd, 4th and 5th of the crown's type; spawn points 0, 4 (wraps), with and without a king.
    for (uint32_t i = 0; i < 7; ++i) {
        uint32_t node = At(kCrownNodes + 0x10 * i);
        if (i == 1 || i == 3 || i == 4)
            g_answer[{node, Hash("Type")}] = 0xAF86421B;
        else if (i == 5)
            g_answer[{node, Hash("Type")}] = 0x11111111;
    }
    Players({0, 1, 2});
    for (int step = 0; step < 3; ++step) {
        Word<int>(kManager + 0x68) = step == 0 ? 0 : 4;
        Flags(2, step == 2 ? 0x80 : 0);
        Command(original, 0x00538170, CFunc_SpawnCrown);
        Rec(0xF007, {Word<uint32_t>(kManager + 0x6C)});
    }
}

void TestNetGameCommands()
{
    SaveGlobals({{0x007CCE1C, 3, {}}, {0x007CCA82, 1, {}}, {0x007CCDF4, 4, {}}, {0x007CCE28, 4, {}}});
    RunCase("GameNet commands: start / end the network game, game over to observers", Games);
    RunCase("GameNet commands: teams, host, ready", Teams);
    RunCase("GameNet commands: the player menu and options, pro set", PlayerMenu);
    RunCase("GameNet commands: SpawnCrown", Crown);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetGameCommands, GameNetKit::Cleanup);
