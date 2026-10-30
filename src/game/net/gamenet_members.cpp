// The network manager's members after its tasks (GameNet::Manager, 0x00533F20-0x00534FC0): the network mode flags,
// the team flags, the player / observer counts and password among the network preferences, the server's end and the
// custom tag texture a client sends.

#include <cstdint>
#include "hook/game_code.h"
#include <cstdio>

#pragma warning(disable : 4996) // (the original's sprintf, kept)

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/mth/random.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lists.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/net/stats_man.h"
#include "game/net/vault_man.h"
#include "game/obj/object.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skater/tagging.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/cas_options.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/ass/assman.h"
#include "game/ed/park_editor.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint32_t kFlagsServer = 1;   // m_flags bit 0
constexpr uint32_t kFlagsClient = 2;   // bit 1
constexpr uint32_t kFlagsInternet = 4; // bit 2
constexpr uint32_t kFlagsLan = 8;      // bit 3

constexpr uint32_t kServerTask2 = 0xE8; // Tsk::BaseTask* (removed with the server)
constexpr uint32_t kStartPoints = 0x48; // int[8]: the starting points
constexpr uint32_t kCrownStart = 0x68;  // int: the crown's
constexpr uint32_t kShowMetrics = 0x45; // bool
constexpr uint32_t kShowScores = 0x46;  // bool
constexpr uint32_t kMetricsTask = 0xC4; // Tsk::BaseTask*

// Checksums.
constexpr uint32_t kKillTeamFlags = 0xEBB3642E;     // Kill_Team_Flags
constexpr uint32_t kCreateTeamFlags = 0x20B145E1;   // create_team_flags
constexpr uint32_t kNumTeams = 0x2EBE6CD1;          // num_teams
constexpr uint32_t kNumPlayers = 0x1CB2A6B0;        // num_players
constexpr uint32_t kNumObservers = 0xA8DA5CB7;      // num_observers
constexpr uint32_t kPassword = 0xCA3DB92A;          // password
constexpr uint32_t kUiString = 0x96875C0F;          // ui_string
constexpr uint32_t kChecksum = 0x21902065;          // checksum
constexpr uint32_t kName = 0xA1DC81F9;              // name
constexpr uint32_t kStrPlayers = 0x50589935;        // netmenu_str_players
constexpr uint32_t kStrNoObservers = 0x8AD4F7EA;    // netmenu_str_no_observers
constexpr uint32_t kStrObserver = 0x201D7F4E;       // netmenu_str_observer
constexpr uint32_t kStrObservers = 0x5848516E;      // netmenu_str_observers
constexpr uint32_t kPlayerCollision = 0x043EE978;   // player_collision
constexpr uint32_t kBooleanTrue = 0xF81BC89B;       // boolean_true
constexpr uint32_t kScoreDisplay = 0x92A6A8C8;      // score_display
constexpr uint32_t kScoreTeams = 0xD071112F;        // score_teams
constexpr uint32_t kHatLogo = 0x5B288E0B;           // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;         // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;          // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;       // deck_graphic
// The game modes.
constexpr uint32_t kFreeskate = 0x34861A16;         // freeskate
constexpr uint32_t kHorse = 0x9D65D0E7;             // horse
constexpr uint32_t kNetLobby = 0x1C471C60;          // netlobby
constexpr uint32_t kNetSlap = 0xF9D5D933;           // netslap
constexpr uint32_t kSlap = 0xCA1F360F;              // slap
constexpr uint32_t kNetFirefight = 0xBFF33600;      // netfirefight
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;     // netgoalattack
constexpr uint32_t kNetCtf = 0x6C5FF266;            // netctf
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;  // netscavengerhunt
constexpr uint32_t kNetElimiskate = 0x10F84276;     // netelimiskate
constexpr uint32_t kIsHorse = 0xD1993397;           // is_horse
constexpr uint32_t kOff = 0xD443A2BC;               // off

// Messages.
constexpr uint8_t kMsgTagTexture = 0x97;
constexpr uint8_t kMsgTagPalette = 0x98;
constexpr uint8_t kMsgTagLogos = 0x9A;
constexpr uint8_t kGroupTag = 10;

const char* const kTagPaletteDesc = GAME_TEXT(0x0064C974, "CAG Palette"); // "CAG Palette"
const char* const kTagTextureDesc = GAME_TEXT(0x0064C968, "CAG Texture"); // "CAG Texture"

