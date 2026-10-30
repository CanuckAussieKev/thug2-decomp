// Self-tests for the network manager's session handlers (src/game/net/gamenet_session_handlers.cpp): the original and
// ours handle the same messages on the same stand-in arena (gamenet_testkit.h), everything they call outside stood in
// for by recording code; the calls, results and arena are compared. (The bytes the original leaves uninitialised in
// what it sends aren't compared.)

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/core/task.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/obj/trick_objects.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Script::CArray;
using Script::CStruct;

constexpr uint32_t kGoals = 0x16000;    // stand-in goals, 0x40 apart
constexpr uint32_t kNodes = 0x16800;    // list nodes, 0x20 apart
constexpr uint32_t kItems = 0x16C00;    // list items, 0x40 apart
constexpr uint32_t kFakes = 0x17000;    // stand-in structures / arrays, 0x40 apart
constexpr uint32_t kStrings = 0x17800;  // strings, 0x40 apart

uint32_t g_sender, g_host, g_local, g_king;
int g_namedSlots; // the player name slots the server info fills (the rest: the original's stack)
int g_playerCount, g_newCount;
bool g_teamGame, g_netGame, g_taskInList, g_observerPlays;
uint32_t g_gameType, g_goalCount, g_runningGoal;
int g_numPlayers, g_numObservers, g_maxPlayers, g_maxObservers, g_skill;
void* g_goalVtable[0x10];

