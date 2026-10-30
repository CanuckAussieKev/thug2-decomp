// Self-tests for the network game's course and its players' lives (src/game/net/gamenet_flow.cpp, the player life in
// gamenet_players.cpp): back to the lobby, the start of a game, a join cancelled, the manager's tasks, new / destroyed
// / dropped players, the pending players added back. The original and ours on the same stand-in arena
// (net2_testkit.h) with the manager's real player lists; the calls and the arenas compared.

#include <cstdint>
#include <cstring>
#include <map>

#include "game/core/crc.h"
#include "game/front/screen_element_manager.h"
#include "game/mth/random.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/net/stats_man.h"
#include "game/obj/game_objects.h"
#include "game/obj/smtptr.h"
#include "game/obj/trick_objects.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/skater/walk_camera.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater_camera_component.h"
#include "hook/log.h"
#include "hook/net2_testkit.h"

namespace {

using namespace Net2Kit;
using GameNet::Manager;

Manager* TheManager() { return &Word<Manager>(kManager); }

// Stand-in answers.
bool g_inNetGame;
bool g_untimed;
uint32_t g_gameType;
uint32_t g_king;          // the king of the hill (arena offset, 0 none)
uint32_t g_nextPlayer;    // 0x0053A580's (arena offset, 0 none)
uint32_t g_fcfsHost;      // 0x005373D0's
uint32_t g_element;       // the current menu (0 none)
int g_loading;            // 0x00537530
bool g_allReady;          // 0x00538DC0
int g_numPlayers;         // 0x00539B00
bool g_peerConnected;
bool g_statsOn;
std::vector<int> g_rnd;   // Mth::Rnd's answers in turn
size_t g_rndAt;
std::map<uint32_t, uint32_t> g_prefValues;    // GetPreferenceValue / GetPreferenceChecksum by name
std::map<uint32_t, uint32_t> g_prefStructs;   // GetPreference by name: an arena structure

constexpr uint32_t kLobby = kFree + 0x1000; // the lobby (+0x139 hosting, +0x140 its peer)
constexpr uint32_t kBuddy = kFree + 0x1200; // the presence (+0x10 logged in)
constexpr uint32_t kStats = kFree + 0x1300;
constexpr uint32_t kConns = kFree + 0x400;  // player i's connection at +0x40 * i
constexpr uint32_t kTasks = kFree + 0x1400; // the task (+0x1C the manager)
constexpr uint32_t kProfile = kFree + 0x1500;
constexpr uint32_t kSplitPrefs = kFree + 0x1600;

uint32_t ConnAt(int i) { return kConns + 0x40 * static_cast<uint32_t>(i); }

void Install(StandIns& s)
{
    auto answer = [](bool& value) { return [&value](Regs&) { return value ? 1u : 0u; }; };
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, answer(g_inNetGame));
    Stub(s, {0x00532FE0, AddressOf(&Manager::IsUntimedGame)}, 0, 0x2FE0, answer(g_untimed));
    Stub(s, {0x00532CF0, AddressOf(&Manager::GetNumTeams)}, 0, 0x2CF0, [](Regs&) { return 3u; });
    Stub(s, {0x005330B0, AddressOf(&Manager::GetFireballLevel)}, 0, 0x30B0, [](Regs&) { return 4u; });
    StubCdecl(s, {0x00403DE0, AddressOf(&Mth::Rnd)}, 0x3DE0, 1, [](Regs&) {
        return static_cast<uint32_t>(g_rndAt < g_rnd.size() ? g_rnd[g_rndAt++] : 7);
    });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Rec(0xC4A0, {r.ecx, r.args[0]});
        auto it = g_prefStructs.find(r.args[0]);
        return it == g_prefStructs.end() ? At(kStructs + 0x380) : At(it->second);
    });
    s.At({0x0046C550, AddressOf(&Prefs::Preferences::GetPreferenceValue)}, 8, [](Regs& r) {
        Rec(0xC550, {r.ecx, r.args[0], r.args[1]});
        return g_prefValues[r.args[0] ^ r.args[1]];
    });
    s.At({0x0046C590, AddressOf(&Prefs::Preferences::GetPreferenceChecksum)}, 8, [](Regs& r) {
        Rec(0xC590, {r.ecx, r.args[0], r.args[1]});
        return g_prefValues[r.args[0] ^ r.args[1]];
    });
    Stub(s, {0x00564D80, AddressOf(&Mdl::Skate::GetSplitScreenPreferences)}, 0, 0x4D80, [](Regs&) { return At(kSplitPrefs); });
    // The game mode.
    Stub(s, {0x00553130, AddressOf(&Game::CGameMode::NumTeams)}, 0, 0x3130, [](Regs&) { return 2u; });
    Stub(s, {0x00553AD0, AddressOf(&Game::CGameMode::LoadGameType)}, 4, 0x3AD0);
    Stub(s, {0x005535A0, AddressOf(&Game::CGameMode::SetNumTeams)}, 4, 0x35A0);
    Stub(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x00553220, AddressOf(&Game::CGameMode::IsTrue)}, 4, 0x3220, [](Regs& r) { return r.args[0] == 0x11941568 ? 1u : 0u; });
    // The manager's own calls not rewritten here.
    Stub(s, {0x00537620, AddressOf(&GameNet::Manager::ResetPlayers)}, 0, 0x7620);
    Stub(s, {0x00537480, AddressOf(&GameNet::Manager::GetKingOfTheHill)}, 0, 0x7480, [](Regs&) { return g_king ? At(g_king) : 0u; });
    Stub(s, {0x00544340, AddressOf(&GameNet::PlayerInfo::MarkAsKing)}, 4, 0x4340);
    Stub(s, {0x00537B30, AddressOf(&GameNet::Manager::ClearQueuedScripts)}, 0, 0x7B30);
    Stub(s, {0x00536820, AddressOf(&GameNet::Manager::RemoveAllNewPlayers)}, 0, 0x6820);
    Stub(s, {0x005359F0, AddressOf(&GameNet::Manager::ChooseStartPoints)}, 0, 0x59F0);
    Stub(s, {0x00535AD0, AddressOf(&GameNet::Manager::GetTeamGraffitiMode)}, 0, 0x5AD0, [](Regs&) { return 0x5AD05AD0u; });
    Stub(s, {0x005399F0, AddressOf(&GameNet::Manager::DisconnectClients)}, 0, 0x99F0);
    Stub(s, {0x0053A580, AddressOf(&GameNet::Manager::NextCamPlayer)}, 0, 0xA580, [](Regs&) { return g_nextPlayer ? At(g_nextPlayer) : 0u; });
    Stub(s, {0x005373D0, AddressOf(&GameNet::Manager::GetServerPlayer)}, 0, 0x73D0, [](Regs&) { return g_fcfsHost ? At(g_fcfsHost) : 0u; });
    Stub(s, {0x00537530, AddressOf(&GameNet::Manager::CountOtherPlayers)}, 0, 0x7530, [](Regs&) { return static_cast<uint32_t>(g_loading); });
    StubCdecl(s, {0x00538DC0, AddressOf(&GameNet::CFunc_AllPlayersAreReady)}, 0x8DC0, 2, [](Regs&) { return g_allReady ? 1u : 0u; });
    Stub(s, {0x00539B00, AddressOf(&GameNet::Manager::CountPlayers)}, 0, 0x9B00, [](Regs&) { return static_cast<uint32_t>(g_numPlayers); });
    // The deferred new player (a NewPlayerInfo on the caller's stack, `frames`): its fields recorded.
    s.At({0x0053A310, AddressOf(&GameNet::Manager::AddJoiningPlayer)}, 8, [](Regs& r) {
        const auto* info = reinterpret_cast<const uint8_t*>(r.args[0]);
        auto word = [info](uint32_t offset) { return *reinterpret_cast<const uint32_t*>(info + offset); };
        Rec(0xA310, {r.ecx, r.args[1], Hash(reinterpret_cast<const char*>(info)), info[0x10], word(0x14), word(0x1C),
                     word(0x20), word(0x2C), word(0x30), word(0x34), word(0x38), word(0x3C), word(0x40)});
        return 0u;
    });
    // The panel message (broadcast, text, string 1, string 2, object, properties, important, time): strings hashed.
    s.At({0x005376F0, AddressOf(&GameNet::Manager::CreateNetPanelMessage)}, 0x20, [](Regs& r) {
        Rec(0x76F0, {r.ecx, r.args[0] & 0xFF, r.args[1], GameNetKit::Str(r.args[2]), GameNetKit::Str(r.args[3]), r.args[4], GameNetKit::Str(r.args[5]),
                     r.args[6] & 0xFF, r.args[7]});
        return 0u;
    });
    // The new / joining players' constructors and destructor.
    Stub(s, {0x00543740, AddressOf(&GameNet::PlayerInfo::Construct)}, 4, 0x3740, [](Regs& r) {
        Word<uint32_t>(r.ecx - g_base + 0x1D0) = r.args[0];
        Word<uint32_t>(r.ecx - g_base + 0x1EC) = 0;
        return r.ecx;
    });
    s.At({0x005434D0, AddressOf(&GameNet::NewPlayerInfo::Construct)}, 0, [](Regs& r) { // (on the stack: not recorded)
        g_trace.push_back(0x34D0);
        reinterpret_cast<uint32_t*>(r.ecx)[0x1C / 4] = At(kProfile);
        return r.ecx;
    });
    s.At({0x00543560, AddressOf(&GameNet::NewPlayerInfo::DestroyBody)}, 0, [](Regs&) {
        g_trace.push_back(0x3560);
        return 0u;
    });
    Stub(s, {0x00585ED0, AddressOf(&Obj::CSkaterProfile::CopyFrom)}, 4, 0x5ED0);
    // The stats, the presence, GameSpy.
    Stub(s, {0x0054C9B0, AddressOf(&GameNet::StatsMan::UpdateLoggedIn)}, 0, 0xC9B0, answer(g_statsOn));
    Stub(s, {0x0054CE80, AddressOf(&GameNet::StatsMan::StartGame)}, 0, 0xCE80);
    Stub(s, {0x0054C9F0, AddressOf(&GameNet::StatsMan::RemovePlayer)}, 4, 0xC9F0);
    Stub(s, {0x00548320, AddressOf(&GameNet::BuddyMan::SetStatus)}, 12, 0x8320, [](Regs& r) {
        Rec(0x8321, {GameNetKit::Str(r.args[1]), GameNetKit::Str(r.args[2])});
        return 0u;
    });
    StubCdecl(s, {0x005FC730}, 0xC730, 1, answer(g_peerConnected));
    StubCdecl(s, {0x005FCCF0}, 0xCCF0, 1);
    StubCdecl(s, {0x005FBAD0}, 0xBAD0, 1);
    StubCdecl(s, {0x005F9860}, 0x9860, 2);
    // Skaters, the skate module, the crown, trick objects.
    Stub(s, {0x00581510, AddressOf(&Obj::CSkater::HideSkater)}, 0, 0x1510);
    Stub(s, {0x005814D0, AddressOf(&Obj::CSkater::Reset27C)}, 0, 0x14D0);
    Stub(s, {0x0057FEC0, AddressOf(&Obj::CSkater::GetSkaterNumber)}, 0, 0xFEC0, [](Regs& r) { return (r.ecx - At(kSkaters)) / 0x100 % 2; });
    Stub(s, {0x00565790}, 4, 0x5790);
    Stub(s, {0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, 0x6520, [](Regs&) { return At(kSkaters + 0x700); });
    Stub(s, {0x005782D0, AddressOf(&Obj::CCrownObject::TakeOff)}, 0, 0x82D0);
    Stub(s, {0x00589230, AddressOf(&Obj::CTrickObjectManager::ReleasePlayerClusters)}, 4, 0x9230);
    // Apps and connections.
    Stub(s, {0x00458CA0, AddressOf(&Net::App::SendData)}, 4, 0x8CA0);
    Stub(s, {0x00457280, AddressOf(&Net::App::SendMessageTo)}, 24, 0x7280);
    Stub(s, {0x00457400, AddressOf(&Net::App::BanConnection)}, 4, 0x7400);
    Stub(s, {0x00455C10, AddressOf(&Net::Conn::Invalidate)}, 0, 0x5C10);
    Stub(s, {0x00456770, AddressOf(&Net::Conn::DestroyMessageQueues)}, 0, 0x6770);
    // The screen elements.
    s.At({0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 12, [](Regs& r) {
        Rec(0xAE20, {r.args[1], r.args[2]});
        reinterpret_cast<uint32_t*>(r.args[0])[0] = g_element ? At(g_element) : 0u;
        return r.args[0];
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x37E0, {r.ecx}); // (the pointer itself is on the stack)
        return 0u;
    });
    // The players' deleting destructor (vtable slot 0).
    void* destroy = s.Thunk(4, [](Regs& r) {
        Rec(0xDE57, {r.ecx, r.args[0]});
        return 0u;
    });
    Word<uint32_t>(kVtables + 0x2C0) = reinterpret_cast<uint32_t>(destroy);
    RecordingObject(s, 0, {{7, 4}});
    RecordingObject(s, 1, {{7, 4}});
    Word<uint32_t>(kObjects + 0x40 * 6 + 0x2C) = At(kFree + 0x1700); // a skater's physics control's state
    Stub(s, {0x004FA3C0, AddressOf(&Obj::CSkaterCameraComponent::SetSkater)}, 4, 0xA3C0);
    Stub(s, {0x00524C50, AddressOf(&Obj::CWalkCameraComponent::SetSkater)}, 4, 0x4C50);
    Stub(s, {0x00524C30, AddressOf(&Obj::CWalkCameraComponent::ResetNow)}, 0, 0x4C30);
}

