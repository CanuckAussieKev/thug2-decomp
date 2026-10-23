// The skate module's network handlers and their registration (0x0056C920-0x0056F090). See skate_net.h.

#include "game/skate/skate_net.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/net/thugpro_compat.h"
#include "game/net/thugpro_peer.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/skater/local_net_logic.h"
#include "game/skater/skater.h"
#include "game/skater/state_history.h"
#include "game/util/bitstream.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (the original's sprintf into its fixed buffers, kept)

namespace Mdl {

namespace {

using Script::CArray;
using Script::CStruct;
namespace Thugpro = GameNet::Thugpro;

// Names.
constexpr uint32_t kWidth = 0x73E5BAD0;                       // width
constexpr uint32_t kTitle = 0xD4C98794;                       // title
constexpr uint32_t kNetNoticeMsg = 0x170356DF;                // net_notice_msg
constexpr uint32_t kServerRemovedYouDialog = 0xB836BD2B;      // CreateServerRemovedYouDialog
constexpr uint32_t kServerMovedOnDialog = 0x2A874456;         // CreateServerMovedOnDialog
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;                // load_mainmenu
constexpr uint32_t kScavengerAddTime = 0x15B30635;            // goal_scavenger_add_time
constexpr uint32_t kScavenger = 0x1F3430CE;                   // scavenger (a goal)
constexpr uint32_t kScavengerInterval = 0x8A6B04AD;           // scavenger_interval
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F;      // scavenger_collect_period
constexpr uint32_t kName = 0xA1DC81F9;                        // name
constexpr uint32_t kOneLeftYou = 0x8C7162C9;                  // ScavengerHuntOneObjectLeftYou
constexpr uint32_t kOneLeftOther = 0x6343ED6F;                // ScavengerHuntOneObjectLeftOther
constexpr uint32_t kOneLeftOtherTeam = 0x2834F181;            // ScavengerHuntOneObjectLeftOtherTeam
constexpr uint32_t kFiveLeftYou = 0x14D1928C;                 // ScavengerHuntFiveObjectsLeftYou
constexpr uint32_t kFiveLeftOther = 0x53F03C93;               // ScavengerHuntFiveObjectsLeftOther
constexpr uint32_t kFiveLeftOtherTeam = 0x81FECA4E;           // ScavengerHuntFiveObjectsLeftOtherTeam
constexpr uint32_t kId = 0x40C698AF;                          // id
constexpr uint32_t kKillObject = 0xAB0B62AA;                  // ScavengerHunt_KillObject
constexpr uint32_t kIndex = 0x7F8C98FE;                       // index
constexpr uint32_t kObjId = 0x0830ECAF;                       // ObjId
constexpr uint32_t kPos = 0x7F261953;                         // pos
constexpr uint32_t kShowNow = 0x4F285F1E;                     // show_now
constexpr uint32_t kDropObject = 0x5A968743;                  // DropSHObject
constexpr uint32_t kShouldModulateColor = 0x11941568;         // should_modulate_color
constexpr uint32_t kNetFirefight = 0xBFF33600;                // netfirefight
constexpr uint32_t kAnnounceElimination = 0x9B043179;         // announce_elimination
constexpr uint32_t kNotifyCheating = 0x1CEDB62C;              // notify_cheating
constexpr uint32_t kNetElimiskate = 0x10F84276;               // netelimiskate
constexpr uint32_t kShowLeaderMessages = 0xD63DFD0F;          // show_leader_messages
constexpr uint32_t kString0 = 0xA4B08520;                     // String0
constexpr uint32_t kWonGame = 0x96935417;                     // WonGame
constexpr uint32_t kLostGame = 0xE3EC7E9E;                    // LostGame
constexpr uint32_t kLeaderYou = 0xA1D334D0;                   // NewScoreLeaderYou
constexpr uint32_t kLeaderOther = 0x7A4B9D14;                 // NewScoreLeaderOther
constexpr uint32_t kLeaderYourTeam = 0xA8D9DCC2;              // NewScoreLeaderYourTeam
constexpr uint32_t kLeaderOtherTeam = 0x6C499C84;             // NewScoreLeaderOtherTeam
constexpr uint32_t kLevel = 0x651533EC;                       // level
constexpr uint32_t kRequestLevel = 0x38028FC9;                // request_level
constexpr uint32_t kLeaveFrontEnd = 0xB89ABE96;               // leave_front_end
constexpr uint32_t kCleanupBeforeLoading = 0x65ED88DA;        // cleanup_before_loading_level
constexpr uint32_t kProSetGeoFlag = 0xC12C8094;               // FLAG_PROSET1_GEO_ON
constexpr uint32_t kFlag = 0x2E0B1465;                        // flag
constexpr uint32_t kSetFlag = 0x1BC80657;                     // SetFlag
constexpr uint32_t kNetLobby = 0x1C471C60;                    // netlobby
constexpr uint32_t kNetCtf = 0x6C5FF266;                      // netctf
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;               // netgoalattack
constexpr uint32_t kFirefight = 0x3D6D444F;                   // firefight
constexpr uint32_t kTargetScoreArray = 0x50E63314;            // targetScoreArray
constexpr uint32_t kHighestScoreArray = 0x1A2C22F9;           // highestScoreArray
constexpr uint32_t kScore = 0xCD66C8AE;                       // score
constexpr uint32_t kVictoryConditions = 0x94A24C5E;           // victory_conditions
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;            // default_time_limit
constexpr uint32_t kNetGameTypeInfo = 0x2F94C542;             // net_game_type_info
constexpr uint32_t kMpGameTypeInfo = 0x4DA72272;              // mp_game_type_info
constexpr uint32_t kChecksum = 0x21902065;                    // checksum
constexpr uint32_t kTime = 0x906B67BA;                        // time
constexpr uint32_t kUnlimitedTime = 0xF0E712D2;               // unlimited_time
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;            // netscavengerhunt
constexpr uint32_t kElimiskateInterval = 0xF194FB10;          // StartGoal_ElimiSkate_Interval
constexpr uint32_t kElimiskateBreak = 0xA420A073;             // StartGoal_ElimiSkate_Break_Period
constexpr uint32_t kScavengerIntervalGoal = 0x5DF0B0E0;       // StartGoal_Scavenger_Interval
constexpr uint32_t kScavengerCollectGoal = 0x991E4ABE;        // StartGoal_Scavenger_Collect

constexpr uint8_t kSymbolChecksum = 0xD; // ESYMBOLTYPE_NAME
constexpr uint8_t kSymbolString = 3;     // ESYMBOLTYPE_STRING

// The singletons (not rewritten): the skate module, the network manager (GameNet::Manager), the game mode
// (Game::CGameMode), the object manager, the goal manager, the profile manager (graffiti colours).
uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
uint8_t* NetManager() { return GameVariable<uint8_t*>(0x007CCE14); }
GameNet::Manager* Gn(uint8_t* net = NetManager()) { return reinterpret_cast<GameNet::Manager*>(net); }
uint8_t* GameMode() { return GameVariable<uint8_t*>(0x007CE470); }
Obj::CBaseManager* ObjectManager() { return GameVariable<Obj::CBaseManager*>(0x006F0FB8); }
uint8_t* GoalManager() { return GameFunction<uint8_t*(__cdecl*)()>(0x005591B0)(); }

// thiscall calls into code not rewritten yet, by return type.
template <typename R = void, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return GameFunction<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}
template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

CStruct* NewStructure()
{
    CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
CArray* NewArrayInit()
{
    CArray* memory = Script::NewArray();
    return memory ? memory->Init() : nullptr;
}
void Run(uint32_t name, CStruct* params = nullptr, Obj::CObject* object = nullptr)
{
    Script::RunScript(name, params, object, false, nullptr);
}

// A player's info (GameNet::PlayerInfo): +0x14 its skater, +0x30 its team, +0x34 its scavenger hunt objects, +0x38
// its name.
GameNet::PlayerInfo* Info(const uint8_t* player)
{
    return reinterpret_cast<GameNet::PlayerInfo*>(const_cast<uint8_t*>(player));
}
uint8_t* LocalPlayer() { return static_cast<uint8_t*>(Gn()->GetLocalPlayer()); }
uint8_t* PlayerByObjectId(uint32_t id) { return Gn()->PlayerByObjectId(static_cast<uint16_t>(id)); }
bool IsLocalPlayer(const uint8_t* player) { return Info(player)->IsLocalPlayer() != 0; }
// 0x005430D0 on the network manager: its last leading team (+0x1D4) set - the getter 0x005331A0's field. (The
// address is folded with PlayerInfo::SetReadyQueryOwed, a setter of the same offset.)
void SetLastLeadingTeam(uint8_t* net, int team) { Field<int>(net, 0x1D4) = team; }
Obj::CSkater* Skater(uint8_t* skater) { return reinterpret_cast<Obj::CSkater*>(skater); }
Obj::CObject* PlayerSkater(const uint8_t* player) { return Field<Obj::CObject*>(player, 0x14); }

// A goal by name (the goal manager's 0x00557F90) and two of its virtuals: +0xC is it active, +0x14 its update.
uint8_t* GetGoal(uint8_t* manager, uint32_t name) { return Call<uint8_t*>(0x00557F90, manager, name, 1); }
bool GoalVirtualBool(uint8_t* goal, int offset)
{
    return reinterpret_cast<bool(__thiscall*)(uint8_t*)>(Field<void**>(goal, 0)[offset / 4])(goal);
}
void GoalVirtual(uint8_t* goal, int offset)
{
    reinterpret_cast<void(__thiscall*)(uint8_t*)>(Field<void**>(goal, 0)[offset / 4])(goal);
}

// Lst::Search<PlayerInfo> (vtable 0x0064C1A8) for the network manager's FirstPlayerInfo / NextPlayerInfo.
struct PlayerSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064C1A8);
    void* m_node = nullptr;
};