uint32_t PlayerAt(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
uint32_t Fake(int i) { return kFakes + 0x40 * static_cast<uint32_t>(i); }
uint32_t String(int i) { return kStrings + 0x40 * static_cast<uint32_t>(i); }

// A structure's checksum answers: (the structure's arena offset, the name) -> value.
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answer;

void Install(StandIns& s)
{
    InstallCommon(s);
    Stub(s, {0x0053AE10, AddressOf(&GameNet::Manager::PlayerByConnection)}, 4, 0xAE10, [](Regs&) { return g_sender ? At(g_sender) : 0u; });
    Stub(s, {0x005373D0, AddressOf(&GameNet::Manager::GetServerPlayer)}, 0, 0x73D0, [](Regs&) { return g_host ? At(g_host) : 0u; });
    Stub(s, {0x00537320, AddressOf(&GameNet::Manager::GetLocalPlayer)}, 0, 0x7320, [](Regs&) { return g_local ? At(g_local) : 0u; });
    Stub(s, {0x00537480, AddressOf(&GameNet::Manager::GetKingOfTheHill)}, 0, 0x7480, [](Regs&) { return g_king ? At(g_king) : 0u; });
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
    // The new players: infos at kItems (0x40 apart).
    s.At({0x00537070, AddressOf(&GameNet::Manager::FirstNewPlayerInfo)}, 4, [](Regs& r) {
        Rec(0x7070, {r.ecx});
        reinterpret_cast<uint32_t*>(r.args[0])[1] = 0;
        return g_newCount > 0 ? At(kItems) : 0u;
    });
    s.At({0x004573D0, AddressOf(&GameNet::Manager::NextNewPlayerInfo)}, 4, [](Regs& r) {
        uint32_t& index = reinterpret_cast<uint32_t*>(r.args[0])[1];
        Rec(0x73D0, {index});
        ++index;
        return static_cast<int>(index) < g_newCount ? At(kItems + 0x40 * index) : 0u;
    });
    Stub(s, {0x005434C0, AddressOf(&GameNet::PlayerInfo::IsPendingObserver)}, 0, 0x34C0, [](Regs&) { return g_observerPlays ? 1u : 0u; });
    Stub(s, {0x00539B00, AddressOf(&GameNet::Manager::CountPlayers)}, 0, 0x9B00, [](Regs&) { return static_cast<uint32_t>(g_numPlayers); });
    Stub(s, {0x00539C70, AddressOf(&GameNet::Manager::CountObservers)}, 0, 0x9C70, [](Regs&) { return static_cast<uint32_t>(g_numObservers); });
    Stub(s, {0x00532C50, AddressOf(&Manager::GetMaxPlayers)}, 0, 0x2C50, [](Regs&) { return static_cast<uint32_t>(g_maxPlayers); });
    Stub(s, {0x00532D60, AddressOf(&Manager::GetMaxObservers)}, 0, 0x2D60, [](Regs&) { return static_cast<uint32_t>(g_maxObservers); });
    Stub(s, {0x00532BE0, AddressOf(&Manager::GetSkillLevel)}, 0, 0x2BE0, [](Regs&) { return static_cast<uint32_t>(g_skill); });
    Stub(s, {0x00534310, AddressOf(&GameNet::Manager::GetPassword)}, 0, 0x4310, [](Regs&) { return At(String(1)); });
    Stub(s, {0x005369A0, AddressOf(&Manager::GetLevelName)}, 4, 0x69A0, [](Regs& r) { return At(String(2 + (r.args[0] & 1))); });
    Stub(s, {0x00536A40, AddressOf(&GameNet::Manager::GetGameTypeName)}, 0, 0x6A40, [](Regs&) { return At(String(4)); });
    s.At({0x00457280, AddressOf(&Net::App::SendMessageTo)}, 24, [](Regs& r) {
        auto* info = reinterpret_cast<const uint8_t*>(r.args[2]);
        g_trace.push_back(0x7280);
        g_trace.push_back(Rel(r.ecx));
        g_trace.push_back(r.args[0] & 0xFF);
        g_trace.push_back(r.args[1] & 0xFFFF);
        g_trace.push_back(r.args[3]);
        g_trace.push_back(r.args[4] & 0xFFFF);
        g_trace.push_back(r.args[5]);
        // the header, the name, level, mode, skill, time, and the names written
        g_trace.push_back(HashBytes(info, 8));
        g_trace.push_back(Hash(reinterpret_cast<const char*>(info + 0x08)));
        g_trace.push_back(Hash(reinterpret_cast<const char*>(info + 0x18)));
        g_trace.push_back(Hash(reinterpret_cast<const char*>(info + 0x38)));
        g_trace.push_back(info[0x58]);
        g_trace.push_back(*reinterpret_cast<const uint32_t*>(info + 0x5C));
        for (int i = 0; i < g_namedSlots; ++i)
            g_trace.push_back(Hash(reinterpret_cast<const char*>(info + 0x60 + 16 * i)));
        return 1u;
    });
    // Preferences.
    Stub(s, {0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, 0xC4A0, [](Regs&) { return At(Fake(0)); });
    s.At({0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             Rec(0x6C60, {r.ecx, r.args[0], r.args[2] & 0xFF});
             *reinterpret_cast<uint32_t*>(r.args[1]) = At(String(0));
             return 1u;
         });
    Stub(s, {0x0046C5D0, AddressOf(&Prefs::Preferences::SetPreference)}, 8, 0xC5D0);
    Stub(s, {0x0046C550, AddressOf(&Prefs::Preferences::GetPreferenceValue)}, 8, 0xC550, [](Regs&) { return 77u; });
    Stub(s, {0x00534F50, AddressOf(&GameNet::Manager::GetChoiceName)}, 8, 0x4F50, [](Regs& r) {
        uint32_t value = r.args[1];
        return value == 0xDEAD ? 0u : At(String(5 + (value & 3)));
    });
    Stub(s, {0x00534FC0, AddressOf(&GameNet::Manager::GetChoiceInteger)}, 12, 0x4FC0, [](Regs& r) { return r.args[1] == 0 ? 0u : r.args[1] * 3 + (r.args[2] & 0xF); });
    s.At({0x00626EFB}, 0, [](Regs& r) { // _stricmp (the game's C runtime; ours uses the DLL's: not recorded)
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    Stub(s, {0x0053CB70, AddressOf(&GameNet::Manager::DropPlayer)}, 8, 0xCB70);
    Stub(s, {0x00565790}, 4, 0x5790);
    Stub(s, {0x00558960}, 4, 0x8960);
    StubCdecl(s, {0x005388F0, AddressOf(&GameNet::CFunc_SetNumTeams)}, 0x88F0, 2);
    // (the goal count is the goal manager's first word: the original reads it through the getter folded at
    // 0x00469490, ours as the field - it's put there, not answered by a stand-in)
    StubCdecl(s, {0x005591B0, AddressOf(&Game::GetGoalManager)}, 0x91B0, 0, [](Regs&) {
        Word<uint32_t>(kMisc + 0x700) = static_cast<uint32_t>(g_goalCount);
        return At(kMisc + 0x700);
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        uint8_t data[0x60] = {};
        if (desc->m_data)
            std::memcpy(data, desc->m_data, desc->m_length < 0x60 ? desc->m_length : 0x60);
        if (desc->m_id == 0x33) { // (the game info's 5-7 and 0x4D-0x4F)
            std::memset(data + 5, 0, 3);
            std::memset(data + 0x4D, 0, 3);
        }
        if (desc->m_id == 0x49) // (the spawned script's 13-15)
            std::memset(data + 13, 0, 3);
        for (int i = 0; i < 0x60; i += 4) // (the data word by word)
            g_trace.push_back(Rel(*reinterpret_cast<uint32_t*>(data + i)));
        Rec(0x7EB0, {r.ecx, r.args[0], desc->m_id, desc->m_length,
                     static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                     desc->m_singular, static_cast<uint32_t>(desc->m_delay)});
        return 0u;
    });
    s.At({0x00458AB0, AddressOf(&Net::App::StreamMessage)}, 32, [](Regs& r) {
        Rec(0x8AB0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF,
                     HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), r.args[2] & 0xFFFF), GameNetKit::Str(r.args[4]),
                     r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    // The level change.
    Stub(s, {0x00537B30, AddressOf(&GameNet::Manager::ClearQueuedScripts)}, 0, 0x7B30);
    Stub(s, {0x00406850, AddressOf(&Tsk::BaseTask::InList)}, 0, 0x6850, [](Regs&) { return g_taskInList ? 1u : 0u; });
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return 0x00010000u; });
    StubCdecl(s, {0x004F4970, AddressOf(&Ed::CRailEditorComponent::Instance)}, 0x4970, 0, [](Regs&) { return At(kMisc + 0x500); });
    Stub(s, {0x004F7550, AddressOf(&Ed::CRailEditorComponent::Clear)}, 0, 0x7550);
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    s.At({0x005376F0, AddressOf(&GameNet::Manager::CreateNetPanelMessage)}, 0x20, [](Regs& r) {
        Rec(0x76F0, {r.ecx, r.args[0] & 0xFF, r.args[1], GameNetKit::Str(r.args[2]), GameNetKit::Str(r.args[3]), r.args[4], r.args[5],
                     r.args[6] & 0xFF, r.args[7]});
        return 0u;
    });
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_netGame ? 1u : 0u; });
    // The start of a game.
    Stub(s, {0x00581510, AddressOf(&Obj::CSkater::HideSkater)}, 0, 0x1510);
    Stub(s, {0x005814D0, AddressOf(&Obj::CSkater::Reset27C)}, 0, 0x14D0);
    Stub(s, {0x00553AD0}, 4, 0x3AD0);
    Stub(s, {0x00564D90, AddressOf(&Mdl::Skate::GetGoalManager)}, 4, 0x4D90, [](Regs&) { return At(kMisc + 0x780); });
    Stub(s, {0x00558250, AddressOf(&Game::CGoalManager::DeactivateAllGoals)}, 0, 0x8250);
    StubCdecl(s, {0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0x9070, 1, [](Regs& r) {
        switch (r.args[0]) {
        case 0x50E63314: return At(Fake(10)); // targetScoreArray
        case 0x1A2C22F9: return At(Fake(11)); // highestScoreArray
        default: return At(Fake(12));         // the game type infos
        }
    });
    StubCdecl(s, {0x00478A60, AddressOf(&Script::CopyArray)}, 0x8A60, 2, [](Regs& r) {
        auto* dest = reinterpret_cast<CArray*>(r.args[0]);
        dest->m_size = 1;
        dest->m_single = At(Fake(20));
        return 0u;
    });
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        auto* a = reinterpret_cast<CArray*>(r.ecx);
        return a->m_size == 1 ? a->m_single : a->m_elements[r.args[0]];
    });
    Stub(s, {0x00478C80, 0x00478B00, AddressOf(static_cast<void (CStruct::*)(uint32_t, CArray*)>(&CStruct::AddArray))}, 8,
         0x8C80);
    // (0x005430D0, the setter of +0x1D4 the original calls on the manager - its last leading team -, isn't stood in:
    // ours writes the field itself)
    for (uint32_t a : {0x00564D50u, 0x00553BE0u, 0x005535A0u, 0x005530A0u, 0x00553080u})
        Stub(s, {a}, 4, a & 0xFFFF);
    const std::pair<uint32_t, uint32_t> noArgs[] = {{0x00566A60u, 0}, {0x00537620u, AddressOf(&GameNet::Manager::ResetPlayers)},
                                                    {0x00543100u, AddressOf(&GameNet::PlayerInfo::ClearLastHitTime)},
                                                    {0x00564A10u, 0}};
    for (auto [a, ours] : noArgs) {
        if (ours)
            Stub(s, {a, ours}, 0, a & 0xFFFF);
        else
            Stub(s, {a}, 0, a & 0xFFFF);
    }
    Stub(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, 0xDB90, [](Regs& r) {
        // (skater 2's has nothing at +0x18; the original doesn't check for no component)
        return r.ecx == At(kSkaters + 0x200) ? At(kMisc + 0x8E0) : At(kMisc + 0x880);
    });
    Stub(s, {0x00544340, AddressOf(&GameNet::PlayerInfo::MarkAsKing)}, 4, 0x4340);
    auto answer = [](Regs& r) {
        auto it = g_answer.find({r.ecx - g_base, r.args[0]});
        if (it == g_answer.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476EC0, 0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [answer](Regs& r) {
             Rec(0x6EC0, {r.ecx, r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2] & 0xFF});
             return answer(r);
         });
    // The game's state.
    Stub(s, {0x00553270}, 0, 0x3270, [](Regs&) { return 0x3270AAAAu; });
    Stub(s, {0x005533B0}, 0, 0x33B0, [](Regs&) { return 123456u; });
    Stub(s, {0x00553170, AddressOf(&Game::CGameMode::GetTimeLimit)}, 0, 0x3170, [](Regs&) { return 0x3170BBBBu; });
    Stub(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x00553130}, 0, 0x3130, [](Regs&) { return 3u; });
    Stub(s, {0x00534F40, AddressOf(&GameNet::Manager::GetLevel)}, 0, 0x4F40, [](Regs&) { return 0x4F40CCCCu; });
    Stub(s, {0x00535B20, AddressOf(&GameNet::Manager::GetGoalsPreference)}, 0, 0x5B20, [](Regs&) { return 0x5B20DDDDu; });
    Stub(s, {0x00535AD0, AddressOf(&GameNet::Manager::GetTeamGraffitiMode)}, 0, 0x5AD0, [](Regs&) { return 0x5AD0EEEEu; });
    Stub(s, {0x00537140, AddressOf(&GameNet::Manager::SetReadyToPlay)}, 4, 0x7140);
    Stub(s, {0x0056B990}, 4, 0xB990);
    s.At({0x00588520, AddressOf(&Obj::CTrickObjectManager::WriteGraffitiState)}, 4, [](Regs& r) { // (the buffer: a local in the original - not recorded)
        Rec(0x8520, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.args[0]), 0x5C, 0x30);
        return 0x30u;
    });
    Stub(s, {0x00557FB0, AddressOf(&Game::CGoalManager::GetGoalByIndex)}, 4, 0x7FB0, [](Regs& r) { return At(kGoals + 0x40 * r.args[0]); });
    Stub(s, {0x005542F0}, 0, 0x42F0, [](Regs& r) { return ((r.ecx - At(kGoals)) / 0x40) % 2 == 1 ? 1u : 0u; });
    Stub(s, {0x00554280}, 0, 0x4280, [](Regs& r) { return 0x60A10000u + (r.ecx - At(kGoals)) / 0x40; });
    Stub(s, {0x00557F90}, 8, 0x7F90, [](Regs& r) { return At(kGoals + 0x40 * (r.args[0] & 7)); });
    g_goalVtable[3] = s.Thunk(0, [](Regs& r) {
        Rec(0x60A3, {r.ecx});
        return r.ecx == At(kGoals + 0x40 * (g_runningGoal & 7)) ? 1u : 0u;
    });
    Stub(s, {0x0053A1B0, AddressOf(&GameNet::Manager::SendFacesTo)}, 4, 0xA1B0);
    Stub(s, {0x00539DA0, AddressOf(&GameNet::Manager::SendTagTexturesTo)}, 4, 0x9DA0);
    StubCdecl(s, {0x004E8470, AddressOf(&Ed::CGoalEditorComponent::Get)}, 0x8470, 0, [](Regs&) { return At(kMisc + 0x600); });
    Stub(s, {0x004E7E50, AddressOf(&Ed::CGoalEditorComponent::CountCompleteGoals)}, 4, 0x7E50, [](Regs&) { return g_answers[0x7E50]; });
    Stub(s, {0x004EBB70, AddressOf(&Ed::CGoalEditorComponent::WriteLevelGoalsToBuffer)}, 12, 0xBB70, [](Regs&) { return 0x30u; });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_answer.clear();
        g_sender = PlayerAt(2);
        g_host = PlayerAt(0);
        g_local = PlayerAt(0);
        g_king = 0;
        g_playerCount = 4;
        g_newCount = 0;
        g_teamGame = g_netGame = g_taskInList = g_observerPlays = false;
        g_gameType = 0;
        g_goalCount = 0;
        g_runningGoal = 9;
        g_numPlayers = 3;
        g_numObservers = 1;
        g_maxPlayers = 8;
        g_maxObservers = 2;
        g_skill = 2;
        g_namedSlots = 0;
        GameVariable<uint32_t>(0x007CCDE8) = 0;
        for (int i = 0; i < 8; ++i) {
            Word<uint32_t>(PlayerAt(i) + 0x18) = At(kMisc + 0xB00 + 0x40 * static_cast<uint32_t>(i));
            Word<int>(kMisc + 0xB00 + 0x40 * static_cast<uint32_t>(i) + 0x3C) = 0x20 + i;
            Word<int>(PlayerAt(i) + 0x30) = i % 2;
        }
        Word<uint32_t>(PlayerAt(0) + 0x1D0) = 0x00000001;
        Word<uint32_t>(PlayerAt(1) + 0x1D0) = 0x00000004; // an observer
        Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0;
        Word<uint32_t>(PlayerAt(3) + 0x1D0) = 0x00000080;
        for (int i = 0; i < 6; ++i)
            std::snprintf(&Word<char>(String(i)), 0x40, "String number %d", i);
        Word<char>(String(1)) = 0; // no password
        for (uint32_t g = 0; g < 8; ++g)
            Word<void**>(kGoals + 0x40 * g) = g_goalVtable;
        Word<uint32_t>(kManager + 0x04) = At(kApp + 0x400); // the server app
        Word<uint32_t>(kManager + 0x18) = At(kMisc + 0x940); // the GameSpy presence
        Word<uint32_t>(kConn + 0x40) = 0x0A00002A;
        Word<uint16_t>(kConn + 0x48) = 0x1421;
        GameVariable<uint32_t>(0x006EFDD8) = At(kMisc + 0x800 - 0x128 + 0x60); // (Net::Manager: its +0x128 at kMisc+0x860)
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

