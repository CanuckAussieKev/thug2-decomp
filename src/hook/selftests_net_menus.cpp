// Self-tests for the GameSpy lobby's menus, chat and destructors (src/game/net/gamenet_lobby_menus.cpp): the original
// and ours on the same stand-in arena (net2_testkit.h); the script calls, the SDK and the presence stood in for by
// recording code (script arrays and the room list built for real); the calls and the arenas compared.

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/task.h"
#include "game/lst/head.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/stats_man.h"
#include "hook/log.h"
#include "hook/net2_testkit.h"

#pragma warning(disable : 4996) // (strcpy into the stand-in records)

namespace {

using namespace Net2Kit;
using GameNet::LobbyMan;
using GameNet::LobbyPlayer;
using GameNet::LobbyRoom;
using GameNet::Manager;

constexpr uint32_t kLobby = kFree + 0x1000;
constexpr uint32_t kBuddy = kFree + 0x1200;
constexpr uint32_t kRecords = kFree + 0x400; // players / rooms, 0x80 apart
constexpr uint32_t kRoomList = kFree + 0xC00; // the room list's head
constexpr uint32_t kServer = kFree + 0xC40;

bool g_isBuddy;
int g_buddies;
int g_rating;
int g_lobbyNum;
int g_neversoft;

void Install(StandIns& s)
{
    s.At({0x00478670, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::CStruct*)>(&Script::CStruct::AddStructure))},
         8, [](Regs& r) {
             Rec(0x8670, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478B00, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::CArray*)>(&Script::CStruct::AddArray))},
         8, [](Regs& r) {
             const auto* array = reinterpret_cast<const Script::CArray*>(r.args[1]);
             Rec(0x8B00, {r.ecx, r.args[0], r.args[1], array->m_size, array->m_type});
             for (uint32_t i = 0; i < array->m_size; ++i)
                 g_trace.push_back(array->m_elements[i]);
             return 0u;
         });
    s.At({0x00477C60, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))},
         8, [](Regs& r) {
             Rec(0x7C60, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        char* text = &Word<char>(kFree + 0x1800);
        std::snprintf(text, 32, "<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    Stub(s, {0x00549CB0, AddressOf(&GameNet::BuddyMan::IsBuddy)}, 4, 0x9CB0, [](Regs&) { return g_isBuddy ? 1u : 0u; });
    Stub(s, {0x00548E10, AddressOf(&GameNet::BuddyMan::CountBuddies)}, 0, 0x8E10, [](Regs&) { return static_cast<uint32_t>(g_buddies); });
    StubCdecl(s, {0x005FCA80}, 0xCA80, 2, [](Regs& r) {
        Rec(0xCA81, {r.args[2] ? Hash(reinterpret_cast<const char*>(r.args[2])) : 0u, r.args[3]});
        return 0u;
    });
    Stub(s, {0x005453E0, AddressOf(&GameNet::Manager::SetServiceState)}, 4, 0x53E0);
    Stub(s, {0x00545860, AddressOf(&GameNet::LobbyRoom::Construct)}, 0, 0x5860, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0xCC, sizeof(LobbyRoom));
        std::memset(reinterpret_cast<uint8_t*>(r.ecx) + 0x68, 0, 4); // (the flags clear)
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0x0064CBE0;
        return r.ecx;
    });
    StubCdecl(s, {0x005FF560}, 0xF560, 1, [](Regs& r) {
        const char* key = reinterpret_cast<const char*>(r.args[1]);
        Rec(0xF561, {Hash(key), r.args[2]});
        if (std::strcmp(key, "lobby_num") == 0)
            return static_cast<uint32_t>(g_lobbyNum);
        if (std::strcmp(key, "maxrating") == 0)
            return 2000u;
        if (std::strcmp(key, "minrating") == 0)
            return 1000u;
        return static_cast<uint32_t>(g_neversoft);
    });
    // (the stats' rating: the original reads it through the getter folded at 0x00469490, ours as the field - the rating
    // is put there, not answered by a stand-in)
    Stub(s, {0x0054CA80, AddressOf(&GameNet::StatsMan::GetPersonalStats)}, 0, 0xCA80, [](Regs&) {
        Word<int>(kFree + 0x1900) = g_rating;
        return At(kFree + 0x1900);
    });
    Stub(s, {0x00577290, AddressOf(&Tsk::BaseTask::Destruct)}, 0, 0x7290);
    s.At({0x006239E0}, 0, [](Regs& r) { // (the game's C runtime strncpy, not started offline)
        std::strncpy(reinterpret_cast<char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1]), r.args[2]);
        return r.args[0];
    });
    RecordingObject(s, 10, {{2, 4}});
    RecordingObject(s, 11, {{2, 4}});
}