// A skater's state history (+0x27C): the newest of 20 entries at count % 20.
struct StateEntry {
    int16_t pos[3];         // +0x00 the position, in half units (y in quarters)
    uint16_t pad06;
    Mth::Matrix matrix;     // +0x08 from the angles
    Mth::Vector position;   // +0x48 (w 0)
    float angles[3];        // +0x58
    uint32_t unknown64;
    uint32_t value68;       // +0x68 (5 bits)
    uint32_t value6C;       // +0x6C (3 bits)
    int state;              // +0x70 (3 bits; kept as a signed byte)
    uint8_t flag74;         // +0x74 (always 0 from a message: bit 7 of a 4-bit value)
    uint8_t flag75;         // +0x75
    uint8_t flag76;         // +0x76
    uint8_t pad77;
    uint16_t time;          // +0x78 the message's time stamp
    uint16_t wraps;         // +0x7A the times the time stamp went back
    uint8_t value7C;        // +0x7C (6 bits)
    uint8_t pad7D[3];
    uint32_t value80;       // +0x80 (32 bits)
};
static_assert(sizeof(StateEntry) == 0x84 && offsetof(StateEntry, angles) == 0x58 &&
                  offsetof(StateEntry, value80) == 0x80,
              "layout must match the original");
struct StateHistory {
    uint8_t unknown0[0x18];
    int count;              // +0x18
    StateEntry entries[20]; // +0x1C
};

// A Tsk::Task<Skate> (0x20 bytes): the task node (0x00406890 makes it with its priority), +0x18 the code, +0x1C
// the skate module.
uint8_t* NewTask(uint8_t* skate, uint32_t code)
{
    auto* task = static_cast<uint8_t*>(Mem::Calloc(0x20));
    if (!task)
        return nullptr;
    reinterpret_cast<Tsk::BaseTask*>(task)->Construct(-1000);
    Field<uint32_t>(task, 0) = 0x006476F0;
    Field<uint32_t>(task, 0x18) = code;
    Field<uint8_t*>(task, 0x1C) = skate;
    return task;
}

void AddHandler(Net::App* app, int id, Net::MsgHandlerCode code, uint32_t flags, void* data, int priority = 0x80)
{
    app->m_dispatcher.AddHandler(static_cast<uint8_t>(id), code, flags, data, priority);
}
// (a handler not rewritten yet, by address)
void AddHandler(Net::App* app, int id, uint32_t code, uint32_t flags, void* data, int priority = 0x80)
{
    AddHandler(app, id, reinterpret_cast<Net::MsgHandlerCode>(code), flags, data, priority);
}

} // namespace

// 0x0056E2B0
void SkateNet::CreateNetworkTasks()
{
    auto* skate = reinterpret_cast<uint8_t*>(this);
    Field<uint8_t*>(skate, 0x18) = NewTask(skate, 0x00569BB0);
    Field<uint8_t*>(skate, 0x1C) = NewTask(skate, 0x0056BE10);
}