void FindServer(bool original)
{
    // Not 12 bytes; then the counts (max below the count), a password, LAN / internet logged in / not, observers
    // (playing or not), new players (some already players, some observing).
    Word<uint32_t>(kMsg) = 0x7777AAAA;
    Handle(original, 0x0052DD40, GameNet::HandleFindServer, 0x11, 8);
    g_maxPlayers = 2;
    g_maxObservers = 0;
    Word<uint8_t>(kManager) = 0;
    Word<uint8_t>(kMisc + 0x940 + 0x10) = 1; // logged in, but not on the internet
    g_namedSlots = 3;
    Handle(original, 0x0052DD40, GameNet::HandleFindServer, 0x11, 12);
    Word<char>(String(1)) = 'p';
    Word<uint8_t>(kManager) = 4;
    Word<uint8_t>(kMisc + 0x940 + 0x10) = 1;
    g_gameType = 0x1C471C60;
    g_observerPlays = true;
    std::memcpy(&Word<char>(kItems + 0x00), "NewOne", 7);
    Word<uint8_t>(kItems + 0x10) = 0x12; // already a player's id
    Word<uint32_t>(kItems + 0x20) = 0x10;
    std::memcpy(&Word<char>(kItems + 0x40), "NewTwo", 7);
    Word<uint8_t>(kItems + 0x50) = 0x19;
    Word<uint32_t>(kItems + 0x60) = 0x14; // observing, but flags 0x10: listed
    std::memcpy(&Word<char>(kItems + 0x80), "NewThree", 9);
    Word<uint8_t>(kItems + 0x90) = 0x1A;
    Word<uint32_t>(kItems + 0xA0) = 0x00;
    std::memcpy(&Word<char>(kItems + 0xC0), "NewFour", 8);
    Word<uint8_t>(kItems + 0xD0) = 0x1B;
    Word<uint32_t>(kItems + 0xE0) = 0x04; // observing: not listed
    g_newCount = 4;
    g_namedSlots = 6;
    Handle(original, 0x0052DD40, GameNet::HandleFindServer, 0x11, 12);
    Word<uint8_t>(kMisc + 0x940 + 0x10) = 0;
    g_observerPlays = false;
    std::memcpy(&Word<char>(PlayerAt(2) + 0x38), "A very long player name", 24);
    g_namedSlots = 5;
    Handle(original, 0x0052DD40, GameNet::HandleFindServer, 0x11, 12);
}