void Prepare()
{
    g_isBuddy = false;
    g_buddies = 3;
    g_rating = 1500;
    g_lobbyNum = 0;
    g_neversoft = 0;
    Word<uint32_t>(kManager + 0x10) = At(kLobby);
    Word<uint32_t>(kManager + 0x18) = At(kBuddy);
    Word<uint32_t>(kManager + 0x1C) = At(kFree + 0x1300);
    Word<uint8_t>(kBuddy + 0x10) = 1;
    Word<uint32_t>(kBuddy + 4) = 0x1000;
    Word<uint32_t>(kLobby + 0x140) = At(kLobby + 0x180);
    Word<uint32_t>(kMisc + 0x800 + 0x13C) = 0xBEE0013Cu;
    Word<uint32_t>(kMisc + 0x800 + 0x140) = 0xBEE00140u;
    BuildList(kRoomList, {});
    GameVariable<uint8_t>(0x007CCE38) = 0;
    GameVariable<uint8_t>(0x007CCE39) = 0;
}

LobbyMan* TheLobby() { return &Word<LobbyMan>(kLobby); }
LobbyPlayer* Player(int i, const char* nick, int profile, int rating)
{
    auto* player = &Word<LobbyPlayer>(kRecords + 0x80 * static_cast<uint32_t>(i));
    std::strcpy(player->m_nick, nick);
    player->m_profile = profile;
    player->m_rating = rating;
    return player;
}

void Items(bool original)
{
    Prepare();
    using PlayerFn = void(__thiscall*)(LobbyMan*, LobbyPlayer*);
    LobbyPlayer* buddy = Player(0, "Buddy", 0x2000, 3600);
    original ? Original<PlayerFn>(0x00540900)(TheLobby(), buddy) : TheLobby()->AddBuddyMenuItem(buddy);
    struct Case {
        int profile;
        bool loggedIn, isBuddy;
        int buddies, rating;
    };
    const Case cases[] = {
        {0x2000, true, false, 3, 1500},  // may be added
        {0x2000, true, false, 30, 3510}, // too many buddies (rank 10)
        {0x1000, true, false, 3, 350},   // itself (rank 0: 1 if computed in floats)
        {0x2000, true, true, 3, 35100},  // a buddy already (rank at most 10)
        {0, true, false, 3, -500},       // no profile
        {0x2000, false, false, 3, 0},    // logged out
    };
    int i = 1;
    for (const Case& c : cases) {
        Word<uint8_t>(kBuddy + 0x10) = c.loggedIn;
        g_isBuddy = c.isBuddy;
        g_buddies = c.buddies;
        LobbyPlayer* player = Player(i++, "SomeOne", c.profile, c.rating);
        original ? Original<PlayerFn>(0x005411E0)(TheLobby(), player) : TheLobby()->AddPlayerListItem(player);
    }
    // The regions menu.
    using RoomFn = void(__thiscall*)(LobbyMan*, LobbyRoom*, int);
    for (int variant = 0; variant < 3; ++variant) {
        auto* room = &Word<LobbyRoom>(kRecords + 0x400);
        std::strcpy(room->m_name, "Beenox");
        room->m_numPlayers = 7 + variant;
        room->m_maxPlayers = 20;
        room->m_full = variant == 1;
        room->m_outOfRange = variant == 2;
        original ? Original<RoomFn>(0x00540FB0)(TheLobby(), room, 4 + variant) : TheLobby()->AddRoomMenuItem(room, 4 + variant);
    }
}