// 0x0056DEE0
void SkateNet::StartServer()
{
    uint8_t* mlp = GameVariable<uint8_t*>(0x00696D68);
    uint8_t* net = NetManager();
    bool autoServing = false;
    if (Gn(net)->InNetMode() != 0) {
        autoServing = true;
        Gn(net)->DisconnectClients();
    }
    auto* server = Gn(net)->SpawnServer(!autoServing, true);
    if (!server)
        return;
    auto* skate = reinterpret_cast<uint8_t*>(this);
    reinterpret_cast<Tsk::Stack*>(mlp + 4)->AddTask(Field<Tsk::BaseTask*>(skate, 0x18));
    reinterpret_cast<Tsk::Stack*>(mlp + 4)->AddTask(Field<Tsk::BaseTask*>(skate, 0x1C));
    AddHandler(server, 0x3B, 0x00569E20, 0, nullptr);
    AddHandler(server, 0x52, 0x00569EF0, 0, this);
    AddHandler(server, 0x86, 0x00569E90, 0, this);
    AddHandler(server, 0x4C, 0x0056A000, 2, nullptr);
    AddHandler(server, 0x45, 0x0056A3E0, 2, nullptr);
    AddHandler(server, 0x23, &SkateNet::HandleClientObjectUpdate, 3, nullptr);
    AddHandler(server, 0x8F, 0x0056B0B0, 0, this);
    AddHandler(server, 0x90, 0x0056AD10, 0, this);
    if (server->IsLocal())
        return;
    // The messages a server relays for its clients.
    AddHandler(server, 0x7E, 0x0056B5D0, 0, nullptr);
    AddHandler(server, 0x97, 0x0056B2B0, 0, this);
    AddHandler(server, 0x98, 0x0056B2B0, 0, this);
    AddHandler(server, 0x9A, 0x0056B2B0, 0, this);
    AddHandler(server, 0x83, 0x0056A000, 0, this);
    for (int id : {0x99, 0x89})
        AddHandler(server, id, 0x0056A000, 0, nullptr);
    AddHandler(server, 0x89, 0x00569FD0, 0, nullptr);
    AddHandler(server, 0x29, 0x0056A000, 0, nullptr);
    AddHandler(server, 0x2B, 0x0056A170, 0, nullptr);
    for (int id : {0x2A, 0x76, 0x8C, 0x77, 0x78, 0x2C})
        AddHandler(server, id, 0x0056A000, 0, nullptr);
    AddHandler(server, 0x3C, 0x0056A170, 0, nullptr);
    for (int id : {0x3D, 0x3E, 0x41})
        AddHandler(server, id, 0x0056A000, 0, nullptr);
    AddHandler(server, 0x35, 0x0056A170, 0, nullptr);
    for (int id : {0x36, 0x39, 0x3A, 0x37})
        AddHandler(server, id, 0x0056A000, 0, nullptr);
    AddHandler(server, 0x38, 0x0056A000, 2, nullptr);
    AddHandler(server, 0x48, 0x0056A2D0, 2, nullptr);
    AddHandler(server, 0x49, 0x0056A2D0, 2, nullptr);
}

// 0x0056EB50
void SkateNet::AddClientHandlers(Net::App* client, int index)
{
    AddHandler(client, 0x33, HandleStartInfo, 0, this);
    AddHandler(client, 0x23, HandleObjectUpdates, 3, this);
    AddHandler(client, 0x29, 0x0056A450, 0, this);
    AddHandler(client, 0x7F, 0x0056C7E0, 0, this);
    for (int id : {0x2B, 0x76, 0x8C, 0x77, 0x78, 0x2A})
        AddHandler(client, id, 0x0056A450, 0, this);
    AddHandler(client, 0x2C, 0x0056A450, 0, this, 0xFF);
    AddHandler(client, 0x2C, 0x0056C8C0, 0, this);
    for (int id : {0x3C, 0x3D, 0x3E, 0x41})
        AddHandler(client, id, 0x0056A450, 0, this);
    AddHandler(client, 0x2D, 0x0056ABD0, 2, this);
    AddHandler(client, 0x32, 0x0056AC50, 2, this);
    AddHandler(client, 0x35, 0x0056B800, 0, this);
    AddHandler(client, 0x36, 0x0056B800, 0, this);
    AddHandler(client, 0x37, 0x0056B7A0, 0, this);
    AddHandler(client, 0x38, 0x0056B7A0, 0, this);
    AddHandler(client, 0x39, 0x0056B7E0, 0, this);
    AddHandler(client, 0x3A, 0x0056B7E0, 0, this);
    AddHandler(client, 0x40, HandleScores, 2, this);
    AddHandler(client, 0x46, 0x00569560, 0, this);
    AddHandler(client, 0x48, HandleRunScript, 2, this);
    AddHandler(client, 0x49, 0x0056C330, 2, this);
    AddHandler(client, 0x51, HandleServerRemovedYou, 2, this);
    AddHandler(client, 0x56, HandleServerRemovedYou, 2, this);
    AddHandler(client, 0x5B, 0x0056AC90, 0, this);
    AddHandler(client, 0x5C, HandleGraffitiColors, 0, this);
    AddHandler(client, 0x7E, 0x0056B5D0, 0, this);
    for (int id : {0x96, 0x97, 0x98, 0x9A})
        AddHandler(client, id, 0x0056B2B0, 0, this);
    AddHandler(client, 0x82, 0x0056C6E0, 0, this);
    AddHandler(client, 0x83, 0x0056C3D0, 0, this);
    AddHandler(client, 0x8F, HandleScavengerDropObjects, 0, this);
    AddHandler(client, 0x89, 0x0056A410, 0, this);
    AddHandler(client, 0x95, HandleScavengerHunt, 0, this);
    AddHandler(client, 0x93, HandleScavengerHunt, 0, this);
    AddHandler(client, 0x90, HandleScavengerKillObject, 0, this);
    AddHandler(client, 0x99, 0x0056B220, 0, this);
    // The handlers of this client's own skaters.
    for (uint32_t i = 0; i < 8; ++i) {
        uint8_t* skater = Call<uint8_t*>(0x005665C0, this, i);
        if (!skater || !Field<bool>(skater, 0xBC) || Skater(skater)->GetSkaterNumber() != index)
            continue;
        AddHandler(client, 0x3B, 0x005626F0, 0, skater);
        AddHandler(client, 0x44, &Obj::CSkaterStateHistoryComponent::HandleCollisionMessages, 0, skater);
        AddHandler(client, 0x43, &Obj::CSkaterStateHistoryComponent::HandleCollisionMessages, 0, skater);
        AddHandler(client, 0x85, &Obj::CSkaterStateHistoryComponent::HandleHitMessages, 0, skater);
        AddHandler(client, 0x84, &Obj::CSkaterStateHistoryComponent::HandleHitMessages, 0, skater);
        AddHandler(client, 0x4B, &Obj::CSkaterLocalNetLogicComponent::HandleGraffitiSteal, 0, skater);
    }
    if (index == 0) {
        for (int id : {0x91, 0x92, 0x94})
            AddHandler(client, id, HandleScavengerHunt, 0, this);
    }
    // NOT ORIGINAL - the THUGPRO peer mode's messages, 155-159 (game/net/thugpro_peer.h)
    if (GameNet::Thugpro::PeerMode())
        GameNet::Thugpro::AddClientHandlers(client, this);
}

// 0x0056C920
int SkateNet::HandleServerRemovedYou(Net::MsgHandlerContext* context)
{
    uint8_t* skate = Skate();
    uint8_t* net = NetManager();
    Script::DeleteSpawnedScripts();
    CStruct* params = NewStructure();
    params->AddComponentOfType(kWidth, Script::ESYMBOLTYPE_INTEGER, 300);
    params->AddComponentOfType(kTitle, kSymbolChecksum, kNetNoticeMsg);
    // (the dialogs get no parameters: the structure isn't passed)
    if (context->m_MsgId == 'Q')
        Run(kServerRemovedYouDialog);
    else if (context->m_MsgId == 'V')
        Run(kServerMovedOnDialog);
    DeleteStructure(params);
    if (Field<uint32_t>(skate, 0x374) == kLoadMainMenu) {
        Gn(net)->CancelJoin();
        Gn(net)->SetJoinState(10);
        return 2;
    }
    Gn(net)->RemovePlayers();
    Gn(net)->DisconnectClients();
    return 3;
}