void HostOptions(bool original)
{
    auto handle = [&](uint8_t id) { Handle(original, 0x005301A0, GameNet::HandleHostOptions, id); };
    // Not the host's connection; no host.
    Word<uint32_t>(PlayerAt(0) + 0x18) = At(kMisc + 0xF00);
    handle(0x60);
    g_host = 0;
    handle(0x60);
    g_host = PlayerAt(0);
    Word<uint32_t>(PlayerAt(0) + 0x18) = At(kConn);
    // The options: every value, then the score falling back to captures, then to times.
    for (uint32_t i = 0; i < 0x34; i += 4)
        Word<uint32_t>(kMsg + i) = 0x100 + i;
    handle(0x60);
    Word<uint32_t>(kMsg + 0x1C) = 0xDEAD; // (no target score text)
    handle(0x60);
    Word<uint32_t>(kMsg + 0x1C) = 0; // (a score of 0)
    handle(0x60);
    // Kick / ban: the second player named right, the wrong name, past the end.
    Message({1, 1});
    std::memcpy(&Word<char>(kMsg + 2), "PLAYER1", 8);
    handle(0x61);
    Message({0, 2});
    std::memcpy(&Word<char>(kMsg + 2), "player2", 8);
    handle(0x61);
    std::memcpy(&Word<char>(kMsg + 2), "player3", 8);
    handle(0x61);
    Message({0, 9});
    handle(0x61);
    Word<uint32_t>(kMsg) = 0x0BADC0DE;
    handle(0x62);
    handle(0x63);
    handle(0x64);
    handle(0x65);
    for (uint8_t teams : std::initializer_list<uint8_t>{0, 1, 2, 3, 4, 5, 0xFF}) {
        Message({teams});
        handle(0x66);
    }
    handle(0x67);
}

