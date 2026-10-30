// Self-tests for the network game's players (src/game/net/player_info.cpp): the original and ours run on the same
// stand-in arena (gamenet_testkit.h), everything they call outside stood in for by recording code; the calls, the
// results and the arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/skate/profile_manager.h"
#include "game/sys/timer.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::NewPlayerInfo;
using GameNet::PlayerInfo;

constexpr uint32_t kComponents = 0x13600; // stand-in components (kMisc + 0x600): tagging +0x00, score +0x40
constexpr uint32_t kHead = 0x13700;       // a list head (kMisc + 0x700)
constexpr uint32_t kObject = 0x13740;     // an object with a vtable (kMisc + 0x740; its vtable at +0x10)
constexpr uint32_t kData = 0x12000;       // bytes to copy (kMsg)
constexpr uint32_t kScratch = 0x12400;    // a NewPlayerInfo and the like (kMsg + 0x400)

uint32_t g_now;
uint32_t g_tagging;       // GetComponent's answers (arena offsets, 0 = none)
uint32_t g_score;
int g_failAllocations;    // the next allocations that fail
uint32_t g_remote;
uint32_t g_bandwidth;
uint32_t g_gameType;
uint32_t g_lives;

uint32_t PlayerAt(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
PlayerInfo* Player(int i) { return &Word<PlayerInfo>(PlayerAt(i)); }

void Install(StandIns& s)
{
    InstallCommon(s);
    // Allocations that may fail (over the kit's).
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
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        uint32_t at = r.args[0] == 0x5B512EDC ? g_tagging : r.args[0] == 0xD262802F ? g_score : 0;
        return at ? At(at) : 0u;
    });
    Stub(s, {0x00586D60, AddressOf(&Obj::CSkaterProfile::Construct)}, 0, 0x6D60, [](Regs& r) { return r.ecx; });
    Stub(s, {0x00585ED0, AddressOf(&Obj::CSkaterProfile::CopyFrom)}, 4, 0x5ED0, [](Regs& r) { return r.ecx; });
    Stub(s, {0x004890F0}, 0, 0x90F0);
    Stub(s, {0x0053A910, AddressOf(&GameNet::Manager::ObservePlayer)}, 4, 0xA910);
    Stub(s, {0x00455A30, AddressOf(&Net::Conn::IsRemote)}, 0, 0x5A30, [](Regs&) { return g_remote; });
    Stub(s, {0x00456630, AddressOf(&Net::Conn::ClearResendCounts)}, 0, 0x6630);
    Stub(s, {0x004559A0, AddressOf(&Net::Conn::GetBandwidthType)}, 0, 0x59A0, [](Regs&) { return g_bandwidth; });
    Stub(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x00553270}, 0, 0x3270, [](Regs&) { return 0x4321u; });
    Stub(s, {0x00561B40, AddressOf(&Mdl::Score::GetTotalScore)}, 0, 0x1B40, [](Regs&) { return g_lives; });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_now = 1000;
        g_tagging = 0;
        g_score = 0;
        g_failAllocations = 0;
        g_remote = 1;
        g_bandwidth = 0;
        g_gameType = 0;
        g_lives = 0;
        for (uint32_t i = 0x800; i < 0xDF0; i += 4)
            Word<uint32_t>(kMisc + i) = 0x6D000000u + i; // the memory manager's fields told apart
        for (uint32_t i = 0; i < 0x40; ++i)
            Word<uint8_t>(kData + i) = static_cast<uint8_t>(0x30 + i * 7);
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

