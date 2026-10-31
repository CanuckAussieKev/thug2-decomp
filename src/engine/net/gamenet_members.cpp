// The network manager's members after its tasks (OnlinePlay::Hub, 0x00533F20-0x00534FC0): the network mode flags,
// the team flags, the player / observer counts and password among the network preferences, the server's end and the
// custom tag texture a client sends.

#include <cstdint>
#include <initializer_list>
#include "engine/core/code_address.h"
#include <cstdio>

#pragma warning(disable : 4996) // (the original's sprintf, kept)

#include "engine/core/checksums.h"
#include "engine/net/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/maths/random.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lists.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/net/stats_man.h"
#include "engine/net/vault_man.h"
#include "engine/objects/entity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/rider/tagging.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/cas_options.h"
#include "engine/assets/resource_registry.h"
#include "game/park_editing/park_editor.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"

namespace OnlinePlay {

using namespace Detail;

namespace {

constexpr uint32_t kFlagsServer = 1;   // m_flagBits bit 0
constexpr uint32_t kFlagsClient = 2;   // bit 1
constexpr uint32_t kFlagsInternet = 4; // bit 2
constexpr uint32_t kFlagsLan = 8;      // bit 3

constexpr uint32_t kServerTask2 = 0xE8; // Scheduling::SchedulerTask* (removed with the server)
constexpr uint32_t kInitialScores = 0x48; // int[8]: the starting points
constexpr uint32_t kCrownStart = 0x68;  // int: the crown's
constexpr uint32_t kShowMetrics = 0x45; // bool
constexpr uint32_t kShowScores = 0x46;  // bool
constexpr uint32_t kMetricsTask = 0xC4; // Scheduling::SchedulerTask*

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

const char* const kTagPaletteDesc = "CAG Palette"; // "CAG Palette"
const char* const kTagTextureDesc = "CAG Texture"; // "CAG Texture"

// The game mode's members (not rewritten): its max_players, the flags accumulate_score / track_best_combo /
// is_elimiskate, a property set (IsTrue).
int ModeMaxPlayers() { return Call<int>(CodeAddress(&GoalSystem::CMatchRules::GetRiderCeiling), GameMode()); }
bool AccumulatesScore() { return Call<bool>(CodeAddress(&GoalSystem::CMatchRules::KeepsRunningTotal), GameMode()); }
bool TracksBestCombo() { return Call<bool>(CodeAddress(&GoalSystem::CMatchRules::KeepsBestChain), GameMode()); }
bool IsElimiskate() { return Call<bool>(CodeAddress(&GoalSystem::CMatchRules::FinishRulesReached), GameMode()); }
bool ModeIsTrue(uint32_t property) { return Call<bool>(CodeAddress(&GoalSystem::CMatchRules::IsTrue), GameMode(), property); }

Scheduling::PileUp* LogicTasks() { return reinterpret_cast<Scheduling::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 4); }

// The CFunction InSplitScreenGame (0x005A1180), called with no parameters.
bool InSplitScreenGame() { return ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr) != 0; }

// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool ProfileHasPart(void* profile, uint32_t part)
{
    return GameObjects::PartIsCreatedGraphic(profile, part);
}

// The custom tag texture's parts (Engine3D::CBitmapTexture's virtual slots 25-28).
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
void SetCountPreference(Hub* manager, uint32_t preference, const char* uiString, uint32_t checksum)
{
    Scripts::CStruct* values = NewParams();
    values->AppendWord(kUiString, Scripts::QBVAL_STRING, uiString);
    values->AddComponentOfType(kChecksum, Scripts::QBVAL_NAME, checksum);
    manager->GetOnlineSettings()->ChangeSetting(preference, values);
}

} // namespace

// 0x00533F20
void Hub::SetNetworkMode(int mode)
{
    m_flagBits &= ~(kFlagsInternet | kFlagsLan);
    if (mode == 1)
        m_flagBits |= kFlagsLan;
    else if (mode == 2)
        m_flagBits |= kFlagsInternet;
}

