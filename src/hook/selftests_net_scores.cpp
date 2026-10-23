// Self-tests for the network game's player announcements and score list (src/game/net/player_flags.cpp,
// score_list.cpp): the original and ours run on the same stand-in arena (gamenet_testkit.h), everything they call
// outside stood in for by recording code; the calls, the results and the arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/net/score_list.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using GameNet::PlayerInfo;

constexpr uint32_t kComponents = 0x13600; // SkaterScore components, 0x20 apart (8): +0x18 the score object
constexpr uint32_t kScores = 0x13500;     // score objects, 0x10 apart (8): +0 the points
constexpr uint32_t kTask = 0x13480;       // the score task: +0x1C the manager
constexpr uint32_t kGoal = 0x13440;       // the scavenger goal (its vtable at kGoal + 0x10)
constexpr uint32_t kElements = 0x12400;   // score line elements, 0x40 apart (8; their vtable at kElements + 0x200)
constexpr uint32_t kCrownObject = 0x12700;

uint32_t g_now;
std::vector<int> g_players; // the manager's players, in order (indexes)
int g_local;                // LocalPlayer's (-1: none)
int g_king;                 // KingOfTheHill's (-1: none)
uint32_t g_gameType;
bool g_teamGame;            // the game mode's IsTeamGame
bool g_teamMode;            // the manager's 0x005343E0
int g_numTeams;
int g_teamPlayers[4];
uint32_t g_noScore;         // skaters (bits) without a SkaterScore
bool g_inNetGame;
uint32_t g_goalActive;      // 0: no goal, 1: inactive, 2: active
uint32_t g_elementMask;     // score lines with an element (bits)
uint32_t g_startHeld;       // 0x005373D0's answer

