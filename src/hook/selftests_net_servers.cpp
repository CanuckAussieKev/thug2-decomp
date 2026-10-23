// Self-tests for the network manager's server list and the online service's downloads
// (src/game/net/server_list.cpp): the original and ours run on the same stand-in arena (gamenet_testkit.h), the
// GameSpy SDK and everything else they call stood in for by recording code; the calls, the results and the arena
// are compared. Nothing goes online.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/task.h"
#include "game/mth/random.h"
#include "game/net/buddy_man.h"
#include "game/net/net_conn.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_manager.h"
#include "game/net/server_list.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using GameNet::ServerInfo;

constexpr uint32_t kServerArea = 0x14000; // servers, 0xC0 apart (8)
constexpr uint32_t kLobby = 0x14600;      // the LobbyMan (0x154)
constexpr uint32_t kBuddies = 0x14780;    // the buddy list: +0x10 logged in
constexpr uint32_t kPrefs = 0x147C0;      // stand-in structures
constexpr uint32_t kTasks = 0x14800;      // tasks (0x40 apart)
constexpr uint32_t kNet = 0x14900;        // Net::Manager
constexpr uint32_t kScript = 0x14A00;     // a script: +0x14 its parameters
constexpr uint32_t kClient = 0x15000;     // the match client (Net::App, 0x1400)
constexpr uint32_t kNews = 0x16400;       // a news download (0x200)

uint32_t g_now;
int g_failAllocations;
bool g_inList;
uint32_t g_spawned;
int g_result;
std::map<uint32_t, uint32_t> g_ours;

uint32_t ServerAt(int i) { return kServerArea + 0xC0 * static_cast<uint32_t>(i); }
ServerInfo* Server(int i) { return &Word<ServerInfo>(ServerAt(i)); }
Manager* TheManager() { return &Word<Manager>(kManager); }
uint32_t Text(uint32_t text) { return text ? Hash(reinterpret_cast<const char*>(text)) : 0xDEAD0000u; }
uint32_t Code(uint32_t address)
{
    auto it = g_ours.find(address);
    return it == g_ours.end() ? Rel(address) : it->second;
}

