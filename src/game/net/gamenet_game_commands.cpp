// The network manager's script commands, second part (0x00538050-0x00539570): capture the flag's flags, the crown,
// starting and ending a network game, teams, the host, readiness, and the player list menu with its options dialog.

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace GameNet {

using namespace Detail;
using Script::CScript;
using Script::CStruct;

namespace {

// Messages.
constexpr uint8_t kMsgGameOver = 0x5A;
constexpr uint8_t kMsgEndGameRequest = 0x65;
constexpr uint8_t kMsgNumTeamsRequest = 0x66;
constexpr uint8_t kMsgJoinTeamRequest = 0x67;
constexpr uint8_t kMsgTeamChange = 0x68;
constexpr uint8_t kMsgNumTeams = 0x69;
constexpr uint8_t kMsgGameEnded = 0x6A;
constexpr uint8_t kMsgSelectedGoals = 0x6D;
constexpr uint8_t kMsgTeamGame = 0x7A;

constexpr uint32_t kFlagsServer = 1;   // the manager's flags
constexpr uint32_t kFlagsInternet = 4;
constexpr uint32_t kPlayerFlags = 0x1D0;
constexpr uint32_t kPlayerProfile = 0x28; // PlayerInfo: the GameSpy profile id
constexpr uint32_t kRemoveMark = 0x400;   // a player's flags: being removed (its options dialog is up)

constexpr uint32_t kGameStartTask = 0xE8;  // Tsk::BaseTask*
constexpr uint32_t kChangeLevelTask = 0xEC;
constexpr uint32_t kCrownSpawn = 0x68;     // int
constexpr uint32_t kCrown = 0x6C;          // Obj::CCrownObject*

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;            // checksum
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;       // netgoalattack
constexpr uint32_t kAutoStarting = 0x65C14F78;        // net_message_auto_starting_game
constexpr uint32_t kStarting = 0x831E8336;            // net_message_starting_game
constexpr uint32_t kDoBackendRetry = 0x1D02CE3B;      // do_backend_retry
constexpr uint32_t kPrepareSkaterForMove = 0xF1B110ED; // PrepareSkaterForMove
constexpr uint32_t kBlue = 0x61C9354B;                // blue
constexpr uint32_t kRed = 0x059EA070;                 // red
constexpr uint32_t kGreen = 0x2F6511DE;               // green
constexpr uint32_t kIndex = 0x7F8C98FE;               // index
constexpr uint32_t kName = 0xA1DC81F9;                // name
constexpr uint32_t kProfile = 0x7EA855F0;             // profile
constexpr uint32_t kNick = 0xD6F4D0C8;                // nick
constexpr uint32_t kAllowAddHomie = 0xBA3F599F;       // allow_add_homie
constexpr uint32_t kAllowRemoveHomie = 0x5CBA31EC;    // allow_remove_homie
constexpr uint32_t kCreatePlayerOptionsDialog = 0xF95F6E22; // create_player_options_dialog
constexpr uint32_t kText = 0xC4745838;                // text
constexpr uint32_t kCentered = 0x2A434D05;            // centered
constexpr uint32_t kId = 0x40C698AF;                  // id
constexpr uint32_t kPadChooseScript = 0x2B0CFCA1;     // pad_choose_script
constexpr uint32_t kChooseSelectedPlayer = 0x405A36F9; // choose_selected_player
constexpr uint32_t kRgba = 0x3F6BCDBA;                // rgba
constexpr uint32_t kUnfocusParams = 0xF974BF7C;       // unfocus_params
constexpr uint32_t kPadChooseParams = 0xB8437568;     // pad_choose_params
constexpr uint32_t kThemeMenuAddItem = 0xC16BF7BD;    // theme_menu_add_item
constexpr uint32_t kCrownSpawnType = 0xAF86421B;      // (a node's Type: the crown's starting point; the name isn't known)

const char* const kTeam = reinterpret_cast<const char*>(0x0064CA14);           // "team"
const char* const kNetStatusProps = reinterpret_cast<const char*>(0x0064CA1C); // "netstatusprops"
const char* const kType = reinterpret_cast<const char*>(0x0064654C);           // "Type"
const char* const kBit = reinterpret_cast<const char*>(0x0064C9CC);            // "bit"
const char* const kParamId = reinterpret_cast<const char*>(0x0064C9C0);        // "param_id"

constexpr int kPlayerMenuIdBase = 123456;  // the menu items' ids: this plus the player's place in the list
constexpr uint32_t kJoinTeamInterval = 150; // ms between team requests
constexpr int kMaxCrownSpawns = 10;

Manager* Net() { return GameVariable<Manager*>(0x007CCE14); }
Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(GameVariable<uint8_t*>(0x00696D68) + 4); }
uint32_t& LastTeamRequest() { return GameVariable<uint32_t>(0x007CCE28); }
uint32_t& NetGameStartTime() { return GameVariable<uint32_t>(0x007CCDF4); }
uint8_t& GameReported() { return GameVariable<uint8_t>(0x007CCE1E); } // GameSpy told a game is on

bool IsHead(const Mem::ListNode* node) { return reinterpret_cast<intptr_t>(node->data) == -1; }
Mem::ListNode* NextNode(const Mem::ListNode* node) { return IsHead(node->next) ? nullptr : node->next; }
uint8_t* DataOf(const Mem::ListNode* node) { return node ? static_cast<uint8_t*>(node->data) : nullptr; }
Mem::ListNode* FirstPlayerNode(Manager* manager) { return NextNode(&Field<Mem::ListNode>(manager, 0x11C)); }

enum class Recipients { kAll, kRemote, kObservers };
// `desc` through the server to the players (all, those not local, the observers).
void SendToPlayers(Manager* manager, Net::App* server, Net::MsgDesc* desc, Recipients recipients)
{
    for (Mem::ListNode* node = FirstPlayerNode(manager); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (recipients == Recipients::kRemote && IsLocalPlayer(player))
            continue;
        if (recipients == Recipients::kObservers && !IsObserver(player))
            continue;
        server->EnqueueMessage(PlayerHandle(player), desc);
    }
}
void InitDesc(Net::MsgDesc& desc, uint8_t id)
{
    desc.Construct();
    desc.m_id = id;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
}

// The lobby's GameSpy peer told the game's state changed (connected and hosting), and GameSpy's reporting told the
// game is on / off (once, as `on` changes).
void ReportGameState(Manager* manager, bool on)
{
    void* peer = Call<void*>(0x0053F880, manager->m_lobbyMan);
    if (peer && GameFunction<int(__cdecl*)(void*)>(0x005FC730)(peer) && Call<bool>(0x0053F810, manager->m_lobbyMan))
        GameFunction<void(__cdecl*)(void*)>(0x005FCCF0)(peer);
    // (the byte compared with 1 when starting, tested for non-zero when ending, as in the original)
    uint8_t& reported = GameReported();
    if (on ? reported == 1 : reported == 0)
        return;
    reported = on ? 1 : 0;
    if (GameVariable<bool>(0x007CCA82) && GameVariable<Manager*>(0x007CCE14)->InNetGame() &&
        GameVariable<bool>(0x007CCE1C))
        GameFunction<void(__cdecl*)(int)>(0x005FBAD0)(0);
}

// The GameSpy presence (+0x18): logged in (0x005480B0), `profile` a buddy (0x00549CB0).
bool LoggedIn(Manager* manager) { return Call<bool>(0x005480B0, manager->m_buddyMan); }
bool IsBuddy(Manager* manager, int profile) { return Call<bool>(0x00549CB0, manager->m_buddyMan, profile); }

} // namespace