void Chat(bool original)
{
    Prepare();
    using MsgFn = void(__cdecl*)(void*, int, const char*, const char*, int, void*);
    original ? Original<MsgFn>(0x00540B60)(nullptr, 1, "Nick", "hello there", 0, TheLobby())
             : LobbyMan::RoomMessageCallback(nullptr, 1, "Nick", "hello there", 0, TheLobby());
    using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
    ArenaStruct none(kStructs);
    ArenaStruct plain(kStructs + 0x80);
    plain.String(0xC4745838, "a message");
    ArenaStruct system(kStructs + 0x100);
    system.String(0xC4745838, "restart").Add(Script::ESYMBOLTYPE_NAME, 0, 0xA531B313);
    for (ArenaStruct* params : {&none, &plain, &system}) {
        bool result = original ? Original<CFunc>(0x00540E00)(params->Struct(), TestScript())
                               : GameNet::CFunc_SendMessage(params->Struct(), TestScript());
        Rec(0x0E00, {result ? 1u : 0u});
    }
}

void Rooms(bool original)
{
    Prepare();
    using Fn = void(__cdecl*)(void*, int, int, void*, const char*, int, int, int, int, void*);
    void* list = &Word<uint8_t>(kRoomList);
    void* server = &Word<uint8_t>(kServer);
    auto room = [&](int success, int group, const char* name, int waiting, int most) {
        int players = waiting * 2 - 1;
        original ? Original<Fn>(0x00541690)(nullptr, success, group, server, name, waiting, most, players, 3, list)
                 : LobbyMan::GroupRoomCallback(nullptr, success, group, server, name, waiting, most, players, 3, list);
        Rec(0x1690, {GameVariable<uint8_t>(0x007CCE38), GameVariable<uint8_t>(0x007CCE39)});
    };
    room(0, 5, "failed", 0, 0);
    room(1, 0, nullptr, 0, 0); // the end, nothing asked for
    room(1, 3, "A room with a rather long name that goes past the sixty-three characters a room has", 4, 8);
    g_rating = 2500; // above its range
    room(1, 7, "Full", 8, 8);
    g_rating = 900;  // below
    g_neversoft = 1;
    room(1, 5, "Overfull", 9, 8);
    g_rating = 1500;
    g_neversoft = 0;
    std::memcpy(&Word<char>(kManager + 0xFDF8), "\0", 1); // (no pending data: its size 0)
    Word<uint32_t>(kManager + 0xFDFC) = 0;
    g_lobbyNum = 77; // asked for, not this one
    room(1, 2, "Asked", 1, 8);
    room(1, 0, nullptr, 0, 0); // the end: the asked one wasn't found
    g_lobbyNum = static_cast<int>(Crc::GenerateCRCCaseless("", 0));
    room(1, 9, "Found", 1, 8);
    room(1, 0, nullptr, 0, 0);
}

