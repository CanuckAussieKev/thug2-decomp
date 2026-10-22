// Self-tests for the network manager's list types, its destructor, the joining players' removal and the server
// search / GameSpy / level members (src/game/net/gamenet_lists.cpp, gamenet_players.cpp, gamenet_members.cpp,
// gamenet_settings.cpp) and the input handler's scalar deleting destructor (src/game/sys/viewer.cpp): the original and
// ours on the same stand-in arena (gamenet_testkit.h), everything they call outside stood in for by recording code;
// the calls, results and arena are compared, case by case.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/task.h"
#include "game/net/gamenet_lists.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_manager.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/sys/viewer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Mem::ListNode;
using Script::CArray;
using Script::CStruct;

constexpr uint32_t kNetMgr = 0x14000;  // Net::Manager
constexpr uint32_t kNodes = 0x14400;   // list nodes, 0x20 apart (32)
constexpr uint32_t kRecords = 0x14C00; // joining players' records, 0x20 apart: +0x14 their connection
constexpr uint32_t kArray = 0x15000;   // mode_info (a CArray) and its elements
constexpr uint32_t kTasks = 0x15100;   // tasks, 0x10 apart: a vtable pointer
constexpr uint32_t kHandler = 0x15300; // an input handler (0x2C)

bool g_netGame, g_peer, g_peerConnected, g_lobbyHosting, g_spawnSetsClient;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answer;
void* g_nodeVtable[4];
void* g_taskVtable[4];

uint32_t Node(int i) { return kNodes + 0x20 * static_cast<uint32_t>(i); }
uint32_t Record(int i) { return kRecords + 0x20 * static_cast<uint32_t>(i); }
ListNode* NodeAt(uint32_t offset) { return &Word<ListNode>(offset); }