// 0x00538050: TeamFlagTaken team=<n> - a player (no observers) carries that team's flag (0x005433D0).
bool __cdecl CFunc_TeamFlagTaken(CStruct* params, CScript*)
{
    Manager* manager = Net();
    PlayerSearch search;
    int team = 0; // (uninitialised in the original)
    params->GetInteger(kTeam, &team, 0);
    for (void* player = manager->FirstPlayerInfo(&search, false); player;
         player = manager->NextPlayerInfo(&search, false)) {
        if (Call<int>(0x005433D0, player) == team)
            return true;
    }
    return false;
}

// 0x00538170: SpawnCrown - the crown made (+0x6C), placed at a node of the crown's type (the manager's random +0x68
// modulo their count; at most 10 counted, no bounds check) and worn by the king of the hill if there is one.
bool __cdecl CFunc_SpawnCrown(CStruct*, CScript*)
{
    Manager* manager = Net();
    void* memory = Mem::Calloc(sizeof(Obj::CCrownObject));
    auto* crown = memory ? static_cast<Obj::CCrownObject*>(memory)->Construct() : nullptr;
    int spawn = Field<int>(manager, kCrownSpawn);
    Field<Obj::CCrownObject*>(manager, kCrown) = crown;
    uint32_t names[kMaxCrownSpawns];
    CStruct* nodes[kMaxCrownSpawns];
    int count = 0;
    SkateScript::StartNodeWalk(0);
    for (CStruct* node = SkateScript::NextNode(); node; node = SkateScript::NextNode()) {
        uint32_t type = 0;
        node->GetChecksum(kType, &type, 0);
        if (type == kCrownSpawnType) {
            names[count] = SkateScript::GetNodeName(node); // (kept, never used)
            nodes[count] = node;
            ++count;
        }
    }
    SkateScript::EndNodeWalk();
    (void)names;
    CStruct* node = nodes[spawn % count]; // (no node of the type divides by zero, as in the original)
    Mth::Vector position;
    SkateScript::GetPosition(node, &position);
    Field<Obj::CCrownObject*>(manager, kCrown)->InitFromNode(GameVariable<Obj::CCompositeObjectManager*>(0x006F0FB8),
                                                             node);
    if (auto* king = static_cast<uint8_t*>(manager->GetKingOfTheHill()))
        Field<Obj::CCrownObject*>(manager, kCrown)->PutOn(reinterpret_cast<Obj::CCompositeObject*>(PlayerSkater(king)));
    return true;
}