// 0x00533F50
void Hub::SetServerMode(bool on)
{
    if (on)
        m_flagBits |= kFlagsServer;
    else
        m_flagBits &= ~kFlagsServer;
}

// 0x00533F70
int Hub::IsLocalNetworkPlay() { return (m_flagBits >> 3) & 1; }

// 0x00533FA0
int Hub::IsClientSide() { return (m_flagBits >> 1) & 1; }

// 0x00533FB0
void Hub::BuildTeamFlagSet(int numTeams)
{
    Run(kKillTeamFlags);
    if (!numTeams)
        return;
    Scripts::CStruct* params = NewParams();
    params->PutInt(kNumTeams, numTeams);
    Run(kCreateTeamFlags, params);
    DeleteParams(params);
}

// 0x00534090
void Hub::SetMaxPlayersPreference(int count)
{
    char numName[32];
    char uiString[32];
    std::sprintf(numName, "num_%d", count);
    uint32_t checksum = Checksums::HashFromText(numName);
    std::sprintf(uiString, "%d %s", count, Scripts::FetchText(kStrPlayers));
    SetCountPreference(this, kNumPlayers, uiString, checksum);
}

// 0x005341B0
void Hub::SetMaxObserversPreference(int count)
{
    char numName[32];
    char uiString[32];
    std::sprintf(numName, "num_%d", count);
    uint32_t checksum = Checksums::HashFromText(numName);
    if (count == 0)
        std::sprintf(uiString, Scripts::FetchText(kStrNoObservers)); // (the text is the format, as in the original)
    else if (count == 1)
        std::sprintf(uiString, "1 %s", Scripts::FetchText(kStrObserver));
    else
        std::sprintf(uiString, "%d %s", count, Scripts::FetchText(kStrObservers));
    SetCountPreference(this, kNumObservers, uiString, checksum);
}

// 0x00534310
const char* Hub::FetchSecret()
{
    Scripts::CStruct* preference = GetOnlineSettings()->FetchSetting(kPassword);
    // On a miss the original returns its uninitialised local: the slot ECX was pushed into - the manager.
    auto password = reinterpret_cast<const char*>(this);
    preference->FetchText(kUiString, &password, 1);
    return password;
}

// 0x00534340
bool Hub::PlayersCanCollide()
{
    if ((m_matchEnded || GameType() == kFreeskate || GameType() == kHorse) && GameType() != kNetLobby)
        return false;
    if (InSplitScreenGame())
        return true;
    if (GetOnlineSettings()->GetPreferenceChecksum(kPlayerCollision, kChecksum) == kBooleanTrue)
        return true;
    return GameType() == kNetSlap || GameType() == kSlap;
}

// 0x005343E0
bool Hub::ShowTeamScores()
{
    if (GameType() == kNetLobby || !IsSquadMatch())
        return false;
    return GetOnlineSettings()->GetPreferenceChecksum(kScoreDisplay, kChecksum) == kScoreTeams;
}

// 0x00534CF0
void Hub::DestroyServer()
{
    NetLink::Hub* net = NetLink::Hub::Sole();
    if (m_host) {
        if (!m_host->IsHostLocal() && (m_flagBits & kFlagsInternet)) {
            static_cast<RoomManager*>(m_lobbyService)->StopHosting(); // the lobby's reporting stopped
            GameSpy::NatFreeNegotiations();    // (GameSpy)
        }
        net->ReleaseApp(m_host);
        m_hostAdmitJob->DropOut();
        Prop<Scheduling::SchedulerTask*>(this, kServerTask2)->DropOut();
        m_host = nullptr;
    }
    m_flagBits &= ~kFlagsServer;
}

// 0x00534D50
void Hub::DestroyMatchClient()
{
    if (m_browserClient) {
        NetLink::Hub::Sole()->ReleaseApp(m_browserClient);
        m_browserClient = nullptr;
    }
}