// A circular list: `head` (data -1) then the nodes at `nodes` (data: `data[i]`), each with `vtable`.
void MakeList(uint32_t head, void** headVtable, std::initializer_list<std::pair<uint32_t, uint32_t>> nodes, void** vtable)
{
    ListNode* h = NodeAt(head);
    h->vtable = headVtable;
    h->data = reinterpret_cast<void*>(-1);
    h->priority = 0;
    ListNode* last = h;
    for (const auto& [offset, data] : nodes) {
        ListNode* n = NodeAt(offset);
        n->vtable = vtable;
        n->data = data ? reinterpret_cast<void*>(At(data)) : nullptr;
        n->priority = 0;
        last->next = n;
        n->prev = last;
        last = n;
    }
    last->next = h;
    h->prev = last;
}

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
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return 0x00ABCDEFu; });
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    Stub(s, {0x005453E0}, 4, 0x53E0);
    Stub(s, {0x00454590, AddressOf(&Net::Manager::DisconnectFromInternet)}, 0, 0x4590, [](Regs&) { return 1u; });
    for (uint32_t a : {0x00545370u, 0x005462F0u, 0x00545300u, 0x00543560u, 0x00540D50u, 0x0054B470u, 0x005488B0u,
                       0x0054D610u, 0x00588B70u})
        Stub(s, {a}, 0, a & 0xFFFF);
    Stub(s, {0x00534430, AddressOf(&Manager::SpawnMatchClient)}, 0, 0x4430, [](Regs& r) {
        if (g_spawnSetsClient)
            *reinterpret_cast<uint32_t*>(r.ecx + 0x2C) = At(kApp);
        return g_spawnSetsClient ? At(kApp) : 0u;
    });
    s.At({0x00457280, AddressOf(&Net::App::SendMessageTo)}, 24, [](Regs& r) {
        // (only the time's word of the 12 bytes: the rest is the original's stack)
        Rec(0x7280, {r.ecx, r.args[0] & 0xFF, r.args[1] & 0xFFFF, *reinterpret_cast<uint32_t*>(r.args[2]), r.args[3],
                     r.args[4] & 0xFFFF, r.args[5]});
        return 1u;
    });
    Stub(s, {0x00457300, AddressOf(&Net::App::TerminateConnection)}, 4, 0x7300);
    for (uint32_t a : {0x005F9C40u, 0x005F9BE0u})
        StubCdecl(s, {a}, a & 0xFFFF, 0);
    for (uint32_t a : {0x005FBBA0u, 0x005F9900u, 0x005FBB20u, 0x005FCCF0u, 0x005FBAD0u})
        StubCdecl(s, {a}, a & 0xFFFF, 1);
    Stub(s, {0x0053F880}, 0, 0xF880, [](Regs&) { return g_peer ? At(kNetMgr + 0x380) : 0u; });
    StubCdecl(s, {0x005FC730}, 0xC730, 1, [](Regs&) { return g_peerConnected ? 1u : 0u; });
    Stub(s, {0x0053F810}, 0, 0xF810, [](Regs&) { return g_lobbyHosting ? 1u : 0u; });
    Stub(s, {0x00564D80}, 0, 0x4D80, [](Regs&) { return At(kMisc + 0x600); });
    Stub(s, {0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, 0xC4A0, [](Regs& r) { return r.args[0]; });
    Stub(s, {0x0046C490, AddressOf(&Prefs::Preferences::Deinit)}, 0, 0xC490);
    s.At({0x00476EC0, 0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) { return Answer(r, 0x6950, r.args[0]); });
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) { return Answer(r, 0x6C60, r.args[0]); });
    StubCdecl(s, {0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0x9070, 1,
              [](Regs&) { return At(kArray); });
    Stub(s, {0x00577280, AddressOf(&Mdl::ViewerInputHandler::DestroyBody)}, 0, 0x7280A);
    g_nodeVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x9DE1, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_taskVtable[2] = s.Thunk(4, [](Regs& r) {
        Rec(0x7A52, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_answer.clear();
        g_netGame = g_peer = g_peerConnected = g_lobbyHosting = g_spawnSetsClient = false;
        GameVariable<uint32_t>(0x006EFDD8) = At(kNetMgr);
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

Manager* Self() { return &Word<Manager>(kManager); }
template <typename R, typename... A>
R Orig(uint32_t address, A... args)
{
    return Original<R(__thiscall*)(Manager*, A...)>(address)(Self(), args...);
}
void Result(uint32_t tag, uint32_t value) { Rec(0xF000 | tag, {value}); }

void Lists(bool original)
{
    using namespace GameNet;
    // Four lists of three nodes; each destructor on a middle node, a last node and a lone node (linked to itself).
    auto call = [&](uint32_t address, ListNode* node, int which, unsigned int flags, auto oursBody, auto oursScalar,
                    auto oursHead) {
        uint32_t result = 0;
        if (original) {
            if (which == 0)
                Original<void(__thiscall*)(ListNode*)>(address)(node);
            else
                result = reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(ListNode*, unsigned int)>(address)(node, flags));
        } else if (which == 0) {
            oursBody(node);
        } else if (which == 1) {
            result = reinterpret_cast<uint32_t>(oursScalar(node, flags));
        } else {
            result = reinterpret_cast<uint32_t>(oursHead(node, flags));
        }
        Result(1, result);
    };
    int n = 0;
    auto test = [&](uint32_t body, uint32_t scalar, uint32_t head, auto oursBody, auto oursScalar, auto oursHead) {
        uint32_t base = Node(n);
        n += 4;
        MakeList(base, reinterpret_cast<void**>(0x1111), {{base + 0x20, 0x100}, {base + 0x40, 0x200}, {base + 0x60, 0}},
                 reinterpret_cast<void**>(0x2222));
        call(body, NodeAt(base + 0x20), 0, 0, oursBody, oursScalar, oursHead);
        call(scalar, NodeAt(base + 0x60), 1, 1, oursBody, oursScalar, oursHead);
        call(scalar, NodeAt(base + 0x40), 1, 0, oursBody, oursScalar, oursHead);
        if (head)
            call(head, NodeAt(base), 2, 2, oursBody, oursScalar, oursHead), call(head, NodeAt(base), 2, 1, oursBody, oursScalar, oursHead);
    };
    test(0x005366A0, 0x00536760, 0x00536CE0, [](ListNode* p) { static_cast<PlayerNode*>(p)->DestroyBody(); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<PlayerNode*>(p)->Destroy(f)); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<PlayerNode*>(p)->DestroyHead(f)); });
    test(0x005366E0, 0x005367E0, 0x00536D10, [](ListNode* p) { static_cast<NewPlayerNode*>(p)->DestroyBody(); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<NewPlayerNode*>(p)->Destroy(f)); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<NewPlayerNode*>(p)->DestroyHead(f)); });
    test(0x005366C0, 0x005367A0, 0x00536D40, [](ListNode* p) { static_cast<ServerNode*>(p)->DestroyBody(); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<ServerNode*>(p)->Destroy(f)); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<ServerNode*>(p)->DestroyHead(f)); });
    test(0x00536700, 0x00536720, 0, [](ListNode* p) { static_cast<QueuedScriptNode*>(p)->DestroyBody(); },
         [](ListNode* p, unsigned int f) { return static_cast<void*>(static_cast<QueuedScriptNode*>(p)->Destroy(f)); },
         [](ListNode*, unsigned int) { return static_cast<void*>(nullptr); });
    // The input handler's.
    for (unsigned int flags : {0u, 1u, 2u}) {
        auto* handler = &Word<Mdl::ViewerInputHandler>(kHandler);
        uint32_t result = original ? reinterpret_cast<uint32_t>(
                                         Original<void*(__thiscall*)(void*, unsigned int)>(0x00536670)(handler, flags))
                                   : reinterpret_cast<uint32_t>(handler->Destroy(flags));
        Result(2, result);
    }
}