uint32_t PlayerAt(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
PlayerInfo* Player(int i) { return &Word<PlayerInfo>(PlayerAt(i)); }
int SkaterIndex(uint32_t skater) { return static_cast<int>((skater - At(kSkaters)) / 0x100); }

void Install(StandIns& s)
{
    InstallCommon(s);
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_now; });
    s.At({0x005372C0}, 8, [](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1] & 0xFF});
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return g_players.empty() ? 0u : At(PlayerAt(g_players[0]));
    });
    s.At({0x00537010}, 8, [](Regs& r) {
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        Rec(0x7010, {r.args[1] & 0xFF, index});
        ++index;
        return index < g_players.size() ? At(PlayerAt(g_players[index])) : 0u;
    });
    Stub(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return g_local < 0 ? 0u : At(PlayerAt(g_local)); });
    Stub(s, {0x00537480}, 0, 0x7480, [](Regs&) { return g_king < 0 ? 0u : At(PlayerAt(g_king)); });
    Stub(s, {0x0053AEA0}, 4, 0xAEA0, [](Regs& r) {
        uint32_t id = r.args[0] & 0xFFFF;
        return id < 8 && id != 7 ? At(PlayerAt(static_cast<int>(id))) : 0u; // (id 7: no such player)
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        int i = SkaterIndex(r.ecx);
        if (r.args[0] != 0xD262802F || (g_noScore >> i) & 1)
            return 0u;
        return At(kComponents + 0x20 * static_cast<uint32_t>(i));
    });
    Stub(s, {0x00561B40}, 0, 0x1B40, [](Regs& r) { return *reinterpret_cast<uint32_t*>(r.ecx); });
    StubCdecl(s, {0x005591B0}, 0x91B0, 0, [](Regs&) { return At(kMisc + 0x500); });
    Stub(s, {0x00558B10}, 4, 0x8B10, [](Regs& r) { return 900 + r.args[0]; });
    Stub(s, {0x00558AB0}, 4, 0x8AB0, [](Regs& r) { return 700 + r.args[0]; });
    Stub(s, {0x00557F90}, 8, 0x7F90, [](Regs&) { return g_goalActive ? At(kGoal) : 0u; });
    Stub(s, {0x005532A0}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    Stub(s, {0x005343E0}, 0, 0x43E0, [](Regs&) { return g_teamMode ? 1u : 0u; });
    Stub(s, {0x00553130}, 0, 0x3130, [](Regs&) { return static_cast<uint32_t>(g_numTeams); });
    Stub(s, {0x00539880}, 4, 0x9880, [](Regs& r) { return r.args[0] < 4 ? static_cast<uint32_t>(g_teamPlayers[r.args[0]]) : 0u; });
    Stub(s, {0x00580050}, 0, 0x0050, [](Regs& r) { return Word<uint8_t>(r.ecx - g_base + 0x2C4); });
    Stub(s, {0x0057FEC0}, 0, 0xFEC0, [](Regs& r) { return static_cast<uint32_t>(SkaterIndex(r.ecx) & 1); });
    Stub(s, {0x00581580}, 0, 0x1580, [](Regs& r) { return At(kMisc + 0xF00 + 0x10 * static_cast<uint32_t>(SkaterIndex(r.ecx) & 7)); });
    Stub(s, {0x005782B0}, 4, 0x82B0);
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_inNetGame ? 1u : 0u; });
    Stub(s, {0x005373D0}, 0, 0x73D0, [](Regs&) { return g_startHeld; });
    Stub(s, {0x0053AAC0}, 0, 0xAAC0);
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        RecordDesc(0x7EB0, r.ecx, reinterpret_cast<const Net::MsgDesc*>(r.args[1]));
        Rec(0x7EB1, {r.args[0]});
        return 0u;
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        char* text = &Word<char>(kMisc + 0xE40);
        if (r.args[0] == 0xA1DD0949)
            std::memcpy(text, "[K%%]", 6); // (a format: the original's sprintf is given it as one)
        else
            std::snprintf(text, 32, "<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    // The score lines' elements.
    s.At({0x004AAE20}, 12, [](Regs& r) {
        Rec(0xAE20, {r.ecx, r.args[1], r.args[2]});
        uint32_t element = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            static const uint32_t lines[8] = {0x2B083809, 0xB20169B3, 0xC5065925, 0x5B62CC86,
                                              0x2C65FC10, 0xB56CADAA, 0xC26B9D3C, 0x52D480AD};
            if (r.args[1] == lines[i] && ((g_elementMask >> i) & 1))
                element = At(kElements + 0x40 * i);
        }
        reinterpret_cast<uint32_t*>(r.args[0])[0] = element;
        return r.args[0];
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x37E0, {r.ecx});
        reinterpret_cast<uint32_t*>(r.args[0])[0] = 0;
        return 0u;
    });
    void* setProperties = s.Thunk(4, [](Regs& r) {
        Rec(0x96B0, {r.ecx, r.args[0]});
        return 0u;
    });
    void* isActive = s.Thunk(0, [](Regs& r) {
        Rec(0x6AC7, {r.ecx});
        return g_goalActive == 2 ? 1u : 0u;
    });
    Word<uint32_t>(kElements + 0x200 + 4) = reinterpret_cast<uint32_t>(setProperties);
    Word<uint32_t>(kGoal + 0x10 + 0xC) = reinterpret_cast<uint32_t>(isActive);
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_now = 50000;
        g_players = {0, 1, 2, 3};
        g_local = 1;
        g_king = -1;
        g_gameType = 0x12345678;
        g_teamGame = false;
        g_teamMode = false;
        g_numTeams = 2;
        g_teamPlayers[0] = 2;
        g_teamPlayers[1] = 2;
        g_teamPlayers[2] = 0;
        g_teamPlayers[3] = 1;
        g_noScore = 0;
        g_inNetGame = true;
        g_goalActive = 0;
        g_elementMask = 0xFF;
        g_startHeld = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            Word<uint32_t>(kComponents + 0x20 * i + 0x18) = At(kScores + 0x10 * i);
            Word<int>(kScores + 0x10 * i) = static_cast<int>(100 * i + 37 * (i & 1));
            Word<uint32_t>(kElements + 0x40 * i) = At(kElements + 0x200);
            std::snprintf(&Word<char>(kMisc + 0xF00 + 0x10 * i), 16, "Display%u", i);
            PlayerInfo* p = Player(static_cast<int>(i));
            p->m_conn = &Word<Net::Conn>(kConn);
            p->m_team = static_cast<int>(i & 1);
            p->m_flags = 0x20;
            p->m_unknown34 = static_cast<int>(i);
            Word<uint8_t>(kSkaters + 0x100 * i + 0x2C4) = 1;
            Word<uint32_t>(kSkaters + 0x100 * i + 0x14) = i; // the skaters' ids
        }
        Player(1)->m_flags = 0x21;
        Word<uint32_t>(kGoal) = At(kGoal + 0x10);
        Word<uint32_t>(kTask + 0x1C) = At(kManager);
        Word<uint32_t>(kManager + 0x6C) = 0; // no crown
        GameVariable<uint32_t>(0x00701440) = At(kMisc + 0x540);
        GameVariable<uint32_t>(0x007CE43C) = 0;
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        Rec(0xC10B, {GameVariable<uint32_t>(0x007CE43C)});
        delete s;
        Word<uint32_t>(kElements + 0x200 + 4) = 0; // (the thunks' addresses differ between the runs)
        Word<uint32_t>(kGoal + 0x10 + 0xC) = 0;
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
void* TheManager() { return &Word<uint8_t>(kManager); }

