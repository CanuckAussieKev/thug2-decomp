// Self-tests for the network manager's game handlers (src/game/net/gamenet_game_handlers.cpp): the original and ours
// handle the same messages on the same stand-in arena (gamenet_testkit.h) - players, goals with stand-in virtuals,
// crown nodes, the editors - with everything they call outside stood in for by recording code; the calls, results and
// arena are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/task.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/game_objects.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/goals/goal_manager.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using Script::CStruct;

constexpr uint32_t kGoals = 0x16000;    // stand-in goals, 0x40 apart (16): vtable +0, +0x14 a word, +0x28 its chain
constexpr uint32_t kChains = 0x16800;   // goal name chains
constexpr uint32_t kNodes = 0x16C00;    // stand-in nodes, 0x10 apart
constexpr uint32_t kNewInfos = 0x17000; // new player infos, 0x40 apart

uint32_t g_presentIds;      // object ids 0x10 + i with a player (bits)
uint32_t g_local;           // the local player (arena offset, 0 none)
uint32_t g_king;            // the king of the hill (arena offset, 0 none)
uint32_t g_firstPlayer;     // the players' list: kPlayers + i * kPlayerSize for i < g_playerCount
int g_playerCount;
int g_newCount;
bool g_netGame, g_teamGame;
int g_teamCount;
uint32_t g_currentGoal;     // 0x0055DBF0's answer (arena offset, 0 none)
int g_goalsTotal, g_goalsDone;
std::vector<uint32_t> g_nodeTypes; // the node walk: each node's Type
size_t g_nodeIndex;
void* g_goalVtable[0x30];   // (outside the arena: the same in both runs)