void ChangeLevel(bool original)
{
    auto handle = [&]() {
        Word<uint8_t>(kManager + 0x3A) = 1;
        Handle(original, 0x005311F0, GameNet::HandleChangeLevel, 0x46);
        Rec(0xCDE8, {GameVariable<uint32_t>(0x007CCDE8), Word<uint32_t>(kManager + 0x34)});
    };
    Word<uint32_t>(kManager + 0xEC) = At(kMisc + 0x480);
    Word<uint32_t>(kMsg) = 0x11112222;
    Word<uint8_t>(kMsg + 4) = 0;
    handle();
    Word<uint8_t>(kMsg + 4) = 1;
    g_taskInList = true;
    Word<uint8_t>(kManager) = 0; // client
    handle();
    Word<uint8_t>(kManager) = 1; // server
    Word<uint32_t>(kMsg) = 0x55556666;
    handle();
    g_taskInList = false;
    handle();
    Word<uint32_t>(kMsg) = 0xB664035D; // the created park
    handle();
    Word<uint8_t>(kManager) = 0;
    handle(); // (the created park again)
    Word<uint32_t>(kMsg) = 0x33334444; // leaving the created park
    handle();
    handle();
}

void StartGame(bool original)
{
    // The type infos: three structures, the third this mode's (with / without a goal script).
    auto* types = &Word<CArray>(Fake(12));
    types->m_size = 3;
    types->m_elements = &Word<uint32_t>(Fake(13));
    for (int i = 0; i < 3; ++i)
        Word<uint32_t>(Fake(13) + 4 * static_cast<uint32_t>(i)) = At(Fake(14 + i));
    g_answer[{Fake(14), 0x21902065}] = 0x11111111;
    g_answer[{Fake(16), 0x21902065}] = 0x6C5FF266;
    g_answer[{Fake(16), 0x79F90D81}] = 0x90A15C21;
    Word<uint32_t>(kMisc + 0x880 + 0x18) = At(kMisc + 0x8C0);  // the score component's +0x18
    struct Case {
        uint32_t mode, time;
        bool net;
    };
    const Case cases[] = {{0x6C5FF266, 0, true},  {0x6C5FF266, 300, false}, {0xEC200EAA, 0, true},
                          {0x3D6D444F, 0, true},  {0xBFF33600, 0, false},   {0x12345678, 0, true},
                          {0x11111111, 60, true}};
    for (const Case& c : cases) {
        std::memset(&Word<uint8_t>(kMsg), 0, 0x50);
        Word<uint32_t>(kMsg + 0x00) = c.mode;
        Word<uint32_t>(kMsg + 0x04) = c.time;
        Word<uint32_t>(kMsg + 0x08) = 2500;
        Word<uint32_t>(kMsg + 0x0C) = 3;
        Word<uint8_t>(kMsg + 0x14) = 0xFF;
        Word<uint8_t>(kMsg + 0x15) = 1;
        Word<uint8_t>(kMsg + 0x16) = 0x9C;
        for (uint32_t i = 0x18; i < 0x48; i += 4)
            Word<uint32_t>(kMsg + i) = 0x5000 + i;
        Word<uint8_t>(kMsg + 0x24) = 0xF9;
        g_netGame = c.net;
        g_king = c.time == 300 ? PlayerAt(3) : 0;
        if (c.mode == 0x11111111)
            g_answer.erase({Fake(16), 0x21902065}); // (no checksum: compares the previous one)
        Handle(original, 0x005313C0, GameNet::HandleStartGame, 0x34);
    }
}