void Scores(bool original)
{
    for (int team : {0, 1, 2, -1})
        CallRec(original, 0x005435F0, &Manager::GetTeamScore, TheManager(), team);
    Player(2)->m_flags = 0; // not fully in
    CallRec(original, 0x005435F0, &Manager::GetTeamScore, TheManager(), 0);
    Player(1)->m_flags = 1; // local, not marked fully in
    CallRec(original, 0x005435F0, &Manager::GetTeamScore, TheManager(), 1);
    for (uint32_t id : {0u, 1u, 3u, 0x10003u})
        CallRec(original, 0x005436E0, &Manager::GetPlayerScore, TheManager(), id);
    g_gameType = 0xEC200EAA; // net goal attack
    CallRec(original, 0x005435F0, &Manager::GetTeamScore, TheManager(), 1);
    CallRec(original, 0x005435F0, &Manager::GetTeamScore, TheManager(), 3); // no player of the team
    CallRec(original, 0x005436E0, &Manager::GetPlayerScore, TheManager(), 2u);
    Player(1)->m_flags |= 4; // the local player observes
    CallRec(original, 0x005435F0, &Manager::GetTeamScore, TheManager(), 1);
    CallRec(original, 0x005436E0, &Manager::GetPlayerScore, TheManager(), 2u);
}

void Invulnerable(bool original)
{
    PlayerInfo* p = Player(2);
    p->m_skater = &Word<Obj::CSkater>(kSkaters + 0x200);
    Word<uint32_t>(kSkaters + 0x200 + 0x27C) = At(kMisc + 0x5C0);
    auto check = [&]() { CallRec(original, 0x00543910, &PlayerInfo::IsInvulnerable, p); Rec(0x1D0, {p->m_flags}); };
    Word<uint32_t>(kConn + 0x60) = 0; // not ready
    check();
    Word<uint32_t>(kConn + 0x60) = 1;
    Word<uint8_t>(kSkaters + 0x200 + 0x2C4) = 0; // not in play
    check();
    Word<uint8_t>(kSkaters + 0x200 + 0x2C4) = 1;
    Word<int>(kMisc + 0x5C0 + 0x18) = 9; // too few frames
    check();
    Word<int>(kMisc + 0x5C0 + 0x18) = 10;
    p->m_joinTime = g_now - 999;
    check();
    p->m_joinTime = g_now - 1000;
    p->m_flags = 0x22;
    check(); // just joined: the flag
    p->m_flags = 0x20;
    check();
    p->m_joinTime = g_now - 5000;
    p->m_flags = 0x22;
    check();
    p->m_joinTime = g_now - 5001;
    check(); // the flag cleared
    g_gameType = 0xBFF33600; // netfirefight
    Word<int>(kScores + 0x20) = 0;
    check();
    Word<int>(kScores + 0x20) = -2;
    check();
    Word<int>(kScores + 0x20) = 3;
    p->m_flags = 0x20022;
    check(); // eliminated
    p->m_flags = 0x22;
    Word<uint32_t>(kSkaters + 0x200 + 0x104) = 4;
    check();
    Word<uint32_t>(kSkaters + 0x200 + 0x104) = 0xFFFFFFFB;
    check();
}