void Prepare(const std::vector<int>& list, uint8_t managerFlags)
{
    g_inNetGame = true;
    g_untimed = false;
    g_gameType = 0x1C471C60;
    g_king = g_nextPlayer = g_fcfsHost = g_element = 0;
    g_loading = 0;
    g_allReady = false;
    g_numPlayers = 2;
    g_peerConnected = true;
    g_statsOn = true;
    g_rnd.clear();
    g_rndAt = 0;
    g_prefValues.clear();
    g_prefStructs.clear();
    for (int i = 0; i < 8; ++i) {
        uint32_t player = PlayerAt(i);
        Word<uint32_t>(player + 0) = At(kVtables + 0x2C0);
        Word<uint32_t>(player + 0x18) = At(ConnAt(i));
        Word<int>(ConnAt(i) + 0x3C) = 0x31 + i;
        Word<uint32_t>(ConnAt(i) + 0x40) = 0x0A000001u + static_cast<uint32_t>(i);
        Word<uint32_t>(ConnAt(i) + 0x44) = 0x100 + static_cast<uint32_t>(i);
        Word<uint16_t>(ConnAt(i) + 0x48) = static_cast<uint16_t>(5154 + i);
        Word<uint32_t>(player + 0x28) = 0x2800 + static_cast<uint32_t>(i);
        Word<uint32_t>(player + 0x2C) = 0x2C00 + static_cast<uint32_t>(i);
        Word<uint32_t>(player + 0x48) = 0x4800 + static_cast<uint32_t>(i);
        Word<uint32_t>(player + 0x4C) = 0x4C00 + static_cast<uint32_t>(i);
        Word<uint32_t>(kSkaters + 0x100 * static_cast<uint32_t>(i) + 0x14) = static_cast<uint32_t>(i);
        PlayerFlags(i) = 0;
    }
    SetPlayers(list);
    SetNewPlayers({});
    ArenaStruct(kStructs + 0x300).Checksum(Crc::GenerateCRCFromString("Checksum"), 0xFC5F0004u);
    g_prefStructs[0x043EE978] = kStructs + 0x300; // (first come, first served's text)
    Word<uint8_t>(kManager) = managerFlags;
    Word<uint32_t>(kManager + 0x04) = At(kApp);
    Word<uint32_t>(kManager + 0x08) = At(kApp + 0x400);
    Word<uint32_t>(kManager + 0x10) = At(kLobby);
    Word<uint32_t>(kManager + 0x18) = At(kBuddy);
    Word<uint32_t>(kManager + 0x1C) = At(kStats);
    Word<uint32_t>(kManager + 0x6C) = 0; // no crown
    Word<uint32_t>(kManager + 0xDC) = At(kMisc + 0x480);
    Word<uint32_t>(kManager + 0xE4) = At(kMisc + 0x4A0);
    Word<uint32_t>(kManager + 0xE8) = At(kMisc + 0x4C0);
    Word<uint32_t>(kManager + 0x1CC) = 0;
    Word<uint32_t>(kLobby + 0x140) = At(kLobby + 0x180); // the peer
    Word<uint8_t>(kLobby + 0x139) = 1;
    std::memcpy(&Word<char>(kLobby + 0x34), "Lobby", 6);
    Word<uint8_t>(kBuddy + 0x10) = 1;
    Word<uint32_t>(kTasks + 0x1C) = At(kManager);
    GameVariable<uint8_t>(0x007CCA82) = 1;
    GameVariable<uint8_t>(0x007CCE1C) = 1;
    GameVariable<uint8_t>(0x007CCE1D) = 0;
    GameVariable<uint8_t>(0x007CCE2C) = 0;
    GameVariable<uint32_t>(0x007CCDF4) = 0x1000;
    GameVariable<uint32_t>(0x007CE500) = At(kMisc + 0x200);
    GameVariable<uint8_t>(0x007CE4FD) = 1;
    Word<uint32_t>(kMisc + 0x800 + 0x12C) = 0xBEE0012Cu; // the network heap
    Word<uint32_t>(kMisc + 0x800 + 0x5D4) = 0xBEE005D4u; // the skater info heap
}