// 0x00534D70
bool Hub::SendCustomTagTexture()
{
    NetLink::ConnSearch search;
    search.m_vtable = NetLink::kConnSearchVtable;
    search.m_listLink = nullptr;
    bool onServer = (m_flagBits & kFlagsServer) != 0;
    uint8_t* skate = Detail::Skate();
    if (NetLink::Hub::Sole()->GetNetLinkKind() == NetLink::Hub::kConnectionModem || onServer)
        return false;
    auto* skater = Call<GameObjects::CGameEntity*>(CodeAddress(GameModules::Skate::GetLocalSkaterStub), skate); // the local skater
    if (!skater)
        return false;
    auto* tagging = static_cast<GameObjects::CTaggingComponent*>(skater->FetchModule(GameObjects::CTaggingComponent::kType));
    if (!tagging || !tagging->HasCustomTagTexture())
        return false;

    NetLink::Conn* server = m_clientApp->BeginConnWalk(&search);
    if (server->GetLinkSpeedClass() == 0) {
        void* profile = Call<void*>(CodeAddress(static_cast<GameObjects::CRiderIdentity* (GameModules::Skate::*)(int)>(&GameModules::Skate::FetchAccount)), Detail::Skate(), 0);
        uint32_t logos = ProfileHasPart(profile, kHatLogo) ? 1 : 0;
        if (ProfileHasPart(profile, kFrontLogo))
            logos |= 2;
        if (ProfileHasPart(profile, kBackLogo))
            logos |= 4;
        if (ProfileHasPart(profile, kDeckGraphic))
            logos |= 8;
        void* texture = Call<void*>(CodeAddress(&GoalSystem::CObjective::FetchArgs), tagging); // the custom tag texture
        if (texture) {
            NetLink::NetMessageInfo desc;
            desc.MakeNew();
            desc.m_ident = kMsgTagLogos;
            desc.m_content = &logos;
            desc.m_byteCount = 4;
            desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
            desc.m_groupId = kGroupTag;
            void* palette = TexturePalette(texture);
            m_clientApp->SendBulkToConn(server, kMsgTagPalette, TexturePaletteSize(texture), palette,
                                          kTagPaletteDesc, kGroupTag, false, true);
            void* pixels = TexturePixels(texture);
            m_clientApp->SendBulkToConn(server, kMsgTagTexture, TextureSize(texture), pixels, kTagTextureDesc,
                                          kGroupTag, false, true);
            SendToServer(m_clientApp, &desc);
        }
    }
    return true;
}

// 0x00534F40
uint32_t Hub::FetchStage() { return m_level; }

// 0x00534F50
const char* Hub::GetChoiceName(uint32_t list, uint32_t choice)
{
    Scripts::CArray* choices = Scripts::FetchList(list);
    const char* name = nullptr;
    // The checksum is read into the argument's slot: an entry without one compares what the slot last held (at
    // first the list's own name).
    uint32_t checksum = list;
    for (int i = 0; i < static_cast<int>(choices->m_extent); ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(choices->FetchPart(i));
        entry->ReadHashValue(kChecksum, &checksum, 1);
        if (checksum == choice)
            entry->FetchText(kName, &name, 1);
    }
    return name;
}

// 0x00534FC0
int Hub::GetChoiceInteger(uint32_t list, uint32_t choice, uint32_t field)
{
    Scripts::CArray* choices = Scripts::FetchList(list);
    int value = 0;
    // (as above, the checksum goes into `choice`'s slot: an entry without one matches)
    uint32_t checksum = choice;
    for (int i = 0; i < static_cast<int>(choices->m_extent); ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(choices->FetchPart(i));
        entry->ReadHashValue(kChecksum, &checksum, 1);
        if (checksum == choice)
            entry->FetchIntValue(field, &value, 1);
    }
    return value;
}

// 0x005359F0
void Hub::ChooseStartPoints()
{
    int* points = &Prop<int>(this, kInitialScores);
    int offset = Maths::Rnd(256);
    int count;
    if (InNetGame()) {
        count = 8;
    } else {
        count = ModeMaxPlayers();
        if (Prop<uint32_t>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor), 0x40) != kOff && !ModeIsTrue(kIsHorse)) {
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
            points[i] = Maths::Rnd(count);
            taken = false;
            for (int j = 0; j < count && !taken; ++j)
                taken = i != j && points[i] == points[j];
        } while (taken);
    }
    for (int i = 0; i < count; ++i)
        points[i] += offset;
    Prop<int>(this, kCrownStart) = Maths::Rnd(10);
}