void Install(StandIns& s)
{
    InstallCommon(s);
    g_ours = {{AddressOf(&GameNet::NewsCallback), 0x00545490},
              {reinterpret_cast<uint32_t>(&GameNet::NewsCallback), 0x00545490}};
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1] & 0xFF, r.args[2]});
        if (g_failAllocations > 0) {
            --g_failAllocations;
            return 0u;
        }
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_now; });
    // The C runtime the original calls.
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x006239E0}, 0, [](Regs& r) {
#pragma warning(suppress : 4996)
        return reinterpret_cast<uint32_t>(std::strncpy(reinterpret_cast<char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1]), r.args[2]));
    });
    s.At({0x00625171}, 0, [](Regs& r) {
#pragma warning(suppress : 4996)
        return reinterpret_cast<uint32_t>(std::strtok(reinterpret_cast<char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    // Scripts, structures, preferences.
    s.At({0x004758F0, AddressOf(static_cast<void (*)(const char*, Script::CStruct*, Obj::CObject*, bool)>(&Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x58F0, {Text(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF});
             return 0u;
         });
    s.At({0x00478670, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::CStruct*)>(&Script::CStruct::AddStructure))}, 8, [](Regs& r) {
        Rec(0x8670, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        char* text = &Word<char>(kMisc + 0xE40);
        std::snprintf(text, 32, "<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0x1C30, {Text(r.args[0]), Text(r.args[1])});
        return 0u;
    });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Rec(0xC4A0, {r.ecx, r.args[0]});
        return At(kPrefs);
    });
    auto getString = [](Regs& r) {
        Rec(0x6590, {r.ecx, r.args[0], r.args[2] & 0xFF});
        *reinterpret_cast<uint32_t*>(r.args[1]) = At(kPrefs + 0x10);
        return 1u;
    };
    s.At({0x00476590, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(&Script::CStruct::GetString))},
         12, getString);
    s.At({0x00476C60}, 12, getString);
    // The game's other code.
    Stub(s, {0x00540220}, 0, 0x0220);
    s.At({0x00548320, AddressOf(&GameNet::BuddyMan::SetStatus)}, 12, [](Regs& r) {
        Rec(0x8321, {r.ecx, r.args[0], Text(r.args[1]), Text(r.args[2])});
        return 0u;
    });
    Stub(s, {0x004548F0, AddressOf(&Net::Manager::GetLocalAddress)}, 0, 0x48F0, [](Regs&) { return 0x0100007Fu; });
    Stub(s, {0x00534D50}, 0, 0x4D50);
    Stub(s, {0x00534430, AddressOf(&Manager::SpawnMatchClient)}, 0, 0x4430, [](Regs&) { return g_spawned; });
    Stub(s, {0x00406850, AddressOf(&Tsk::BaseTask::InList)}, 0, 0x6850, [](Regs&) { return g_inList ? 1u : 0u; });
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    Stub(s, {0x0045A690, AddressOf(&Net::Dispatcher::Deinit)}, 0, 0xA690);
    Stub(s, {0x00532ED0, AddressOf(&Manager::FindServersOnLan)}, 0, 0x2ED0);
    Stub(s, {0x0053F860}, 0, 0xF860);
    Stub(s, {0x0053C950}, 4, 0xC950);
    // GameSpy.
    StubCdecl(s, {0x005FC950}, 0xC950, 1, [](Regs&) { return 0x0A0B0C0Du; });
    StubCdecl(s, {0x005FCA40}, 0xCA40, 1);
    StubCdecl(s, {0x005FC9D0}, 0xC9D0, 1, [](Regs& r) {
        const uint8_t* keys = reinterpret_cast<const uint8_t*>(r.args[1]);
        Rec(0xC9D1, {keys[0], keys[1], keys[2], keys[3], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
    StubCdecl(s, {0x005F8160}, 0x8160, 1);
    StubCdecl(s, {0x005FF8A0}, 0xF8A0, 0, [](Regs& r) {
        Rec(0xF8A1, {Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7],
                     Code(r.args[8]), r.args[9]});
        return 0x5EE0u;
    });
    StubCdecl(s, {0x005FFCD0}, 0xFCD0, 0, [](Regs& r) {
        Rec(0xFCD1, {r.args[0], r.args[1], Text(r.args[2]), r.args[3], r.args[4]});
        return 0x5EE1u;
    });
}

void InstallBrowsing(StandIns& s);
uint32_t g_rnd;

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_now = 70000;
        g_failAllocations = 0;
        g_inList = false;
        g_spawned = At(kClient);
        g_result = 0;
        for (uint32_t i = 0x800; i < 0xDF0; i += 4)
            Word<uint32_t>(kMisc + i) = 0x6D000000u + i; // the memory manager's fields told apart
        for (uint32_t head : {0x158u, 0x16Cu}) {
            Word<uint32_t>(kManager + head + 0x00) = 0x0064CDFC;
            Word<uint32_t>(kManager + head + 0x04) = 0xFFFFFFFF;
            Word<uint32_t>(kManager + head + 0x08) = 0;
            Word<uint32_t>(kManager + head + 0x0C) = At(kManager + head);
            Word<uint32_t>(kManager + head + 0x10) = At(kManager + head);
        }
        Word<uint8_t>(kManager) = 0; // (flags: LAN off, internet off)
        Word<uint32_t>(kManager + 0x10) = At(kLobby);
        Word<uint32_t>(kManager + 0x18) = At(kBuddies);
        Word<uint32_t>(kManager + 0x2C) = 0; // no match client
        Word<uint32_t>(kManager + 0xE0) = At(kTasks);
        Word<uint32_t>(kManager + 0xF0) = At(kTasks + 0x40);
        Word<uint32_t>(kManager + 0x1BC) = 6;
        Word<uint8_t>(kBuddies + 0x10) = 1;
        Word<uint32_t>(kLobby + 0x140) = At(kMisc + 0xA00); // the peer
        std::memcpy(&Word<char>(kLobby + 0x34), "Beenox", 7);
        Word<uint8_t>(kLobby + 0x139) = 0;
        std::memcpy(&Word<char>(kPrefs + 0x10), "MyServer", 9);
        Word<uint32_t>(kScript + 0x14) = At(kScript + 0x40);
        GameVariable<uint32_t>(0x006EFDD8) = At(kNet);
        GameVariable<uint32_t>(0x00696D68) = At(kMisc + 0x400);
        GameVariable<uint32_t>(0x007CE444) = 0x44;
        GameVariable<uint32_t>(0x007CE448) = 1000;
        GameVariable<uint8_t>(0x007CE44C) = 0;
        GameVariable<uint32_t>(0x007CE450) = 69000;
        GameVariable<uint8_t>(0x007CE441) = 0;
        StandIns* s = new StandIns;
        Install(*s);
        InstallBrowsing(*s);
        g_rnd = 7;
        GameVariable<uint32_t>(0x007CCA84) = 0;
        GameVariable<uint8_t>(0x007CCA88) = 0;
        scenario(original);
        Rec(0xC10B, {GameVariable<uint32_t>(0x007CE444), GameVariable<uint32_t>(0x007CE448),
                     GameVariable<uint8_t>(0x007CE44C), GameVariable<uint32_t>(0x007CE450)});
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (expected[i] == actual[i])
            continue;
        for (size_t j = i > 24 ? i - 24 : 0; j < i + 4 && j < expected.size() && j < actual.size(); ++j)
            Log("    [%u] %08X %08X", static_cast<unsigned>(j), expected[j], actual[j]);
        break;
    }
}

template <typename R, typename C, typename... P, typename... A>
void CallRec(bool original, uint32_t address, R (C::*ours)(P...), void* object, A... args)
{
    R result = original ? Original<R(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...)
                        : (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    uint32_t word = 0;
    std::memcpy(&word, &result, sizeof(result) < 4 ? sizeof(result) : 4);
    if (sizeof(result) == 1)
        word &= 0xFF;
    Rec(0xCA11, {address, word});
}
template <typename C, typename... P, typename... A>
void CallVoid(bool original, uint32_t address, void (C::*ours)(P...), void* object, A... args)
{
    if (original)
        Original<void(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...);
    else
        (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    Rec(0xCA11, {address});
}
using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void RunCFunc(bool original, uint32_t address, CFunc ours)
{
    auto* params = &Word<Script::CStruct>(kScript + 0x80);
    auto* script = &Word<Script::CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    Rec(0xCF00, {address, result ? 1u : 0u});
}

// Server `i` made (the original's constructor) and named, at the end of the list at `head`.
ServerInfo* MakeServer(bool original, int i, uint32_t head, const char* name, uint32_t ip)
{
    std::memset(Server(i), 0x3C, sizeof(ServerInfo));
    CallRec(original, 0x00545F60, &ServerInfo::Construct, Server(i));
    std::memcpy(Server(i)->m_name, name, std::strlen(name) + 1);
    Server(i)->m_ip = ip;
    Server(i)->m_sbServer = &Word<uint8_t>(kMisc + 0xB00 + 0x10 * static_cast<uint32_t>(i));
    std::snprintf(Server(i)->m_mode, 32, "Mode%d", i);
    Mem::ListNode* list = &Word<Mem::ListNode>(kManager + head);
    Server(i)->prev = list->prev;
    Server(i)->next = list;
    list->prev->next = Server(i);
    list->prev = Server(i);
    return Server(i);
}

void Servers(bool original)
{
    MakeServer(original, 0, 0x158, "Alpha", 0x01020304);
    MakeServer(original, 1, 0x158, "Beta", 0x05060708);
    MakeServer(original, 2, 0x16C, "Gamma", 0x090A0B0C);
    MakeServer(original, 3, 0x16C, "Delta", 0x0D0E0F10);
    // Players: two added, one with a long name; one failing.
    CallVoid(original, 0x00545790, &ServerInfo::AddPlayer, Server(0), "Skater", 120);
    Word<uint8_t>(kManager) = 4; // internet
    CallVoid(original, 0x00545790, &ServerInfo::AddPlayer, Server(0), "AVeryLongPlayerName", 340);
    for (int i : {0, 1, 2, -1}) {
        CallRec(original, 0x005466D0, &ServerInfo::GetPlayerName, Server(0), i);
        CallRec(original, 0x00546730, &ServerInfo::GetPlayerRating, Server(0), i);
    }
    CallRec(original, 0x005466D0, &ServerInfo::GetPlayerName, Server(1), 0);
    // Finding them.
    for (const char* name : {"alpha", "BETA", "Gamma", "delta", "Epsilon"})
        for (uint32_t ip : {0x01020304u, 0x05060708u, 0x090A0B0Cu, 0x0D0E0F10u})
            CallRec(original, 0x00546400, &Manager::HasServer, TheManager(), name, ip);
    for (int internet : {4, 0}) {
        Word<uint8_t>(kManager) = static_cast<uint8_t>(internet);
        for (int i = 0; i < 4; ++i) {
            CallRec(original, 0x005464D0, &Manager::GetServer, TheManager(), At(kMisc + 0xB00 + 0x10 * static_cast<uint32_t>(i)));
            CallRec(original, 0x005464D0, &Manager::GetServer, TheManager(), At(ServerAt(i)));
        }
    }
    RunCFunc(original, 0x00545F20, GameNet::CFunc_NumServersInLobby);
    Server(1)->m_inMenu = true;
    Server(0)->m_chosen = true;
    Server(1)->m_chosen = true;
    CallRec(original, 0x00546070, &Manager::NumServersInMenu, TheManager());
    CallVoid(original, 0x00546010, &Manager::ClearChosenServers, TheManager());
    Rec(0x8B, {Server(0)->m_chosen ? 1u : 0u, Server(1)->m_chosen ? 1u : 0u, Server(2)->m_chosen ? 1u : 0u});
    // The waiting ones shown (internet ids, then LAN).
    Word<uint8_t>(kManager) = 4;
    CallVoid(original, 0x00547320, &Manager::ShowFoundServers, TheManager());
    RunCFunc(original, 0x00545F20, GameNet::CFunc_NumServersInLobby);
    Word<uint8_t>(kManager) = 0;
    MakeServer(original, 4, 0x16C, "Eps", 1);
    CallVoid(original, 0x005460D0, &Manager::AddServerToMenu, TheManager(), Server(4), 9);
    // The servers deleted (their players through the manager), the searches' destructors.
    Word<uint8_t>(kManager) = 8; // LAN
    CallVoid(original, 0x005462F0, &Manager::ClearServerList, TheManager());
    RunCFunc(original, 0x00545F20, GameNet::CFunc_NumServersInLobby);
    MakeServer(original, 5, 0x158, "Zeta", 2);
    CallRec(original, 0x00547F70, &ServerInfo::Destroy, Server(5), 0u);
    MakeServer(original, 5, 0x158, "Eta", 5);
    CallRec(original, 0x00547F70, &ServerInfo::Destroy, Server(5), 1u);
    Word<uint32_t>(ServerAt(6)) = 0x11;
    CallVoid(original, 0x005451D0, &GameNet::ServerSearch::DestroyBody, Server(6));
    CallRec(original, 0x00545460, &GameNet::ServerSearch::Destroy, Server(6), 0u);
    CallRec(original, 0x00545460, &GameNet::ServerSearch::Destroy, Server(7), 1u);
}

void States(bool original)
{
    CallVoid(original, 0x005451B0, &Manager::SetNextServiceState, TheManager(), 5);
    CallRec(original, 0x005451C0, &Manager::GetServiceState, TheManager());
    for (int state : {3, 2, 1, 0})
        CallVoid(original, 0x005453E0, &Manager::SetServiceState, TheManager(), state);
    CallRec(original, 0x005451C0, &Manager::GetServiceState, TheManager());
    CallVoid(original, 0x005451B0, &Manager::SetNextServiceState, TheManager(), 2);
    CallVoid(original, 0x005453E0, &Manager::SetServiceState, TheManager(), 1); // (wakes, then stops)
    CallRec(original, 0x005451C0, &Manager::GetServiceState, TheManager());
    for (bool refocus : {false, true})
        CallVoid(original, 0x005456B0, &Manager::ShowNoServersFound, TheManager(), refocus);
}

void Browsing(bool original)
{
    // Start / stop on the LAN and the internet.
    CallRec(original, 0x00545300, &Manager::StartServerList, TheManager()); // not LAN
    Word<uint8_t>(kManager) = 8;
    CallRec(original, 0x00545300, &Manager::StartServerList, TheManager());
    g_inList = true;
    CallRec(original, 0x00545300, &Manager::StartServerList, TheManager());
    g_spawned = 0;
    CallRec(original, 0x00545300, &Manager::StartServerList, TheManager());
    CallVoid(original, 0x00545370, &Manager::StopServerList, TheManager()); // no client
    Word<uint32_t>(kManager + 0x2C) = At(kClient);
    CallVoid(original, 0x00545370, &Manager::StopServerList, TheManager());
    Word<uint8_t>(kManager) = 4;
    CallVoid(original, 0x00545370, &Manager::StopServerList, TheManager());
    Word<uint32_t>(kLobby + 0x140) = 0;
    CallVoid(original, 0x00545370, &Manager::StopServerList, TheManager());
    Word<uint32_t>(kLobby + 0x140) = At(kMisc + 0xA00);
    // Refreshes: too soon, forced, internet / LAN, the list task.
    g_spawned = At(kClient);
    MakeServer(original, 0, 0x16C, "Wait", 3);
    GameVariable<uint8_t>(0x007CE44C) = 0;
    CallVoid(original, 0x00547E80, &Manager::RefreshServerList, TheManager(), false); // 69000 since: ok
    Rec(0x44C, {GameVariable<uint8_t>(0x007CE44C)});
    g_now += 4999;
    CallVoid(original, 0x00547E80, &Manager::RefreshServerList, TheManager(), false);
    CallVoid(original, 0x00547E80, &Manager::RefreshServerList, TheManager(), true);
    Word<uint8_t>(kManager) = 8;
    g_now += 5000;
    CallVoid(original, 0x00547E80, &Manager::RefreshServerList, TheManager(), false);
    void* task = &Word<uint8_t>(kTasks + 0x80);
    Word<uint32_t>(kTasks + 0x80 + 0x1C) = At(kManager);
    MakeServer(original, 1, 0x16C, "Late", 4);
    auto runTask = [&](uint32_t now) {
        g_now = now;
        original ? Original<void(__cdecl*)(void*)>(0x00547440)(task) : GameNet::ServerListTask(task);
        Rec(0x7A5C, {GameVariable<uint32_t>(0x007CE450), GameVariable<uint8_t>(0x007CE44C)});
    };
    GameVariable<uint32_t>(0x007CE450) = 100000;
    GameVariable<uint32_t>(0x007CE448) = 100000;
    GameVariable<uint8_t>(0x007CE44C) = 1;
    for (uint32_t now : {100500u, 100501u, 100600u, 103000u, 103001u, 103001u, 103500u})
        runTask(now);
    GameVariable<uint8_t>(0x007CE44C) = 1;
    runTask(103502u);
}

void Hosting(bool original)
{
    CallVoid(original, 0x005451E0, &Manager::UpdateHostingStatus, TheManager());
    Word<uint32_t>(kLobby + 0x140) = 0;
    CallVoid(original, 0x005451E0, &Manager::UpdateHostingStatus, TheManager());
    Word<uint8_t>(kBuddies + 0x10) = 0;
    CallVoid(original, 0x005451E0, &Manager::UpdateHostingStatus, TheManager());
    Word<uint8_t>(kBuddies + 0x10) = 1;
    RunCFunc(original, 0x00545440, GameNet::CFunc_PostGame);
    Word<uint8_t>(kLobby + 0x139) = 1;
    RunCFunc(original, 0x00545440, GameNet::CFunc_PostGame);
    for (const char* location : {"10thug211:1:2:3:x", "10THUG211", "thugpro:1", ":10thug211", "10thug2110:5"}) {
        bool result = original ? Original<bool(__cdecl*)(const char*)>(0x00548040)(location) : GameNet::IsThug2Location(location);
        Rec(0x1C07, {result ? 1u : 0u});
    }
}

void Downloads(bool original)
{
    void* manager = TheManager();
    for (int test : {0, 1}) {
        GameVariable<uint8_t>(0x007CE441) = static_cast<uint8_t>(test);
        unsigned long result = original ? Original<unsigned long(__stdcall*)(void*)>(0x00545540)(manager) : GameNet::NewsThread(manager);
        Rec(0x5540, {result, Word<uint32_t>(kManager + 0x99F4 + 0x63FC), Word<uint32_t>(kManager + 0x1B8)});
        g_now += 10;
    }
    unsigned long result = original ? Original<unsigned long(__stdcall*)(void*)>(0x00545620)(manager) : GameNet::MotdThread(manager);
    Rec(0x5620, {result, Word<uint32_t>(kManager + 0x1B8)});
    // The news arriving: failed, a bad checksum, good (texts of different lengths), an empty download.
    using Callback = int(__cdecl*)(void*, int, uint8_t*, uint32_t, int32_t, void*);
    auto deliver = [&](int resultCode, const char* text, const char* rest, bool goodChecksum, uint32_t sizeHigh) {
        uint8_t* buffer = &Word<uint8_t>(kNews);
        std::memset(buffer, 0, 0x200);
        size_t length = std::strlen(text);
        std::memcpy(buffer + 8, text, length + 1);
        size_t padded = (length + 4) & ~3u;
        std::memcpy(buffer + 8 + padded, rest, std::strlen(rest) + 1);
        int remaining = static_cast<int>(padded + std::strlen(rest) + 1);
        *reinterpret_cast<int*>(buffer + 4) = remaining;
        uint32_t checksum = Crc::GenerateCRCCaseless(reinterpret_cast<const char*>(buffer + 8), remaining);
        *reinterpret_cast<uint32_t*>(buffer) = goodChecksum ? checksum : checksum ^ 1;
        uint32_t size = static_cast<uint32_t>(8 + remaining);
        for (uint32_t i = 0; i < size; ++i)
            buffer[i] ^= 0x6D;
        int r = original ? Original<Callback>(0x00545490)(nullptr, resultCode, buffer, size, static_cast<int32_t>(sizeHigh), nullptr)
                         : GameNet::NewsCallback(nullptr, resultCode, buffer, size, static_cast<int32_t>(sizeHigh), nullptr);
        Rec(0x5490, {static_cast<uint32_t>(r), Word<uint32_t>(kManager + 0x1B8), Rel(Word<uint32_t>(kManager + 0xFDF4)),
                     Rel(Word<uint32_t>(kManager + 0xFDF8)), Word<uint32_t>(kManager + 0xFDFC), Word<uint8_t>(kManager + 0xFE09)});
    };
    deliver(3, "News", "x", true, 0);
    deliver(0, "News", "more", false, 0);
    deliver(0, "News!", "rest of it", true, 0);
    deliver(0, "Hello", "", true, 0);
    deliver(0, "NewsNews", "ab", true, 0);
}

// --- the browsing (server_browser.cpp) ---

constexpr uint32_t kSb = 0x16600; // SBServer stand-ins, 0x40 apart (6): +0 ping, +4 direct, +8 public ip, +0xC private
                                  // ip, +0x10 has private, +0x14 port, +0x18 basic keys, +0x1C full keys, +0x20 players
constexpr uint32_t kContext = 0x16800; // a message context (0x100) and its reply (kContext + 0x100, 0xE0)

uint32_t Count(Mem::ListNode* head)
{
    uint32_t count = 0;
    for (Mem::ListNode* node = head->next; node != head; node = node->next)
        ++count;
    return count;
}
uint32_t SbAt(int i) { return kSb + 0x40 * static_cast<uint32_t>(i); }
uint32_t SbField(uint32_t sb, uint32_t offset) { return *reinterpret_cast<uint32_t*>(sb + offset); }

void InstallBrowsing(StandIns& s)
{
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         12, [](Regs& r) {
             Rec(0x6950, {r.ecx, r.args[0], r.args[2]});
             *reinterpret_cast<uint32_t*>(r.args[1]) = Word<uint32_t>(kScript + 0x90); // (the id asked for)
             return 1u;
         });
    StubCdecl(s, {0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0x9070, 1,
              [](Regs&) { return At(kPrefs + 0x20); });
    Stub(s, {0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, 0xCC30, [](Regs&) { return At(kPrefs + 0x30); });
    Stub(s, {0x0053F830}, 4, 0xF830);
    Stub(s, {0x0053F870}, 0, 0xF870);
    s.At({0x005BEAB4}, 4, [](Regs& r) {
        Rec(0xEAB4, {r.args[0]});
        char* text = &Word<char>(kMisc + 0xE80);
        std::snprintf(text, 32, "ip%08X", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    StubCdecl(s, {0x00403DE0, AddressOf(&Mth::Rnd)}, 0x3DE0, 1, [](Regs&) { return g_rnd++; });
    s.At({0x0054B6B0}, 4, [](Regs& r) {
        Mem::ListNode* head = reinterpret_cast<Mem::ListNode*>(r.args[0]);
        Mem::ListNode* node = head->next->data == reinterpret_cast<void*>(-1) ? nullptr : head->next;
        reinterpret_cast<uint32_t*>(r.ecx)[1] = reinterpret_cast<uint32_t>(node);
        Rec(0xB6B0, {r.args[0]});
        return node ? reinterpret_cast<uint32_t>(node->data) : 0u;
    });
    s.At({0x0047DD10}, 0, [](Regs& r) {
        Mem::ListNode* head = reinterpret_cast<Mem::ListNode*>(r.ecx);
        uint32_t count = 0;
        for (Mem::ListNode* node = head->next; node != head; node = node->next)
            ++count;
        Rec(0xDD10, {r.ecx, count});
        return count;
    });
    // GameSpy's server browsing.
    auto field = [&s](uint32_t address, uint32_t tag, uint32_t offset) {
        StubCdecl(s, {address}, tag, 1, [offset](Regs& r) { return SbField(r.args[0], offset); });
    };
    field(0x005FF410, 0xF410, 0x00);
    field(0x005FEF30, 0xEF30, 0x04);
    field(0x00603360, 0x3360, 0x08);
    field(0x005FEF70, 0xEF70, 0x0C);
    field(0x005FEF50, 0xEF50, 0x10);
    field(0x005FEF00, 0xEF00, 0x14);
    field(0x005FEEC0, 0xEEC0, 0x18);
    field(0x005FEEE0, 0xEEE0, 0x1C);
    StubCdecl(s, {0x005FC940}, 0xC940, 1, [](Regs&) { return 0x0A0B0C0Du; });
    StubCdecl(s, {0x005FCA60}, 0xCA60, 3);
    StubCdecl(s, {0x005FED50}, 0xED50, 1, [](Regs& r) {
        Rec(0xED51, {Text(r.args[1]), Text(r.args[2])});
        char* text = &Word<char>(kMisc + 0xEC0);
        std::snprintf(text, 32, "%s-%u", reinterpret_cast<const char*>(r.args[1]), (r.args[0] - At(kSb)) / 0x40);
        return reinterpret_cast<uint32_t>(text);
    });
    s.At({0x005FF560}, 0, [](Regs& r) {
        Rec(0xF560, {Rel(r.args[0]), Text(r.args[1]), r.args[2]});
        uint32_t h = Hash(reinterpret_cast<const char*>(r.args[1]));
        if (h == Hash("numplayers"))
            return SbField(r.args[0], 0x20);
        return (h & 0x7) + ((r.args[0] - At(kSb)) / 0x40);
    });
    s.At({0x005FEDD0}, 0, [](Regs& r) {
        Rec(0xEDD0, {Rel(r.args[0]), Text(r.args[1]), r.args[2]});
        return (Hash(reinterpret_cast<const char*>(r.args[1])) >> 3) & 1;
    });
    s.At({0x005FF5C0}, 0, [](Regs& r) {
        Rec(0xF5C0, {Rel(r.args[0]), r.args[1], Text(r.args[2]), r.args[3]});
        return 1000 + 500 * r.args[1];
    });
    s.At({0x005FEE30}, 0, [](Regs& r) {
        Rec(0xEE30, {Rel(r.args[0]), r.args[1], Text(r.args[2]), Text(r.args[3])});
        char* text = &Word<char>(kMisc + 0xF00 + 0x20 * (r.args[1] & 3));
        std::snprintf(text, 32, "Player%u", r.args[1]);
        return reinterpret_cast<uint32_t>(text);
    });
}

void Describing(bool original)
{
    for (uint32_t i = 0; i < 0x10; ++i)
        Word<uint32_t>(kPrefs + 0x20 + 4 * i) = 0; // (the skill array and its structure: stand-ins)
    MakeServer(original, 0, 0x158, "Alpha", 0x04030201);
    CallVoid(original, 0x00545790, &ServerInfo::AddPlayer, Server(0), "One", 100);
    CallVoid(original, 0x00545790, &ServerInfo::AddPlayer, Server(0), "Two", 99999);
    Server(0)->m_numPlayers = 3; // (one past the players: a null name)
    Server(0)->m_maxPlayers = 6;
    Server(0)->m_ping = 9999;
    Word<uint32_t>(kScript + 0x90) = At(ServerAt(0));
    using CFuncAt = void (*)(bool);
    struct Variant {
        bool started;
        int hostMode;
        int ping;
        bool internet;
        bool ranked;
    };
    for (const Variant& v : {Variant{true, 1, 9999, false, false}, Variant{false, 2, 120, true, true},
                             Variant{false, 3, 9998, true, false}}) {
        Server(0)->m_started = v.started;
        Server(0)->m_hostMode = v.hostMode;
        Server(0)->m_ping = v.ping;
        Server(0)->m_ranked = v.ranked;
        Server(0)->m_skillLevel = static_cast<int8_t>(v.hostMode);
        Word<uint8_t>(kManager) = v.internet ? 4 : 0;
        Word<uint32_t>(kScript + 0x90) = v.internet ? At(kMisc + 0xB00) : At(ServerAt(0));
        RunCFunc(original, 0x00546790, GameNet::CFunc_DescribeServer);
    }
    // Choosing: to play (full / not), to watch (full / not), another mode; reachable or not; logged in or not.
    Word<uint8_t>(kManager) = 0;
    Word<uint32_t>(kScript + 0x90) = At(ServerAt(0));
    Word<uint32_t>(kManager + 0xE4) = At(kTasks + 0xC0);
    struct Choice {
        int joinMode;
        int players, maxPlayers, observers, maxObservers;
        bool reach;
        bool loggedIn;
    };
    for (const Choice& c : {Choice{0, 6, 6, 0, 0, true, true}, Choice{0, 5, 6, 9, 0, false, true},
                            Choice{1, 9, 0, 2, 2, true, false}, Choice{1, 9, 0, 1, 2, true, true},
                            Choice{2, 9, 0, 9, 0, false, false}}) {
        Word<int>(kManager + 0x3C) = c.joinMode;
        Server(0)->m_numPlayers = c.players;
        Server(0)->m_maxPlayers = c.maxPlayers;
        Server(0)->m_numObservers = c.observers;
        Server(0)->m_maxObservers = c.maxObservers;
        Server(0)->m_canReach = c.reach;
        Server(0)->m_port = 5153 + c.joinMode;
        Word<uint8_t>(kBuddies + 0x10) = c.loggedIn ? 1 : 0;
        RunCFunc(original, 0x005470D0, GameNet::CFunc_ChooseServer);
    }
    // Retrieving: LAN (nothing), details here, asked for.
    Word<uint8_t>(kManager) = 8;
    RunCFunc(original, 0x00547FA0, GameNet::CFunc_RetrieveServerInfo);
    Word<uint8_t>(kManager) = 4;
    Word<uint32_t>(kScript + 0x90) = At(kMisc + 0xB00);
    Server(0)->m_detailed = true;
    RunCFunc(original, 0x00547FA0, GameNet::CFunc_RetrieveServerInfo);
    Server(0)->m_detailed = false;
    Server(0)->m_chosen = false;
    RunCFunc(original, 0x00547FA0, GameNet::CFunc_RetrieveServerInfo);
    Rec(0x8B, {Server(0)->m_chosen ? 1u : 0u});
}

void LanReplies(bool original)
{
    auto* context = &Word<Net::MsgHandlerContext>(kContext);
    char* reply = &Word<char>(kContext + 0x100);
    auto handle = [&](uint32_t length) {
        std::memset(context, 0, sizeof(Net::MsgHandlerContext));
        context->m_Msg = reply;
        context->m_MsgLength = length;
        context->m_Conn = &Word<Net::Conn>(kConn);
        context->m_data = TheManager();
        using ReplyHandler = int(__cdecl*)(Net::MsgHandlerContext*);
        int r = original ? Original<ReplyHandler>(0x005475C0)(context) : GameNet::HandleServerResponse(context);
        Rec(0x75C0, {static_cast<uint32_t>(r), GameVariable<uint32_t>(0x007CCA84), GameVariable<uint8_t>(0x007CCA88)});
    };
    std::memset(reply, 0, 0xE0);
    const int8_t numbers[8] = {3, 8, 1, 4, 1, 0, 2, 5};
    std::memcpy(reply, numbers, 8);
    std::memcpy(reply + 0x08, "LanServer", 10);
    std::memcpy(reply + 0x18, "Level", 6);
    std::memcpy(reply + 0x38, "Mode", 5);
    reply[0x58] = -2;
    Word<uint32_t>(kContext + 0x100 + 0x5C) = g_now - 37;
    for (int i = 0; i < 3; ++i)
        std::snprintf(reply + 0x60 + 0x10 * i, 16, "LanPlayer%d", i);
    Word<uint32_t>(kConn + 0x40) = 0xC0A80005;
    handle(0xDF);
    handle(0xE1);
    GameVariable<uint32_t>(0x007CCA84) = 0x11223344; // (joining by address)
    GameVariable<uint8_t>(0x007CCA88) = 0;
    handle(0xE0);
    handle(0xE0); // known now
    GameVariable<uint32_t>(0x007CCA84) = 0;
    reply[0] = 0;
    std::memcpy(reply + 0x08, "Other", 6);
    handle(0xE0);
}

void Listing(bool original)
{
    using Callback = void(__cdecl*)(void*, int, const char*, void*, int, int, int, Manager*);
    auto call = [&](int success, const char* name, int sb, int message) {
        void* server = sb >= 0 ? &Word<uint8_t>(SbAt(sb)) : nullptr;
        void* peer = &Word<uint8_t>(kMisc + 0xA00);
        original ? Original<Callback>(0x00547750)(peer, success, name, server, 0, message, 50, TheManager())
                 : GameNet::ListingGamesCallback(peer, success, name, server, 0, message, 50, TheManager());
        Rec(0x7750, {static_cast<uint32_t>(message), static_cast<uint32_t>(Count(&Word<Mem::ListNode>(kManager + 0x158)))});
    };
    // The SBServers: same network; direct with a private address; direct without; not direct with / without.
    const uint32_t sbs[6][9] = {{40, 1, 0x0A0B0C0D, 0xC0A80001, 0, 0x1500, 1, 0, 2},
                                {50, 1, 0x01010101, 0xC0A80002, 1, 0x1501, 0, 1, 1},
                                {60, 1, 0x02020202, 0xC0A80003, 0, 0x11502, 1, 1, 0},
                                {70, 0, 0x03030303, 0xC0A80004, 1, 0x1503, 1, 0, 3},
                                {80, 0, 0x04040404, 0xC0A80005, 0, 0x1504, 0, 0, 1},
                                {90, 0, 0x05050505, 0, 0, 0x1505, 1, 1, 0}};
    for (int i = 0; i < 6; ++i)
        std::memcpy(&Word<uint8_t>(SbAt(i)), sbs[i], sizeof(sbs[i]));
    call(0, "failed", 0, 0);
    for (int i = 0; i < 5; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), i == 2 ? "AVeryLongServerName%d" : "Server%d", i);
        call(1, name, i, 0);
    }
    call(1, "x", 0, 7); // (an unknown message)
    // Updates: an unknown server; one shown now; one getting its full keys while chosen; another.
    call(1, "Gone", 5, 1);
    Word<uint32_t>(SbAt(1) + 0x18) = 1;
    call(1, "Renamed1", 1, 1);
    auto* first = reinterpret_cast<ServerInfo*>(Word<Mem::ListNode>(kManager + 0x158).next);
    first->m_chosen = true;
    Word<uint32_t>(SbAt(0) + 0x1C) = 1;
    call(1, "Renamed0", 0, 1);
    call(1, "Again0", 0, 1);
    Word<uint32_t>(SbAt(4) + 0x20) = 2;
    call(1, "Renamed4", 4, 1);
    // Removed: one listed, one unknown; then cleared.
    call(1, "", 3, 2);
    call(1, "", 5, 2);
    Word<uint8_t>(kManager) = 4;
    call(1, "", -1, 3);
}

void TestNetServers()
{
    SaveGlobals({{0x006EFDD8, 4, {}}, {0x00696D68, 4, {}}, {0x007CE444, 4, {}}, {0x007CE448, 4, {}}, {0x007CE44C, 1, {}},
                 {0x007CE450, 4, {}}, {0x007CE441, 1, {}}, {0x007CCA84, 4, {}}, {0x007CCA88, 1, {}}});
    RunCase("GameNet: the server list", Servers);
    RunCase("GameNet: the online service's states", States);
    RunCase("GameNet: browsing, refreshing, the list task", Browsing);
    RunCase("GameNet: the hosting status, PostGame, locations", Hosting);
    RunCase("GameNet: the news and message of the day", Downloads);
    RunCase("GameNet: describing, choosing, retrieving a server", Describing);
    RunCase("GameNet: LAN servers' replies", LanReplies);
    RunCase("GameNet: GameSpy's game listing callback", Listing);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetServers, GameNetKit::Cleanup);