using Method = void(__thiscall*)(Manager*);

void Lobby(bool original)
{
    Manager* m = TheManager();
    for (int server = 1; server >= 0; --server) {
        Prepare({0, 1, 2}, static_cast<uint8_t>(server));
        PlayerFlags(1) = 0x7804;
        g_king = PlayerAt(2);
        ArenaStruct(kStructs).Checksum(0x21902065, 0x1E7E1000u + server);
        g_prefStructs[0x651533EC] = kStructs;
        original ? Original<Method>(0x0053AF90)(m) : m->ResetToLobby();
    }
    g_king = 0;
    original ? Original<Method>(0x0053AF90)(m) : m->ResetToLobby();
}

void StartGame(bool original)
{
    Manager* m = TheManager();
    struct Case {
        uint32_t type;
        bool netGame;
        uint32_t ctfType;
    };
    const Case cases[] = {
        {0x9D65D0E7, true, 0},           // horse: its own time limit
        {0x6C5FF266, true, 0x938AB24B},  // timed capture the flag
        {0x6C5FF266, true, 0x12345678},  // another capture the flag (no time limit)
        {0x6EF8FDA0, true, 0},           // netking: no time limit
        {0xBFF33600, false, 0},          // split screen (first player only)
        {0x11223344, true, 0},
    };
    for (const Case& c : cases) {
        Prepare({0, 1, 2, 3}, 1);
        g_inNetGame = c.netGame;
        g_untimed = c.type == 0x6C5FF266;
        PlayerFlags(1) = 0x8800;
        PlayerFlags(2) = 0x0004;
        g_king = c.type == 0x6EF8FDA0 ? PlayerAt(3) : 0;
        g_statsOn = c.netGame;
        ArenaStruct(kStructs).Checksum(0x21902065, c.type);
        ArenaStruct(kStructs + 0x80).Checksum(0x21902065, 0x1E7E1001u);
        ArenaStruct(kStructs + 0x100).Checksum(Crc::GenerateCRCFromString("checksum"), 0x60A1500u);
        g_prefStructs[0x9834C4FA] = kStructs;
        g_prefStructs[0x651533EC] = kStructs + 0x80;
        g_prefStructs[0x38DBE1D0] = kStructs + 0x100;
        g_prefValues[0x906B67BA ^ 0xC146142E] = 300;
        g_prefValues[0x906B67BA ^ 0xC51BF6E3] = 120;
        g_prefValues[0x71CD53A2 ^ 0xCD66C8AE] = 5000;
        g_prefValues[0x15421B73 ^ 0x21902065] = c.ctfType;
        g_prefValues[0x162E8C3A ^ 0x21902065] = c.netGame ? 0xF81BC89B : 0;
        Word<uint8_t>(kManager + 0x39) = 8;
        g_rnd = {7, 7, 3}; // a repeat of the last game's id (8) first
        Word<uint8_t>(kManager + 0x68) = 0xFE;
        for (uint32_t i = 0; i < 8; ++i)
            Word<uint32_t>(kManager + 0x48 + 4 * i) = 0x48480000u + i;
        original ? Original<Method>(0x0053B0D0)(m) : m->StartNetworkGame();
    }
}