// 0x00538280: StartNetworkGame - GameSpy told; unless the game start or level change is under way: the game pending,
// its start time (now, or 3 seconds ahead in a network game - 8 for goal attack), the start task added; in a network
// game the players told (the panel message; 0x7A in a team game; for goal attack the selected goals, 0x6D).
bool __cdecl CFunc_StartNetworkGame(CStruct*, CScript*)
{
    Manager* manager = Net();
    Tsk::Stack* tasks = LogicTasks();
    ReportGameState(manager, true);
    if (Field<Tsk::BaseTask*>(manager, kGameStartTask)->InList() ||
        Field<Tsk::BaseTask*>(manager, kChangeLevelTask)->InList())
        return true;
    manager->m_gamePending = true;
    if (!manager->InNetGame())
        NetGameStartTime() = Tmr::GetTime();
    else if (manager->GetGameTypePreference() == kNetGoalAttack)
        NetGameStartTime() = Tmr::GetTime() + 8000;
    else
        NetGameStartTime() = Tmr::GetTime() + 3000;
    tasks->AddTask(Field<Tsk::BaseTask*>(manager, kGameStartTask));
    if (manager->InNetGame()) {
        uint32_t text = manager->m_hostMode == 1 ? kAutoStarting : kStarting;
        manager->CreateNetPanelMessage(true, text, manager->GetGameTypeName(), nullptr, nullptr, kNetStatusProps, true,
                                       3000);
        if (IsTeamGame()) {
            Net::MsgDesc desc;
            Net::App* server = manager->m_server;
            InitDesc(desc, kMsgTeamGame);
            SendToPlayers(manager, server, &desc, Recipients::kAll);
        }
    }
    if (manager->InNetGame() && manager->GetGameTypePreference() == kNetGoalAttack) {
        // The selected goals: a byte 1, then each selected goal's id (each told first - its slot 29), then 0.
        uint8_t buffer[0x1000];
        Net::MsgDesc desc;
        desc.Construct();
        uint8_t* goals = GoalManager();
        Net::App* server = manager->m_server;
        buffer[0] = 1;
        uint8_t* end = buffer + 1;
        int count = Call<int>(0x00469490, goals);
        for (int i = 0; i < count; ++i) {
            uint8_t* goal = Call<uint8_t*>(0x00557FB0, goals, i);
            if (Call<bool>(0x005542F0, goal)) {
                reinterpret_cast<void(__thiscall*)(void*)>((*reinterpret_cast<void***>(goal))[29])(goal);
                uint32_t id = Call<uint32_t>(0x00554280, goal);
                std::memcpy(end, &id, 4);
                end += 4;
            }
        }
        std::memset(end, 0, 4);
        desc.m_data = buffer;
        desc.m_id = kMsgSelectedGoals;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        desc.m_length = static_cast<uint16_t>(static_cast<uint16_t>(end - buffer) + 4);
        SendToPlayers(manager, server, &desc, Recipients::kRemote);
    }
    return true;
}