// A member function: the original at `address` or ours, on `object`; the result recorded.
template <typename R, typename C, typename... P, typename... A>
R Call(bool original, uint32_t address, R (C::*ours)(P...), void* object, A... args)
{
    R result;
    if (original)
        result = Original<R(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...);
    else
        result = (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    uint32_t word = 0;
    std::memcpy(&word, &result, sizeof(result) < 4 ? sizeof(result) : 4);
    Rec(0xCA11, {address, word});
    return result;
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
// A bool result: AL only.
template <typename C, typename... P, typename... A>
void CallBool(bool original, uint32_t address, bool (C::*ours)(P...), void* object, A... args)
{
    bool result = original ? Original<bool(__thiscall*)(void*, P...)>(address)(object, static_cast<P>(args)...)
                           : (static_cast<C*>(object)->*ours)(static_cast<P>(args)...);
    Rec(0xB001, {address, result ? 1u : 0u});
}

void NewPlayers(bool original)
{
    void* info = &Word<uint8_t>(kScratch);
    std::memset(info, 0xAB, sizeof(NewPlayerInfo));
    Call(original, 0x005434D0, &NewPlayerInfo::Construct, info);
    CallVoid(original, 0x00543560, &NewPlayerInfo::DestroyBody, info);
    g_failAllocations = 1;
    Call(original, 0x005434D0, &NewPlayerInfo::Construct, info);
    CallVoid(original, 0x00543560, &NewPlayerInfo::DestroyBody, info);
    // A profile's destructor body by itself.
    CallVoid(original, 0x00543010, &Obj::CSkaterProfile::DestroyBody, &Word<uint8_t>(kScratch + 0x100));
}

void ConstructAndDestroy(bool original)
{
    std::memset(Player(1), 0xCD, sizeof(PlayerInfo));
    Call(original, 0x00543740, &PlayerInfo::Construct, Player(1), 0x00012345u);
    // In a list with two others; a profile, the object, the face texture message.
    for (int i : {2, 3}) {
        std::memset(Player(i), 0x11 * i, sizeof(PlayerInfo));
        Call(original, 0x00543740, &PlayerInfo::Construct, Player(i), static_cast<uint32_t>(i));
    }
    // head <-> 2 <-> 1 <-> 3 <-> head
    auto link = [](uint32_t a, uint32_t b) {
        Word<uint32_t>(a + 0x0C) = At(b);
        Word<uint32_t>(b + 0x10) = At(a);
    };
    link(kHead, PlayerAt(2));
    link(PlayerAt(2), PlayerAt(1));
    link(PlayerAt(1), PlayerAt(3));
    link(PlayerAt(3), kHead);
    Player(1)->m_profile = &Word<Obj::CSkaterProfile>(kScratch);
    Word<uint32_t>(kObject) = At(kObject + 0x10);
    void* slot = StandIns::s_current->Thunk(4, [](Regs& r) {
        Rec(0x1EC2, {r.ecx, r.args[0]});
        return 0u;
    });
    Word<uint32_t>(kObject + 0x18) = reinterpret_cast<uint32_t>(slot);
    Player(1)->m_object1EC = &Word<uint8_t>(kObject);
    Player(1)->m_faceTextureData = &Word<uint8_t>(kScratch + 0x200);
    CallVoid(original, 0x00543810, &PlayerInfo::DestroyBody, Player(1));
    Word<uint32_t>(kObject + 0x18) = 0; // (the thunk's address differs between the runs)
    // The scalar deleting one, with nothing to free but itself, then without the free.
    Call(original, 0x00544750, &PlayerInfo::Destroy, Player(2), 1u);
    Call(original, 0x00544750, &PlayerInfo::Destroy, Player(3), 0u);
}

void SkaterAndProfile(bool original)
{
    for (int i = 0; i < 3; ++i)
        Player(i)->m_flags = i == 1 ? 1u : 0u;
    Word<uint32_t>(kManager + 0x1CC) = At(PlayerAt(0)); // the camera follows player 0
    for (int i = 0; i < 3; ++i)
        CallVoid(original, 0x005438D0, &PlayerInfo::SetSkater, Player(i), &Word<Obj::CSkater>(kSkaters + 0x100 * (5 - i)));
    Word<uint32_t>(kManager + 0x1CC) = At(PlayerAt(1)); // a local one
    CallVoid(original, 0x005438D0, &PlayerInfo::SetSkater, Player(1), static_cast<Obj::CSkater*>(nullptr));
    CallVoid(original, 0x00543270, &PlayerInfo::SetProfile, Player(2), &Word<Obj::CSkaterProfile>(kScratch));
    g_failAllocations = 1;
    CallVoid(original, 0x00543270, &PlayerInfo::SetProfile, Player(3), &Word<Obj::CSkaterProfile>(kScratch + 0x80));
}

void FaceTexture(bool original)
{
    Word<uint8_t>(kSkaters + 0x100 * 2 + 0x14) = 0x5E;
    for (uint32_t size : {0u, 5u, 7u, 0x22u}) {
        CallVoid(original, 0x005431F0, &PlayerInfo::SetFaceTextureData, Player(2), &Word<uint8_t>(kData + size), size);
        Call(original, 0x00543060, &PlayerInfo::GetFaceTextureData, Player(2));
    }
}

void Tagging(bool original)
{
    PlayerInfo* p = Player(4);
    p->m_skater = nullptr;
    CallVoid(original, 0x00543070, &PlayerInfo::SetCustomTag, p, true);
    CallBool(original, 0x00543090, &PlayerInfo::HasCustomTag, p);
    Call(original, 0x005430B0, &PlayerInfo::GetGraffitiTexture, p);
    p->m_skater = &Word<Obj::CSkater>(kSkaters + 0x400);
    CallVoid(original, 0x00543070, &PlayerInfo::SetCustomTag, p, true); // no component
    CallBool(original, 0x00543090, &PlayerInfo::HasCustomTag, p);
    Call(original, 0x005430B0, &PlayerInfo::GetGraffitiTexture, p);
    g_tagging = kComponents;
    Word<uint32_t>(kComponents + 0x1C) = 0x7E570000;
    for (bool custom : {true, false, true}) {
        CallVoid(original, 0x00543070, &PlayerInfo::SetCustomTag, p, custom);
        CallBool(original, 0x00543090, &PlayerInfo::HasCustomTag, p);
    }
    Word<uint8_t>(kComponents + 0x18) = 0x02; // (a bool byte other than 1)
    CallBool(original, 0x00543090, &PlayerInfo::HasCustomTag, p);
    Call(original, 0x005430B0, &PlayerInfo::GetGraffitiTexture, p);
}

void Owed(PlayerInfo* p) { Rec(0x0BED, {p->m_readyQueryOwed, Word<uint32_t>(kConn + 0x60)}); }
void Await(bool original, PlayerInfo* p, uint32_t id)
{
    CallVoid(original, 0x00544670, &PlayerInfo::AwaitReadyResponse, p, id);
    Owed(p);
}
void Receive(bool original, PlayerInfo* p, uint32_t id)
{
    CallVoid(original, 0x005446C0, &PlayerInfo::ReceiveReadyResponse, p, id);
    Owed(p);
}

void Ready(bool original)
{
    PlayerInfo* p = Player(5);
    CallVoid(original, 0x005430D0, &PlayerInfo::SetReadyQueryOwed, p, 0x11u);
    p->m_conn = nullptr;
    Await(original, p, 0x22u);
    Receive(original, p, 0x11u); // nothing owed after
    p->m_readyQueryOwed = 0x33;
    p->m_conn = &Word<Net::Conn>(kConn);
    Word<uint32_t>(kConn + 0x60) = 0xF0F0F0F1;
    g_remote = 0;
    Await(original, p, 0x44u);
    g_remote = 1;
    Await(original, p, 0u);
    Await(original, p, 0x55u);
    Receive(original, p, 0x33u); // not the one owed
    Receive(original, p, 0x55u);
    Word<uint32_t>(kConn + 0x60) = 0x0000000E;
    p->m_readyQueryOwed = 0;
    Receive(original, p, 0u);
}

void Numbers(bool original)
{
    PlayerInfo* p = Player(6);
    CallVoid(original, 0x00543110, &PlayerInfo::SetLastHitTime, p, 2000u);
    for (uint32_t now : {2000u, 7000u, 7001u, 1999u, 0x90000000u}) {
        g_now = now;
        Call(original, 0x005430E0, &PlayerInfo::CanBeHitAgain, p);
    }
    CallVoid(original, 0x00543100, &PlayerInfo::ClearLastHitTime, p);
    g_now = 5000;
    Call(original, 0x005430E0, &PlayerInfo::CanBeHitAgain, p);
    g_now = 5001;
    Call(original, 0x005430E0, &PlayerInfo::CanBeHitAgain, p);
    CallVoid(original, 0x00543130, &PlayerInfo::SetLastSentSkater, p, 3);
    Call(original, 0x00543120, &PlayerInfo::GetLastSentSkater, p);
    p->m_conn = &Word<Net::Conn>(kConn);
    for (uint32_t type : {0u, 1u, 2u, 0xFFFFFFFFu}) {
        g_bandwidth = type;
        Call(original, 0x00543140, &PlayerInfo::GetSkatersPerUpdate, p);
    }
    p->m_accumulatedScore = 100;
    CallVoid(original, 0x00543160, &PlayerInfo::AddToAccumulatedScore, p, -250);
    Call(original, 0x00543150, &PlayerInfo::GetAccumulatedScore, p);
    Call(original, 0x005434A0, &PlayerInfo::GetConnHandle, p);
    p->m_conn = nullptr;
    Call(original, 0x005434A0, &PlayerInfo::GetConnHandle, p);
    p->m_skater = &Word<Obj::CSkater>(kSkaters + 0x200);
    Word<uint32_t>(kSkaters + 0x200 + 0x2BC) = 0xBC2BC2;
    Call(original, 0x005434B0, &PlayerInfo::GetSkaterValue2BC, p);
}

void Flags(bool original)
{
    PlayerInfo* p = Player(7);
    const uint32_t values[] = {0x0, 0x1, 0x4, 0x20, 0x21, 0x80, 0x208, 0x800, 0x1000, 0x2000, 0x4000, 0x3000, 0x6000,
                               0x7800, 0x10000, 0x20000, 0xFFFFFFFF, 0xFFFE87DE};
    for (uint32_t value : values) {
        p->m_flags = value;
        Call(original, 0x00543300, &PlayerInfo::IsLocalPlayer, p);
        Call(original, 0x00543310, &PlayerInfo::IsServerPlayer, p);
        Call(original, 0x00543320, &PlayerInfo::IsObserver, p);
        Call(original, 0x00543330, &PlayerInfo::IsSurveying, p);
        Call(original, 0x00543380, &PlayerInfo::IsFullyIn, p);
        Call(original, 0x005433B0, &PlayerInfo::IsCarryingFlag, p);
        Call(original, 0x005433D0, &PlayerInfo::GetFlagTeam, p);
        Call(original, 0x00543490, &PlayerInfo::IsKing, p);
        Call(original, 0x005434C0, &PlayerInfo::IsPendingObserver, p);
        Rec(0xF1A6, {p->m_flags});
    }
    struct Setter {
        uint32_t address;
        void (PlayerInfo::*ours)();
    };
    const Setter setters[] = {{0x00543340, &PlayerInfo::MarkFaceTextureSent}, {0x00543350, &PlayerInfo::MarkJustJoined},
                              {0x00543370, &PlayerInfo::ClearJustJoined},     {0x00543390, &PlayerInfo::MarkAsServerPlayer},
                              {0x005433A0, &PlayerInfo::ClearServerPlayer},   {0x00543410, &PlayerInfo::ClearFlags},
                              {0x00543420, &PlayerInfo::MarkEliminated}};
    for (uint32_t value : {0x00000000u, 0xFFFFFFFFu, 0x00005A5Au}) {
        for (const Setter& setter : setters) {
            p->m_flags = value;
            g_now += 7;
            CallVoid(original, setter.address, setter.ours, p);
            Rec(0xF1A7, {p->m_flags, p->m_joinTime, p->m_eliminatedTime});
        }
    }
}

void Eliminated(bool original)
{
    PlayerInfo* p = Player(3);
    p->m_flags = 0x20000;
    CallBool(original, 0x00543440, &PlayerInfo::IsEliminated, p);
    p->m_flags = 0xFFFDFFFF;
    g_gameType = 0x12345678;
    CallBool(original, 0x00543440, &PlayerInfo::IsEliminated, p);
    g_gameType = 0xBFF33600;
    p->m_skater = nullptr;
    CallBool(original, 0x00543440, &PlayerInfo::IsEliminated, p);
    p->m_skater = &Word<Obj::CSkater>(kSkaters + 0x300);
    CallBool(original, 0x00543440, &PlayerInfo::IsEliminated, p); // no component
    g_score = kComponents + 0x40;
    Word<uint32_t>(kComponents + 0x40 + 0x18) = At(kComponents + 0x80);
    for (uint32_t lives : {0u, 3u, 0xFFFFFFFFu}) {
        g_lives = lives;
        CallBool(original, 0x00543440, &PlayerInfo::IsEliminated, p);
    }
}

void ScoreLists(bool original)
{
    // Searches: the vtable reset (a free with flag 1).
    uint32_t search = kScratch;
    Word<uint32_t>(search) = 0x11111111;
    CallVoid(original, 0x00543170, &GameNet::ScoreSearch::DestroyBody, &Word<uint8_t>(search));
    Word<uint32_t>(search) = 0x22222222;
    Call(original, 0x00543580, &GameNet::ScoreSearch::Destroy, &Word<uint8_t>(search), 0u);
    Word<uint32_t>(search) = 0x33333333;
    Call(original, 0x00543580, &GameNet::ScoreSearch::Destroy, &Word<uint8_t>(search), 1u);
    // Nodes: head <-> a <-> b <-> c <-> d <-> head; each destructor takes one out.
    const uint32_t nodes[] = {kHead, kScratch + 0x40, kScratch + 0x80, kScratch + 0xC0, kScratch + 0x100};
    for (int i = 0; i < 5; ++i) {
        uint32_t node = nodes[i], following = nodes[(i + 1) % 5];
        Word<uint32_t>(node) = 0x44444444;
        Word<uint32_t>(node + 0x0C) = At(following);
        Word<uint32_t>(following + 0x10) = At(node);
    }
    CallVoid(original, 0x00544700, &GameNet::ScoreNode::DestroyBody, &Word<uint8_t>(nodes[2]));
    Call(original, 0x005435B0, &GameNet::ScoreNode::Destroy, &Word<uint8_t>(nodes[1]), 0u);
    Call(original, 0x005435B0, &GameNet::ScoreNode::Destroy, &Word<uint8_t>(nodes[3]), 1u);
    Call(original, 0x00544720, &GameNet::ScoreNode::DestroyCalling, &Word<uint8_t>(nodes[4]), 3u);
    Word<uint32_t>(kHead + 0x0C) = At(kHead);
    Word<uint32_t>(kHead + 0x10) = At(kHead);
    Call(original, 0x00544720, &GameNet::ScoreNode::DestroyCalling, &Word<uint8_t>(kHead), 0u);
}

void UpdateRecords(bool original)
{
    std::memset(&Word<uint8_t>(kScratch), 0x77, 0x180);
    void* records = &Word<uint8_t>(kScratch + 0x10);
    void* result = original ? Original<void*(__thiscall*)(void*)>(0x00543180)(records) : GameNet::ClearUpdateRecords(records);
    Rec(0xC1EA, {reinterpret_cast<uint32_t>(result)});
}

void TestNet3Players()
{
    SaveGlobals();
    RunCase("GameNet::NewPlayerInfo", NewPlayers);
    RunCase("GameNet::PlayerInfo: constructor and destructors", ConstructAndDestroy);
    RunCase("GameNet::PlayerInfo: skater and profile", SkaterAndProfile);
    RunCase("GameNet::PlayerInfo: the face texture message", FaceTexture);
    RunCase("GameNet::PlayerInfo: the tagging component", Tagging);
    RunCase("GameNet::PlayerInfo: the ready query", Ready);
    RunCase("GameNet::PlayerInfo: hit times, updates, scores", Numbers);
    RunCase("GameNet::PlayerInfo: the flags", Flags);
    RunCase("GameNet::PlayerInfo::IsEliminated", Eliminated);
    RunCase("GameNet: the score lists' destructors", ScoreLists);
    RunCase("GameNet: the update records' constructor", UpdateRecords);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNet3Players, GameNetKit::Cleanup);