void Join(bool original)
{
    Manager* m = TheManager();
    for (int state : {10, 8, 9, 2}) {
        Prepare({0}, 0);
        m->m_joinState = state;
        Word<uint8_t>(kBuddy + 0x10) = state == 2 ? 0 : 1;
        original ? Original<Method>(0x0053C6E0)(m) : m->CancelJoin();
    }
    Prepare({0}, 0);
    m->m_joinState = 1;
    original ? Original<Method>(0x0053C6E0)(m) : m->CancelJoin(); // presence logged in
}

using TaskCode = void(__cdecl*)(void*);
void RunTask(bool original, uint32_t address, TaskCode ours)
{
    void* task = &Word<uint8_t>(kTasks);
    original ? Original<TaskCode>(address)(task) : ours(task);
    Rec(0x7A5C, {address});
}

void Tasks(bool original)
{
    Manager* m = TheManager();
    Prepare({0, 1, 2}, 1);
    // The observer task.
    for (int command : {0, 1}) {
        m->m_observerCommand = command;
        g_nextPlayer = PlayerAt(1);
        RunTask(original, 0x0053BED0, Manager::ObserverTask);
        Rec(0xBED0, {static_cast<uint32_t>(m->m_observerCommand)});
    }
    // Auto-serve: each condition failing in turn, then all met.
    struct Auto {
        uint32_t now, since, type;
        int players;
        bool waiting;
        uint32_t lobbyTime;
    };
    const Auto autos[] = {
        {3000, 1500, 0x1C471C60, 2, false, 0},   // 1.5 s only
        {3000, 1000, 0x12345678, 2, false, 0},   // not in the lobby
        {9000, 1000, 0x1C471C60, 1, false, 0},   // one player
        {9000, 1000, 0x1C471C60, 2, true, 0},    // waiting already
        {9000, 1000, 0x1C471C60, 2, false, 4000}, // only 5 s in the lobby
        {9000, 1000, 0x1C471C60, 3, false, 3999},
    };
    for (const Auto& a : autos) {
        g_now = a.now;
        GameVariable<uint32_t>(0x007CCDEC) = a.since;
        g_gameType = a.type;
        g_numPlayers = a.players;
        m->m_waitingForGameToStart = a.waiting;
        Word<uint32_t>(kManager + 0x1B0) = a.lobbyTime;
        RunTask(original, 0x0053BF00, Manager::AutoServeTask);
        Rec(0xBF00, {m->m_waitingForGameToStart ? 1u : 0u});
    }
    // The join's state, each one.
    Word<uint8_t>(kApp + 0x400 + offsetof(Net::App, m_flags)) = 0; // client 0 not local
    m->m_refusedReason = 0x2E450000u;
    for (int state = -1; state <= 11; ++state) {
        m->m_joinState = state;
        if (state == 6)
            SetNewPlayers({{1, 0}, {2, 5}, {3, 1}}); // the local one observing
        RunTask(original, 0x0053BF90, Manager::JoinStateTask);
        Rec(0xBF90, {static_cast<uint32_t>(m->m_joinState)});
    }
    Word<uint8_t>(kApp + 0x400 + offsetof(Net::App, m_flags)) = 8; // local now
    m->m_joinState = 2;
    RunTask(original, 0x0053BF90, Manager::JoinStateTask);
    SetNewPlayers({{1, 0}, {3, 1}}); // the local one playing
    m->m_joinState = 6;
    RunTask(original, 0x0053BF90, Manager::JoinStateTask);
    SetNewPlayers({{1, 0}});         // no local one
    m->m_joinState = 6;
    RunTask(original, 0x0053BF90, Manager::JoinStateTask);
}