void Destructors(bool original)
{
    Prepare();
    // The lobby: its tasks, its node at +0x14 and its own node in rings of three.
    Word<uint32_t>(kLobby + 0x148) = At(kObjects + 0x40 * 10);
    Word<uint32_t>(kLobby + 0x14C) = 0;
    Word<uint32_t>(kLobby + 0x150) = At(kObjects + 0x40 * 11);
    auto ring = [](uint32_t a, uint32_t b, uint32_t c) {
        uint32_t nodes[3] = {a, b, c};
        for (int i = 0; i < 3; ++i) {
            Word<uint32_t>(nodes[i] + 0xC) = At(nodes[(i + 1) % 3]);
            Word<uint32_t>(nodes[i] + 0x10) = At(nodes[(i + 2) % 3]);
        }
    };
    ring(kLobby + 0x14, kRecords, kRecords + 0x20);
    ring(kLobby, kRecords + 0x40, kRecords + 0x60);
    original ? Original<void(__thiscall*)(LobbyMan*)>(0x00540D50)(TheLobby()) : TheLobby()->DestroyBody();
    // The two node types, their deleting destructors with and without freeing.
    uint32_t n = kRecords + 0x100;
    for (int k = 0; k < 7; ++k) {
        uint32_t node = n + 0x40 * static_cast<uint32_t>(k);
        ring(node, node + 0x18, node + 0x2C - 0x4);
    }
    using Body = void(__thiscall*)(void*);
    using Deleter = void*(__thiscall*)(void*, unsigned int);
    auto at = [n](int k) { return &Word<uint8_t>(n + 0x40 * static_cast<uint32_t>(k)); };
    original ? Original<Body>(0x00540840)(at(0)) : reinterpret_cast<GameNet::LobbyNode*>(at(0))->DestroyBody();
    original ? Original<Body>(0x00540860)(at(1)) : reinterpret_cast<GameNet::LobbyListNode*>(at(1))->DestroyBody();
    for (unsigned int flags : {0u, 1u}) {
        void* r1 = original ? Original<Deleter>(0x00540880)(at(2 + flags * 3), flags)
                            : reinterpret_cast<GameNet::LobbyNode*>(at(2 + flags * 3))->Destroy(flags);
        void* r2 = original ? Original<Deleter>(0x005408C0)(at(3 + flags * 3), flags)
                            : reinterpret_cast<GameNet::LobbyListNode*>(at(3 + flags * 3))->Destroy(flags);
        void* r3 = original ? Original<Deleter>(0x00540F80)(at(4), flags)
                            : reinterpret_cast<GameNet::LobbyNode*>(at(4))->DestroyOther(flags);
        Rec(0x0880, {reinterpret_cast<uint32_t>(r1), reinterpret_cast<uint32_t>(r2), reinterpret_cast<uint32_t>(r3)});
    }
    // The searches' and Task<T>'s deleting destructors.
    for (unsigned int flags : {0u, 1u, 2u, 3u}) {
        uint32_t search = kRecords + 0x300 + 0x10 * flags;
        Word<uint32_t>(search) = 0x55555555;
        Word<uint32_t>(search + 8) = 0x66666666;
        void* a = original ? Original<Deleter>(0x005407E0)(&Word<uint8_t>(search), flags)
                           : reinterpret_cast<GameNet::LobbySearchA*>(&Word<uint8_t>(search))->Destroy(flags);
        void* b = original ? Original<Deleter>(0x00540810)(&Word<uint8_t>(search + 8), flags)
                           : reinterpret_cast<GameNet::LobbySearchB*>(&Word<uint8_t>(search + 8))->Destroy(flags);
        void* t = original ? Original<Deleter>(0x005407B0)(&Word<uint8_t>(search + 4), flags)
                           : reinterpret_cast<GameNet::TaskDeleter*>(&Word<uint8_t>(search + 4))->Destroy(flags);
        Rec(0x07E0, {reinterpret_cast<uint32_t>(a), reinterpret_cast<uint32_t>(b), reinterpret_cast<uint32_t>(t)});
    }
}

void RestoreMenus() { Cleanup(); }

void TestNetMenus()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}, {0x007CCE38, 1, {}}, {0x007CCE39, 1, {}},
                 {0x006F2338, 16, {}}});
    std::memset(reinterpret_cast<void*>(0x006F2338), 0, 16); // (no array pools either)
    RunNet2Case("GameNet lobby menus: buddy, player list and regions items", Items, Install);
    RunNet2Case("GameNet lobby menus: room messages and SendMessage", Chat, Install);
    RunNet2Case("GameNet lobby menus: the group room list", Rooms, Install);
    RunNet2Case("GameNet lobby: its destructors, nodes, searches and Task<T>'s", Destructors, Install);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetMenus, RestoreMenus);