// The game mode's members (not rewritten): its max_players, the flags accumulate_score / track_best_combo /
// is_elimiskate, a property set (IsTrue).
int ModeMaxPlayers() { return Call<int>(GAME_CODE(0x005531D0, &Game::CGameMode::GetMaximumNumberOfPlayers), GameMode()); }
bool AccumulatesScore() { return Call<bool>(GAME_CODE(0x00552FB0, &Game::CGameMode::ShouldAccumulateScore), GameMode()); }
bool TracksBestCombo() { return Call<bool>(GAME_CODE(0x00552FD0, &Game::CGameMode::ShouldTrackBestCombo), GameMode()); }
bool IsElimiskate() { return Call<bool>(GAME_CODE(0x00553910, &Game::CGameMode::EndConditionsMet), GameMode()); }
bool ModeIsTrue(uint32_t property) { return Call<bool>(GAME_CODE(0x00553220, &Game::CGameMode::IsTrue), GameMode(), property); }

Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 4); }

// The CFunction InSplitScreenGame (0x005A1180), called with no parameters.
bool InSplitScreenGame() { return CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr) != 0; }

// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool ProfileHasPart(void* profile, uint32_t part)
{
    return Obj::PartIsCreatedGraphic(profile, part);
}

// The custom tag texture's parts (Nx::CTexture's virtual slots 25-28).
void* TexturePixels(void* texture) { return (*reinterpret_cast<void* (__thiscall***)(void*)>(texture))[25](texture); }
uint16_t TextureSize(void* texture)
{
    return static_cast<uint16_t>((*reinterpret_cast<int(__thiscall***)(void*)>(texture))[26](texture));
}
void* TexturePalette(void* texture) { return (*reinterpret_cast<void* (__thiscall***)(void*)>(texture))[27](texture); }
uint16_t TexturePaletteSize(void* texture)
{
    return static_cast<uint16_t>((*reinterpret_cast<int(__thiscall***)(void*)>(texture))[28](texture));
}

// A network preference set from `values` (the original's new CStruct, never freed).
void SetCountPreference(Manager* manager, uint32_t preference, const char* uiString, uint32_t checksum)
{
    Script::CStruct* values = NewParams();
    values->AddString(kUiString, Script::ESYMBOLTYPE_STRING, uiString);
    values->AddComponentOfType(kChecksum, Script::ESYMBOLTYPE_NAME, checksum);
    manager->GetNetworkPreferences()->SetPreference(preference, values);
}

} // namespace

// 0x00533F20
void Manager::SetNetworkMode(int mode)
{
    m_flags &= ~(kFlagsInternet | kFlagsLan);
    if (mode == 1)
        m_flags |= kFlagsLan;
    else if (mode == 2)
        m_flags |= kFlagsInternet;
}

// 0x00533F50
void Manager::SetServerMode(bool on)
{
    if (on)
        m_flags |= kFlagsServer;
    else
        m_flags &= ~kFlagsServer;
}

// 0x00533F70
int Manager::InLanMode() { return (m_flags >> 3) & 1; }

// 0x00533FA0
int Manager::OnClient() { return (m_flags >> 1) & 1; }

// 0x00533FB0
void Manager::CreateTeamFlags(int numTeams)
{
    Run(kKillTeamFlags);
    if (!numTeams)
        return;
    Script::CStruct* params = NewParams();
    params->AddInteger(kNumTeams, numTeams);
    Run(kCreateTeamFlags, params);
    DeleteParams(params);
}

// 0x00534090
void Manager::SetMaxPlayersPreference(int count)
{
    char numName[32];
    char uiString[32];
    std::sprintf(numName, "num_%d", count);
    uint32_t checksum = Crc::GenerateCRCFromString(numName);
    std::sprintf(uiString, "%d %s", count, Script::GetString(kStrPlayers));
    SetCountPreference(this, kNumPlayers, uiString, checksum);
}