// 0x00538620: SendGameOverToObservers - on the server, game over (0x5A) to every observer.
bool __cdecl CFunc_SendGameOverToObservers(CStruct*, CScript*)
{
    Manager* manager = Net();
    if (!(manager->m_flags & kFlagsServer))
        return true;
    Net::MsgDesc desc;
    InitDesc(desc, kMsgGameOver);
    SendToPlayers(manager, manager->m_server, &desc, Recipients::kObservers);
    return true;
}

// 0x00538720: EndNetworkGame - GameSpy told; on the server do_backend_retry and "game ended" (0x6A) to the remote
// players, on a client the request to end it (0x65) to the server.
bool __cdecl CFunc_EndNetworkGame(CStruct*, CScript*)
{
    Manager* manager = Net();
    ReportGameState(manager, false);
    Net::MsgDesc desc;
    if (manager->m_flags & kFlagsServer) {
        desc.Construct();
        Net::App* server = manager->m_server;
        Run(kDoBackendRetry);
        desc.m_id = kMsgGameEnded;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        SendToPlayers(manager, server, &desc, Recipients::kRemote);
    } else {
        InitDesc(desc, kMsgEndGameRequest);
        SendToServer(manager->m_client, &desc);
    }
    return true;
}

// 0x005388C0: GetMyTeam - the local player's team into the script's team.
bool __cdecl CFunc_GetMyTeam(CStruct*, CScript* script)
{
    if (auto* local = static_cast<uint8_t*>(Net()->GetLocalPlayer()))
        script->m_params->AddInteger(kTeam, PlayerTeam(local));
    return true;
}

// 0x005388F0: SetNumTeams <n> - on a client the request (0x66) to the server; on the server the game mode's team
// count set (a local skater put back to be moved when teams start now - PrepareSkaterForMove, 0x00566370), then (with a
// server app) the count to the remote players (0x69) and every player of a team no longer there (no observers) moved
// to team 0 - each told to the remote players (0x68: its skater's id, 0).
bool __cdecl CFunc_SetNumTeams(CStruct* params, CScript*)
{
    Manager* manager = Net();
    uint8_t* skate = Detail::Skate();
    PlayerSearch search;
    Net::MsgDesc desc;
    desc.Construct();
    int teams = 0;
    params->GetInteger(0u, &teams, 0);
    if (!(manager->m_flags & kFlagsServer)) {
        Net::MsgDesc request;
        request.Construct();
        uint8_t count = static_cast<uint8_t>(teams);
        request.m_data = &count;
        request.m_length = 1;
        request.m_id = kMsgNumTeamsRequest;
        request.m_queue = Net::MsgDesc::kQueueSequenced;
        request.m_groupId = kGroupGame;
        SendToServer(manager->m_client, &request);
        return true;
    }
    if (teams < 1 || IsTeamGame()) {
        Call(0x005535A0, GameMode(), teams);
    } else {
        Call(0x005535A0, GameMode(), teams);
        if (Call<Obj::CObject*>(0x00566520, skate)) { // the local skater
            Run(kPrepareSkaterForMove, nullptr, Call<Obj::CObject*>(0x00566520, skate));
            Call(0x00566370, skate, Call<Obj::CObject*>(0x00566520, skate), 0, 0);
        }
    }
    Net::App* server = manager->m_server;
    if (!server)
        return true;
    uint8_t count = static_cast<uint8_t>(teams);
    desc.m_data = &count;
    desc.m_length = 1;
    desc.m_id = kMsgNumTeams;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToPlayers(manager, server, &desc, Recipients::kRemote);
    uint8_t change[2];
    desc.m_data = change;
    desc.m_length = 2;
    desc.m_id = kMsgTeamChange;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (auto* player = static_cast<uint8_t*>(manager->FirstPlayerInfo(&search, false)); player;
         player = static_cast<uint8_t*>(manager->NextPlayerInfo(&search, false))) {
        if (PlayerTeam(player) < teams)
            continue;
        change[1] = 0;
        change[0] = static_cast<uint8_t>(SkaterId(PlayerSkater(player)));
        SendToPlayers(manager, server, &desc, Recipients::kRemote);
        Field<int>(player, 0x30) = 0;
    }
    return true;
}