// 0x0056CA70
int SkateNet::HandleScavengerHunt(Net::MsgHandlerContext* context)
{
    switch (context->m_MsgId) {
    case 0x91:
        Run(kScavengerAddTime);
        return 1;
    case 0x92: {
        uint8_t* goal = GetGoal(GoalManager(), kScavenger);
        if (GoalVirtualBool(goal, 0xC))
            GoalVirtual(goal, 0x14);
        return 1;
    }
    case 0x93: {
        uint8_t* manager = GoalManager();
        uint8_t* interval = GetGoal(manager, kScavengerInterval);
        if (GoalVirtualBool(interval, 0xC)) {
            GoalVirtual(interval, 0x14);
        } else if (GoalVirtualBool(GetGoal(manager, kScavenger), 0xC)) {
            Script::RunScript("ForceStartScavengerCollect", nullptr, nullptr, false);
        }
        return 1;
    }
    case 0x94: {
        uint8_t* goal = GetGoal(GoalManager(), kScavengerCollectPeriod);
        if (GoalVirtualBool(goal, 0xC))
            GoalVirtual(goal, 0x14);
        return 1;
    }
    case 0x95:
        break;
    default:
        return 1;
    }
    // Objects left: [0] the player (or the team), [1] how many.
    auto* data = reinterpret_cast<const int8_t*>(context->m_Msg);
    uint8_t* net = NetManager();
    CStruct* params = nullptr;
    char text[128];
    if (Call<bool>(0x005535F0, GameMode())) { // teams
        int localTeam = -1;
        uint8_t* local = static_cast<uint8_t*>(Gn(net)->GetLocalPlayer());
        if (local && !Info(local)->IsObserver() && Info(local)->IsFullyIn())
            localTeam = Field<int>(local, 0x30);
        params = NewStructure();
        params->AddString(kName, reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(local) + 0x38));
        if (data[1] == 1 || data[1] == 5) {
            bool one = data[1] == 1;
            if (data[0] == localTeam) {
                Run(one ? kOneLeftYou : kFiveLeftYou);
            } else {
                std::sprintf(text, "team_%d_name", data[0] + 1);
                params->AddString(kName, Script::GetString(text));
                Run(one ? kOneLeftOtherTeam : kFiveLeftOtherTeam, params);
            }
        }
    } else {
        uint8_t* player = Gn(net)->PlayerByObjectId(static_cast<uint16_t>(static_cast<int16_t>(data[0])));
        if (!player)
            return 1;
        params = NewStructure();
        params->AddString(kName, reinterpret_cast<const char*>(player + 0x38));
        if (data[1] == 1 || data[1] == 5) {
            bool one = data[1] == 1;
            if (IsLocalPlayer(player))
                Run(one ? kOneLeftYou : kFiveLeftYou);
            else
                Run(one ? kOneLeftOther : kFiveLeftOther, params);
        }
    }
    DeleteStructure(params);
    return 1;
}

// 0x0056CDF0
int SkateNet::HandleScavengerKillObject(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<const uint32_t*>(context->m_Msg);
    CStruct* params = NewStructure();
    params->AddChecksum(kId, data[0]);
    Run(kKillObject, params);
    DeleteStructure(params);
    return 1;
}

// 0x0056CEC0
int SkateNet::HandleScavengerDropObjects(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<const uint8_t*>(context->m_Msg);
    uint8_t* player = PlayerByObjectId(static_cast<uint32_t>(static_cast<int16_t>(static_cast<int8_t>(data[0]))));
    if (!player)
        return 4;
    Field<int>(player, 0x34) = *reinterpret_cast<const int*>(data + 4);
    auto* positions = reinterpret_cast<const Script::VectorValue*>(data + 8);
    for (int i = 0; i < Field<int>(player, 0x34); ++i) {
        CStruct* params = NewStructure();
        params->AddInteger(kIndex, i);
        params->AddInteger(kObjId, static_cast<int>(PlayerSkater(player)->m_id));
        params->AddVector(kPos, positions[i]);
        params->AddChecksum(kShowNow, kShowNow);
        Run(kDropObject, params, PlayerSkater(player));
        DeleteStructure(params);
    }
    return 1;
}

// 0x0056D030
int SkateNet::HandleGraffitiColors(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<const uint8_t*>(context->m_Msg);
    if (!Call<bool>(0x00553220, GameMode(), kShouldModulateColor)) {
        Dbg::Printf("No need to continue cause we're not in graffiti mode");
        return 4;
    }
    const uint8_t* colors = data + 8;
    uint8_t* profiles = GameVariable<uint8_t*>(0x007CE500);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    CStruct* params = NewStructure();
    for (int i = 0; i < 8; ++i) {
        if (!data[i])
            continue;
        CArray* array = NewArrayInit();
        array->SetSizeAndType(data[i], static_cast<Script::ESymbolType>(kSymbolChecksum));
        for (int j = 0; j < static_cast<int>(data[i]); ++j)
            array->SetElement(static_cast<uint32_t>(j),
                              reinterpret_cast<Obj::CTrickObjectManager*>(profiles)->GetClusterName(*colors++));
        char name[32];
        std::sprintf(name, "Skater%d", i);
        params->AddArray(Crc::GenerateCRCFromString(name), array); // (the array isn't freed: the original's too)
    }
    reinterpret_cast<Obj::CTrickCluster*>(profiles)->SetParams(params); // (on the manager: its +0x38)
    reinterpret_cast<Obj::CTrickObjectManager*>(profiles)->ApplyPlayerGraffiti();
    DeleteStructure(params);
    Mem::Manager::Get()->PopContext();
    return 1;
}