// 0x005341B0
void Manager::SetMaxObserversPreference(int count)
{
    char numName[32];
    char uiString[32];
    std::sprintf(numName, "num_%d", count);
    uint32_t checksum = Crc::GenerateCRCFromString(numName);
    if (count == 0)
        std::sprintf(uiString, Script::GetString(kStrNoObservers)); // (the text is the format, as in the original)
    else if (count == 1)
        std::sprintf(uiString, "1 %s", Script::GetString(kStrObserver));
    else
        std::sprintf(uiString, "%d %s", count, Script::GetString(kStrObservers));
    SetCountPreference(this, kNumObservers, uiString, checksum);
}

// 0x00534310
const char* Manager::GetPassword()
{
    Script::CStruct* preference = GetNetworkPreferences()->GetPreference(kPassword);
    // On a miss the original returns its uninitialised local: the slot ECX was pushed into - the manager.
    auto password = reinterpret_cast<const char*>(this);
    preference->GetString(kUiString, &password, 1);
    return password;
}

// 0x00534340
bool Manager::PlayerCollisionEnabled()
{
    if ((m_gameOver || GameType() == kFreeskate || GameType() == kHorse) && GameType() != kNetLobby)
        return false;
    if (InSplitScreenGame())
        return true;
    if (GetNetworkPreferences()->GetPreferenceChecksum(kPlayerCollision, kChecksum) == kBooleanTrue)
        return true;
    return GameType() == kNetSlap || GameType() == kSlap;
}

// 0x005343E0
bool Manager::ShowTeamScores()
{
    if (GameType() == kNetLobby || !IsTeamGame())
        return false;
    return GetNetworkPreferences()->GetPreferenceChecksum(kScoreDisplay, kChecksum) == kScoreTeams;
}

// 0x00534CF0
void Manager::DestroyServer()
{
    Net::Manager* net = Net::Manager::Instance();
    if (m_server) {
        if (!m_server->IsLocal() && (m_flags & kFlagsInternet)) {
            static_cast<LobbyMan*>(m_lobbyMan)->StopHosting(); // the lobby's reporting stopped
            GameFunction<void(__cdecl*)()>(0x005FBCD0)();    // (GameSpy)
        }
        net->DestroyApp(m_server);
        m_serverAddNewPlayersTask->Remove();
        Field<Tsk::BaseTask*>(this, kServerTask2)->Remove();
        m_server = nullptr;
    }
    m_flags &= ~kFlagsServer;
}

// 0x00534D50
void Manager::DestroyMatchClient()
{
    if (m_matchClient) {
        Net::Manager::Instance()->DestroyApp(m_matchClient);
        m_matchClient = nullptr;
    }
}

// 0x00534D70
bool Manager::SendCustomTagTexture()
{
    Net::ConnSearch search;
    search.m_vtable = Net::kConnSearchVtable;
    search.m_node = nullptr;
    bool onServer = (m_flags & kFlagsServer) != 0;
    uint8_t* skate = Detail::Skate();
    if (Net::Manager::Instance()->GetConnectionType() == Net::Manager::kConnectionModem || onServer)
        return false;
    auto* skater = Call<Obj::CCompositeObject*>(GAME_CODE(0x00566520, Mdl::Skate::GetLocalSkaterStub), skate); // the local skater
    if (!skater)
        return false;
    auto* tagging = static_cast<Obj::CTaggingComponent*>(skater->GetComponent(Obj::CTaggingComponent::kType));
    if (!tagging || !tagging->HasCustomTagTexture())
        return false;

    Net::Conn* server = m_client->FirstConnection(&search);
    if (server->GetBandwidthType() == 0) {
        void* profile = Call<void*>(GAME_CODE(0x00564FC0, static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile)), Detail::Skate(), 0);
        uint32_t logos = ProfileHasPart(profile, kHatLogo) ? 1 : 0;
        if (ProfileHasPart(profile, kFrontLogo))
            logos |= 2;
        if (ProfileHasPart(profile, kBackLogo))
            logos |= 4;
        if (ProfileHasPart(profile, kDeckGraphic))
            logos |= 8;
        void* texture = Call<void*>(GAME_CODE(0x00553E90, &Game::CGoal::GetParams), tagging); // the custom tag texture
        if (texture) {
            Net::MsgDesc desc;
            desc.Construct();
            desc.m_id = kMsgTagLogos;
            desc.m_data = &logos;
            desc.m_length = 4;
            desc.m_queue = Net::MsgDesc::kQueueSequenced;
            desc.m_groupId = kGroupTag;
            void* palette = TexturePalette(texture);
            m_client->StreamMessageToConn(server, kMsgTagPalette, TexturePaletteSize(texture), palette,
                                          kTagPaletteDesc, kGroupTag, false, true);
            void* pixels = TexturePixels(texture);
            m_client->StreamMessageToConn(server, kMsgTagTexture, TextureSize(texture), pixels, kTagTextureDesc,
                                          kGroupTag, false, true);
            SendToServer(m_client, &desc);
        }
    }
    return true;
}