void GameState(bool original)
{
    auto handle = [&]() { Handle(original, 0x00532460, GameNet::HandleSendGameState, 0x54); };
    // The spawned scripts' list at +0x150: two items.
    auto node = [](int i) { return kNodes + 0x20 * static_cast<uint32_t>(i); };
    Word<uint32_t>(node(0) + 4) = 0xFFFFFFFF;
    for (int i = 1; i <= 2; ++i) {
        Word<uint32_t>(node(i) + 4) = At(kItems + 0x100 + 0x40 * static_cast<uint32_t>(i));
        Word<uint32_t>(node(i) + 0xC) = At(node(i == 2 ? 0 : i + 1));
        for (uint32_t f = 0x14; f <= 0x1C; f += 4)
            Word<uint32_t>(kItems + 0x100 + 0x40 * static_cast<uint32_t>(i) + f) = 0x49000000 + f + 0x100 * i;
    }
    Word<uint32_t>(kManager + 0x150) = At(node(1));
    Word<uint32_t>(kMisc + 0x860) = 1; // the connection type: LAN
    // Unknown; told already; the local player; a remote one (game over / scavenger hunt variants / elimiskate /
    // graffiti observer, created goals, a king, selected goals).
    g_sender = 0;
    handle();
    g_sender = PlayerAt(2);
    Word<uint32_t>(PlayerAt(2) + 0x1D0) = 0x40;
    handle();
    g_sender = PlayerAt(0);
    handle();
    struct Case {
        uint32_t sender, type, running;
        bool over, goals;
    };
    const Case cases[] = {{2, 0x11111111, 9, true, false}, {4, 0xFC13B531, 0x1F3430CE, false, true},
                          {5, 0xFC13B531, 0x8A6B04AD, false, false}, {6, 0xFC13B531, 0xD4F04E7F, false, false},
                          {7, 0xFC13B531, 9, false, false}, {2, 0x10F84276, 0xF6198005, false, false},
                          {1, 0x5E2EA50C, 9, false, false}};
    for (const Case& c : cases) {
        g_sender = PlayerAt(static_cast<int>(c.sender));
        Word<uint32_t>(PlayerAt(static_cast<int>(c.sender)) + 0x1D0) &= ~0x40u;
        g_gameType = c.type;
        g_runningGoal = c.running;
        Word<uint8_t>(kManager + 0x3A) = c.over ? 1 : 0;
        g_answers[0x7E50] = c.goals ? 2 : 0;
        g_goalCount = c.goals ? 5 : 0;
        g_king = c.goals ? PlayerAt(3) : 0;
        g_netGame = c.goals;
        Word<uint32_t>(kMisc + 0x860) = c.goals ? 2 : 1;
        handle();
    }
}

void TestGameNetSessionHandlers()
{
    SaveGlobals({{0x006EFDD8, 4, {}}, {0x007CCDE8, 4, {}}});
    RunCase("GameNet::HandleFindServer", FindServer);
    RunCase("GameNet::HandleHostOptions", HostOptions);
    RunCase("GameNet::HandleChangeLevel", ChangeLevel);
    RunCase("GameNet::HandleStartGame", StartGame);
    RunCase("GameNet::HandleSendGameState", GameState);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestGameNetSessionHandlers, GameNetKit::Cleanup);