// 0x00535BC0
void Hub::ToggleNetMetrics()
{
    Scheduling::PileUp* tasks = LogicTasks();
    uint8_t& show = Prop<uint8_t>(this, kShowMetrics); // (a byte flipped by its bit 0, as in the original)
    show ^= 1;
    if (show)
        tasks->QueueJob(Prop<Scheduling::SchedulerTask*>(this, kMetricsTask));
    else
        Prop<Scheduling::SchedulerTask*>(this, kMetricsTask)->DropOut();
}

// 0x00535BF0
void Hub::ToggleScores()
{
    AccumulatesScore(); // (asked and not used)
    Prop<uint8_t>(this, kShowScores) ^= 1;
}

// 0x00535D00
bool Hub::KeepsScore()
{
    if (!AccumulatesScore() && !TracksBestCombo() && GameType() != kNetFirefight && GameType() != kNetGoalAttack &&
        GameType() != kNetCtf && GameType() != kNetScavengerHunt)
        return false;
    return !IsElimiskate() || GameType() == kNetFirefight || GameType() == kNetElimiskate;
}

// 0x00535D90
void Hub::SetPresetOptionBits(char flags) { m_scriptFlagBits = static_cast<uint32_t>(static_cast<int>(flags)); }

// (no address: the list code folded at 0x00588B70 - see the header; the first node's data read before the null check,
// as there)
void __fastcall Hub::UnlinkAllNodes(MemSys::ListNode* head)
{
    MemSys::ListNode* node = head->next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        MemSys::ListNode* following = node->next;
        node->prev->next = following;
        following->prev = node->prev;
        node->next = node;
        node->prev = node;
        node = following->data == reinterpret_cast<void*>(-1) ? nullptr : following;
    }
}

bool g_queryReporting;

// 0x00536D70
void Hub::DestroyBody()
{
    // GameSpy shut down (the SDK's), its reporting if it's up.
    GameSpy::CdKeyDisconnectAll(1003);
    GameSpy::CdKeyShutdown();
    if (g_queryReporting) {
        GameSpy::Qr2Shutdown(nullptr);
        g_queryReporting = false;
    }
    // The lobby, the vault, the presence and the stats: each one's destructor, then freed.
    if (auto* lobby = static_cast<RoomManager*>(m_lobbyService)) {
        lobby->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(lobby);
    }
    if (auto* vault = static_cast<VaultMan*>(m_vaultMan)) {
        vault->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(vault);
    }
    if (auto* buddies = static_cast<FriendsManager*>(m_presenceService)) {
        buddies->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(buddies);
    }
    if (auto* stats = static_cast<RankingsManager*>(m_ratingService)) {
        stats->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(stats);
    }
    // The tasks, deleted through their vtables (slot 2), in the original's order.
    for (uint32_t offset : {0xD0u, 0xCCu, 0xC0u, 0xC4u, 0xD4u, 0xE0u, 0xE4u, 0xE8u, 0xECu, 0xF0u}) {
        void* task = Prop<void*>(this, offset);
        if (task)
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(task))[2])(task, 1);
    }
    // The lists: the players' nodes unlinked, then each head's destructor.
    UnlinkAllNodes(&Prop<MemSys::ListNode>(this, 0x11C));
    Prop<ServerNode>(this, 0x16C).DestroyBody();
    Prop<ServerNode>(this, 0x158).DestroyBody();
    Prop<QueuedScriptNode>(this, 0x144).DestroyBody();
    Prop<NewPlayerNode>(this, 0x130).DestroyBody();
    Prop<PlayerNode>(this, 0x11C).DestroyBody();
    GetTauntSettings()->TearDownState();
    GetOnlineSettings()->TearDownState();
}

} // namespace OnlinePlay