uint32_t PlayerAt(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        uint32_t id = r.args[0] & 0xFFFF;
        Rec(0xAEA0, {r.ecx, id});
        uint32_t i = id - 0x10;
        return i < 8 && ((g_presentIds >> i) & 1) ? At(PlayerAt(static_cast<int>(i))) : 0u;
    });
    Stub(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return g_local ? At(g_local) : 0u; });
    Stub(s, {0x00537480, AddressOf(&GameNet::Manager::GetKingOfTheHill)}, 0, 0x7480, [](Regs&) { return g_king ? At(g_king) : 0u; });
    Stub(s, {0x00544340, AddressOf(&GameNet::PlayerInfo::MarkAsKing)}, 4, 0x4340);
    s.At({0x005376F0, AddressOf(&GameNet::Manager::CreateNetPanelMessage)}, 0x20, [](Regs& r) {
        Rec(0x76F0, {r.ecx, r.args[0] & 0xFF, r.args[1], Str(r.args[2]), Str(r.args[3]), r.args[4], r.args[5],
                     r.args[6] & 0xFF, r.args[7]});
        return 0u;
    });
    Stub(s, {0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_netGame ? 1u : 0u; });
    StubCdecl(s, {0x0045C1D0, AddressOf(&SkateScript::StartNodeWalk)}, 0xC1D0, 1, [](Regs&) {
        g_nodeIndex = 0;
        return 0u;
    });
    StubCdecl(s, {0x0045C1F0, AddressOf(&SkateScript::NextNode)}, 0xC1F0, 0, [](Regs&) {
        return g_nodeIndex < g_nodeTypes.size() ? At(kNodes + 0x10 * static_cast<uint32_t>(g_nodeIndex++)) : 0u;
    });
    StubCdecl(s, {0x0045C200, AddressOf(&SkateScript::EndNodeWalk)}, 0xC200, 0);
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Rec(0x6950, {r.ecx, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
             uint32_t node = (r.ecx - At(kNodes)) / 0x10;
             *reinterpret_cast<uint32_t*>(r.args[1]) = g_nodeTypes[node];
             return 1u;
         });
    StubCdecl(s, {0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(SkateScript::GetPosition))}, 0xC050, 1, [](Regs& r) { // (the out vector is a local: not recorded)
        auto* out = reinterpret_cast<float*>(r.args[1]);
        uint32_t node = (r.args[0] - At(kNodes)) / 0x10;
        out[0] = 1.5f * node;
        out[1] = -2.0f;
        out[2] = 100.25f + node;
        out[3] = 1.0f;
        return 0u;
    });
    // NewPlayerInfo: its constructor (a profile at +0x1C), destructor; the profile's appearance; the new player.
    s.At({0x005434D0, AddressOf(&GameNet::NewPlayerInfo::Construct)}, 0, [](Regs& r) {
        Rec(0x34D0, {});
        std::memset(reinterpret_cast<void*>(r.ecx + 0x18), 0, 0x2C);
        *reinterpret_cast<uint32_t*>(r.ecx + 0x1C) = At(kMisc + 0xA00);
        return r.ecx;
    });
    s.At({0x00543560, AddressOf(&GameNet::NewPlayerInfo::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x3560, {*reinterpret_cast<uint32_t*>(r.ecx + 0x1C)});
        return 0u;
    });
    Stub(s, {0x00586DC0, AddressOf(&Obj::CSkaterProfile::ReadFromBuffer)}, 4, 0x6DC0);
    s.At({0x0053A310, AddressOf(&GameNet::Manager::AddJoiningPlayer)}, 8, [](Regs& r) {
        auto* info = reinterpret_cast<uint8_t*>(r.args[0]);
        g_trace.push_back(0xA310);
        g_trace.push_back(Rel(r.ecx));
        g_trace.push_back(r.args[1]);
        for (uint32_t i = 0; i < 0x44; i += 4) // (+0x11-+0x13: padding, never written)
            g_trace.push_back(Rel(*reinterpret_cast<uint32_t*>(info + i) & (i == 0x10 ? 0xFFu : 0xFFFFFFFFu)));
        return 0u;
    });
    // The editors.
    StubCdecl(s, {0x004F4970, AddressOf(&Ed::CRailEditorComponent::Instance)}, 0x4970, 0, [](Regs&) { return At(kMisc + 0x500); });
    s.At({0x004F3C00}, 4, [](Regs& r) {
        Rec(0x3C00, {r.ecx, r.args[0], GameVariable<uint8_t>(0x0067F000)});
        return 0u;
    });
    s.At({0x004F7DF0}, 0, [](Regs& r) {
        Rec(0x7DF0, {r.ecx, GameVariable<uint8_t>(0x0067F000)});
        return 0u;
    });
    StubCdecl(s, {0x004E8470, AddressOf(&Ed::CGoalEditorComponent::Get)}, 0x8470, 0, [](Regs&) { return At(kMisc + 0x600); });
    Stub(s, {0x004EA2C0}, 8, 0xA2C0);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        RecordDesc(0x9E50, r.ecx, reinterpret_cast<Net::MsgDesc*>(r.args[0]));
        return 0u;
    });
    // Goals.
    StubCdecl(s, {0x005591B0, AddressOf(&Game::GetGoalManager)}, 0x91B0, 0, [](Regs&) { return At(kMisc + 0x700); });
    Stub(s, {0x00558580}, 0, 0x8580);
    Stub(s, {0x00557F90}, 8, 0x7F90, [](Regs& r) {
        uint32_t name = r.args[0];
        return name >= 0x60A10000 && name < 0x60A10010 ? At(kGoals + 0x40 * (name - 0x60A10000)) : 0u;
    });
    Stub(s, {0x00554440}, 0, 0x4440);
    Stub(s, {0x0055DBF0}, 4, 0xDBF0, [](Regs&) { return g_currentGoal ? At(g_currentGoal) : 0u; });
    Stub(s, {0x00558200}, 0, 0x8200, [](Regs&) { return static_cast<uint32_t>(g_goalsTotal); });
    Stub(s, {0x00558B70}, 0, 0x8B70, [](Regs&) { return static_cast<uint32_t>(g_goalsDone); });
    Stub(s, {0x0055F3D0}, 0, 0xF3D0);
    Stub(s, {0x00555540}, 0, 0x5540);
    s.At({0x00554570}, 4, [](Regs& r) {
        Rec(0x4570, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0])});
        *reinterpret_cast<uint32_t*>(r.args[0]) = At(kMisc + 0xD00); // the goal's text
        return 0u;
    });
    for (uint32_t slot : {0x34u, 0x3Cu, 0x68u, 0x6Cu, 0x70u, 0x74u}) {
        uint16_t pop = slot == 0x34 ? 8 : slot == 0x6C || slot == 0x70 ? 4 : 0;
        g_goalVtable[slot / 4] = s.Thunk(pop, [slot, pop](Regs& r) {
            g_trace.push_back(0x60A0 + slot);
            g_trace.push_back(Rel(r.ecx));
            for (int i = 0; i < pop / 4; ++i)
                g_trace.push_back(Rel(r.args[i]));
            return 0u;
        });
    }
    // Players and the game mode.
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1] & 0xFF});
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return g_playerCount > 0 ? At(PlayerAt(0)) : 0u;
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        Rec(0x7010, {r.args[1] & 0xFF, index});
        ++index;
        return static_cast<int>(index) < g_playerCount ? At(PlayerAt(static_cast<int>(index))) : 0u;
    });
    Stub(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    Stub(s, {0x00553130}, 0, 0x3130, [](Regs&) { return static_cast<uint32_t>(g_teamCount); });
    Stub(s, {0x00543420, AddressOf(&GameNet::PlayerInfo::MarkEliminated)}, 0, 0x3420);
    // The new players waiting to be added: infos at kNewInfos, 0x40 apart.
    s.At({0x00537070, AddressOf(&GameNet::Manager::FirstNewPlayerInfo)}, 4, [](Regs& r) {
        Rec(0x7070, {r.ecx});
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return g_newCount > 0 ? At(kNewInfos) : 0u;
    });
    s.At({0x004573D0, AddressOf(&GameNet::Manager::NextNewPlayerInfo)}, 4, [](Regs& r) {
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        Rec(0x73D0, {r.ecx, index});
        ++index;
        return static_cast<int>(index) < g_newCount ? At(kNewInfos + 0x40 * index) : 0u;
    });
    Stub(s, {0x00564C60}, 8, 0x4C60);
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_presentIds = 0xFF;
        g_local = PlayerAt(0);
        g_king = 0;
        g_playerCount = 4;
        g_newCount = 0;
        g_netGame = g_teamGame = false;
        g_teamCount = 2;
        g_currentGoal = 0;
        g_goalsTotal = g_goalsDone = 0;
        g_nodeTypes.clear();
        // players 0 / 1 local, the others remote; teams 0, 1, 2, 0...
        Word<uint32_t>(PlayerAt(0) + 0x1D0) = 0x00000001;
        Word<uint32_t>(PlayerAt(1) + 0x1D0) = 0x00000801; // local, carrying team 0's flag
        Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x00002000; // carrying team 2's flag
        Word<uint32_t>(PlayerAt(3) + 0x1D0) = 0x00000004; // an observer
        for (uint32_t g = 0; g < 16; ++g) {
            Word<void**>(kGoals + 0x40 * g) = g_goalVtable;
            Word<uint32_t>(kGoals + 0x40 * g + 0x14) = 0x60B00000 + g;
            Word<uint32_t>(kGoals + 0x40 * g + 0x28) = At(kChains + 0x40 * g); // empty chains
        }
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

void Players(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    // A new player: before ready to play (flags 5 - no appearance), after (flags 0).
    for (uint32_t flags : {5u, 0x10u, 1u, 4u}) {
        std::memset(&Word<uint8_t>(kMsg), 0, 0x100);
        for (uint32_t i = 0; i < 0x20; i += 4)
            Word<uint32_t>(kMsg + i) = 0x31000000 + i;
        Word<uint32_t>(kMsg + 8) = flags;
        Word<uint8_t>(kMsg + 0x18) = 0x13;
        Word<uint8_t>(kMsg + 0x19) = 0xFE;
        std::memcpy(&Word<char>(kMsg + 0x20), "A rather long player name", 26);
        Word<bool>(kManager + 0x18C) = flags != 5 && flags != 4;
        handle(0x0052DB80, GameNet::HandleNewPlayer, 0x22);
    }
    std::memcpy(&Word<char>(kMsg + 0x20), "Short", 6);
    handle(0x0052DB80, GameNet::HandleNewPlayer, 0x22);
    // Teams: a remote player joining team 2 (team game / not), a local one, everyone on one team.
    for (int teamGame = 0; teamGame < 2; ++teamGame) {
        g_teamGame = teamGame != 0;
        Message({0x12, 0x02});
        handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
        Message({0x10, 0x01});
        handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
    }
    for (int i = 0; i < 4; ++i)
        Word<int>(PlayerAt(i) + 0x30) = 1;
    Message({0x12, 0x01});
    handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
    g_teamCount = 0;
    handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
    g_playerCount = 1;
    g_teamCount = 2;
    handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
    Message({0x10, 0x01}); // only the player itself in the list
    handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
    Message({0x17, 0x01});
    g_presentIds = 0x0F;
    handle(0x00531A80, GameNet::HandleJoinedTeam, 0x68);
}

void FlagsAndNotices(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    Message({0x44, 0x33, 0x22, 0x11});
    handle(0x005300C0, GameNet::HandleFcfsNotice, 0x5F);
    Rec(0xFCF5, {Word<uint32_t>(PlayerAt(0) + 0x1D0)});
    // Flag returned: no player, a player without a flag, the local carrier, a remote carrier (the local player on
    // that team / an observer / no local player).
    g_presentIds = 0x07;
    Message({0x15});
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    Message({0x10});
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    Message({0x11});
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    Word<uint32_t>(PlayerAt(1) + 0x1D0) = 0x00004001; // team 3's flag
    Word<int>(PlayerAt(0) + 0x30) = 3;
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    Message({0x12});
    Word<int>(PlayerAt(0) + 0x30) = 2;
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x00001000;
    Word<uint32_t>(PlayerAt(0) + 0x1D0) = 0x00000005; // the local player observing
    Word<int>(PlayerAt(0) + 0x30) = 1;
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x00000800;
    g_local = 0;
    handle(0x00530CB0, GameNet::HandleFlagReturned, 0x75);
    // Eliminations: another id, none, an unknown player, the local one, a remote one.
    g_local = PlayerAt(0);
    Word<uint32_t>(kMsg) = 0x10;
    handle(0x00532280, GameNet::HandleEliminated, 0x8E);
    handle(0x00532280, GameNet::HandleEliminated, 0x8C);
    Word<uint32_t>(kMsg) = 0;
    handle(0x00532280, GameNet::HandleEliminated, 0x8D);
    Word<uint32_t>(kMsg) = 0xFFFFFFFF;
    handle(0x00532280, GameNet::HandleEliminated, 0x8D);
    Word<uint32_t>(kMsg) = 0x00010016; // (the id's low 16 bits)
    handle(0x00532280, GameNet::HandleEliminated, 0x8D);
    Word<uint32_t>(kMsg) = 0x00010011;
    handle(0x00532280, GameNet::HandleEliminated, 0x8D);
    Word<uint32_t>(kMsg) = 0x12;
    handle(0x00532280, GameNet::HandleEliminated, 0x8D);
    handle(0x00532390, GameNet::HandleObserversFull, 0x4E);
}

void Crown(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    // The crown object (in the manager's +0x6C) and nodes: crown nodes between others.
    Word<uint32_t>(kManager + 0x6C) = At(kMisc + 0x900);
    g_nodeTypes = {0x11111111, 0xAF86421B, 0x22222222, 0xAF86421B, 0xAF86421B, 0x33333333};
    struct Case {
        uint32_t king;
        bool net;
        uint8_t spot;
    };
    const Case cases[] = {{0, false, 0}, {PlayerAt(0), false, 1}, {PlayerAt(2), true, 2}, {PlayerAt(2), false, 4},
                          {PlayerAt(3), false, 7}, {0, true, 200}};
    for (const Case& c : cases) {
        Word<uint8_t>(kMisc + 0x900 + 0x110) = 1; // worn
        g_king = c.king;
        g_netGame = c.net;
        Message({c.spot});
        handle(0x0052E870, GameNet::HandleCrownSpawn, 0x58);
    }
    // A king whose skater is missing; no other local player; no crown.
    Word<uint32_t>(PlayerAt(2) + 0x14) = 0;
    g_king = PlayerAt(2);
    g_netGame = false;
    handle(0x0052E870, GameNet::HandleCrownSpawn, 0x58);
    Word<uint32_t>(PlayerAt(2) + 0x14) = At(kSkaters + 0x200);
    Word<uint32_t>(kSkaters + 0x200 + 0x14) = 0; // id 0: the other is 1
    g_presentIds = 0x01;
    handle(0x0052E870, GameNet::HandleCrownSpawn, 0x58);
    Word<uint32_t>(kManager + 0x6C) = 0;
    handle(0x0052E870, GameNet::HandleCrownSpawn, 0x58);
}

void CreatedData(bool original)
{
    // Rails / goals: arrived, damaged (asked for again), damaged while leaving for the main menu.
    Word<uint32_t>(kManager + 0xE4) = At(kMisc + 0x480);
    Word<uint32_t>(kMsg) = 0x51515151;
    Word<uint32_t>(kMisc + 0x800 + 0x110) = 0x11011011; // the memory manager's heaps
    Word<uint32_t>(kMisc + 0x800 + 0x114) = 0x11411411;
    for (int pass = 0; pass < 4; ++pass) {
        Word<uint32_t>(kMisc + 0x378) = pass == 2 ? 0xE92ECAFE : 0x12345678;
        uint32_t flags = pass == 0 ? 0 : pass == 3 ? Net::kPacketForeign : Net::kPacketChecksumMismatch | (pass - 1);
        for (bool goals : {false, true}) {
            Net::MsgHandlerContext context;
            std::memset(&context, 0, sizeof(context));
            context.m_Msg = &Word<char>(kMsg);
            context.m_MsgId = goals ? 0x87 : 0x88;
            context.m_App = &Word<Net::App>(kApp);
            context.m_Conn = &Word<Net::Conn>(kConn);
            context.m_data = &Word<uint8_t>(kManager);
            context.m_PacketFlags = flags;
            using Code = int(__cdecl*)(Net::MsgHandlerContext*);
            Code ours = goals ? GameNet::HandleGoalsData : GameNet::HandleRailsData;
            int result = original ? Original<Code>(goals ? 0x00530FF0 : 0x00530E50)(&context) : ours(&context);
            Rec(0xEE00, {static_cast<uint32_t>(result), GameVariable<uint8_t>(0x0067F000)});
        }
    }
}

void Goals(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    // Selected goals: three named, then the menu; none, no menu.
    Message({1, 0x02, 0x00, 0xA1, 0x60, 0x05, 0x00, 0xA1, 0x60, 0x09, 0x00, 0xA1, 0x60, 0, 0, 0, 0});
    handle(0x00531950, GameNet::HandleSelectedGoals, 0x6D);
    Message({2, 0, 0, 0, 0});
    handle(0x00531950, GameNet::HandleSelectedGoals, 0x6D);
    // Completed: no goal; by the local player (goals left / none); by a teammate (chain / no chain; the current goal
    // the end or not; none left); by another team; not a team game; a player without a skater.
    auto completed = [&](uint8_t player, uint32_t goal) {
        Message({player, 0, 0, 0});
        Word<uint32_t>(kMsg + 4) = goal;
        handle(0x00531C80, GameNet::HandleGoalCompleted, 0x6E);
    };
    completed(0x10, 0x12345678);
    g_goalsTotal = 5;
    g_goalsDone = 2;
    completed(0x10, 0x60A10003);
    g_goalsDone = 5;
    completed(0x10, 0x60A10003);
    g_teamGame = true;
    g_goalsDone = 1;
    g_currentGoal = kGoals + 0x40 * 7;
    for (int i = 0; i < 4; ++i)
        Word<int>(PlayerAt(i) + 0x30) = i == 3 ? 1 : 0;
    completed(0x12, 0x60A10004); // no chain
    Word<uint32_t>(kChains + 0x40 * 4) = 0x60A10005; // 4 -> 5 -> 6
    Word<uint32_t>(kChains + 0x40 * 5) = 0x60A10006;
    completed(0x12, 0x60A10004);
    g_currentGoal = kGoals + 0x40 * 6; // the chain's end
    completed(0x12, 0x60A10004);
    g_goalsDone = 9;
    completed(0x11, 0x60A10004);
    g_currentGoal = kGoals + 0x40 * 4; // the goal itself
    completed(0x12, 0x60A10004);
    g_currentGoal = kGoals + 0x40 * 6;
    completed(0x13, 0x60A10004); // another team
    g_teamGame = false;
    completed(0x12, 0x60A10004);
    g_currentGoal = 0;
    completed(0x12, 0x60A10004);
    Word<uint32_t>(PlayerAt(2) + 0x14) = 0;
    completed(0x12, 0x60A10004);
    // Started: no goal, another team, the same team.
    auto started = [&](uint8_t player, uint32_t goal) {
        Message({player, 0, 0, 0});
        Word<uint32_t>(kMsg + 4) = goal;
        handle(0x00531F10, GameNet::HandleGoalStarted, 0x6F);
    };
    started(0x11, 0x99999999);
    started(0x13, 0x60A10001);
    started(0x11, 0x60A10001);
}

void NewKing(bool original)
{
    auto handle = [&](uint8_t id) {
        Message({id});
        Handle(original, 0x0052E760, GameNet::HandleNewKing, 0x55);
    };
    // New players: ids 0x18, 0x19, 0x1A; the second has the crown.
    g_newCount = 3;
    for (uint32_t i = 0; i < 3; ++i) {
        Word<uint8_t>(kNewInfos + 0x40 * i + 0x10) = static_cast<uint8_t>(0x18 + i);
        Word<uint32_t>(kNewInfos + 0x40 * i + 0x20) = i == 1 ? 0x184 : 0x104;
    }
    g_presentIds = 0x0F;
    handle(0x12);   // no king: the new player's crown off; a player gets it
    g_king = PlayerAt(2);
    handle(0x1A);   // the king's off; a new player gets it
    g_king = 0;
    handle(0x1E);   // nobody: the crown off nobody; no such new player
    g_newCount = 0;
    handle(0x1A);
}

void TestGameNetGameHandlers()
{
    SaveGlobals({{0x0067F000, 1, {}}});
    RunCase("GameNet handlers: new players, joined teams", Players);
    RunCase("GameNet handlers: fcfs notice, flag returned, eliminations, observers full", FlagsAndNotices);
    RunCase("GameNet::HandleCrownSpawn", Crown);
    RunCase("GameNet handlers: created rails / goals arriving", CreatedData);
    RunCase("GameNet handlers: selected / completed / started goals", Goals);
    RunCase("GameNet::HandleNewKing", NewKing);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestGameNetGameHandlers, GameNetKit::Cleanup);