// 0x00534F40
uint32_t Manager::GetLevel() { return m_level; }

// 0x00534F50
const char* Manager::GetChoiceName(uint32_t list, uint32_t choice)
{
    Script::CArray* choices = Script::GetArray(list);
    const char* name = nullptr;
    // The checksum is read into the argument's slot: an entry without one compares what the slot last held (at
    // first the list's own name).
    uint32_t checksum = list;
    for (int i = 0; i < static_cast<int>(choices->m_size); ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(choices->GetElement(i));
        entry->GetChecksum(kChecksum, &checksum, 1);
        if (checksum == choice)
            entry->GetString(kName, &name, 1);
    }
    return name;
}

// 0x00534FC0
int Manager::GetChoiceInteger(uint32_t list, uint32_t choice, uint32_t field)
{
    Script::CArray* choices = Script::GetArray(list);
    int value = 0;
    // (as above, the checksum goes into `choice`'s slot: an entry without one matches)
    uint32_t checksum = choice;
    for (int i = 0; i < static_cast<int>(choices->m_size); ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(choices->GetElement(i));
        entry->GetChecksum(kChecksum, &checksum, 1);
        if (checksum == choice)
            entry->GetInteger(field, &value, 1);
    }
    return value;
}

// 0x005359F0
void Manager::ChooseStartPoints()
{
    int* points = &Field<int>(this, kStartPoints);
    int offset = Mth::Rnd(256);
    int count;
    if (InNetGame()) {
        count = 8;
    } else {
        count = ModeMaxPlayers();
        if (Field<uint32_t>(reinterpret_cast<uint8_t*>(Ed::g_parkEditor), 0x40) != kOff && !ModeIsTrue(kIsHorse)) {
            count = 2;
            offset = 0;
        }
    }
    // (no bounds check: a mode with more than 8 players writes past the eight words, as in the original)
    for (int i = 0; i < count; ++i)
        points[i] = -1;
    for (int i = 0; i < count; ++i) {
        // Drawn again until it's one no other slot has.
        bool taken;
        do {
            points[i] = Mth::Rnd(count);
            taken = false;
            for (int j = 0; j < count && !taken; ++j)
                taken = i != j && points[i] == points[j];
        } while (taken);
    }
    for (int i = 0; i < count; ++i)
        points[i] += offset;
    Field<int>(this, kCrownStart) = Mth::Rnd(10);
}

// 0x00535BC0
void Manager::ToggleNetMetrics()
{
    Tsk::Stack* tasks = LogicTasks();
    uint8_t& show = Field<uint8_t>(this, kShowMetrics); // (a byte flipped by its bit 0, as in the original)
    show ^= 1;
    if (show)
        tasks->AddTask(Field<Tsk::BaseTask*>(this, kMetricsTask));
    else
        Field<Tsk::BaseTask*>(this, kMetricsTask)->Remove();
}

// 0x00535BF0
void Manager::ToggleScores()
{
    AccumulatesScore(); // (asked and not used)
    Field<uint8_t>(this, kShowScores) ^= 1;
}

// 0x00535D00
bool Manager::KeepsScore()
{
    if (!AccumulatesScore() && !TracksBestCombo() && GameType() != kNetFirefight && GameType() != kNetGoalAttack &&
        GameType() != kNetCtf && GameType() != kNetScavengerHunt)
        return false;
    return !IsElimiskate() || GameType() == kNetFirefight || GameType() == kNetElimiskate;
}

// 0x00535D90
void Manager::SetProSetFlags(char flags) { m_proSetFlags = static_cast<uint32_t>(static_cast<int>(flags)); }

