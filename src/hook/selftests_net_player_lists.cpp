// Self-tests for the network manager's player records and queued scripts (src/game/net/gamenet_player_lists.cpp) and the
// FoundServers command: the original and ours on the same stand-in arena (gamenet_testkit.h) - real lists of players,
// joining players and queued scripts - everything they call outside stood in for by recording code (the players'
// flag getters run as they are: they only read the flags); the calls, results and arena are compared, case by case.

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into the test's own arena)

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lists.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/score.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Mem::ListNode;

constexpr uint32_t kNodes = 0x14000;    // list nodes, 0x20 apart (64)
constexpr uint32_t kNewInfos = 0x14800; // joining players' records, 0x20 apart
constexpr uint32_t kSkaters = 0x14C00;  // stand-in skaters (a vtable pointer), 0x10 apart
constexpr uint32_t kScripts = 0x14D00;  // stand-in scripts (CScript, 0x100 apart)
constexpr uint32_t kScores = 0x15500;   // score components, 0x20 apart
constexpr uint32_t kObjects = 0x15700;  // objects for the scripts (CRefCounted), 0x20 apart
constexpr uint32_t kTexts = 0x15900;    // texts, 0x40 apart

void* g_skaterVtable[16];
void* g_recordVtable[2];
int g_updates;               // a script's Update calls before it finishes
uint32_t g_lookupObject;     // GetObjectByID's answer (arena offset or 0)
int g_skaterCount;