void StartTask(bool original)
{
    for (int server = 1; server >= 0; --server) {
        Prepare({0, 1}, static_cast<uint8_t>(server));
        ArenaStruct(kStructs).Checksum(0x21902065, 0x6EF8FDA0);
        ArenaStruct(kStructs + 0x80).Checksum(0x21902065, 0x1E7E1002u);
        ArenaStruct(kStructs + 0x100).Checksum(Crc::GenerateCRCFromString("checksum"), 0x60A1501u);
        g_prefStructs[0x9834C4FA] = kStructs;
        g_prefStructs[0x651533EC] = kStructs + 0x80;
        g_prefStructs[0x38DBE1D0] = kStructs + 0x100;
        g_loading = 1;
        g_element = kObjects + 0x40 * 5; // a menu up
        RunTask(original, 0x0053C1F0, Manager::StartGameTask);
        g_element = 0;
        RunTask(original, 0x0053C1F0, Manager::StartGameTask);
        RunTask(original, 0x0053C1F0, Manager::StartGameTask); // (shown already)
        g_loading = 0;
        RunTask(original, 0x0053C1F0, Manager::StartGameTask); // not every one ready
        g_allReady = true;
        g_now = 0x0FFF;
        RunTask(original, 0x0053C1F0, Manager::StartGameTask); // too early
        g_now = 0x1000;
        RunTask(original, 0x0053C1F0, Manager::StartGameTask);
        Rec(0xC1F0, {GameVariable<uint8_t>(0x007CCE2C)});
    }
}