// 0x00538C90: JoinTeam <blue | red | green | other> - the local player's skater's request (0x67: its id, the team
// 1 / 0 / 2 / 3) to the server, at most every 150 ms. False without a local player with a skater.
bool __cdecl CFunc_JoinTeam(CStruct* params, CScript*)
{
    Manager* manager = Net();
    Net::MsgDesc desc;
    desc.Construct();
    auto* local = static_cast<uint8_t*>(manager->GetLocalPlayer());
    if (!local || !PlayerSkater(local))
        return false;
    uint32_t color = 0; // (uninitialised in the original: without one, its slot decides - most likely team 3)
    params->GetChecksum(0u, &color, 0);
    uint8_t request[2];
    request[1] = color == kBlue ? 1 : color == kRed ? 0 : color == kGreen ? 2 : 3;
    request[0] = static_cast<uint8_t>(SkaterId(PlayerSkater(local)));
    Net::App* client = manager->m_client;
    if (Tmr::GetTime() - LastTeamRequest() >= kJoinTeamInterval) {
        LastTeamRequest() = Tmr::GetTime();
        desc.m_data = request;
        desc.m_length = 2;
        desc.m_id = kMsgJoinTeamRequest;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        SendToServer(client, &desc);
    }
    return true;
}

// 0x00538D70: IsHost - in a network game: on the server, host mode 0 (serving); on a client, the local player is the
// server's.
bool __cdecl CFunc_IsHost(CStruct*, CScript*)
{
    Manager* manager = Net();
    if (!manager->InNetGame())
        return false;
    if (manager->m_flags & kFlagsServer)
        return manager->m_hostMode == 0;
    void* local = manager->GetLocalPlayer();
    return local && Call<bool>(0x00543310, local);
}

// 0x00538DC0: AllPlayersAreReady - on the server, every player's connection is ready (+0x60 bit 0); true elsewhere.
bool __cdecl CFunc_AllPlayersAreReady(CStruct*, CScript*)
{
    Manager* manager = Net();
    if (!(manager->m_flags & kFlagsServer))
        return true;
    for (Mem::ListNode* node = FirstPlayerNode(manager); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (!(Field<uint8_t>(Field<void*>(player, 0x18), 0x60) & 1))
            return false;
    }
    return true;
}

// 0x00538E70: CreatePlayerOptions index=<n> name=<string> - the index'th player (observers too), if its name is
// `name` (case aside): marked (flags 0x400) and create_player_options_dialog {[profile, nick, allow_add_homie /
// allow_remove_homie - on the internet, logged in to GameSpy, the player has a profile], name}.
bool __cdecl CFunc_CreatePlayerOptions(CStruct* params, CScript*)
{
    Manager* manager = Net();
    int index = 0;
    const char* name = nullptr;
    params->GetInteger(kIndex, &index, 0);
    params->GetString(kName, &name, 0);
    Mem::ListNode* node = FirstPlayerNode(manager);
    uint8_t* player = DataOf(node);
    if (!player)
        return true;
    while (index != 0) {
        --index;
        node = NextNode(node);
        player = DataOf(node);
        if (!player)
            return true;
    }
    if (_stricmp(PlayerName(player), name) != 0)
        return true;
    Field<uint32_t>(player, kPlayerFlags) |= kRemoveMark;
    CStruct* dialog = NewParams();
    int profile = Field<int>(player, kPlayerProfile);
    if ((manager->m_flags & kFlagsInternet) && profile && LoggedIn(manager)) {
        dialog->AddInteger(kProfile, Field<int>(player, kPlayerProfile));
        dialog->AddString(kNick, PlayerName(player));
        dialog->AddChecksum(0u, IsBuddy(manager, Field<int>(player, kPlayerProfile)) ? kAllowRemoveHomie : kAllowAddHomie);
    }
    dialog->AddString(kName, Script::ESYMBOLTYPE_STRING, name);
    Run(kCreatePlayerOptionsDialog, dialog);
    DeleteParams(dialog);
    return true;
}