uint32_t Node(int i) { return kNodes + 0x20 * static_cast<uint32_t>(i); }
uint32_t Player(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
uint32_t NewInfo(int i) { return kNewInfos + 0x20 * static_cast<uint32_t>(i); }
uint32_t Text(int i) { return kTexts + 0x40 * static_cast<uint32_t>(i); }

// A circular list at `head` (data -1) of nodes (offset, data offset or 0).
void MakeList(uint32_t head, std::initializer_list<std::pair<uint32_t, uint32_t>> nodes, void** vtable = nullptr)
{
    auto* h = &Word<ListNode>(head);
    h->data = reinterpret_cast<void*>(-1);
    ListNode* last = h;
    for (const auto& [offset, data] : nodes) {
        auto* n = &Word<ListNode>(offset);
        n->vtable = vtable;
        n->data = data ? reinterpret_cast<void*>(At(data)) : nullptr;
        last->next = n;
        n->prev = last;
        last = n;
    }
    last->next = h;
    h->prev = last;
}
void Players(std::initializer_list<int> which)
{
    std::vector<std::pair<uint32_t, uint32_t>> nodes;
    for (int i : which)
        nodes.push_back({Node(i), Player(i)});
    auto* h = &Word<ListNode>(kManager + 0x11C);
    h->data = reinterpret_cast<void*>(-1);
    ListNode* last = h;
    for (const auto& [offset, data] : nodes) {
        auto* n = &Word<ListNode>(offset);
        n->data = reinterpret_cast<void*>(At(data));
        last->next = n;
        n->prev = last;
        last = n;
    }
    last->next = h;
    h->prev = last;
}
void Flags(int player, uint32_t flags) { Word<uint32_t>(Player(player) + 0x1D0) = flags; }

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) { // (a player's connection handle)
        Rec(0x34A0, {r.ecx});
        return (r.ecx - g_base) / 0x10;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[1]);
        uint8_t data[0x40] = {};
        if (desc->m_data)
            std::memcpy(data, desc->m_data, desc->m_length < 0x40 ? desc->m_length : 0x40);
        if (desc->m_id == 0x3F) { // the panel message: its two strings up to their ends
            g_trace.push_back(Hash(reinterpret_cast<const char*>(data + 0x0C)));
            g_trace.push_back(Hash(reinterpret_cast<const char*>(data + 0x1C)));
            std::memset(data + 0x0C, 0, 0x20);
        }
        if (desc->m_id == 0x70) // (the bit's three bytes after it)
            std::memset(data + 5, 0, 3);
        for (int i = 0; i < 0x30; i += 4)
            g_trace.push_back(Rel(*reinterpret_cast<uint32_t*>(data + i)));
        Rec(0x7EB0, {r.ecx, r.args[0], desc->m_id, desc->m_stream, desc->m_length,
                     static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                     desc->m_singular, static_cast<uint32_t>(desc->m_delay)});
        return 0u;
    });
    StubCdecl(s, {0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0x8FD0, 1,
              [](Regs& r) { return At(Text(static_cast<int>(r.args[0] & 3))); });
    s.At({0x00623B10}, 0, [](Regs& r) { // strstr (the game's C runtime; ours uses the DLL's: not recorded)
        const char* found = std::strstr(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1]));
        return reinterpret_cast<uint32_t>(found);
    });
    Stub(s, {0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, 0x65C0, [](Regs& r) {
        return r.args[0] % 3 == 1 ? 0u : At(kSkaters + 0x10 * r.args[0]); // (skater 1 and 4: none)
    });
    g_skaterVtable[11] = s.Thunk(0, [](Regs& r) { Rec(0x5C11, {r.ecx}); return 0u; });
    g_skaterVtable[12] = s.Thunk(0, [](Regs& r) { Rec(0x5C12, {r.ecx}); return 0u; });
    g_recordVtable[0] = s.Thunk(4, [](Regs& r) { Rec(0x9DE1, {r.ecx, r.args[0]}); return r.ecx; });
    Stub(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, 0xDB90,
         [](Regs& r) { return At(kScores + 0x20 * ((r.ecx - At(kSkaters)) / 0x10 % 4)); });
    Stub(s, {0x00564A10, AddressOf(&Mdl::Score::Reset)}, 0, 0x4A10);
    Stub(s, {0x00569580}, 0, 0x9580);
    Stub(s, {0x00569590}, 0, 0x9590);
    s.At({0x0047B5D0, AddressOf(&Sfx::CSfxManager::SetMainVolume)}, 4, [](Regs& r) {
        Rec(0xB5D0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, Script::CStruct*, uint32_t, Script::CStruct*, int,
                                                                  uint32_t, bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             g_updates = static_cast<int>(r.args[0] & 3);
             return At(kScripts + 0x100 * (r.args[0] & 3));
         });
    Stub(s, {0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, 0x19F0,
         [](Regs&) { return g_lookupObject ? At(g_lookupObject) : 0u; });
    Stub(s, {0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, 0x37C0);
    Stub(s, {0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, 0x37E0);
    Stub(s, {0x004750B0, AddressOf(&Script::CScript::Update)}, 0, 0x50B0,
         [](Regs&) { return g_updates-- > 0 ? 1u : 0u; });
    StubCdecl(s, {0x004756C0, AddressOf(&Script::KillSpawnedScript)}, 0x56C0, 1);
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_updates = 0;
        g_lookupObject = 0;
        for (int i = 0; i < 8; ++i)
            Word<void**>(kSkaters + 0x10 * static_cast<uint32_t>(i)) = g_skaterVtable;
        Word<uint32_t>(kManager + 4) = At(kApp); // the server
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

void Walks(bool original)
{
    // Players 0-5: 1 and 4 observers, 0 local, 3 the server's, 5 has the crown (0x80), 2 flags 0x20.
    Players({0, 1, 2, 3, 4, 5});
    const uint32_t flags[] = {0x1, 0x4, 0x20, 0x200, 0x4, 0x80};
    for (int i = 0; i < 6; ++i)
        Flags(i, flags[i]);
    for (int observers = 0; observers < 2; ++observers) {
        GameNet::Detail::PlayerSearch search;
        void* p = original ? Orig<void*>(0x005372C0, static_cast<void*>(&search), observers != 0)
                           : Self()->FirstPlayerInfo(&search, observers != 0);
        while (p) {
            Result(1, reinterpret_cast<uint32_t>(p));
            Result(2, reinterpret_cast<uint32_t>(search.m_node));
            p = original ? Orig<void*>(0x00537010, static_cast<void*>(&search), observers != 0)
                         : Self()->NextPlayerInfo(&search, observers != 0);
        }
        Result(3, reinterpret_cast<uint32_t>(search.m_node));
    }
    auto finds = [&]() {
        Result(4, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x00537320) : Self()->GetLocalPlayer()));
        Result(5, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x005373D0) : Self()->GetServerPlayer()));
        Result(6, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x00537480) : Self()->GetKingOfTheHill()));
        Result(7, original ? Orig<uint32_t>(0x00537530) : static_cast<uint32_t>(Self()->CountOtherPlayers()));
    };
    finds();
    // First an observer; then none of them; then none at all.
    Players({1, 4, 2, 0});
    finds();
    Flags(0, 0x4);
    Flags(2, 0x4);
    finds();
    Players({});
    finds();
    for (int observers = 0; observers < 2; ++observers) {
        GameNet::Detail::PlayerSearch search;
        Result(8, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x005372C0, static_cast<void*>(&search), observers != 0)
                                                      : Self()->FirstPlayerInfo(&search, observers != 0)));
    }
}