// 0x0056D250
int SkateNet::HandleScores(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<const uint8_t*>(context->m_Msg);
    auto* skate = static_cast<uint8_t*>(context->m_data);
    uint8_t* net = NetManager();
    if (!Gn(net)->OnServer())
        Call(0x00553400, GameMode(), *reinterpret_cast<const uint32_t*>(data));
    // Each skater's score: [9] entries of 5 bytes from [10] - the object's id, its score.
    const uint8_t* entry = data + 10;
    for (int i = 0; i < static_cast<int8_t>(data[9]); ++i) {
        int8_t id = static_cast<int8_t>(entry[0]);
        int value = *reinterpret_cast<const int*>(entry + 1);
        entry += 5;
        Obj::CObject* object = ObjectManager()->GetObjectByID(static_cast<uint32_t>(static_cast<int>(id)));
        if (!object)
            continue;
        auto* skater = reinterpret_cast<uint8_t*>(object);
        uint8_t* score;
        if (!Call<bool>(0x00553220, GameMode(), kShouldModulateColor) && Call<bool>(0x005530F0, GameMode())) {
            if (Field<bool>(skater, 0xBC)) // (a local skater keeps its own)
                continue;
            score = static_cast<uint8_t*>(Skater(skater)->GetScore());
        } else {
            score = static_cast<uint8_t*>(Skater(skater)->GetScore());
            if (Call<uint32_t>(0x005532A0, GameMode()) == kNetFirefight && !Field<bool>(skater, 0xBC) &&
                Call<int>(0x00561B40, score) > 0 && value == 0) {
                // Eliminated.
                if (uint8_t* player = PlayerByObjectId(object->m_id))
                    Info(player)->MarkEliminated();
                CStruct* params = NewStructure();
                params->AddString(kName, Skater(skater)->GetDisplayName());
                Run(kAnnounceElimination, params);
                DeleteStructure(params);
                Call(0x00564C60, skate, skater, 1);
            }
        }
        Call(0x00561A80, score, value);
    }
    if (data[8])
        Gn(net)->SetFinalScoresReceived(true);
    if (*reinterpret_cast<const int*>(data + 4) != Call<int>(0x005695A0, skate) && !Gn(net)->CheatingNoted()) {
        Run(kNotifyCheating);
        Gn(net)->NoteCheating();
    }
    bool final = data[8] != 0;
    char text[128];
    if (Call<bool>(0x005535F0, GameMode()) && Call<uint32_t>(0x005532A0, GameMode()) != kNetElimiskate) {
        // The leading team (none on a tie).
        int teams = Call<int>(0x00553130, GameMode());
        int best = -1;
        int bestScore = 0;
        for (int team = 0; team < teams; ++team) {
            int score = Gn(net)->GetTeamScore(team);
            if (score > bestScore) {
                bestScore = score;
                best = team;
            } else if (score == bestScore) {
                best = -1;
            }
        }
        if (best == -1)
            return 1;
        uint8_t* local = static_cast<uint8_t*>(Gn(net)->GetLocalPlayer());
        if (final) {
            Obj::CObject* skater = PlayerSkater(local);
            if (skater)
                skater->SelfEvent(Field<int>(local, 0x30) == best ? kWonGame : kLostGame, nullptr);
            return 1;
        }
        if (Gn(net)->GetLastLeadingTeam() == best || !Call<bool>(0x00553220, GameMode(), kShowLeaderMessages) ||
            Gn(net)->IsGameOver())
            return 1;
        if (PlayerSkater(local) && Field<int>(local, 0x30) == best) {
            Run(kLeaderYourTeam, nullptr, PlayerSkater(local));
        } else {
            std::sprintf(text, "team_%d_name", best + 1);
            CStruct* params = NewStructure();
            params->Clear();
            params->AddString(kString0, kSymbolString, Script::GetString(text));
            Run(kLeaderOtherTeam, params, PlayerSkater(local));
            DeleteStructure(params);
        }
        SetLastLeadingTeam(net, best);
        return 1;
    }
    // The leading player (none on a tie).
    PlayerSearch search;
    int count = 0;
    uint8_t* best = nullptr;
    int bestScore = 0;
    for (auto* player = static_cast<uint8_t*>(Gn(net)->FirstPlayerInfo(&search, false)); player;
         player = static_cast<uint8_t*>(Gn(net)->NextPlayerInfo(&search, false))) {
        int score = Gn(net)->GetPlayerScore(PlayerSkater(player)->m_id);
        if (score > bestScore) {
            bestScore = score;
            best = player;
        } else if (score == bestScore) {
            best = nullptr;
        }
        ++count;
    }
    if (!best)
        return 1;
    uint8_t* local = static_cast<uint8_t*>(Gn(net)->GetLocalPlayer());
    if (final) {
        // (the loser's event goes to the leader's skater too: the original's)
        if (IsLocalPlayer(best))
            PlayerSkater(best)->SelfEvent(kWonGame, nullptr);
        else if (PlayerSkater(local))
            PlayerSkater(best)->SelfEvent(kLostGame, nullptr);
        return 1;
    }
    if (static_cast<uint8_t*>(Gn(net)->GetLastLeader()) == best || count <= 1 ||
        !Call<bool>(0x00553220, GameMode(), kShowLeaderMessages) || GameVariable<bool>(0x006FA7D4))
        return 1;
    if (IsLocalPlayer(best)) {
        Run(kLeaderYou, nullptr, PlayerSkater(best));
    } else {
        CStruct* params = NewStructure();
        params->Clear();
        params->AddString(kString0, kSymbolString, reinterpret_cast<const char*>(best + 0x38));
        Run(kLeaderOther, params, PlayerSkater(local));
        DeleteStructure(params);
    }
    Gn(net)->SetLastLeader(best);
    return 1;
}