void Lives(bool original)
{
    Manager* m = TheManager();
    using NewFn = uint8_t*(__thiscall*)(Manager*, Obj::CObject*, Net::Conn*, uint32_t);
    using PlayerFn = void(__thiscall*)(Manager*, uint8_t*);
    Prepare({0, 1}, 1);
    auto newPlayer = [&](uint32_t skater, int conn, uint32_t flags) {
        auto* s = skater ? &Word<Obj::CObject>(skater) : nullptr;
        auto* c = &Word<Net::Conn>(ConnAt(conn));
        uint8_t* p = original ? Original<NewFn>(0x0053C770)(m, s, c, flags) : m->NewPlayer(s, c, flags);
        Rec(0xC770, {reinterpret_cast<uint32_t>(p), Word<uint32_t>(kManager + 0x1CC)});
        return p;
    };
    // A remote player while the local one observes and watches nobody; then one while it watches someone.
    PlayerFlags(0) = 5;
    newPlayer(kSkaters + 0x200, 2, 0x20);
    Word<uint32_t>(kManager + 0x1CC) = At(PlayerAt(1));
    newPlayer(kSkaters + 0x300, 3, 0);
    PlayerFlags(0) = 1;
    Word<uint32_t>(kManager + 0x1CC) = 0;
    newPlayer(kSkaters + 0x300, 3, 0); // the local one plays
    Word<uint32_t>(kManager + 0x1CC) = At(PlayerAt(1));
    newPlayer(kSkaters + 0x400, 4, 1); // a local player
    g_nextPlayer = PlayerAt(1);
    newPlayer(0, 5, 5);                // a local observer
    // Destroyed: the watched player (next: another, the same, none), with the crown and a flag.
    Prepare({0, 1, 2, 3}, 1);
    PlayerFlags(0) = 1;
    PlayerFlags(1) = 0x1080; // the crown, team 1's flag
    PlayerFlags(2) = 0x2001; // a local player with team 2's flag (same team as the flag? no: 0)
    Word<int>(PlayerAt(0) + 0x30) = 1;
    Word<uint32_t>(kManager + 0x6C) = At(kMisc + 0x600); // the crown
    for (int next : {2, 1, -1}) {
        Word<uint32_t>(kManager + 0x1CC) = At(PlayerAt(1));
        g_nextPlayer = next < 0 ? 0 : PlayerAt(next);
        Word<uint32_t>(PlayerAt(0) + 0x14) = next == 2 ? 0 : At(kSkaters); // the local skater (none once)
        uint8_t* p = &Word<uint8_t>(PlayerAt(1));
        original ? Original<PlayerFn>(0x0053C950)(m, p) : m->DestroyPlayer(p);
        Rec(0xC950, {Word<uint32_t>(kManager + 0x1CC)});
        GameVariable<uint8_t>(0x007CCA82) = next == 2 ? 0 : 1;
        g_peerConnected = next != 1;
    }
    Word<uint32_t>(kManager + 0x1CC) = 0;
    Word<uint8_t>(kLobby + 0x139) = 0;
    original ? Original<PlayerFn>(0x0053C950)(m, &Word<uint8_t>(PlayerAt(2))) : m->DestroyPlayer(&Word<uint8_t>(PlayerAt(2)));
    Word<uint32_t>(kManager + 0x6C) = 0;
    Word<uint32_t>(kLobby + 0x140) = 0;
    GameVariable<uint8_t>(0x007CCE1C) = 0;
    original ? Original<PlayerFn>(0x0053C950)(m, &Word<uint8_t>(PlayerAt(3))) : m->DestroyPlayer(&Word<uint8_t>(PlayerAt(3)));
}