void NewPlayers(bool original)
{
    // Ids 0x10, 0x85 (negative as a byte), 0x7F.
    const uint8_t ids[] = {0x10, 0x85, 0x7F};
    for (int i = 0; i < 3; ++i)
        Word<uint8_t>(NewInfo(i) + 0x10) = ids[i];
    MakeList(kManager + 0x130, {{Node(10), NewInfo(0)}, {Node(11), NewInfo(1)}, {Node(12), NewInfo(2)}});
    GameNet::Detail::NewPlayerSearch search;
    Result(1, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x00537070, static_cast<void*>(&search))
                                                  : Self()->FirstNewPlayerInfo(&search)));
    Result(2, reinterpret_cast<uint32_t>(search.m_node));
    for (uint32_t id : {0x10u, 0x85u, 0xFF85u, 0x7Fu, 0x11u, 0u}) {
        auto id16 = static_cast<uint16_t>(id);
        Result(3, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x005370A0, id16)
                                                      : Self()->GetNewPlayerInfoByObjectId(id16)));
    }
    MakeList(kManager + 0x130, {});
    Result(4, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x00537070, static_cast<void*>(&search))
                                                  : Self()->FirstNewPlayerInfo(&search)));
    Result(5, reinterpret_cast<uint32_t>(original ? Orig<void*>(0x005370A0, static_cast<uint16_t>(0x10))
                                                  : Self()->GetNewPlayerInfoByObjectId(0x10)));
    // FoundServers: none, then three.
    for (int n : {0, 3, 1}) {
        if (n == 3)
            MakeList(kManager + 0x158, {{Node(20), 0}, {Node(21), 0}, {Node(22), 0}});
        else if (n == 1)
            MakeList(kManager + 0x158, {{Node(23), 0}});
        else
            MakeList(kManager + 0x158, {});
        bool found = original ? Original<bool(__cdecl*)(void*, void*)>(0x00537100)(nullptr, nullptr)
                              : GameNet::CFunc_FoundServers(nullptr, nullptr);
        Result(6, found ? 1u : 0u);
    }
}

void Ready(bool original)
{
    // The skate module's skater list: 5 nodes (skaters 1 and 4 missing).
    MakeList(kMisc + 0x28, {{Node(30), 0}, {Node(31), 0}, {Node(32), 0}, {Node(33), 0}, {Node(34), 0}});
    for (int ready : {1, 1, 0, 0, 1, 2}) {
        if (original)
            Orig<void>(0x00537140, ready != 0);
        else
            Self()->SetReadyToPlay(ready != 0);
        Result(1, Word<uint8_t>(kManager + 0x18C));
    }
    // Players reset: with skaters (scores with / without +0x18) and without.
    Players({0, 1, 2, 3});
    for (int i = 0; i < 4; ++i) {
        Flags(i, 0xFFFFFFFF);
        Word<uint32_t>(Player(i) + 0x1C) = 0x11;
        Word<uint32_t>(Player(i) + 0x20) = 0x22;
        Word<uint32_t>(Player(i) + 0x34) = 0x33;
        Word<uint32_t>(Player(i) + 0x14) = i == 2 ? 0 : At(kSkaters + 0x10 * static_cast<uint32_t>(i));
        Word<uint32_t>(kScores + 0x20 * static_cast<uint32_t>(i) + 0x18) = i == 1 ? 0 : At(kMisc + 0x700 + 4 * i);
    }
    if (original)
        Orig<void>(0x00537620);
    else
        Self()->ResetPlayers();
}