void FullyIn(bool original)
{
    PlayerInfo* p = Player(3);
    auto mark = [&](uint32_t flags) {
        p->m_flags = flags;
        CallVoid(original, 0x005439D0, &PlayerInfo::MarkAsFullyIn, p);
        Rec(0x1D0, {p->m_flags});
    };
    Word<uint32_t>(kManager + 0x04) = At(kApp); // the server app
    Word<int>(kConn + 0x3C) = 9;
    g_inNetGame = false;
    mark(0);
    g_inNetGame = true;
    Word<uint8_t>(kManager) = 0; // a client
    mark(0);
    Word<uint8_t>(kManager) = 1; // the server
    Word<int>(kManager + 0x40) = 1;
    mark(1); // its own player
    Word<int>(kManager + 0x40) = 1;
    mark(0x100);
    p->m_conn = nullptr;
    mark(0);
    Word<int>(kManager + 0x40) = 2;
    mark(0);
    g_startHeld = 1;
    mark(0);
    Word<int>(kManager + 0x40) = 3;
    mark(0);
    Word<int>(kManager + 0x40) = 0;
    mark(0);
}

void FlagsOf() { Rec(0x1D2, {Player(0)->m_flags, Player(1)->m_flags, Player(2)->m_flags, Player(3)->m_flags}); }
template <typename... A>
void Announce(bool original, uint32_t address, void (PlayerInfo::*ours)(A...), int player, A... args)
{
    CallVoid(original, address, ours, Player(player), args...);
    FlagsOf();
}

void Flags(bool original)
{
    // Captured: by the local player, against the local player's team, between others.
    Announce(original, 0x00543A90, &PlayerInfo::CapturedFlag, 1, 0);
    Player(2)->m_flags = 0x1820;
    Announce(original, 0x00543A90, &PlayerInfo::CapturedFlag, 2, 1);
    Player(2)->m_flags = 0x4020;
    Announce(original, 0x00543A90, &PlayerInfo::CapturedFlag, 2, 0);
    Player(1)->m_flags = 0x25; // the local player observes
    Announce(original, 0x00543A90, &PlayerInfo::CapturedFlag, 3, 1);
    Player(1)->m_flags = 0x21;
    // Stolen: carriers of the team's flag lose it.
    Player(0)->m_flags = 0x1020;
    Player(3)->m_flags = 0x3020;
    Announce(original, 0x00543C70, &PlayerInfo::StoleFlag, 1, 1);
    Player(1)->m_flags = 0x0821; // the local player carries team 0's
    Announce(original, 0x00543C70, &PlayerInfo::StoleFlag, 2, 0);
    Announce(original, 0x00543C70, &PlayerInfo::StoleFlag, 2, 3);
    Player(1)->m_flags = 0x0825;
    Announce(original, 0x00543C70, &PlayerInfo::StoleFlag, 0, 0);
    Announce(original, 0x00543C70, &PlayerInfo::StoleFlag, 0, -1);
    Player(1)->m_flags = 0x21;
    // Taken.
    Announce(original, 0x00543F00, &PlayerInfo::TookFlag, 1, 2);
    Announce(original, 0x00543F00, &PlayerInfo::TookFlag, 2, 1); // the local player's team (1)
    Announce(original, 0x00543F00, &PlayerInfo::TookFlag, 2, 0);
    Player(1)->m_flags = 0x25;
    Announce(original, 0x00543F00, &PlayerInfo::TookFlag, 3, 1);
    Player(1)->m_flags = 0x21;
    // Retrieved: the first carrier of the player's team's flag loses its flags.
    Player(0)->m_flags = 0x3020;
    Player(2)->m_flags = 0x1020;
    Player(3)->m_flags = 0x1820;
    Announce(original, 0x00544110, &PlayerInfo::RetrievedFlag, 1); // local, team 1
    Announce(original, 0x00544110, &PlayerInfo::RetrievedFlag, 3); // team 1: the local player's
    Announce(original, 0x00544110, &PlayerInfo::RetrievedFlag, 2); // team 0
    Player(1)->m_flags = 0x25; // the local player observes
    Announce(original, 0x00544110, &PlayerInfo::RetrievedFlag, 3);
    g_local = -1;
    Announce(original, 0x00544110, &PlayerInfo::RetrievedFlag, 0);
}