void NewPlayers(bool original)
{
    auto removeAll = [&]() {
        if (original)
            Orig<void>(0x00536820);
        else
            Self()->RemoveAllNewPlayers();
    };
    auto remove = [&](uint32_t player) {
        if (original)
            Orig<void>(0x00536B10, player ? At(player) : 0u);
        else
            Self()->RemoveNewPlayer(player ? reinterpret_cast<void*>(At(player)) : nullptr);
    };
    for (int i = 0; i < 6; ++i)
        Word<uint32_t>(Record(i) + 0x14) = i == 2 ? 0 : At(kMisc + 0x900 + 0x10 * static_cast<uint32_t>(i));
    auto list = [&](std::initializer_list<std::pair<uint32_t, uint32_t>> nodes) {
        MakeList(kManager + 0x130, reinterpret_cast<void**>(0x64C9F4), nodes, g_nodeVtable);
    };
    // Empty; three players on the server; on a client (no server: nothing terminated).
    list({});
    removeAll();
    list({{Node(0), Record(0)}, {Node(1), Record(1)}, {Node(2), Record(2)}});
    removeAll();
    Word<uint32_t>(kManager + 4) = 0;
    list({{Node(0), Record(0)}, {Node(1), 0}, {Node(2), Record(2)}});
    removeAll();
    // One removed: not there, the first, the last, the one without a connection - server, then client.
    for (int server = 1; server >= 0; --server) {
        Word<uint32_t>(kManager + 4) = server ? At(kApp + 0x400) : 0u;
        for (uint32_t which : {Record(5), Record(0), Record(3), Record(2)}) {
            list({{Node(0), Record(0)}, {Node(1), Record(1)}, {Node(2), Record(2)}, {Node(3), Record(3)}});
            remove(which);
        }
        list({});
        remove(Record(1));
    }
}

void Destructor(bool original)
{
    // Every part there (GameSpy's reporting up), then none (reporting down).
    for (int full = 1; full >= 0; --full) {
        GameVariable<bool>(0x007CCE1C) = full != 0;
        uint32_t parts[] = {0x10, 0x14, 0x18, 0x1C};
        for (uint32_t i = 0; i < 4; ++i)
            Word<uint32_t>(kManager + parts[i]) = full ? At(kMisc + 0xA00 + 0x20 * i) : 0u;
        uint32_t tasks[] = {0xC0, 0xC4, 0xCC, 0xD0, 0xD4, 0xE0, 0xE4, 0xE8, 0xEC, 0xF0};
        for (uint32_t i = 0; i < 10; ++i) {
            Word<void**>(kTasks + 0x10 * i) = g_taskVtable;
            Word<uint32_t>(kManager + tasks[i]) = (full || i % 3 == 0) ? At(kTasks + 0x10 * i) : 0u;
        }
        MakeList(kManager + 0x11C, reinterpret_cast<void**>(0x64C9F0), {{Node(0), Record(0)}}, reinterpret_cast<void**>(0x64C9E8));
        MakeList(kManager + 0x130, reinterpret_cast<void**>(0x64C9F4), {{Node(1), Record(1)}, {Node(2), Record(2)}},
                 reinterpret_cast<void**>(0x64C9E4));
        MakeList(kManager + 0x144, reinterpret_cast<void**>(0x64C9F8), {}, reinterpret_cast<void**>(0x64C8EC));
        MakeList(kManager + 0x158, reinterpret_cast<void**>(0x64C9FC), {{Node(3), Record(3)}}, reinterpret_cast<void**>(0x64C9EC));
        MakeList(kManager + 0x16C, reinterpret_cast<void**>(0x64C9FC), {{Node(4), Record(4)}, {Node(5), Record(5)}},
                 reinterpret_cast<void**>(0x64C9EC));
        if (original)
            Orig<void>(0x00536D70);
        else
            Self()->DestroyBody();
        Result(3, GameVariable<uint8_t>(0x007CCE1C));
        // The heads and nodes as they're left (the full words: whatever a head's unlinking wrote).
        for (uint32_t i = 0; i < 0x64; i += 4)
            Result(4, Word<uint32_t>(kManager + 0x11C + i));
        for (uint32_t i = 0; i < 0xC0; i += 4)
            Result(5, Word<uint32_t>(kNodes + i));
    }
}