// 0x0056D810
int SkateNet::HandleStartInfo(Net::MsgHandlerContext* context)
{
    uint8_t* net = NetManager();
    Gn(net)->SetJoinState(8);
    Gn(net)->ClearServerList();
    auto* data = reinterpret_cast<const uint8_t*>(context->m_Msg);
    auto* skate = static_cast<uint8_t*>(context->m_data);
    auto u32 = [data](uint32_t offset) { return *reinterpret_cast<const uint32_t*>(data + offset); };
    Net::Conn* conn = context->m_Conn; // (the server's connection)
    conn->SetBandwidthType(data[1] == 0 ? 1 : 0);
    conn->SetSendInterval(static_cast<uint32_t>((1000 / GameVariable<int>(0x00684FB4)) << 1));
    Gn(net)->SetGameId(data[2]);
    Gn(net)->SetLevel(u32(0x10));
    Gn(net)->SetCrownSpawnPoint(static_cast<char>(data[0]));
    Gn(net)->SetStartInfoWords(reinterpret_cast<const uint32_t*>(data + 0x28));
    CStruct* params = NewStructure();
    params->Clear();
    params->AddComponentOfType(kLevel, kSymbolChecksum, u32(0x10));
    Run(kRequestLevel, params);
    DeleteStructure(params);
    Run(kLeaveFrontEnd);
    Run(kCleanupBeforeLoading);
    conn->UpdateCommTime(0);
    Call(0x005531B0, GameMode(), static_cast<int>(static_cast<int8_t>(data[4])));
    Call(0x00553AD0, GameMode(), u32(0xC));
    Call(0x00566A60, skate);
    Call(0x00553AD0, GameMode(), u32(0xC));
    Call(0x005535A0, GameMode(), u32(0x14));
    Call(0x005530A0, GameMode(), u32(0x18));
    Call(0x00553080, GameMode(), u32(0x1C));
    Call(0x00553250, GameMode(), u32(0x48));
    Gn(net)->SetProSetFlags(static_cast<char>(data[3]));
    // The pro set geometry flags.
    for (int i = 0; i < 7; ++i) {
        if (!(static_cast<int>(static_cast<int8_t>(data[3])) & (1 << i)))
            continue;
        CStruct* flag = NewStructure();
        flag->AddInteger(kFlag, Script::GetInteger(kProSetGeoFlag) + i);
        Run(kSetFlag, flag);
        DeleteStructure(flag);
    }
    // The victory conditions.
    CStruct* victory = NewStructure();
    uint32_t mode = u32(0xC);
    if (mode != kNetLobby) {
        if (u32(0x20) == 0 && mode != kNetCtf && mode != kNetGoalAttack) {
            if (mode != kFirefight && mode != kNetFirefight) {
                CArray* conditions = NewArrayInit();
                Script::CopyArray(conditions, Script::GetArray(kTargetScoreArray));
                auto* condition = reinterpret_cast<CStruct*>(conditions->GetElement(0));
                condition->AddComponentOfType(kScore, Script::ESYMBOLTYPE_INTEGER, u32(0x24));
                victory->AddArray(kVictoryConditions, conditions);
            }
            victory->AddComponentOfType(kDefaultTimeLimit, Script::ESYMBOLTYPE_INTEGER, 0);
            Call(0x00564D50, skate, 0);
        } else {
            victory->AddComponentOfType(kDefaultTimeLimit, Script::ESYMBOLTYPE_INTEGER, u32(0x20));
            if (u32(0xC) == kNetCtf) {
                CArray* conditions = NewArrayInit();
                Script::CopyArray(conditions, Script::GetArray(kHighestScoreArray));
                victory->AddArray(kVictoryConditions, conditions);
            }
            Call(0x00564D50, skate, u32(0x20));
            Call(0x00553400, GameMode(), u32(8));
        }
        Call(0x00553BE0, GameMode(), victory);
    }
    DeleteStructure(victory);
    // The mode's goal_script.
    CArray* types = Script::GetArray(Gn(net)->InNetGame() ? kNetGameTypeInfo : kMpGameTypeInfo);
    for (int i = 0; i < static_cast<int>(types->m_size); ++i) {
        auto* type = reinterpret_cast<CStruct*>(types->GetElement(static_cast<uint32_t>(i)));
        uint32_t checksum;
        type->GetChecksum(kChecksum, &checksum, 1);
        if (checksum != u32(0xC))
            continue;
        CStruct* goalParams = NewStructure();
        goalParams->AddInteger(kTime, static_cast<int>(u32(0x20)));
        goalParams->AddInteger(kScore, static_cast<int>(u32(0x24)));
        goalParams->AddInteger(kUnlimitedTime, u32(0x20) == 0);
        // (the original keeps the script in its argument's slot: with no goal_script, the skate module's address)
        uint32_t script = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(skate));
        uint8_t variant = data[0x4C];
        if (!variant)
            type->GetChecksum("goal_script", &script, 1);
        else if (u32(0xC) == kNetElimiskate)
            script = variant == 1 ? kElimiskateInterval : kElimiskateBreak;
        else if (u32(0xC) == kNetScavengerHunt)
            script = variant == 1 ? kScavengerIntervalGoal : kScavengerCollectGoal;
        Run(script, goalParams);
        DeleteStructure(goalParams);
        break;
    }
    if (Gn(net)->InNetGame() && Gn(net)->GetJoinMode() == 0 &&
        !GameFunction<bool(__cdecl*)(CStruct*, Script::CScript*)>(0x005A0D90)(nullptr, nullptr)) { // IsObserving
        Gn(net)->PrepareLocalFace();
        Gn(net)->SendCustomTagTexture();
    }
    return 1;
}

// 0x0056E380
int SkateNet::HandleRunScript(Net::MsgHandlerContext* context)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_scriptHeap);
    auto* data = reinterpret_cast<const uint32_t*>(context->m_Msg);
    Obj::CObject* object = ObjectManager()->GetObjectByID(data[1]);
    CStruct* params = NewStructure();
    Script::ReadFromBuffer(params, reinterpret_cast<const uint8_t*>(data + 2)); // the script reader
    Run(data[0], params, object);
    DeleteStructure(params);
    Mem::Manager::Get()->PopContext();
    return 1;
}

// 0x0056E470
int SkateNet::HandleObjectUpdates(Net::MsgHandlerContext* context)
{
    Util::BitStream stream;
    stream.SetInputData(context->m_Msg, 0x400);
    uint32_t objects = stream.ReadBits(8);
    for (uint32_t i = 0; i < 8; ++i) {
        if (!(objects & (1u << i)))
            continue;
        uint16_t time = static_cast<uint16_t>(stream.ReadBits(16));
        uint32_t fields = stream.ReadBits(9);
        int16_t pos[3] = {};
        float angles[3] = {};
        uint8_t state = 0, flag74 = 0, value7C = 0, flag75 = 0, flag76 = 0;
        uint32_t value68 = 0, value6C = 0, value80 = 0;
        for (int k = 0; k < 3; ++k) {
            if (fields & (1u << k))
                pos[k] = static_cast<int16_t>(stream.ReadSignedBits(16));
        }
        for (int k = 0; k < 3; ++k) {
            if (fields & (8u << k))
                angles[k] = static_cast<float>(static_cast<int16_t>(stream.ReadSignedBits(16)) * (1.0 / 4096.0));
        }
        if (fields & 0x40) {
            uint32_t bits = stream.ReadBits(4);
            state = static_cast<uint8_t>(bits & 7);
            flag74 = static_cast<uint8_t>((bits >> 7) & 1); // (always 0: the original's)
            value7C = static_cast<uint8_t>(stream.ReadBits(6));
            flag75 = static_cast<uint8_t>(stream.ReadBits(1));
            flag76 = static_cast<uint8_t>(stream.ReadBits(1));
        }
        if (fields & 0x80) {
            value68 = stream.ReadBits(5);
            value6C = stream.ReadBits(3);
        }
        if (fields & 0x100)
            value80 = stream.ReadBits(32);

        Obj::CObject* object = ObjectManager()->GetObjectByID(i);
        if (!object || (context->m_PacketFlags & 3))
            continue;
        auto* skater = static_cast<Obj::CCompositeObject*>(object);
        auto* history = Field<StateHistory*>(object, 0x27C);
        int previous;
        if (history->count > 0) {
            previous = (history->count - 1) % 20;
        } else {
            // The first: what came before is where the skater is now.
            Mth::Matrix matrix;
            matrix = skater->m_matrix;
            Mth::Vector eulers;
            matrix.GetEulerAngles(eulers);
            StateEntry& first = history->entries[0];
            for (int k = 0; k < 3; ++k) {
                // (TP-R6: in a big level as sent there; THUGPRO's own left this one in halves / quarters)
                if (k == 1)
                    first.pos[1] = Thugpro::PackHeight(skater->m_pos.y);
                else
                    first.pos[k] = Thugpro::PackHorizontal((&skater->m_pos.x)[k]);
                first.angles[k] = (&eulers.x)[k];
            }
            previous = 0;
        }
        StateEntry& prev = history->entries[previous];
        StateEntry& cur = history->entries[history->count % 20];
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (1u << k)))
                pos[k] = prev.pos[k];
        }
        for (int k = 0; k < 3; ++k)
            cur.pos[k] = pos[k];
        // (TP-R6: whole units in a big level)
        float position[3] = {Thugpro::UnpackHorizontal(pos[0]), Thugpro::UnpackHeight(pos[1]),
                             Thugpro::UnpackHorizontal(pos[2])};
        for (int k = 0; k < 3; ++k) {
            if (!(fields & (8u << k)))
                angles[k] = prev.angles[k];
        }
        for (int k = 0; k < 3; ++k)
            cur.angles[k] = angles[k];
        Mth::Matrix matrix;
        matrix.SetFromEulerAngles(angles[0], angles[1], angles[2]);
        cur.matrix = matrix;
        cur.position = {position[0], position[1], position[2], 0.0f};
        if (!(fields & 0x40)) {
            state = static_cast<uint8_t>(prev.state);
            flag74 = prev.flag74;
            value7C = prev.value7C;
            flag75 = prev.flag75;
            flag76 = prev.flag76;
        }
        if (!(fields & 0x80)) {
            value68 = prev.value68;
            value6C = prev.value6C;
        }
        if (!(fields & 0x100))
            value80 = prev.value80;
        cur.value68 = value68;
        cur.value6C = value6C;
        cur.state = static_cast<int8_t>(state);
        cur.value7C = value7C;
        cur.flag74 = flag74;
        cur.flag76 = flag76;
        cur.flag75 = flag75;
        cur.value80 = value80;
        uint16_t wraps = prev.wraps;
        if (prev.time > time) // (the time stamp went round)
            ++wraps;
        cur.wraps = wraps;
        cur.time = time;
        ++history->count;
    }
    return 1;
}