// (no address: the list code folded at 0x00588B70 - see the header; the first node's data read before the null check,
// as there)
void __fastcall Manager::UnlinkAllNodes(Mem::ListNode* head)
{
    Mem::ListNode* node = head->next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        Mem::ListNode* following = node->next;
        node->prev->next = following;
        following->prev = node->prev;
        node->next = node;
        node->prev = node;
        node = following->data == reinterpret_cast<void*>(-1) ? nullptr : following;
    }
}

GAME_GLOBAL(bool, g_queryReporting, 0x007CCE1C);

// 0x00536D70
void Manager::DestroyBody()
{
    // GameSpy shut down (the SDK's), its reporting if it's up.
    GameFunction<void(__cdecl*)(int)>(0x005F9900)(1003);
    GameFunction<void(__cdecl*)()>(0x005F9BE0)();
    if (g_queryReporting) {
        GameFunction<void(__cdecl*)(int)>(0x005FBB20)(0);
        g_queryReporting = false;
    }
    // The lobby, the vault, the presence and the stats: each one's destructor, then freed.
    if (auto* lobby = static_cast<LobbyMan*>(m_lobbyMan)) {
        lobby->DestroyBody();
        Mem::Manager::Get()->Free(lobby);
    }
    if (auto* vault = static_cast<VaultMan*>(m_vaultMan)) {
        vault->DestroyBody();
        Mem::Manager::Get()->Free(vault);
    }
    if (auto* buddies = static_cast<BuddyMan*>(m_buddyMan)) {
        buddies->DestroyBody();
        Mem::Manager::Get()->Free(buddies);
    }
    if (auto* stats = static_cast<StatsMan*>(m_statsMan)) {
        stats->DestroyBody();
        Mem::Manager::Get()->Free(stats);
    }
    // The tasks, deleted through their vtables (slot 2), in the original's order.
    for (uint32_t offset : {0xD0u, 0xCCu, 0xC0u, 0xC4u, 0xD4u, 0xE0u, 0xE4u, 0xE8u, 0xECu, 0xF0u}) {
        void* task = Field<void*>(this, offset);
        if (task)
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(task))[2])(task, 1);
    }
    // The lists: the players' nodes unlinked, then each head's destructor.
    UnlinkAllNodes(&Field<Mem::ListNode>(this, 0x11C));
    Field<ServerNode>(this, 0x16C).DestroyBody();
    Field<ServerNode>(this, 0x158).DestroyBody();
    Field<QueuedScriptNode>(this, 0x144).DestroyBody();
    Field<NewPlayerNode>(this, 0x130).DestroyBody();
    Field<PlayerNode>(this, 0x11C).DestroyBody();
    GetTauntPreferences()->Deinit();
    GetNetworkPreferences()->Deinit();
}

REPLACE(0x00533F20, &Manager::SetNetworkMode);
REPLACE(0x00533F50, &Manager::SetServerMode);
REPLACE(0x00533F70, &Manager::InLanMode);
REPLACE(0x00533F80, &Manager::InInternetMode);
REPLACE(0x00533F90, &Manager::OnServer);
REPLACE(0x00533FA0, &Manager::OnClient);
REPLACE(0x00533FB0, &Manager::CreateTeamFlags);
REPLACE(0x00534090, &Manager::SetMaxPlayersPreference);
REPLACE(0x005341B0, &Manager::SetMaxObserversPreference);
REPLACE(0x00534310, &Manager::GetPassword);
REPLACE(0x00534340, &Manager::PlayerCollisionEnabled);
REPLACE(0x005343E0, &Manager::ShowTeamScores);
REPLACE(0x00534CF0, &Manager::DestroyServer);
REPLACE(0x00534D50, &Manager::DestroyMatchClient);
REPLACE(0x00534D70, &Manager::SendCustomTagTexture);
REPLACE_SOURCE_ONLY(0x00534F40, &Manager::GetLevel);
REPLACE(0x00534F50, &Manager::GetChoiceName);
REPLACE(0x00534FC0, &Manager::GetChoiceInteger);
REPLACE(0x005359F0, &Manager::ChooseStartPoints);
REPLACE(0x00535BC0, &Manager::ToggleNetMetrics);
REPLACE(0x00535BF0, &Manager::ToggleScores);
REPLACE(0x00535D00, &Manager::KeepsScore);
REPLACE(0x00535D90, &Manager::SetProSetFlags);
REPLACE(0x00536D70, &Manager::DestroyBody);

} // namespace GameNet