void Connection(bool original)
{
    // Every modem state (with a fail script).
    Word<uint32_t>(kManager + 0xD8) = At(kMisc + 0x500);
    Word<uint32_t>(kManager + 0x1C8) = 0x5C819700;
    for (int state = 0; state <= 10; ++state) {
        Word<int>(kNetMgr + offsetof(Net::Manager, m_modemState)) = state;
        Word<int>(kManager + 0x1A8) = 7;
        if (original)
            Original<void(__cdecl*)()>(0x00536C10)();
        else
            Manager::CancelConnectToInternet();
        Result(6, Word<uint32_t>(kNetMgr + offsetof(Net::Manager, m_modemState)));
        Result(7, Word<uint32_t>(kManager + 0x1A8));
    }
    // A server search: no client (made / not), an address or none.
    auto find = [&](uint32_t ip) {
        GameVariable<uint8_t>(0x007CCA88) = 1;
        GameVariable<uint32_t>(0x007CCA84) = 0x77777777;
        if (original)
            Orig<void>(0x00536490, ip);
        else
            Self()->FindServerAt(ip);
        Result(8, GameVariable<uint8_t>(0x007CCA88));
        Result(9, GameVariable<uint32_t>(0x007CCA84));
    };
    Word<uint32_t>(kManager + 0x2C) = 0;
    find(0x0A000001);
    find(0);
    g_spawnSetsClient = true;
    find(0x0A000002);
    find(0x0A000003);
    find(0);
    // GameSpy's think: each flag.
    for (int flags = 0; flags < 8; ++flags) {
        GameVariable<uint8_t>(0x007CCA82) = flags & 1;
        g_netGame = (flags & 2) != 0;
        GameVariable<uint8_t>(0x007CCE1C) = (flags >> 2) & 1;
        if (original)
            Original<void(__cdecl*)()>(0x00536640)();
        else
            Manager::ServiceGameSpy();
    }
    // The level: no peer, not connected, not hosting, hosting; with the reporting flags all up.
    GameVariable<uint8_t>(0x007CCA82) = 1;
    GameVariable<uint8_t>(0x007CCE1C) = 1;
    g_netGame = true;
    for (int step = 0; step < 4; ++step) {
        g_peer = step >= 1;
        g_peerConnected = step >= 2;
        g_lobbyHosting = step >= 3;
        GameVariable<uint8_t>(0x007CCE1D) = 1;
        if (original)
            Orig<void>(0x00536B90, 0x1E7E1000u + static_cast<uint32_t>(step));
        else
            Self()->SetLevel(0x1E7E1000u + static_cast<uint32_t>(step));
        Result(10, Word<uint32_t>(kManager + 0x34));
        Result(11, GameVariable<uint8_t>(0x007CCE1D));
    }
}

void GameTypeName(bool original)
{
    // mode_info with four entries (the third without a checksum: it compares the second's again); the game type each,
    // none, and in / not in a network game.
    auto* array = &Word<CArray>(kArray);
    std::memset(array, 0, sizeof(CArray));
    array->m_type = Script::ESYMBOLTYPE_STRUCTURE;
    array->m_size = 4;
    array->m_elements = &Word<uint32_t>(kArray + 0x10);
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t entry = At(kArray + 0x40 + 8 * i);
        Word<uint32_t>(kArray + 0x10 + 4 * i) = entry;
        if (i != 2)
            g_answer[{entry, 0x21902065}] = 0x70DE0000 + i;
        if (i != 1)
            g_answer[{entry, 0xA1DC81F9}] = At(kArray + 0x80 + 0x10 * i);
    }
    for (int net = 0; net < 2; ++net) {
        g_netGame = net != 0;
        for (uint32_t type : {0x70DE0000u, 0x70DE0001u, 0x70DE0003u, 0x12345678u}) {
            g_answer[{0x9834C4FA, 0x21902065}] = type;
            Result(12, reinterpret_cast<uint32_t>(original ? Orig<const char*>(0x00536A40) : Self()->GetGameTypeName()));
        }
    }
}

void TestNetLists()
{
    SaveGlobals({{0x006EFDD8, 4, {}}, {0x007CCE1C, 2, {}}, {0x007CCA82, 1, {}}, {0x007CCA84, 4, {}}, {0x007CCA88, 1, {}}});
    RunCase("GameNet list types and the input handler's destructor", Lists);
    RunCase("GameNet::Manager::RemoveAllNewPlayers / RemoveNewPlayer", NewPlayers);
    RunCase("GameNet::Manager::DestroyBody", Destructor);
    RunCase("GameNet::Manager::CancelConnectToInternet / FindServerAt / ServiceGameSpy / SetLevel", Connection);
    RunCase("GameNet::Manager::GetGameTypeName", GameTypeName);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetLists, GameNetKit::Cleanup);