namespace {

// A state entry's time stamp as 32 bits (0x005197D0 / 0x005197E0: +0x78 the low half, +0x7A the high).
uint32_t EntryTime(const StateEntry& entry) { return static_cast<uint32_t>(entry.wraps) << 16 | entry.time; }

// The server's record of what it last sent one client about each of the 8 skaters (by object id).
struct RelaySent {
    uint8_t unknown0[0x50];
    uint32_t forcedFrame[8];   // +0x50 the server app's frame when everything was last sent
    uint32_t time[8];          // +0x70 the entry's time stamp last sent
    uint8_t value7C[8];        // +0x90
    uint8_t flag75[8];         // +0x98
    uint8_t flag76[8];         // +0xA0
    int16_t pos[8][3];         // +0xA8
    int16_t angles[8][3];      // +0xD8
    uint32_t value68[8];       // +0x108
    uint32_t value6C[8];       // +0x128
    uint8_t state[8];          // +0x148
    uint8_t flag74[8];         // +0x150
    uint8_t unknown158[8];
    uint32_t value80[8];       // +0x160
};
static_assert(offsetof(RelaySent, time) == 0x70 && offsetof(RelaySent, pos) == 0xA8 &&
                  offsetof(RelaySent, angles) == 0xD8 && offsetof(RelaySent, value68) == 0x108 &&
                  offsetof(RelaySent, state) == 0x148 && offsetof(RelaySent, value80) == 0x160,
              "layout must match the original");

// The server's frame count (its app: the network manager's +4, read through 0x005480C0 - a folded getter).
uint32_t ServerFrame()
{
    Net::App* server = Gn()->m_server;
    return static_cast<uint32_t>(server->m_frameCount);
}

int16_t PackAngle(float angle) { return static_cast<int16_t>(static_cast<int>(static_cast<double>(angle) * 4096.0)); }

} // namespace

// 0x005696C0
uint32_t SkateNet::GetRelayFlags(void* sentRecord, void* stateEntry, uint8_t index)
{
    auto* sent = static_cast<RelaySent*>(sentRecord);
    auto* entry = static_cast<StateEntry*>(stateEntry);
    uint32_t flags = 0;
    // Everything again once 60 of the server's frames have gone by.
    uint32_t frame = ServerFrame();
    uint32_t since = frame > sent->forcedFrame[index] ? ServerFrame() - sent->forcedFrame[index]
                                                       : sent->forcedFrame[index] - ServerFrame();
    bool force = since >= 60;
    if (force)
        sent->forcedFrame[index] = ServerFrame();
    float angles[3] = {entry->angles[0], entry->angles[1], entry->angles[2]};
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value = k == 1 ? Thugpro::PackHeight((&entry->position.x)[1]) : Thugpro::PackHorizontal((&entry->position.x)[k]);
        if (value != sent->pos[index][k] || force)
            flags |= 1u << k;
    }
    for (int k = 0; k < 3; ++k) {
        if (PackAngle(angles[k]) != sent->angles[index][k] || force)
            flags |= 8u << k;
    }
    if (static_cast<uint8_t>(entry->state) != sent->state[index] || entry->flag74 != sent->flag74[index] ||
        entry->value7C != sent->value7C[index] || entry->flag75 != sent->flag75[index] ||
        entry->flag76 != sent->flag76[index] || force)
        flags |= 0x40;
    if (entry->value68 != sent->value68[index] || entry->value6C != sent->value6C[index] || force)
        flags |= 0x80;
    if (entry->value80 != sent->value80[index] || force)
        flags |= 0x100;
    return flags;
}

// 0x005698F0
void SkateNet::WriteObjectUpdate(Obj::CObject* object, RelayStream* out)
{
    uint8_t* player = PlayerByObjectId(Field<uint32_t>(object, 0x14));
    Obj::CObject* skater = PlayerSkater(player);
    uint8_t index = Field<uint8_t>(skater, 0x14);
    auto* history = Field<StateHistory*>(skater, 0x27C);
    StateEntry& entry = history->entries[(history->count - 1) % 20];
    auto* sent = static_cast<RelaySent*>(out->m_sent);
    Util::BitStream& stream = out->m_stream;
    sent->time[index] = EntryTime(entry);
    float angles[3] = {entry.angles[0], entry.angles[1], entry.angles[2]};
    stream.WriteBits(entry.time, 16);
    uint32_t flags = GetRelayFlags(sent, &entry, index);
    stream.WriteBits(flags, 9);
    for (int k = 0; k < 3; ++k) {
        // (TP-R6: whole units in a big level)
        int16_t value = k == 1 ? Thugpro::PackHeight(entry.position.y) : Thugpro::PackHorizontal((&entry.position.x)[k]);
        if (flags & (1u << k))
            stream.WriteBits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        sent->pos[index][k] = value;
    }
    for (int k = 0; k < 3; ++k) {
        int16_t value = PackAngle(angles[k]);
        if (flags & (8u << k))
            stream.WriteBits(static_cast<uint32_t>(static_cast<int>(value)), 16);
        sent->angles[index][k] = value;
    }
    // (the bytes go out sign-extended: WriteBits doesn't mask them, as the original)
    auto signedByte = [](uint8_t value) { return static_cast<uint32_t>(static_cast<int>(static_cast<int8_t>(value))); };
    uint8_t value7C = entry.value7C, state = static_cast<uint8_t>(entry.state), flag74 = entry.flag74;
    uint8_t flag75 = entry.flag75, flag76 = entry.flag76;
    if (flags & 0x40) {
        stream.WriteBits(signedByte(flag74 ? static_cast<uint8_t>(state | 0x80) : state), 4);
        stream.WriteBits(signedByte(value7C), 6);
        stream.WriteBits(signedByte(flag75), 1);
        stream.WriteBits(signedByte(flag76), 1);
        sent->state[index] = state;
        sent->flag74[index] = flag74;
        sent->value7C[index] = value7C;
        sent->flag75[index] = flag75;
        sent->flag76[index] = flag76;
    }
    if (flags & 0x80) {
        sent->value68[index] = entry.value68;
        sent->value6C[index] = entry.value6C;
        stream.WriteBits(entry.value68, 5);
        stream.WriteBits(entry.value6C, 3);
    }
    if (flags & 0x100) {
        sent->value80[index] = entry.value80;
        stream.WriteBits(entry.value80, 32);
    }
}