// 0x00539080: FillPlayerListMenu - (in the front end's heap) a menu item for every player but the local one:
// theme_menu_add_item {text = its name, centered, id = 123456 + its place in the list, pad_choose_script =
// choose_selected_player, rgba (grey; a buddy's red channel 0... - see below), unfocus_params {rgba},
// pad_choose_params {index, name}}.
bool __cdecl CFunc_FillPlayerListMenu(CStruct*, CScript*)
{
    Manager* manager = Net();
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_frontEndHeap);
    int index = 0;
    for (Mem::ListNode* node = FirstPlayerNode(manager); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (IsLocalPlayer(player)) {
            ++index;
            continue;
        }
        // The colour: grey (128, 128, 128, 128); a GameSpy buddy's (128, 128, 0, 128); another logged-in player's
        // with a profile (0, 128, 0, 128).
        Script::CArray* arrayMemory = Script::NewArray();
        Script::CArray* rgba = arrayMemory ? arrayMemory->Init() : nullptr;
        rgba->SetSizeAndType(4, Script::ESYMBOLTYPE_INTEGER);
        uint32_t blue;
        int profile = Field<int>(player, kPlayerProfile);
        if (profile && LoggedIn(manager)) {
            if (IsBuddy(manager, Field<int>(player, kPlayerProfile))) {
                rgba->SetElement(0, 0x80);
                rgba->SetElement(1, 0x80);
            } else {
                rgba->SetElement(0, 0);
                rgba->SetElement(1, 0x80);
            }
            blue = 0;
        } else {
            rgba->SetElement(0, 0x80);
            rgba->SetElement(1, 0x80);
            blue = 0x80;
        }
        rgba->SetElement(2, blue);
        rgba->SetElement(3, 0x80);
        CStruct* item = NewParams();
        item->AddString(kText, PlayerName(player));
        item->AddChecksum(kCentered, kCentered);
        item->AddChecksum(kId, static_cast<uint32_t>(index + kPlayerMenuIdBase));
        item->AddChecksum(kPadChooseScript, kChooseSelectedPlayer);
        CStruct* unfocus = NewParams();
        unfocus->AddArray(kRgba, rgba);
        item->AddArray(kRgba, rgba);
        item->AddStructure(kUnfocusParams, unfocus);
        CStruct* choose = NewParams();
        choose->AddInteger(kIndex, index);
        choose->AddString(kName, PlayerName(player));
        item->AddStructure(kPadChooseParams, choose);
        Run(kThemeMenuAddItem, item);
        DeleteParams(item);
        DeleteParams(choose);
        DeleteParams(unfocus);
        if (rgba) {
            rgba->Clear();
            Script::DeleteArray(rgba);
        }
        ++index;
    }
    Mem::Manager::Get()->PopContext();
    return true;
}

// 0x00539470: RemovePlayer - a jump to CreatePlayerOptions (the same command under a second name).
bool __cdecl CFunc_RemovePlayer(CStruct* params, CScript* script) { return CFunc_CreatePlayerOptions(params, script); }

// 0x00539480: CancelRemovePlayer - every player's (no observers) removal mark (flags 0x400) cleared.
bool __cdecl CFunc_CancelRemovePlayer(CStruct*, CScript*)
{
    Manager* manager = Net();
    PlayerSearch search;
    for (auto* player = static_cast<uint8_t*>(manager->FirstPlayerInfo(&search, false)); player;
         player = static_cast<uint8_t*>(manager->NextPlayerInfo(&search, false)))
        Field<uint32_t>(player, kPlayerFlags) &= ~kRemoveMark;
    return true;
}

// 0x00539570: ToggleProSet bit=<n> param_id=<checksum> - Manager::ToggleProSet.
bool __cdecl CFunc_ToggleProSet(CStruct* params, CScript*)
{
    Manager* manager = Net();
    int bit;
    uint32_t id;
    params->GetInteger(kBit, &bit, 1);
    params->GetChecksum(kParamId, &id, 1);
    manager->ToggleProSet(static_cast<uint8_t>(bit), id);
    return true;
}

REPLACE(0x00538050, CFunc_TeamFlagTaken);
REPLACE(0x00538170, CFunc_SpawnCrown);
REPLACE(0x00538280, CFunc_StartNetworkGame);
REPLACE(0x00538620, CFunc_SendGameOverToObservers);
REPLACE(0x00538720, CFunc_EndNetworkGame);
REPLACE(0x005388C0, CFunc_GetMyTeam);
REPLACE(0x005388F0, CFunc_SetNumTeams);
REPLACE(0x00538C90, CFunc_JoinTeam);
REPLACE(0x00538D70, CFunc_IsHost);
REPLACE(0x00538DC0, CFunc_AllPlayersAreReady);
REPLACE(0x00538E70, CFunc_CreatePlayerOptions);
REPLACE(0x00539080, CFunc_FillPlayerListMenu);
REPLACE(0x00539470, CFunc_RemovePlayer);
REPLACE(0x00539480, CFunc_CancelRemovePlayer);
REPLACE(0x00539570, CFunc_ToggleProSet);

} // namespace GameNet
