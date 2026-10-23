// Self-tests for the online profile and buddy list (src/game/net/buddy_man.cpp, 0x00548560-0x0054A660): the original
// and ours on identical stand-in buddy managers, buddies and messages in GameNetKit's arena; GameSpy's presence SDK,
// the lobby, scripts, structures, preferences, the clock, tasks and threads are stood in for by recording code
// (net4_testkit.h), and the calls, the results and the arena are compared case by case. Nothing goes online: GP's
// answers (buddy status, messages, errors, the new user's result) are made up here.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_ntoa (stood in for)

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <vector>

#include "game/core/crc.h"
#include "game/core/stubs.h"
#include "game/mth/random.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/net/server_list.h"
#include "hook/net4_testkit.h"

namespace {

using namespace Net4Kit;
using GameNet::Buddy;
using GameNet::BuddyMan;
using GameNet::BuddyMessage;

// Arena layout (GameNetKit's manager at 0, its players at 0x10000, its stand-in objects at 0x13000).
constexpr uint32_t kMan = 0x12000;       // the BuddyMan (0x3C)
constexpr uint32_t kTask = 0x12040;      // its task (0x20)
constexpr uint32_t kTaskVtable = 0x12060;
constexpr uint32_t kLobby = 0x12080;
constexpr uint32_t kParams = 0x12100;    // a CFunction's parameters (stood-in structure)
constexpr uint32_t kScript = 0x12140;    // a script (+0x14 its parameters)
constexpr uint32_t kScriptParams = 0x12180;
constexpr uint32_t kArg = 0x12200;       // a GP callback's argument
constexpr uint32_t kText = 0x12300;      // a message's text (writable: strtok)
constexpr uint32_t kOldArray = 0x12500;  // buddy_array (CArray) and its entries
constexpr uint32_t kEntries = 0x12540;
constexpr uint32_t kBuddies = 0x14000;   // buddies, 0x250 apart (8)
constexpr uint32_t kMessages = 0x15400;  // messages, 0x70 apart (8)
constexpr uint32_t kNodes = 0x15C00;     // bare list nodes for the counts, 0x20 apart (40)

uint32_t BuddyAt(int i) { return kBuddies + 0x250 * i; }
uint32_t MessageAt(int i) { return kMessages + 0x70 * i; }

BuddyMan* Man() { return &Word<BuddyMan>(kMan); }

void Link(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
void SetUpMan(bool loggedIn = false, int state = 0)
{
    BuddyMan* man = Man();
    man->m_connection = reinterpret_cast<void*>(0x11);
    man->m_profile = 0x22;
    man->m_state = state;
    man->m_task = &Word<Tsk::BaseTask>(kTask);
    man->m_loggedIn = loggedIn;
    man->m_buddies = {reinterpret_cast<void**>(0x0064D140), reinterpret_cast<void*>(-1), 0, &man->m_buddies,
                      &man->m_buddies};
    man->m_messages = {reinterpret_cast<void**>(0x0064D14C), reinterpret_cast<void*>(-1), 0, &man->m_messages,
                       &man->m_messages};
    Word<uint32_t>(kTask) = At(kTaskVtable);
    Word<uint32_t>(kTask + 0x1C) = At(kMan);
    Word<uint32_t>(kManager + 0x18) = At(kMan);
    Word<uint32_t>(kManager + 0x10) = At(kLobby);
    Word<uint32_t>(kScript + 0x14) = At(kScriptParams);
    uint32_t memory = kMisc + 0x800;
    Word<uint32_t>(memory + 0x12C) = 0x0012C12C; // the heaps (network, internet top-down, bottom-up) told apart
    Word<uint32_t>(memory + 0x13C) = 0x0013C13C;
    Word<uint32_t>(memory + 0x140) = 0x00140140;
}
Buddy* AddBuddyAt(int i, const char* nick, int profile, int status = 1, bool pending = false)
{
    auto* buddy = &Word<Buddy>(BuddyAt(i));
    buddy->m_node = {reinterpret_cast<void**>(0x0064D140), buddy, 0, &buddy->m_node, &buddy->m_node};
    std::snprintf(buddy->m_nick, sizeof(buddy->m_nick), "%s", nick);
    buddy->m_profile = profile;
    buddy->m_status = status;
    buddy->m_pending = pending;
    buddy->m_joinIp = 0x0100007F + i;
    buddy->m_joinPrivateIp = 0x0A00000A + i;
    buddy->m_joinPort = 5000 + i;
    std::snprintf(buddy->m_location, sizeof(buddy->m_location), "Location %d", i);
    std::snprintf(buddy->m_statusText, sizeof(buddy->m_statusText), "Status %d", i);
    Link(&Man()->m_buddies, &buddy->m_node);
    return buddy;
}
BuddyMessage* AddMessageAt(int i, int profile, const char* nick, const char* text)
{
    auto* message = &Word<BuddyMessage>(MessageAt(i));
    message->m_node = {reinterpret_cast<void**>(0x0064D14C), message, 0, &message->m_node, &message->m_node};
    message->m_profile = profile;
    std::snprintf(message->m_nick, sizeof(message->m_nick), "%s", nick);
    std::snprintf(message->m_text, sizeof(message->m_text), "%s", text);
    Link(&Man()->m_messages, &message->m_node);
    return message;
}
// `count` bare nodes on a list (for the counts).
void AddNodes(Mem::ListNode* head, int count)
{
    for (int i = 0; i < count; ++i) {
        auto* node = &Word<Mem::ListNode>(kNodes + 0x20 * i);
        *node = {reinterpret_cast<void**>(0x0064D140), node, 0, node, node};
        Link(head, node);
    }
}

// The players for FillProspectiveBuddyList: GameNetKit's players, the ones listed, and which are local.
std::vector<uint32_t> g_players;
size_t g_playerIndex;
std::set<uint32_t> g_localPlayers;
// gpGetBuddyStatus's answer, the public address, IsThug2Location's answers.
struct Status {
    int profile;
    int status;
    char statusString[256];
    char locationString[256];
    uint32_t ip;
    int port;
};
Status g_status;
uint32_t g_publicIp;
char g_dotted[32];

uint32_t g_taskDestroy; // the stand-in task's destructor (a thunk)

// Everything the buddy code calls.
void Install(StandIns& s)
{
    InstallScriptStandIns(s);
    InstallCrtStandIns(s);
    InstallSystemStandIns(s);
    // GameSpy presence (cdecl).
    StubCdecl(s, {0x005FFD40}, 0xFD40, 3);
    s.At({0x005FFD90}, 0, [](Regs& r) {
        RecN(0xFD90, {r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    StubCdecl(s, {0x005FFD60}, 0xFD60, 1);
    StubCdecl(s, {0x00600020}, 0x0020, 1);
    s.At({0x00600050}, 0, [](Regs& r) {
        Rec(0x0050, {r.args[0], r.args[1], Str(r.args[2])});
        return 0u;
    });
    StubCdecl(s, {0x006002E0}, 0x02E0, 2);
    s.At({0x00600330}, 0, [](Regs& r) {
        Rec(0x0330, {r.args[0], r.args[1], Str(r.args[2]), Str(r.args[3])});
        return 0u;
    });
    s.At({0x005FFEF0}, 0, [](Regs& r) {
        RecN(0xFEF0, {r.args[0], Str(r.args[1]), r.args[2], Str(r.args[3]), Str(r.args[4]), r.args[5], r.args[6],
                      r.args[7], r.args[8], r.args[9]});
        return 0u;
    });
    s.At({0x006001F0}, 0, [](Regs& r) {
        Rec(0x01F0, {r.args[0], r.args[1]});
        std::memcpy(reinterpret_cast<void*>(r.args[2]), &g_status, sizeof(g_status));
        return 0u;
    });
    // The lobby code around this class.
    s.At({0x00548040, AddressOf(&GameNet::IsThug2Location)}, 0, [](Regs& r) {
        const char* location = reinterpret_cast<const char*>(r.args[0]);
        bool ok = std::strncmp(location, "10thug211:", 10) == 0;
        Rec(0x8040, {Str(r.args[0]), ok});
        return static_cast<uint32_t>(ok);
    });
    Stub(s, {0x005482A0, AddressOf(&GameNet::BuddyMan::SetLoggedIn)}, 4, 0x82A0);
    Stub(s, {0x0053F880, AddressOf(&GameNet::LobbyMan::GetPeer)}, 0, 0xF880, [](Regs&) { return At(kLobby + 0x40); });
    StubCdecl(s, {0x005FC940}, 0xC940, 1, [](Regs&) { return g_publicIp; });
    StandInApi(s, 0x006452A4, reinterpret_cast<const void*>(&inet_ntoa), 4, [](Regs& r) {
        Rec(0x52A4, {r.args[0]});
        std::snprintf(g_dotted, sizeof(g_dotted), "addr-%08X", r.args[0]);
        return reinterpret_cast<uint32_t>(g_dotted);
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Rec(0x3DE0, {r.args[0]});
        return 0x1357u;
    });
    Stub(s, {0x00532AA0, AddressOf(&GameNet::Manager::SetJoinMode)}, 4, 0x2AA0);
    Stub(s, {0x00533240, AddressOf(&GameNet::Manager::SetLobbyId)}, 4, 0x3240);
    // The players.
    auto next = [](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1]}); // (args[0]: the caller's search, a local)
        return g_playerIndex < g_players.size() ? At(g_players[g_playerIndex++]) : 0u;
    };
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [next](Regs& r) {
        g_playerIndex = 0;
        return next(r);
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, next);
    s.At({0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, [](Regs& r) {
        bool local = g_localPlayers.count(Rel(r.ecx)) != 0;
        Rec(0x3300, {r.ecx, local});
        return static_cast<uint32_t>(local);
    });
    // The task's scalar deleting destructor (the stand-in task's vtable slot 2).
    void* destroy = s.Thunk(4, [](Regs& r) {
        Rec(0x7A5D, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_taskDestroy = reinterpret_cast<uint32_t>(destroy);
}

void MapOurCode()
{
    g_codeMap.clear();
    MapCode(&BuddyMan::OnError, 0x00549ED0);
    MapCode(&BuddyMan::OnBuddyStatus, 0x00548F50);
    MapCode(&BuddyMan::OnBuddyMessage, 0x005496F0);
    MapCode(&BuddyMan::OnConnectNewUser, 0x0054A2C0);
    MapCode(&BuddyMan::BuddyTask, 0x0054A300);
    MapCode(&BuddyMan::CreateProfileThread, 0x0054A3D0);
    MapCode(&Stubs::DoNothing, 0x005480A0);
}

// Each case on a fresh arena (the trace kept), then the part of the arena the code uses.
void Snapshot()
{
    for (uint32_t i = 0x12000; i < g_heapNext + 0x40 && i < kArenaSize; i += 4)
        g_trace.push_back(Norm(Word<uint32_t>(i)));
    for (uint32_t i = 0; i < 0x200; i += 4)
        g_trace.push_back(Norm(Word<uint32_t>(kManager + i)));
    g_trace.push_back(Norm(Word<uint32_t>(kManager + 0x9950)));
    g_trace.push_back(GameVariable<uint32_t>(0x007CE458));
    g_trace.push_back(GameVariable<uint32_t>(0x007CE45C));
    g_trace.push_back(GameVariable<uint8_t>(0x007CE454));
}
using Case = std::function<void(bool original)>;
void RunCases(int run, const std::vector<Case>& cases)
{
    for (size_t i = 0; i < cases.size(); ++i) {
        std::vector<uint32_t> kept = g_trace;
        SetUpArena(run);
        g_trace = kept;
        ResetAnswers();
        SetUpMan();
        Word<uint32_t>(kTaskVtable + 8) = 0; // (set by the cases that need it)
        g_players.clear();
        g_localPlayers.clear();
        std::memset(&g_status, 0, sizeof(g_status));
        g_publicIp = 0;
        GameVariable<uint32_t>(0x007CE458) = 0x458;
        GameVariable<uint32_t>(0x007CE45C) = 0x45C;
        GameVariable<bool>(0x007CE454) = false;
        g_trace.push_back(0xCA5E0000u + static_cast<uint32_t>(i));
        cases[i](run == 0);
        Snapshot();
    }
}
void Compare2(const char* what, const std::vector<Case>& cases)
{
    MapOurCode();
    RunBoth(what, Install, [&](bool original) { RunCases(original ? 0 : 1, cases); },
            {{0x007CE458, 4, {}}, {0x007CE45C, 4, {}}, {0x007CE454, 1, {}}});
}

using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void CallCFunc(bool original, uint32_t address, CFunc ours)
{
    auto* params = &Word<Script::CStruct>(kParams);
    auto* script = &Word<Script::CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    g_trace.insert(g_trace.end(), {0xC0DE, static_cast<uint32_t>(result)});
}

// --- the lists ---

void TestNetBuddyLists()
{
    std::vector<Case> cases;
    // CountBuddies / IsBuddy / CancelAddBuddy / RemoveBuddy over lists of 0-3 buddies (pending ones among them).
    for (int count = 0; count <= 3; ++count) {
        for (int pendingAt = -1; pendingAt < count; ++pendingAt) {
            auto make = [count, pendingAt]() {
                for (int i = 0; i < count; ++i)
                    AddBuddyAt(i, "Buddy", 100 + i, 1, i == pendingAt || (pendingAt == 0 && i == 2));
            };
            cases.push_back([make](bool original) {
                make();
                int n = original ? Original<int(__thiscall*)(BuddyMan*)>(0x00548E10)(Man()) : Man()->CountBuddies();
                g_trace.push_back(static_cast<uint32_t>(n));
                for (int profile : {99, 100, 101, 102, 103}) {
                    bool is = original ? Original<bool(__thiscall*)(BuddyMan*, int)>(0x00549CB0)(Man(), profile)
                                       : Man()->IsBuddy(profile);
                    g_trace.push_back(is);
                }
            });
            cases.push_back([make](bool original) {
                make();
                if (original)
                    Original<void(__thiscall*)(BuddyMan*)>(0x005499A0)(Man());
                else
                    Man()->CancelAddBuddy();
            });
            for (int profile : {100, 102, 7}) {
                cases.push_back([make, profile](bool original) {
                    make();
                    if (original)
                        Original<void(__thiscall*)(BuddyMan*, int)>(0x00549A50)(Man(), profile);
                    else
                        Man()->RemoveBuddy(profile);
                });
            }
        }
    }
    // LogOff with buddies and messages; AddBuddy.
    for (int buddies = 0; buddies <= 2; ++buddies) {
        for (int messages = 0; messages <= 2; ++messages) {
            cases.push_back([buddies, messages](bool original) {
                SetUpMan(true, 4);
                for (int i = 0; i < buddies; ++i)
                    AddBuddyAt(i, "Buddy", 100 + i);
                for (int i = 0; i < messages; ++i)
                    AddMessageAt(i, 200 + i, "Nick", "Text");
                if (original)
                    Original<void(__thiscall*)(BuddyMan*)>(0x00549B90)(Man());
                else
                    Man()->LogOff();
            });
        }
    }
    for (int buddies = 0; buddies <= 2; ++buddies) {
        cases.push_back([buddies](bool original) {
            for (int i = 0; i < buddies; ++i)
                AddBuddyAt(i, "Buddy", 100 + i);
            if (original)
                Original<void(__thiscall*)(BuddyMan*, const char*, int)>(0x00548D50)(Man(), "NewBuddyNick", 0x4242);
            else
                Man()->AddBuddy("NewBuddyNick", 0x4242);
        });
    }
    // The counting commands around their limits.
    for (int count : {0, 1, 2, 29, 30, 31}) {
        cases.push_back([count](bool original) {
            AddNodes(&Man()->m_buddies, count);
            CallCFunc(original, 0x00548E90, GameNet::HasBuddies);
            CallCFunc(original, 0x00548ED0, GameNet::BuddyListFull);
            CallCFunc(original, 0x00548F10, GameNet::AnyBuddyMessagesLeft);
        });
        cases.push_back([count](bool original) {
            AddNodes(&Man()->m_messages, count);
            CallCFunc(original, 0x00548F10, GameNet::AnyBuddyMessagesLeft);
            CallCFunc(original, 0x00548E90, GameNet::HasBuddies);
        });
    }
    // The commands.
    cases.push_back([](bool original) {
        g_ints[KeyOf(At(kParams), "profile")] = 0x4343;
        g_strings[KeyOf(At(kParams), "nick")] = "CommandNick";
        AddBuddyAt(0, "Buddy", 100);
        CallCFunc(original, 0x00548E40, GameNet::AddBuddy);
    });
    for (int profile : {100, 101, 5}) {
        cases.push_back([profile](bool original) {
            g_ints[KeyOf(At(kParams), "profile")] = profile;
            AddBuddyAt(0, "Buddy", 100);
            AddBuddyAt(1, "Buddy", 101, 2, true);
            CallCFunc(original, 0x00549D90, GameNet::RemoveBuddy);
        });
        cases.push_back([profile](bool original) {
            g_ints[KeyOf(At(kParams), 0x7EA855F0u)] = profile;
            AddBuddyAt(0, "Buddy", 100);
            AddBuddyAt(1, "Buddy", 101, 2, true);
            CallCFunc(original, 0x00549EA0, GameNet::IsAlreadyMyBuddy);
        });
    }
    cases.push_back([](bool original) {
        AddBuddyAt(0, "Buddy", 100);
        AddBuddyAt(1, "Buddy", 101, 2, true);
        CallCFunc(original, 0x00549D80, GameNet::CancelAddBuddy);
    });
    cases.push_back([](bool original) {
        SetUpMan(true, 4);
        AddBuddyAt(0, "Buddy", 100);
        AddMessageAt(0, 7, "Nick", "Text");
        CallCFunc(original, 0x00549D10, GameNet::ProfileLogOff);
    });
    for (int messages = 1; messages <= 2; ++messages) {
        cases.push_back([messages](bool original) {
            for (int i = 0; i < messages; ++i)
                AddMessageAt(i, 300 + i, i ? "Second" : "First", i ? "Text two" : "Text one");
            CallCFunc(original, 0x00549DC0, GameNet::ShowNextBuddyMessage);
        });
    }
    // The constructors and destructors.
    cases.push_back([](bool original) {
        void* memory = &Word<uint8_t>(MessageAt(3));
        auto* result = original ? Original<BuddyMessage*(__thiscall*)(void*)>(0x00548CB0)(memory)
                                : static_cast<BuddyMessage*>(memory)->Construct();
        g_trace.push_back(Rel(reinterpret_cast<uint32_t>(result)));
    });
    for (unsigned int flags : {0u, 1u, 3u}) {
        cases.push_back([flags](bool original) {
            AddBuddyAt(0, "Buddy", 100);
            Buddy* buddy = AddBuddyAt(1, "Buddy", 101);
            AddBuddyAt(2, "Buddy", 102);
            auto* result = original ? Original<Buddy*(__thiscall*)(Buddy*, unsigned int)>(0x00548CF0)(buddy, flags)
                                    : buddy->Destroy(flags);
            g_trace.push_back(Rel(reinterpret_cast<uint32_t>(result)));
        });
        cases.push_back([flags](bool original) {
            AddMessageAt(0, 1, "A", "a");
            BuddyMessage* message = AddMessageAt(1, 2, "B", "b");
            auto* result = original
                               ? Original<BuddyMessage*(__thiscall*)(BuddyMessage*, unsigned int)>(0x00548D20)(message,
                                                                                                             flags)
                               : message->Destroy(flags);
            g_trace.push_back(Rel(reinterpret_cast<uint32_t>(result)));
        });
    }
    cases.push_back([](bool original) {
        std::memset(Man(), 0xEE, sizeof(BuddyMan));
        auto* result = original ? Original<BuddyMan*(__thiscall*)(BuddyMan*)>(0x0054A480)(Man()) : Man()->Construct();
        g_trace.push_back(Rel(reinterpret_cast<uint32_t>(result)));
    });
    for (bool task : {false, true}) {
        cases.push_back([task](bool original) {
            AddBuddyAt(0, "Buddy", 100);
            AddMessageAt(0, 7, "Nick", "Text");
            Word<uint32_t>(kTaskVtable + 8) = g_taskDestroy;
            if (!task)
                Man()->m_task = nullptr;
            if (original)
                Original<void(__thiscall*)(BuddyMan*)>(0x005488B0)(Man());
            else
                Man()->DestroyBody();
        });
    }
    Compare2("buddy lists: counts, IsBuddy, cancel / remove / add, log off, the commands, constructors, destructors",
             cases);
}

// --- the menus ---

void TestNetBuddyMenus()
{
    std::vector<Case> cases;
    for (int status : {1, 2}) {
        for (bool allowJoin : {false, true}) {
            for (bool allowRemove : {false, true}) {
                cases.push_back([=](bool original) {
                    Buddy* buddy = AddBuddyAt(0, "Mixed Case nick", 0x515, status);
                    if (original)
                        Original<void(__stdcall*)(Buddy*, bool, bool)>(0x00548560)(buddy, allowJoin, allowRemove);
                    else
                        BuddyMan::ShowBuddy(buddy, allowJoin, allowRemove);
                });
            }
        }
    }
    for (bool clear : {false, true}) {
        for (int flags = 0; flags < 4; ++flags) {
            cases.push_back([=](bool original) {
                AddBuddyAt(0, "First", 100, 2);
                AddBuddyAt(1, "Pending", 101, 1, true);
                AddBuddyAt(2, "Third", 102, 1);
                bool allowJoin = (flags & 1) != 0, allowRemove = (flags & 2) != 0;
                if (original)
                    Original<void(__thiscall*)(BuddyMan*, bool, bool, bool)>(0x005498E0)(Man(), clear, allowJoin,
                                                                                      allowRemove);
                else
                    Man()->FillBuddyList(clear, allowJoin, allowRemove);
            });
        }
    }
    for (int flags = 0; flags < 8; ++flags) {
        cases.push_back([=](bool original) {
            AddBuddyAt(0, "First", 100, 2);
            AddBuddyAt(1, "Second", 101, 1);
            if (flags & 1)
                g_flags.insert(KeyOf(At(kParams), "clear_list"));
            if (flags & 2)
                g_flags.insert(KeyOf(At(kParams), "allow_join"));
            if (flags & 4)
                g_flags.insert(KeyOf(At(kParams), "allow_remove"));
            CallCFunc(original, 0x00549D20, GameNet::FillBuddyList);
        });
    }
    // The players who could be buddies: a local one, one without a profile, a buddy, two more.
    for (bool command : {false, true}) {
        cases.push_back([command](bool original) {
            AddBuddyAt(0, "Buddy", 0x303);
            int profiles[5] = {0x301, 0, 0x303, 0x304, 0x305};
            for (int i = 0; i < 5; ++i) {
                uint32_t player = kPlayers + kPlayerSize * i;
                Word<int>(player + 0x28) = profiles[i];
                g_players.push_back(player);
            }
            g_localPlayers.insert(Rel(At(kPlayers)));
            if (command)
                CallCFunc(original, 0x0054A2B0, GameNet::FillProspectiveBuddyList);
            else if (original)
                Original<void(__thiscall*)(BuddyMan*)>(0x00549F50)(Man());
            else
                Man()->FillProspectiveBuddyList();
        });
    }
    // JoinBuddy: playing or observing, a buddy behind another address or ours.
    for (bool observe : {false, true}) {
        for (bool sameAddress : {false, true}) {
            cases.push_back([=](bool original) {
                uint32_t params = At(kParams);
                if (observe)
                    g_flags.insert(KeyOf(params, "observe"));
                g_strings[KeyOf(params, "location")] = "Buddy's Game";
                g_ints[KeyOf(params, "join_ip")] = 0x04030201;
                g_ints[KeyOf(params, "join_private_ip")] = 0x0A0B0C0D;
                g_ints[KeyOf(params, "join_port")] = 6500;
                g_publicIp = sameAddress ? 0x04030201u : 0x09090909u;
                CallCFunc(original, 0x00548A40, GameNet::JoinBuddy);
            });
        }
    }
    for (uint32_t now : {0u, 123456u}) {
        cases.push_back([now](bool original) {
            g_now = now;
            CallCFunc(original, 0x00548940, GameNet::SetUniqueId);
        });
    }
    Compare2("buddy menus: ShowBuddy (join / remove / status), FillBuddyList, prospective buddies, JoinBuddy, SetUniqueId",
             cases);
}

// --- the GP callbacks, the task, logging in ---

using Callback = void(__cdecl*)(void*, const void*, BuddyMan*);
void CallBack(bool original, uint32_t address, Callback ours)
{
    void* connection = reinterpret_cast<void*>(0x77);
    const void* arg = &Word<uint8_t>(kArg);
    if (original)
        Original<Callback>(address)(connection, arg, Man());
    else
        ours(connection, arg, Man());
}

void TestNetBuddyCallbacks()
{
    std::vector<Case> cases;
    for (int code : {0x107, 0x201, 0x202, 0x603, 0x999, 0}) {
        cases.push_back([code](bool original) {
            SetUpMan(true, 4);
            AddBuddyAt(0, "Buddy", 100);
            Word<int>(kArg + 4) = code;
            CallBack(original, 0x00549ED0, BuddyMan::OnError);
        });
    }
    // Buddy status: a buddy on the list (first or second; pending or not; playing in a THUG2 game, playing elsewhere,
    // online), or someone not on it (its nick from buddy_array, or not there, or no array).
    struct StatusCase {
        int profile;
        int status;
        const char* location;
        bool pending;
        int array; // -1 no buddy_array, else its size
    };
    const char* game = "10thug211:16909060:168430090:5153:A Game:Name";
    const StatusCase statusCases[] = {
        {100, 1, "Somewhere", false, 2}, {101, 1, "Somewhere", false, 2}, {101, 2, "elsewhere:1:2:3:x", false, 2},
        {100, 2, game, false, 2},        {101, 2, game, true, 2},          {100, 0, "", true, 0},
        {100, 1, "Lobby", true, 3},      {555, 1, "Online", false, 2},      {556, 1, "Online", false, 2},
        {555, 1, "Online", false, -1},   {555, 2, "nothug:1:2:3:x", false, 2}, {556, 2, game, false, 2},
        {555, 0, "", false, 0},
    };
    for (const StatusCase& c : statusCases) {
        cases.push_back([c](bool original) {
            AddBuddyAt(0, "FirstBuddy", 100, 1, c.pending && c.profile == 100);
            AddBuddyAt(1, "SecondBuddy", 101, 1, c.pending && c.profile == 101);
            Word<int>(kArg + 8) = 3; // the buddy index
            g_status.profile = c.profile;
            g_status.status = c.status;
            std::snprintf(g_status.statusString, sizeof(g_status.statusString), "status of %d", c.profile);
            std::snprintf(g_status.locationString, sizeof(g_status.locationString), "%s", c.location);
            if (c.array >= 0) {
                uint32_t array = At(kOldArray);
                g_prefArrays[Crc::GenerateCRCFromString("buddy_array")] = array;
                Word<uint32_t>(kOldArray + 4) = static_cast<uint32_t>(c.array);
                for (int i = 0; i < c.array; ++i) {
                    uint32_t entry = At(kEntries + 0x10 * i);
                    g_elements[{Rel(array), static_cast<uint32_t>(i)}] = entry;
                    g_ints[KeyOf(entry, "profile")] = 554 + i;
                    g_strings[KeyOf(entry, "nick")] = i == 1 ? "ArrayNick" : "OtherNick";
                }
            }
            CallBack(original, 0x00548F50, BuddyMan::OnBuddyStatus);
        });
    }
    // Buddy messages: the limit, one line from a buddy (or not), "nick\ntext".
    for (int waiting : {20, 21}) {
        cases.push_back([waiting](bool original) {
            AddNodes(&Man()->m_messages, waiting);
            std::snprintf(&Word<char>(kText), 0x100, "%s", "Nick\nHello there");
            Word<int>(kArg) = 100;
            Word<uint32_t>(kArg + 8) = At(kText);
            CallBack(original, 0x005496F0, BuddyMan::OnBuddyMessage);
        });
    }
    for (int from : {100, 101, 102}) {
        for (bool twoLines : {false, true}) {
            cases.push_back([from, twoLines](bool original) {
                AddBuddyAt(0, "FirstBuddy", 100);
                AddBuddyAt(1, "SecondBuddy", 101);
                AddMessageAt(0, 7, "Old", "Old text");
                std::snprintf(&Word<char>(kText), 0x100, "%s", twoLines ? "Sender\nTwo line text" : "One line text");
                Word<int>(kArg) = from;
                Word<uint32_t>(kArg + 8) = At(kText);
                CallBack(original, 0x005496F0, BuddyMan::OnBuddyMessage);
            });
        }
    }
    // gpConnectNewUser's answer.
    for (int result : {0, 4, 1}) {
        for (bool creating : {false, true}) {
            cases.push_back([result, creating](bool original) {
                GameVariable<bool>(0x007CE454) = creating;
                Word<int>(kArg) = result;
                Word<int>(kArg + 4) = 0x9876;
                CallBack(original, 0x0054A2C0, BuddyMan::OnConnectNewUser);
            });
        }
    }
    // The task.
    struct TaskCase {
        int state;
        uint32_t now;
    };
    const TaskCase taskCases[] = {{0, 50000}, {1, 20000}, {1, 20001}, {2, 20000}, {2, 20001},
                                  {3, 5},     {4, 31000}, {4, 31001}, {5, 99999}, {-1, 1}};
    for (const TaskCase& c : taskCases) {
        cases.push_back([c](bool original) {
            SetUpMan(c.state == 4, c.state);
            GameVariable<uint32_t>(0x007CE458) = 10000; // the login's start
            GameVariable<uint32_t>(0x007CE45C) = 30000; // the last process
            g_now = c.now;
            auto* task = &Word<Tsk::BaseTask>(kTask);
            if (original)
                Original<void(__cdecl*)(Tsk::BaseTask*)>(0x0054A300)(task);
            else
                BuddyMan::BuddyTask(task);
        });
    }
    // Logging in, making the profile.
    for (bool loggedIn : {false, true}) {
        for (bool inList : {false, true}) {
            cases.push_back([loggedIn, inList](bool original) {
                SetUpMan(loggedIn, 4);
                AddBuddyAt(0, "Buddy", 100);
                g_taskInList = inList;
                g_now = 777;
                if (original)
                    Original<void(__thiscall*)(BuddyMan*)>(0x0054A1A0)(Man());
                else
                    Man()->LogIn();
            });
        }
        cases.push_back([loggedIn](bool original) {
            SetUpMan(loggedIn, 4);
            AddMessageAt(0, 7, "Nick", "Text");
            g_now = 888;
            if (original)
                Original<void(__thiscall*)(BuddyMan*)>(0x0054A5C0)(Man());
            else
                Man()->CreateProfile();
        });
    }
    cases.push_back([](bool original) {
        uint32_t ui = Crc::GenerateCRCFromString("ui_string");
        g_prefStrings[{Crc::GenerateCRCFromString("profile_email"), ui}] = "someone@example.com";
        g_prefStrings[{Crc::GenerateCRCFromString("profile_password"), ui}] = "secret";
        unsigned long result = original ? Original<unsigned long(__stdcall*)(void*)>(0x0054A3D0)(Man())
                                        : BuddyMan::CreateProfileThread(Man());
        g_trace.push_back(static_cast<uint32_t>(result));
    });
    for (bool success : {false, true}) {
        cases.push_back([success](bool original) {
            g_prefChecksums[{Crc::GenerateCRCFromString("profile_success"), Crc::GenerateCRCFromString("checksum")}] =
                Crc::GenerateCRCFromString(success ? "boolean_true" : "boolean_false");
            CallCFunc(original, 0x0054A250, GameNet::ProfileLogIn);
        });
    }
    cases.push_back([](bool original) { CallCFunc(original, 0x0054A650, GameNet::CreateProfile); });
    Compare2("buddy callbacks: errors, buddy status (found / new / pending / THUG2 games), messages, new user, the "
             "task, log in, create profile",
             cases);
}

} // namespace

SELFTEST(Early, TestNetBuddyLists, GameNetKit::Cleanup);
SELFTEST(Early, TestNetBuddyMenus, GameNetKit::Cleanup);
SELFTEST(Early, TestNetBuddyCallbacks, GameNetKit::Cleanup);