void King(bool original)
{
    for (int i = 0; i < 4; ++i)
        Player(i)->m_skater = &Word<Obj::CSkater>(kSkaters + 0x100 * static_cast<uint32_t>(i));
    for (uint32_t i = 0; i < 4; ++i)
        Word<uint32_t>(kSkaters + 0x100 * i + 0x14) = i;
    auto king = [&](int i, bool value) {
        CallVoid(original, 0x00544340, &PlayerInfo::MarkAsKing, Player(i), value);
        Rec(0x1D1, {Player(0)->m_flags, Player(1)->m_flags, Player(2)->m_flags, Player(3)->m_flags});
    };
    king(2, true); // a remote player, no king before, no crown
    Word<uint32_t>(kManager + 0x6C) = At(kCrownObject);
    g_king = 2;
    Player(2)->m_flags |= 0x80;
    king(1, true); // the local one (skater number 1), taking it from player 2
    g_king = 1;
    king(1, true); // already king: only worn
    Player(3)->m_flags = 0x21;
    g_local = 3;
    g_king = -1;
    king(3, true); // a local one with skater number 0... (index 3: number 1)
    g_inNetGame = false; // split screen: the other player (id 0 -> player 1; else player 0) told
    Player(0)->m_flags = 0x20;
    king(0, true);
    Word<uint32_t>(kSkaters + 0x100 * 2 + 0x14) = 7; // (id 7 -> player 0)
    Player(2)->m_flags = 0x20;
    king(2, true);
    Word<uint32_t>(kSkaters + 0x14) = 0;
    // Giving it up.
    for (uint32_t type : {0x6EF8FDA0u, 0x5D32129Cu, 0x11111111u}) {
        g_gameType = type;
        Player(1)->m_flags = 0xA1;
        king(1, false);
    }
    g_gameType = 0x6EF8FDA0;
    Player(2)->m_flags = 0xA0;
    king(2, false); // not local
    king(2, false); // not king
}

void ScoreTask(bool original, int times)
{
    void* task = &Word<uint8_t>(kTask);
    for (int i = 0; i < times; ++i) {
        original ? Original<void(__cdecl*)(void*)>(0x00544780)(task) : GameNet::ScoreUpdateTask(task);
        Rec(0x7A5C, {GameVariable<uint32_t>(0x007CE43C)});
        g_now += 600;
    }
}

void Lines(bool original)
{
    for (int i = 0; i < 6; ++i) {
        Player(i)->m_skater = &Word<Obj::CSkater>(kSkaters + 0x100 * static_cast<uint32_t>(i));
        Word<uint32_t>(kSkaters + 0x100 * static_cast<uint32_t>(i) + 0x14) = static_cast<uint32_t>(5 - i);
    }
    g_players = {0, 1, 2, 3, 4, 5};
    Player(0)->m_flags = 0x1820;   // carries two flags
    Player(2)->m_flags = 0xA0;     // the king
    Player(3)->m_flags = 0x20020;  // eliminated
    Player(4)->m_flags = 0;        // not in
    Player(5)->m_flags = 0x4020;
    std::memcpy(Player(5)->m_name, "AVeryLongName15", 16);
    g_elementMask = 0xED; // lines 2 and 5 without an element
    // In the lobby (player order), then other modes.
    g_gameType = 0x1C471C60;
    ScoreTask(original, 3); // (the second within a second: nothing)
    g_gameType = 0x12345678;
    g_now += 1000;
    ScoreTask(original, 1);
    g_teamGame = true;
    g_now += 1000;
    ScoreTask(original, 1);
    g_gameType = 0xBFF33600; // firefight: player 1 out of lives
    g_teamGame = false;
    Word<int>(kScores + 0x10) = 0;
    g_noScore = 1u << 2; // and player 2 without a score component (kept)
    g_now += 1000;
    ScoreTask(original, 1);
}