void Messages(bool original)
{
    Players({0, 1, 2});
    Flags(0, 1);
    Flags(1, 4);
    Flags(2, 0);
    std::strcpy(&Word<char>(Text(0)), "Plain text");
    std::strcpy(&Word<char>(Text(1)), "Player %s0 joined, %s0 twice");
    std::strcpy(&Word<char>(Text(2)), "%s0");
    std::strcpy(&Word<char>(Text(3)), "Ends with %s");
    static const char kName[] = "Skater";
    static const char kOther[] = "Other";
    static const char kProps[] = "netstatusprops";
    // Broadcast: strings or none, important or not; locally: each text.
    for (int important = 0; important < 2; ++important) {
        auto send = [&](uint32_t text, const char* a, const char* b) {
            if (original)
                Orig<void>(0x005376F0, true, text, a, b, static_cast<void*>(nullptr), kProps, important != 0, 3000);
            else
                Self()->CreateNetPanelMessage(true, text, a, b, nullptr, kProps, important != 0, 3000);
        };
        send(0x12345678, kName, kOther);
        send(0x12345679, nullptr, nullptr);
    }
    for (uint32_t text = 0; text < 4; ++text) {
        if (original)
            Orig<void>(0x005376F0, false, text, kName, kOther, static_cast<void*>(nullptr), kProps, false, 2000 + static_cast<int>(text));
        else
            Self()->CreateNetPanelMessage(false, text, kName, kOther, nullptr, kProps, false, 2000 + static_cast<int>(text));
    }
    // Pro set toggles: bits 0, 6, 33 (wraps to 1), to the remote players.
    for (uint32_t bit : {0u, 6u, 33u}) {
        Word<uint32_t>(kManager + 0x194) = 0x55;
        if (original)
            Orig<void>(0x00537F40, static_cast<uint8_t>(bit), 0xC0DE0000u + bit);
        else
            Self()->ToggleProSet(static_cast<uint8_t>(bit), 0xC0DE0000u + bit);
        Result(1, Word<uint32_t>(kManager + 0x194));
    }
}

void Queued(bool original)
{
    // Queue three (one twice), then launch them (an object for one); queue two more and clear them.
    Word<uint32_t>(kMisc + 0x800 + 0x12C) = At(kMisc + 0xC00); // the network heap
    Word<uint32_t>(kMisc + 0x800 + 0x114) = At(kMisc + 0xC40); // the bottom-up heap
    MakeList(kManager + 0x144, {});
    Word<void**>(kManager + 0x144) = reinterpret_cast<void**>(0x0064C9F8);
    auto queue = [&](int node, uint32_t object, uint32_t script) {
        if (original)
            Orig<void>(0x00537A10, node, object, script);
        else
            Self()->QueueScript(node, object, script);
    };
    queue(5, 0x100, 0x5C000001);
    queue(6, 0x101, 0x5C000002);
    queue(7, 0x102, 0x5C000001); // (already there)
    queue(8, 0x103, 0x5C000003);
    GameVariable<float>(0x0067DA38) = 0.75f;
    for (int i = 0; i < 4; ++i)
        Word<uint32_t>(kScripts + 0x100 * static_cast<uint32_t>(i) + 0xBC) = i == 2 ? At(kObjects + 0x20) : 0u;
    g_lookupObject = kObjects;
    if (original)
        Orig<void>(0x00537D90);
    else
        Self()->LaunchQueuedScripts();
    // (the records were freed into the stand-in heap: their memory isn't the list's any more)
    MakeList(kManager + 0x144, {});
    queue(1, 0x200, 0x5C000004);
    queue(2, 0x201, 0x5C000005);
    // Their vtables made recording ones, then cleared.
    for (ListNode* n = Word<ListNode>(kManager + 0x144).next; n != &Word<ListNode>(kManager + 0x144); n = n->next)
        n->vtable = g_recordVtable;
    if (original)
        Orig<void>(0x00537B30);
    else
        Self()->ClearQueuedScripts();
    MakeList(kManager + 0x144, {});
    if (original)
        Orig<void>(0x00537B30);
    else
        Self()->ClearQueuedScripts();
}

void TestNetPlayerLists()
{
    SaveGlobals({{0x0067DA38, 4, {}}});
    RunCase("GameNet::Manager player walks and look-ups", Walks);
    RunCase("GameNet::Manager joining players, FoundServers", NewPlayers);
    RunCase("GameNet::Manager::SetReadyToPlay / ResetPlayers", Ready);
    RunCase("GameNet::Manager::CreateNetPanelMessage / ToggleProSet", Messages);
    RunCase("GameNet::Manager queued scripts", Queued);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetPlayerLists, GameNetKit::Cleanup);