void Drops(bool original)
{
    Manager* m = TheManager();
    using DropFn = void(__thiscall*)(Manager*, uint8_t*, int);
    for (int reason = 0; reason <= 7; ++reason) {
        for (int variant = 0; variant < 3; ++variant) {
            Prepare({0, 1, 2, 3}, variant == 2 ? 0 : 1);
            PlayerFlags(0) = 0x101;           // the local player, its game ended
            PlayerFlags(1) = 0x100 | 0x200 | (variant ? 0x80 | 0x2000 : 0); // first come first served; crown, flag
            PlayerFlags(2) = variant == 1 ? 0x100 : 0x104; // (an observer)
            PlayerFlags(3) = variant == 0 ? 0x004 : 0x100;
            Word<uint32_t>(kManager + 0x6C) = variant == 1 ? At(kMisc + 0x600) : 0;
            g_fcfsHost = variant == 1 ? PlayerAt(0) : 0;
            g_gameType = variant == 1 ? 0x1C471C60 : 0x6C5FF266;
            g_statsOn = reason != 5;
            std::memcpy(&Word<char>(PlayerAt(1) + 0x38), "Dropped1", 9);
            uint8_t* p = &Word<uint8_t>(PlayerAt(1));
            original ? Original<DropFn>(0x0053CB70)(m, p, reason) : m->DropPlayer(p, reason);
        }
    }
    // An observer dropped; a local player dropped.
    Prepare({0, 1}, 1);
    PlayerFlags(0) = 1;
    PlayerFlags(1) = 4;
    // (a server's player always has a connection: the original reads through it before its own null check)
    original ? Original<DropFn>(0x0053CB70)(m, &Word<uint8_t>(PlayerAt(1)), 1)
             : m->DropPlayer(&Word<uint8_t>(PlayerAt(1)), 1);
    original ? Original<DropFn>(0x0053CB70)(m, &Word<uint8_t>(PlayerAt(0)), 4)
             : m->DropPlayer(&Word<uint8_t>(PlayerAt(0)), 4);
}