void Interval(bool original)
{
    for (uint32_t now : {50000u, 50999u, 51000u, 51999u, 52000u, 52001u}) {
        g_now = now;
        ScoreTask(original, 1);
    }
}

void KingLines(bool original)
{
    for (int i = 0; i < 4; ++i)
        Player(i)->m_skater = &Word<Obj::CSkater>(kSkaters + 0x100 * static_cast<uint32_t>(i));
    for (uint32_t i = 0; i < 4; ++i)
        Word<uint32_t>(kSkaters + 0x100 * i + 0x14) = i;
    Word<int>(kScores + 0x00) = 59999;      // 0:59
    Word<int>(kScores + 0x10) = 3599999;    // 59:59
    Word<int>(kScores + 0x20) = 3600000;    // 1:00:00
    Word<int>(kScores + 0x30) = -1;         // (unsigned: a long time)
    Player(3)->m_flags = 0x80A0;
    for (uint32_t type : {0x6EF8FDA0u, 0x5D32129Cu}) {
        g_gameType = type;
        g_now += 1000;
        ScoreTask(original, 1);
    }
}

void ScavengerLines(bool original)
{
    for (int i = 0; i < 4; ++i)
        Player(i)->m_skater = &Word<Obj::CSkater>(kSkaters + 0x100 * static_cast<uint32_t>(i));
    g_gameType = 0xFC13B531;
    for (uint32_t active : {0u, 1u, 2u}) {
        g_goalActive = active;
        g_now += 1000;
        ScoreTask(original, 1);
    }
}

void TeamLines(bool original)
{
    for (int i = 0; i < 6; ++i)
        Player(i)->m_skater = &Word<Obj::CSkater>(kSkaters + 0x100 * static_cast<uint32_t>(i));
    g_players = {0, 1, 2, 3, 4, 5};
    g_teamMode = true;
    g_numTeams = 4;
    Player(0)->m_flags = 0x80A0;  // team 0: the king
    Player(2)->m_flags = 0x2020;  // team 0: carries team 2's flag
    Player(3)->m_flags = 0x0820;  // team 1: carries team 0's
    Player(5)->m_flags = 0x20020; // eliminated
    Player(4)->m_team = 3;
    Player(1)->m_flags = 0x1021;  // team 1: carries team 1's
    g_teamPlayers[0] = 2;
    g_teamPlayers[1] = 2;
    g_teamPlayers[2] = 0;
    g_teamPlayers[3] = 1;
    g_now += 1000;
    ScoreTask(original, 1);
    g_teamMode = false; // the per-player lines after all
    g_now += 1000;
    ScoreTask(original, 1);
    g_teamMode = true;
    g_gameType = 0x1C471C60; // the lobby: never per team
    g_now += 1000;
    ScoreTask(original, 1);
    g_gameType = 0xEC200EAA; // net goal attack: the goal manager's counts
    g_now += 1000;
    ScoreTask(original, 1);
}

void TestNetScores()
{
    SaveGlobals({{0x00701440, 4, {}}, {0x007CE43C, 4, {}}});
    RunCase("GameNet::Manager::GetTeamScore / GetPlayerScore", Scores);
    RunCase("GameNet::PlayerInfo::IsInvulnerable", Invulnerable);
    RunCase("GameNet::PlayerInfo::MarkAsFullyIn", FullyIn);
    RunCase("GameNet::PlayerInfo: the flag announcements", Flags);
    RunCase("GameNet::PlayerInfo::MarkAsKing", King);
    RunCase("GameNet::ScoreUpdateTask: per player", Lines);
    RunCase("GameNet::ScoreUpdateTask: once a second", Interval);
    RunCase("GameNet::ScoreUpdateTask: king of the hill", KingLines);
    RunCase("GameNet::ScoreUpdateTask: scavenger hunt", ScavengerLines);
    RunCase("GameNet::ScoreUpdateTask: per team", TeamLines);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetScores, GameNetKit::Cleanup);