// 0x0056BE60
int SkateNet::HandleClientObjectUpdate(Net::MsgHandlerContext* context)
{
    Util::BitStream stream;
    uint8_t* player = Gn()->PlayerByConnection(context->m_Conn);
    Obj::CObject* object = player ? PlayerSkater(player) : nullptr;
    if (!object || !(Field<uint8_t>(Field<uint8_t*>(player, 0x18), 0x60) & 1) || (context->m_PacketFlags & 3)) {
        // Not taken: the message's length worked out from its fields (a 32-bit time stamp, 9 field bits, then 16
        // bits for each position and angle, 12 for the state, 8 for the 0x80 pair, 32 for the last).
        stream.SetInputData(context->m_Msg, 0x400);
        stream.ReadSignedBits(32);
        uint32_t fields = stream.ReadBits(9);
        int bits = fields & 1 ? 0x39 : 0x29;
        for (uint32_t bit : {2u, 4u, 8u, 0x10u, 0x20u}) {
            if (fields & bit)
                bits += 0x10;
        }
        if (fields & 0x40)
            bits += 0xC;
        if (fields & 0x80)
            bits += 8;
        if (fields & 0x100)
            bits += 0x20;
        context->m_MsgLength = static_cast<uint32_t>((bits + 7) >> 3);
        return 1;
    }
    auto* history = Field<StateHistory*>(object, 0x27C);
    StateEntry& cur = history->entries[history->count % 20];
    // What isn't sent stays as it was (nothing yet: zero).
    StateEntry* prev = nullptr;
    int16_t pos[3] = {};
    float angles[3] = {};
    uint8_t state = 0, flag74 = 0, value7C = 0, flag75 = 0, flag76 = 0;
    uint32_t value68 = 0, value6C = 0, value80 = 0;
    if (history->count > 0) {
        prev = &history->entries[(history->count + 19) % 20];
        value68 = prev->value68;
        flag74 = prev->flag74;
        value6C = prev->value6C;
        state = static_cast<uint8_t>(prev->state);
        flag75 = prev->flag75;
        value7C = prev->value7C;
        value80 = prev->value80;
        flag76 = prev->flag76;
        for (int k = 0; k < 3; ++k) {
            pos[k] = prev->pos[k];
            angles[k] = prev->angles[k];
        }
    }
    stream.SetInputData(context->m_Msg, 0x400);
    uint32_t time = stream.ReadBits(32);
    uint32_t fields = stream.ReadBits(9);
    for (int k = 0; k < 3; ++k) {
        if (fields & (1u << k))
            pos[k] = static_cast<int16_t>(stream.ReadSignedBits(16));
    }
    for (int k = 0; k < 3; ++k) {
        if (fields & (8u << k))
            angles[k] = static_cast<float>(static_cast<int16_t>(stream.ReadSignedBits(16)) * (1.0 / 4096.0));
    }
    if (fields & 0x40) {
        uint32_t bits = stream.ReadBits(4);
        state = static_cast<uint8_t>(bits & 7);
        flag74 = static_cast<uint8_t>((bits >> 7) & 1); // (always 0: the original's)
        value7C = static_cast<uint8_t>(stream.ReadBits(6));
        flag75 = static_cast<uint8_t>(stream.ReadBits(1));
        flag76 = static_cast<uint8_t>(stream.ReadBits(1));
    }
    if (fields & 0x80) {
        value68 = stream.ReadBits(5);
        value6C = stream.ReadBits(3);
    }
    if (fields & 0x100)
        value80 = stream.ReadBits(32);
    context->m_MsgLength = static_cast<uint32_t>(stream.GetByteCount());
    if (history->count > 0 && time < EntryTime(*prev))
        return 4; // (an older update: dropped)
    Mth::Matrix matrix;
    matrix.SetFromEulerAngles(angles[0], angles[1], angles[2]);
    cur.matrix = matrix;
    // (TP-R6: whole units in a big level)
    cur.position = {Thugpro::UnpackHorizontal(pos[0]), Thugpro::UnpackHeight(pos[1]), Thugpro::UnpackHorizontal(pos[2]),
                    0.0f};
    cur.time = static_cast<uint16_t>(time);
    cur.wraps = static_cast<uint16_t>(time >> 16);
    for (int k = 0; k < 3; ++k) {
        cur.pos[k] = pos[k];
        cur.angles[k] = angles[k];
    }
    cur.flag74 = flag74;
    cur.value68 = value68;
    cur.value6C = value6C;
    cur.value7C = value7C;
    cur.state = static_cast<int8_t>(state);
    cur.value80 = value80;
    cur.flag76 = flag76;
    cur.flag75 = flag75;
    ++history->count;
    return 1;
}

} // namespace Mdl

REPLACE(0x0056C920, Mdl::SkateNet::HandleServerRemovedYou);
REPLACE(0x0056CA70, Mdl::SkateNet::HandleScavengerHunt);
REPLACE(0x0056CDF0, Mdl::SkateNet::HandleScavengerKillObject);
REPLACE(0x0056CEC0, Mdl::SkateNet::HandleScavengerDropObjects);
REPLACE(0x0056D030, Mdl::SkateNet::HandleGraffitiColors);
REPLACE(0x0056D250, Mdl::SkateNet::HandleScores);
REPLACE(0x0056D810, Mdl::SkateNet::HandleStartInfo);
REPLACE(0x0056DEE0, &Mdl::SkateNet::StartServer);
REPLACE(0x0056E2B0, &Mdl::SkateNet::CreateNetworkTasks);
REPLACE(0x0056E380, Mdl::SkateNet::HandleRunScript);
REPLACE(0x0056E470, Mdl::SkateNet::HandleObjectUpdates);
REPLACE(0x005696C0, Mdl::SkateNet::GetRelayFlags);
REPLACE(0x005698F0, Mdl::SkateNet::WriteObjectUpdate);
REPLACE(0x0056BE60, Mdl::SkateNet::HandleClientObjectUpdate);
REPLACE(0x0056EB50, &Mdl::SkateNet::AddClientHandlers);