void Pending(bool original)
{
    Manager* m = TheManager();
    Prepare({0, 1, 2, 3, 4}, 1);
    PlayerFlags(0) = 1;
    PlayerFlags(1) = 8;
    PlayerFlags(2) = 4;
    PlayerFlags(3) = 8 | 0x20;
    for (int i = 0; i < 5; ++i)
        Word<uint32_t>(kSkaters + 0x100 * static_cast<uint32_t>(i) + 0x14) = static_cast<uint32_t>(i * 2);
    std::memcpy(&Word<char>(PlayerAt(3) + 0x38), "Pending3", 9);
    original ? Original<Method>(0x0053D120)(m) : m->ReAddPendingPlayers();
    Rec(0xD120, {GameVariable<uint8_t>(0x007CCE1D)});
    // All destroyed; the lists' variants.
    Prepare({0, 1, 2, 3, 4}, 1);
    PlayerFlags(0) = 1;
    PlayerFlags(1) = 1;
    Word<uint32_t>(PlayerAt(2) + 0x14) = 0;
    original ? Original<Method>(0x0053D900)(m) : m->DestroyAllPlayers();
    Prepare({0, 1}, 1);
    original ? Original<Method>(0x0053E910)(m) : m->RemovePlayers();
    Prepare({0, 1}, 1);
    original ? Original<Method>(0x0053EE70)(m) : m->RemovePlayersAndApps();
    // Not ready: dropped as left out.
    Prepare({0, 1, 2, 3, 4}, 1);
    PlayerFlags(0) = 1;
    PlayerFlags(1) = 0x20;
    PlayerFlags(2) = 4;
    original ? Original<Method>(0x0053E920)(m) : m->DropPlayersNotReady();
    Prepare({0, 3}, 1);
    Script::CScript* script = TestScript();
    bool result = original ? Original<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x0053EA20)(nullptr, script)
                           : GameNet::CFunc_DropPendingPlayers(nullptr, script);
    Rec(0xEA20, {result ? 1u : 0u});
}

void RestoreFlow() { Cleanup(); }

void TestNetFlow()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}, {0x007CCA82, 1, {}},
                 {0x007CCDF8, 1, {}}, {0x007CCE1C, 1, {}}, {0x007CCE1D, 1, {}}, {0x007CCE2C, 1, {}},
                 {0x007CCDF4, 4, {}}, {0x007CE4FD, 1, {}}});
    RunNet2Case("GameNet flow: back to the lobby", Lobby, Install);
    RunNet2Case("GameNet flow: a game started (message 0x34)", StartGame, Install);
    RunNet2Case("GameNet flow: a join cancelled", Join, Install);
    RunNet2Case("GameNet flow: the observer / auto-serve / join state tasks", Tasks, Install);
    RunNet2Case("GameNet flow: the start of a game's task", StartTask, Install);
    RunNet2Case("GameNet players: new and destroyed", Lives, Install);
    RunNet2Case("GameNet players: dropped for each reason", Drops, Install);
    RunNet2Case("GameNet players: pending added back, all destroyed, the not ready dropped", Pending, Install);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetFlow, RestoreFlow);
