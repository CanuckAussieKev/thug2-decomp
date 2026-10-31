// The skate module's network side, the server's part and the rest of the client's (0x00569560-0x0056C91F): the two
// main-loop tasks (the other skaters' updates to each client, the scores every two seconds), the handlers that relay
// a client's messages to the others (as they are, to the skaters near it, sequenced), the scavenger hunt's counts,
// the graffiti / face textures passed on, the client's state-history messages, projectiles and cheats; and the skate
// module's LeaveServer / SendCheats / BroadcastPoints. From the disassembly; see skate_net.h.

#include "game/skate/skate_net.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/engine_texture.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/smtptr.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sfx/terrain.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/skater_state.h"
#include "game/skater/state_history.h"
#include "game/skater/tagging.h"
#include "game/util/bitstream.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/model.h"
#include "game/gfx/anim_blend.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"

namespace Gfx2D {
extern float g_oneOver255; // 0x00645D70 (defined in tod_manager.cpp)
} // namespace Gfx

namespace GameModules {

uint8_t g_netRelayObjectId;
float g_relayTimeScale{0.000244140625f};   // (2^-12: the relayed times and positions)

namespace {

using Scripts::CStruct;
using OnlinePlay::PlayerRecord;

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;              // NetLobby (a game mode)
constexpr uint32_t kFirefight = 0x3D6D444F;             // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;          // netfirefight
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;         // netgoalattack
constexpr uint32_t kShouldModulateColor = 0x11941568;   // should_modulate_color
constexpr uint32_t kSkaterScore = 0xD262802F;           // SkaterScore (the component)
constexpr uint32_t kTagging = 0x5B512EDC;               // Tagging (the component)
constexpr uint32_t kModel = 0x286A8D26;                 // model (the component)
constexpr uint32_t kAnimBlender = 0x7149B753;           // animblender (the component)
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F; // scavenger_collect_period (a goal)
constexpr uint32_t kCreateServerQuitDialog = 0x1D3A6D04; // CreateServerQuitDialog
constexpr uint32_t kPatchCleanup = 0xFF5C39B6;          // PatchCleanup
constexpr uint32_t kHatLogo = 0x5B288E0B;               // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;             // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;              // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;           // deck_graphic
constexpr uint32_t kAll = 0xC4E78E22;                   // ALL
constexpr uint32_t kOn = 0xF649D637;                    // on
constexpr uint32_t kCheatFlag = 0xAE94C183;             // cheat_flag
constexpr uint32_t kClientToggleCheat = 0x3CF9E86D;     // client_toggle_cheat
constexpr uint32_t kOwnerId = 0x81C39E06;               // owner_id
constexpr uint32_t kAmmoType = 0x1D1A8B59;              // ammo_type
constexpr uint32_t kX = 0x7323E97C;                     // x
constexpr uint32_t kY = 0x0424D9EA;                     // y
constexpr uint32_t kZ = 0x9D2D8850;                     // z
constexpr uint32_t kScaledX = 0xBDED7A76;               // scaled_x
constexpr uint32_t kScaledY = 0xCAEA4AE0;               // scaled_y
constexpr uint32_t kScaledZ = 0x53E31B5A;               // scaled_z
constexpr uint32_t kRadius = 0xC48391A5;                // radius
constexpr uint32_t kScale = 0x13B9DA7B;                 // scale
constexpr uint32_t kPos = 0x7F261953;                   // Pos
constexpr uint32_t kVel = 0x0C4C809E;                   // vel
constexpr uint32_t kFireball = 0xD039432C;              // fireball
constexpr uint32_t kClientLaunchFireball = 0xAA2B5552;  // ClientLaunchFireball
constexpr uint32_t kClientLaunchProjectile = 0x866C26A9; // ClientLaunchProjectile

// The cheats SendCheats tells a player about, in order (CHEAT_ALWAYS_SPECIAL twice, as the original).
constexpr uint32_t kSentCheats[] = {
    0xEEFECF1B, // CHEAT_ALWAYS_SPECIAL
    0xB38341C9, // CHEAT_PERFECT_MANUAL
    0xCD09E062, // CHEAT_PERFECT_RAIL
    0x69A1CE96, // CHEAT_PERFECT_SKITCH
    0xEEFECF1B, // CHEAT_ALWAYS_SPECIAL
    0x6B0F24BC, // CHEAT_STATS_13
    0x520A66EF, // FLAG_G_EXPERT_MODE_NO_REVERTS
    0xEBA89179, // FLAG_G_EXPERT_MODE_NO_WALKING
    0xC4FFC672, // FLAG_G_EXPERT_MODE_NO_MANUALS
    0xD5982173, // NO_G_DISPLAY_BALANCE
};

OnlinePlay::Hub* NetLink() { return OnlinePlay::g_gameNetManager; }
GoalSystem::CMatchRules* GameMode() { return GoalSystem::g_gameMode; }
GameObjects::CObjectHubBase* ObjectManager() { return reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager); }

template <typename T>
T& Prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
// thiscall calls into code not rewritten yet, by return type.
template <typename R = void, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return reinterpret_cast<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

PlayerRecord* DetailsOf(uint8_t* player) { return reinterpret_cast<PlayerRecord*>(player); }
PlayerRecord* ByConnection(NetLink::Conn* conn) { return DetailsOf(NetLink()->PlayerByConnection(conn)); }
PlayerRecord* ByObjectId(int id) { return DetailsOf(NetLink()->PlayerByObjectId(static_cast<uint16_t>(id))); }
// An object id from a message byte (signed, as the original reads it).
int IdByte(const uint8_t* data, int offset) { return static_cast<int8_t>(data[offset]); }

// ListKit::Hunt<PlayerRecord> (vtable 0x0064C1A8) for the network manager's BeginPlayerWalk / StepPlayerWalk.
struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
    PlayerRecord* First(bool observers) { return static_cast<PlayerRecord*>(NetLink()->BeginPlayerWalk(this, observers)); }
    PlayerRecord* Next(bool observers) { return static_cast<PlayerRecord*>(NetLink()->StepPlayerWalk(this, observers)); }
};

// A message on the server app to `player`, sequenced in `group`.
void Sequence(NetLink::NetMessageInfo& desc, uint8_t group)
{
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = group;
}
void Enqueue(NetLink::AppHost* app, PlayerRecord* player, NetLink::NetMessageInfo* desc)
{
    int handle = player->GetLinkHandle();
    app->PutOnOutbox(handle, desc);
}
NetLink::AppHost* ServerApp() { return reinterpret_cast<NetLink::AppHost*>(NetLink()->m_host); }

GameObjects::CRider* SkaterOf(PlayerRecord* player) { return player->m_skater; }
uint8_t SkaterId(GameObjects::CRider* skater) { return Prop<uint8_t>(skater, 0x14); }
PointTally* ScoreOf(GameObjects::CRider* skater) { return static_cast<PointTally*>(skater->GetPointsTotal()); }

CStruct* NewStructure()
{
    CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteStructure(CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// A skater's state history (+0x27C) and its newest state / message.
GameObjects::CRiderFrameLog* History(const void* object)
{
    return Prop<GameObjects::CRiderFrameLog*>(object, 0x27C);
}
int& MessageCount(GameObjects::CRiderFrameLog* history) { return Prop<int>(history, 0xA6C); }
// The newest message slot made a message of `id`, stamped with the newest state's time.
GameObjects::StateHistoryMessage* NewMessage(GameObjects::CRiderFrameLog* history, uint8_t id)
{
    GameObjects::StateHistoryMessage* message = history->GetLatestMessage();
    message->m_ident = id;
    auto* state = reinterpret_cast<GameObjects::SkaterStateHistoryFrame*>(history->GetLatestState());
    message->ChangeMoment(state->GetTime());
    return message;
}
template <typename T>
T& At(GameObjects::StateHistoryMessage* message, uint32_t offset)
{
    return Prop<T>(message, offset);
}
float Scaled(uint32_t value, float constant)
{
    return static_cast<float>(static_cast<double>(value) * constant);
}

// The relay message: the received one as it is.
void Relay(NetLink::NetMessageInfo& desc, const NetLink::InboundMsgCtx* context)
{
    desc.m_byteCount = static_cast<uint16_t>(context->m_bodySize);
    desc.m_content = context->m_body;
    desc.m_ident = context->m_typeCode;
}

} // namespace

// --- the server's tasks ---------------------------------------------------------------------------------------------

// 0x00569BB0
void SkateNet::ServerTask(TaskList::SchedulerTask* /*task*/)
{
    PlayerSearch search;
    NetLink::AppHost* server = ServerApp();
    uint8_t buffer[0x1000];
    for (PlayerRecord* player = search.First(true); player; player = search.Next(true)) {
        RelayStream out;
        if (!player->m_conn->IsOffMachine())
            continue;
        out.m_feed.BindOutputBuffer(buffer, sizeof(buffer));
        out.m_unknown18 = static_cast<uint32_t>(player->m_conn->m_hdl);
        out.m_sent = player;
        uint32_t mask = 0;
        int count = 0;
        // (round the skaters from the one after the last sent, as many as the connection takes in one message)
        int start = (player->GetLastSentSkater() + 1) % 8;
        for (int i = 0; i < 8; ++i) {
            int id = (i + start) % 8;
            PlayerRecord* other = ByObjectId(id);
            if (!other)
                continue;
            other = ByObjectId(static_cast<int>(Prop<uint32_t>(SkaterOf(other), 0x14)));
            if (!other || other == player)
                continue;
            GameObjects::CRider* skater = SkaterOf(other);
            auto* history = History(skater);
            int states = Prop<int>(history, 0x18);
            if (!states)
                continue;
            uint16_t newest = Prop<uint16_t>(history, 0x1C + ((states - 1) % 20) * 0x84 + 0x78);
            if (newest == Prop<uint32_t>(player, 0x70 + Prop<uint32_t>(skater, 0x14) * 4))
                continue;
            mask |= 1u << id;
            if (++count >= player->GetSkatersPerUpdate())
                break;
        }
        if (!count)
            continue;
        out.m_feed.WriteBits(mask, 8);
        for (int i = 0; i < 8; ++i) {
            if (!(mask & (1u << i)))
                continue;
            WriteObjectUpdate(reinterpret_cast<GameObjects::CEntity*>(SkaterOf(ByObjectId(i))), &out);
            player->SetLastSentSkater(i);
        }
        out.m_feed.Drain();
        int size = out.m_feed.GetByteCount();
        if (size) {
            NetLink::NetMessageInfo desc;
            desc.MakeNew();
            desc.m_content = buffer;
            desc.m_byteCount = static_cast<uint16_t>(size);
            desc.m_ident = 0x23;
            Enqueue(server, player, &desc);
        }
    }
}

// 0x0056BE10
void SkateNet::ScoreTask(TaskList::SchedulerTask* task)
{
    auto* skate = Prop<Skate*>(task, 0x1C);
    OnlinePlay::Hub* net = NetLink();
    if (!net->KeepsScore())
        return;
    NetLink::AppHost* server = ServerApp();
    if (server->m_clock - net->GetLastScoreUpdate() >= 2000)
        skate->BroadcastPoints(false);
}

// --- the server's handlers ------------------------------------------------------------------------------------------

// 0x00569E20
int SkateNet::HandleTrickObjects(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    PlayerRecord* player = ByConnection(context->m_senderConn);
    if (!player)
        return 4;
    PointTally* score = ScoreOf(SkaterOf(player));
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    if (static_cast<int8_t>(data[0]) == 1 && data[0xA] == net->GetGameId())
        score->NoteTaggedProp(Prop<int>(data, 0xC), Prop<int>(data, 0x10), Prop<uint32_t>(data, 0x14),
                              reinterpret_cast<uint32_t*>(data + 0x18), true);
    return 1;
}

// 0x00569E90
int SkateNet::HandleGameStats(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    PlayerRecord* player = ByConnection(context->m_senderConn);
    if (!player)
        return 4;
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    if (GameMode()->GetNameKey() == kNetLobby || net->GetGameId() != data[0])
        return 4;
    player->m_unknown24 = Prop<uint32_t>(data, 4);
    return 1;
}

// 0x00569EF0
int SkateNet::HandleComboScore(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    PlayerRecord* player = ByConnection(context->m_senderConn);
    if (!player || !SkaterOf(player))
        return 4;
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    if (net->GetGameId() != data[0])
        return 4;
    int combo = Prop<int>(data, 4);
    PointTally* score = ScoreOf(SkaterOf(player));
    if (combo > score->m_topChain)
        score->m_topChain = combo;
    GoalSystem::CMatchRules* mode = GameMode();
    if (mode->IsTrue(kShouldModulateColor) || !mode->KeepsMovePoints())
        return 1;
    if (mode->KeepsRunningTotal()) {
        score->SetGrandTotal(score->GetTotalScore() + combo);
        return 1;
    }
    if (mode->KeepsBestChain() && score->GetTotalScore() < combo)
        score->SetGrandTotal(combo);
    return 1;
}

// 0x00569FD0
int SkateNet::HandlePlayerValue48(NetLink::InboundMsgCtx* context)
{
    PlayerRecord* player = ByConnection(context->m_senderConn);
    if (!player)
        return 4;
    player->m_unknown48 = Prop<uint32_t>(context->m_body, 4);
    return 1;
}

// 0x0056A000
int SkateNet::HandleRelay(NetLink::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* sender = ByConnection(context->m_senderConn);
    if (!sender)
        return 4;
    for (PlayerRecord* player = search.First(true); player; player = search.Next(true)) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        if (player == sender)
            continue;
        Relay(desc, context);
        switch (context->m_typeCode) {
        case 0x3A:
        case 0x4C:
        case 0x89:
            Sequence(desc, 8);
            break;
        case 0x99:
            Sequence(desc, 10);
            break;
        case 0x83: {
            // a projectile in firefight: its owner and the sender's latency put in (once in range)
            uint32_t mode = GameMode()->GetNameKey();
            if (mode != kFirefight && GameMode()->GetNameKey() != kNetFirefight)
                break;
            uint8_t* msg = reinterpret_cast<uint8_t*>(context->m_body);
            if (!(Prop<float>(msg, 0x30) <= 10.0f) || Prop<int>(msg, 0x2C) > 0xF0)
                break;
            msg[0x34] = SkaterId(SkaterOf(sender));
            Prop<int>(msg, 0) = context->m_senderConn->GetMeanPing();
            break;
        }
        default:
            break;
        }
        Enqueue(context->m_receiverApp, player, &desc);
    }
    return 1;
}

// 0x0056A170
int SkateNet::HandleRelayNearby(NetLink::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* sender = ByConnection(context->m_senderConn);
    if (!sender)
        return 4;
    GameObjects::CRider* from = SkaterOf(sender);
    if (!from)
        return 4;
    for (PlayerRecord* player = search.First(true); player; player = search.Next(true)) {
        if (player == sender)
            continue;
        if (!player->IsObserver()) {
            GameObjects::CRider* to = SkaterOf(player);
            if (!to)
                continue;
            // (x87: the differences and the sum unrounded)
            double dx = static_cast<double>(Prop<float>(to, 0x4C)) - Prop<float>(from, 0x4C);
            double dy = static_cast<double>(Prop<float>(to, 0x50)) - Prop<float>(from, 0x50);
            double dz = static_cast<double>(Prop<float>(to, 0x54)) - Prop<float>(from, 0x54);
            double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
            if (!(distance < 1800.0))
                continue;
        }
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        Relay(desc, context);
        Enqueue(context->m_receiverApp, player, &desc);
    }
    return 1;
}

// 0x0056A2D0
int SkateNet::HandleRelaySequenced(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    PlayerSearch search;
    PlayerRecord* sender = ByConnection(context->m_senderConn);
    if (!sender)
        return 4;
    for (PlayerRecord* player = search.First(true); player; player = search.Next(true)) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        if (player == sender)
            continue;
        Relay(desc, context);
        Sequence(desc, 8);
        Enqueue(context->m_receiverApp, player, &desc);
    }
    // a script run on every machine (0x49): the server queues its own
    auto* msg = reinterpret_cast<uint8_t*>(context->m_body);
    if (context->m_typeCode == 0x49 && msg[0xC])
        net->QueueScript(Prop<int>(msg, 8), Prop<uint32_t>(msg, 4), Prop<uint32_t>(msg, 0));
    return 1;
}

// 0x0056A3E0
int SkateNet::HandleJoined(NetLink::InboundMsgCtx* context)
{
    PlayerRecord* player = ByConnection(context->m_senderConn);
    if (player)
        player->ClearJustJoined();
    return 1;
}

// 0x0056AD10
int SkateNet::HandleScavengerCollected(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    PlayerSearch search;
    auto* msg = context->m_body;
    PlayerRecord* collector = ByConnection(context->m_senderConn);
    if (SkaterOf(collector)) {
        auto* component = SkaterOf(collector)->FetchModule(kSkaterScore);
        PointTally* score = Prop<PointTally*>(component, 0x18);
        if (score)
            score->SetGrandTotal(score->GetTotalScore() + 1);
        int total = 0;
        for (PlayerRecord* player = search.First(false); player; player = search.Next(false))
            total += player->m_unknown34;
        GoalSystem::CMatchRules* mode = GameMode();
        int collected = mode->IsSquadMatch() ? net->GetSquadPoints(collector->m_team) : score->GetTotalScore();
        NetLink::NetMessageInfo desc;
        uint8_t left[2];
        if (collected == total) {
            // all collected: the collect period over everywhere
            desc.MakeNew();
            NetLink::AppHost* server = ServerApp();
            desc.m_ident = 0x94;
            Sequence(desc, 8);
            for (PlayerRecord* player = search.First(true); player; player = search.Next(true)) {
                if (!player->IsOwnPlayer())
                    Enqueue(server, player, &desc);
            }
            GoalSystem::CObjective* goal = GoalSystem::GetObjectiveBoard()->GetGoal(kScavengerCollectPeriod, 1);
            if (GoalSystem::Virtual<bool>(goal, GoalSystem::CObjective::kVtIsActive))
                GoalSystem::Virtual<void>(goal, GoalSystem::CObjective::kVtExpire);
        } else {
            // five or one left: told to everyone (0x95: the player or the team, how many)
            uint8_t count = 0;
            if (collected == total - 5 && !ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr))
                count = 5;
            else if (score->GetTotalScore() == total - 1 && !ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr))
                count = 1;
            if (count) {
                desc.MakeNew();
                NetLink::AppHost* server = ServerApp();
                left[0] = GameMode()->IsSquadMatch() ? static_cast<uint8_t>(collector->m_team)
                                                    : SkaterId(SkaterOf(collector));
                left[1] = count;
                desc.m_content = left;
                desc.m_ident = 0x95;
                Sequence(desc, 8);
                desc.m_byteCount = 2;
                for (PlayerRecord* player = search.First(true); player; player = search.Next(true))
                    Enqueue(server, player, &desc);
            }
        }
    }
    // the object picked up: told to the collector's team mates
    if (GameMode()->IsSquadMatch()) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        NetLink::AppHost* server = ServerApp();
        for (PlayerRecord* player = search.First(false); player; player = search.Next(false)) {
            if (player == collector || player->m_team != collector->m_team)
                continue;
            desc.m_content = msg;
            desc.m_ident = 0x90;
            Sequence(desc, 8);
            desc.m_byteCount = 4;
            Enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B0B0
int SkateNet::HandleScavengerDropped(NetLink::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* dropper = ByConnection(context->m_senderConn);
    if (!dropper)
        return 4;
    if (dropper->m_unknown34 > 5 || (dropper->m_unknown34 == 5 && !dropper->IsOwnPlayer()))
        return 4;
    auto* position = reinterpret_cast<const uint32_t*>(context->m_body);
    if (!dropper->IsOwnPlayer()) {
        auto* to = &Prop<uint32_t>(dropper, static_cast<uint32_t>((dropper->m_unknown34 + 0x18) * 0x10));
        for (int i = 0; i < 4; ++i)
            to[i] = position[i];
    }
    if (!dropper->IsOwnPlayer())
        ++dropper->m_unknown34;
    int dropped = 0;
    int players = 0;
    for (PlayerRecord* player = search.First(false); player; player = search.Next(false)) {
        dropped += player->m_unknown34;
        ++players;
    }
    if (dropped == players * 5) {
        // every player's five dropped: the hunt goes on (0x92 to everyone)
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        NetLink::AppHost* server = ServerApp();
        for (PlayerRecord* player = search.First(true); player; player = search.Next(true)) {
            desc.m_ident = 0x92;
            Sequence(desc, 8);
            Enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B2B0
int SkateNet::HandleGraffitiTexture(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    auto* msg = reinterpret_cast<uint8_t*>(context->m_body);
    // (a client's server: the sender; a client: the player the last 0x96 named)
    auto from = [&]() -> PlayerRecord* {
        if (net->OnServer())
            return ByConnection(context->m_senderConn);
        return ByObjectId(g_netRelayObjectId);
    };
    // the texture's virtual slot 5 (0x00456B30): its texture and palette
    auto load = [](void* texture, const void* textureData, uint32_t textureSize, const void* palette,
                   uint32_t paletteSize) {
        reinterpret_cast<void(__thiscall*)(void*, const void*, uint32_t, const void*, uint32_t)>(
            Prop<void**>(texture, 0)[5])(texture, textureData, textureSize, palette, paletteSize);
    };
    switch (context->m_typeCode) {
    case 0x96: // the player the next ones are for
        g_netRelayObjectId = msg[0];
        break;
    case 0x98: { // the palette
        PlayerRecord* player = from();
        if (player)
            load(player->GetGraffitiTexture(), nullptr, 0, msg, context->m_bodySize);
        break;
    }
    case 0x97: { // the texture
        PlayerRecord* player = from();
        if (!player)
            break;
        load(player->GetGraffitiTexture(), msg, context->m_bodySize, nullptr, 0);
        player->SetCustomTag(true);
        break;
    }
    case 0x9A: { // which board parts show it - the last: passed on to the others by a server
        bool onServer = false;
        PlayerRecord* player;
        if (net->OnServer()) {
            player = ByConnection(context->m_senderConn);
            onServer = true;
        } else {
            player = ByObjectId(g_netRelayObjectId);
        }
        uint32_t parts = 0;
        if (player) {
            parts = Prop<uint32_t>(msg, 0);
            if (parts & 1)
                SkaterOf(player)->ShowBoardElement(kHatLogo);
            if (parts & 2)
                SkaterOf(player)->ShowBoardElement(kFrontLogo);
            if (parts & 4)
                SkaterOf(player)->ShowBoardElement(kBackLogo);
            if (parts & 8)
                SkaterOf(player)->ShowBoardElement(kDeckGraphic);
        }
        if (!onServer || !player)
            break;
        PlayerSearch search;
        void* texture = player->GetGraffitiTexture();
        NetLink::AppHost* app = context->m_receiverApp;
        for (PlayerRecord* other = search.First(true); other; other = search.Next(true)) {
            NetLink::NetMessageInfo desc;
            desc.MakeNew();
            if (other->IsOwnPlayer() || other == player || other->m_conn->GetLinkSpeedClass() == 1)
                continue;
            uint8_t id = SkaterId(SkaterOf(player));
            desc.m_content = &id;
            desc.m_ident = 0x96;
            desc.m_byteCount = 1;
            Sequence(desc, 10);
            Enqueue(app, other, &desc);
            void* palette = Call<void*>(reinterpret_cast<uint32_t>(CodeAddress(Engine3D::VirtualSlot27Jump)), texture);
            uint32_t paletteSize = Call<uint32_t>(reinterpret_cast<uint32_t>(CodeAddress(Engine3D::VirtualSlot28Jump)), texture);
            app->SendBulkToConn(other->m_conn, 0x98, static_cast<uint16_t>(paletteSize), palette,
                                     "CAG Palette", 10, false, true); // "CAG Palette"
            void* pixels = Call<void*>(reinterpret_cast<uint32_t>(CodeAddress(Engine3D::VirtualSlot25Jump)), texture);
            uint32_t pixelsSize = Call<uint32_t>(reinterpret_cast<uint32_t>(CodeAddress(Engine3D::VirtualSlot26Jump)), texture);
            app->SendBulkToConn(other->m_conn, 0x97, static_cast<uint16_t>(pixelsSize), pixels,
                                     "CAG Texture", 10, false, true); // "CAG Texture"
            uint32_t sentParts = parts;
            desc.m_content = &sentParts;
            desc.m_ident = 0x9A;
            desc.m_byteCount = 4;
            Sequence(desc, 10);
            Enqueue(app, other, &desc);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

// 0x0056B5D0
int SkateNet::HandleFaceTexture(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    void* memory = MemSys::AllocZeroed(0x449C);
    void* texture = memory ? reinterpret_cast<void*>(static_cast<Gfx2D::CFaceSkin*>(memory)->MakeNew()) : nullptr;
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    uint32_t size = context->m_bodySize;
    PlayerRecord* player;
    if (net->OnServer()) {
        player = ByConnection(context->m_senderConn);
    } else {
        // (a client: the player's object id first)
        uint8_t id = *data++;
        --size;
        player = ByObjectId(id);
    }
    // (quirk kept: the texture is leaked here)
    if (!player)
        return 4;
    GameObjects::CRider* skater = SkaterOf(player);
    if (!skater)
        return 4;
    static_cast<Gfx2D::CFaceSkin*>(texture)->ReadFrom(data, static_cast<int>(size));
    static_cast<Gfx2D::CFaceSkin*>(texture)->MarkUsable(true);
    auto* model = skater->FetchModule(kModel);
    static_cast<Engine3D::CRenderModel*>(Prop<void*>(model, 0x208))->ReplaceFaceTexture(texture, "CS_NSN_facemap.png", kAll);
    if (net->OnServer()) {
        // the server keeps the face and streams it to the others
        PlayerSearch search;
        NetLink::AppHost* app = context->m_receiverApp;
        PlayerRecord* sender = ByConnection(context->m_senderConn);
        sender->SetFaceTextureData(reinterpret_cast<const uint8_t*>(context->m_body), context->m_bodySize);
        for (PlayerRecord* other = search.First(true); other; other = search.Next(true)) {
            if (other->IsOwnPlayer() || other == sender)
                continue;
            app->SendBulkToConn(other->m_conn, 0x7E, 0x4499, sender->GetFaceTextureData(),
                                     "Face Data", 9, false, true); // "Face Data"
        }
    }
    // (freed without its destructor, as the original)
    MemSys::Hub::Sole()->Dealloc(texture);
    return 1;
}

// --- the client's handlers ------------------------------------------------------------------------------------------

// 0x00569560
int SkateNet::HandleChangeLevel(NetLink::InboundMsgCtx* context)
{
    static_cast<Skate*>(context->m_content)->ChangeLevel(Prop<uint32_t>(context->m_body, 0));
    return 1;
}

// 0x0056A410
int SkateNet::HandleHistoryValue(NetLink::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    GameObjects::CRider* skater = static_cast<Skate*>(context->m_content)->GetRiderByNumber(static_cast<uint32_t>(IdByte(data, 0)));
    if (!skater)
        return 4;
    auto* history = skater->FetchModule(GameObjects::CRiderFrameLog::kType);
    Prop<uint32_t>(history, 0x1104) = Prop<uint32_t>(data, 4);
    return 1;
}

// 0x0056A450
int SkateNet::HandleSkaterMessages(NetLink::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->m_content);
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    GameObjects::CObjectHubBase* objects = ObjectManager();
    // the object (by the message's id byte at `offset`) and, for some, the skater too
    auto object = [&](int offset) { return objects->LookUpEntity(static_cast<uint32_t>(IdByte(data, offset))); };
    auto skater = [&](int offset) -> GameObjects::CEntity* {
        if (!object(offset))
            return nullptr;
        return reinterpret_cast<GameObjects::CEntity*>(skate->GetRiderByNumber(static_cast<uint32_t>(IdByte(data, offset))));
    };
    GameObjects::CEntity* target = nullptr;
    GameObjects::StateHistoryMessage* message;
    int result = 1;
    switch (context->m_typeCode) {
    case 0x29: {
        uint8_t first = data[0];
        uint8_t id = data[1];
        uint32_t value = Prop<uint32_t>(data, 2);
        uint16_t a = Prop<uint16_t>(data, 6), b = Prop<uint16_t>(data, 8), time = Prop<uint16_t>(data, 10);
        uint16_t c = Prop<uint16_t>(data, 12);
        target = objects->LookUpEntity(static_cast<uint32_t>(static_cast<int8_t>(id)));
        if (!target)
            return 1;
        float scaledTime = Scaled(time, g_relayTimeScale);
        OnlinePlay::Hub* net = NetLink();
        if (net->InNetGame()) {
            // (only the camera's own player's)
            uint8_t* player = net->PlayerByObjectId(static_cast<uint16_t>(target->m_ident));
            void* camera = net->GetCamPlayer();
            if (!camera || player != camera)
                scaledTime = 0.0f;
        }
        message = NewMessage(History(target), 0x29);
        At<uint32_t>(message, 0x20) = value;
        At<float>(message, 0x10) = Scaled(a, g_relayTimeScale);
        At<uint8_t>(message, 1) = id;
        At<float>(message, 0x18) = scaledTime;
        At<uint8_t>(message, 2) = first;
        At<float>(message, 0x14) = Scaled(b, g_relayTimeScale);
        At<float>(message, 0x1C) = Scaled(c, g_relayTimeScale);
        break;
    }
    case 0x2B:
        target = object(0);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x2B);
        At<uint8_t>(message, 1) = data[0];
        At<float>(message, 0xC) = Scaled(data[1], Gfx2D::g_oneOver255);
        break;
    case 0x2A:
        target = object(0);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x2A);
        At<uint8_t>(message, 1) = data[0];
        At<bool>(message, 9) = data[1] != 0;
        break;
    case 0x3C: {
        target = object(1);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x3C);
        At<uint8_t>(message, 1) = data[1];
        uint32_t bits = data[0];
        // five settings picked by the bits of [0]: two bits, then one each
        static const float kFirst[4] = {std::bit_cast<float>(0x3D4CCCCDu), std::bit_cast<float>(0x3DCCCCCDu), std::bit_cast<float>(0x4121999Au), 0.0f}; // 0.05, 0.1, 10.1, 0 (0x00645D6C, 0x00645D74, 0x006486B8, 0x0066D520)
        At<float>(message, 0x28) = kFirst[bits & 3];
        At<float>(message, 0x2C) = (bits >> 2 & 1) ? std::bit_cast<float>(0x4121999Au) : std::bit_cast<float>(0x3D23D70Au); // 10.1 / 0.04 (0x006486B8 / 0x006486B4)
        At<float>(message, 0x30) = (bits >> 3 & 1) ? std::bit_cast<float>(0x41A00000u) : std::bit_cast<float>(0x3B102DE0u); // 20 / 0.0022 (0x006486AC / 0x006486B0)
        At<float>(message, 0x34) = (bits >> 4 & 1) ? std::bit_cast<float>(0x41200000u) : std::bit_cast<float>(0x3ADED289u); // 10 / 0.0017 (0x00646710 / 0x006486A8)
        At<float>(message, 0x38) = (bits >> 5 & 1) ? std::bit_cast<float>(0x3F800000u) : std::bit_cast<float>(0x3FC00000u); // 1 / 1.5 (0x006453A8 / 0x00646744)
        break;
    }
    case 0x3D:
        target = object(0);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x3D);
        At<uint8_t>(message, 1) = data[0];
        At<uint8_t>(message, 2) = data[1];
        break;
    case 0x3E:
        target = object(4);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x3E);
        At<uint8_t>(message, 1) = data[4];
        At<uint32_t>(message, 0x20) = Prop<uint32_t>(data, 0);
        At<bool>(message, 0xA) = data[5] != 0;
        break;
    case 0x41:
        target = object(4);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x41);
        At<uint8_t>(message, 1) = data[4];
        At<uint32_t>(message, 0x1C) = Prop<uint32_t>(data, 0);
        break;
    case 0x2C:
        // (also handled by 0x0056C8C0: 4 lets that run)
        target = skater(0);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x2C);
        At<bool>(message, 8) = data[1] != 0;
        At<uint8_t>(message, 1) = data[0];
        result = 4;
        break;
    case 0x76:
        target = skater(0xC);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x76);
        At<uint8_t>(message, 1) = data[0xC];
        At<uint32_t>(message, 0x3C) = Prop<uint32_t>(data, 0);
        At<uint32_t>(message, 0x40) = Prop<uint32_t>(data, 4);
        At<float>(message, 0x48) = static_cast<float>(Prop<int16_t>(data, 8));
        At<float>(message, 0x4C) = static_cast<float>(Prop<int16_t>(data, 0xA));
        At<uint8_t>(message, 0x50) = data[0xD];
        At<uint8_t>(message, 3) = data[0xE];
        break;
    case 0x8C:
        target = skater(0);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x8C);
        At<uint8_t>(message, 1) = data[0];
        break;
    case 0x77:
        target = skater(8);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x77);
        At<uint8_t>(message, 1) = data[8];
        At<uint32_t>(message, 0x24) = Prop<uint32_t>(data, 4);
        At<int>(message, 0x44) = static_cast<int8_t>(data[9]);
        At<uint32_t>(message, 0x20) = Prop<uint32_t>(data, 0);
        break;
    case 0x78:
        target = skater(8);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x78);
        At<uint8_t>(message, 1) = data[8];
        At<int>(message, 0x44) = static_cast<int8_t>(data[9]);
        break;
    default:
        return 1;
    }
    ++MessageCount(History(target));
    return result;
}

// 0x0056ABD0
int SkateNet::HandlePlayerLeft(NetLink::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->m_content);
    OnlinePlay::Hub* net = NetLink();
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    uint8_t* player = net->PlayerByObjectId(static_cast<uint16_t>(IdByte(data, 0)));
    if (player) {
        GameObjects::CRider* skater = DetailsOf(player)->m_skater;
        net->EvictPlayer(player, IdByte(data, 1));
        skate->RemoveSkater(skater);
    } else {
        // (one still joining)
        void* joining = net->GetNewPlayerInfoByObjectId(static_cast<uint16_t>(IdByte(data, 0)));
        if (joining)
            net->RemoveNewPlayer(joining);
    }
    net->SendReadyResponse();
    return 1;
}

// 0x0056AC50
int SkateNet::HandleServerQuit(NetLink::InboundMsgCtx* /*context*/)
{
    OnlinePlay::Hub* net = NetLink();
    Scripts::LaunchScript(kCreateServerQuitDialog, nullptr, nullptr, false, nullptr);
    net->RemovePlayers();
    net->DisconnectClients();
    return 3;
}

// 0x0056AC90
int SkateNet::HandleGraffitiObjects(NetLink::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    OnlinePlay::Hub* net = NetLink();
    if (data[0] != net->GetGameId())
        return 4;
    int owner;
    if (GameMode()->IsSquadMatch())
        owner = DetailsOf(net->PlayerByObjectId(Prop<uint16_t>(data, 4)))->m_team;
    else
        owner = Prop<int>(data, 4);
    auto* names = reinterpret_cast<const uint32_t*>(data + 0xC);
    for (uint32_t i = 0; i < Prop<uint32_t>(data, 8); ++i)
        GameObjects::g_trickObjectManager->SetObjectGraffitiColor(names[i], owner);
    return 1;
}

// 0x0056B220
int SkateNet::HandlePlaceTag(NetLink::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    PlayerRecord* player = ByObjectId(IdByte(data, 0));
    if (!player)
        return 4;
    auto* tagging = static_cast<GameObjects::CTaggingComponent*>(SkaterOf(player)->FetchModule(kTagging));
    if (!tagging)
        return 4;
    Maths::Vec4 start = Prop<Maths::Vec4>(data, 4);
    Maths::Vec4 end = Prop<Maths::Vec4>(data, 0x14);
    tagging->PlaceTag(data[1] != 0, start, end, Prop<int>(data, 0x24), false);
    return 1;
}

// 0x0056B7A0
int SkateNet::HandleSparks(NetLink::InboundMsgCtx* context)
{
    auto* object = reinterpret_cast<GameObjects::CRider*>(
        ObjectManager()->LookUpEntity(static_cast<uint32_t>(IdByte(reinterpret_cast<uint8_t*>(context->m_body), 0))));
    if (!object)
        return 1;
    if (context->m_typeCode == 0x37)
        object->SparksOn();
    else if (context->m_typeCode == 0x38)
        object->SparksOff();
    return 1;
}

// 0x0056B7E0
int SkateNet::HandleLookUpObject(NetLink::InboundMsgCtx* context)
{
    // (the object looked up, nothing done with it)
    ObjectManager()->LookUpEntity(static_cast<uint32_t>(IdByte(reinterpret_cast<uint8_t*>(context->m_body), 0)));
    return 1;
}

// 0x0056B800
int SkateNet::HandleTerrainSound(NetLink::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    Maths::Vec4 position;
    position.x = static_cast<float>(Prop<int16_t>(data, 0));
    position.y = static_cast<float>(Prop<int16_t>(data, 2));
    position.z = static_cast<float>(Prop<int16_t>(data, 4));
    position.w = 0.0f; // (the original leaves w as the stack had it)
    float volume = static_cast<float>(static_cast<int8_t>(data[8]));
    float pitch = static_cast<float>(static_cast<int8_t>(data[9]));
    float value = static_cast<float>(static_cast<int8_t>(data[0xA]));
    SurfaceKind::PlayTerrainSound(data[6], data[7], &position, volume, pitch, value, false);
    return 1;
}

// 0x0056C330
int SkateNet::HandleRunScriptNow(NetLink::InboundMsgCtx* context)
{
    OnlinePlay::Hub* net = NetLink();
    auto* data = reinterpret_cast<uint32_t*>(context->m_body);
    if (!Prop<bool>(net, 0x18C)) {
        net->QueueScript(static_cast<int>(data[2]), data[1], data[0]);
        return 1;
    }
    GameObjects::CEntity* object = ObjectManager()->LookUpEntity(data[1]);
    Scripts::CScript* script =
        Scripts::SpawnScript(data[0], nullptr, 0, nullptr, static_cast<int>(data[2]), 0, false, false, false, false);
    script->m_entity.AssignValue(object);
    script->TickFrame();
    return 1;
}

// 0x0056C3D0
int SkateNet::HandleProjectile(NetLink::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->m_content);
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    GameObjects::CRider* skater = skate->GetRiderByNumber(static_cast<uint32_t>(IdByte(data, 0x34)));
    if (!skater)
        return 4;
    Maths::Vec4 velocity = Prop<Maths::Vec4>(data, 0x14);
    Maths::Vec4 position = Prop<Maths::Vec4>(data, 4);
    velocity.MakeUnit();
    uint32_t time = Prop<uint32_t>(data, 0);
    if (!NetLink()->OnServer()) {
        // (a client: the server's latency added)
        NetLink::ConnSearch search{NetLink::kConnSearchVtable, nullptr};
        NetLink::Conn* conn = context->m_receiverApp->BeginConnWalk(&search);
        time += conn->IsHostLocal() ? 0 : static_cast<uint32_t>(conn->m_latencyTest.m_averageLatency);
    }
    // where it is now: moved along its direction for the time it took (x87: unrounded until stored)
    double seconds = static_cast<double>(time) * static_cast<double>(GoalSystem::g_goalLifeSecondsPerMillisecond);
    position.x = static_cast<float>(velocity.x * seconds + position.x);
    position.y = static_cast<float>(position.y + velocity.y * seconds);
    position.z = static_cast<float>(position.z + velocity.z * seconds);
    CStruct* params = NewStructure();
    params->PutNameKey(kOwnerId, static_cast<uint32_t>(IdByte(data, 0x34)));
    params->PutNameKey(kAmmoType, Prop<uint32_t>(data, 0x28));
    params->PutInt(kX, static_cast<int>(position.x));
    params->PutInt(kY, static_cast<int>(position.y));
    params->PutInt(kZ, static_cast<int>(position.z));
    params->PutInt(kScaledX, static_cast<int>(Prop<float>(data, 0x14)));
    params->PutInt(kScaledY, static_cast<int>(Prop<float>(data, 0x18)));
    params->PutInt(kScaledZ, static_cast<int>(Prop<float>(data, 0x1C)));
    params->PutInt(kRadius, Prop<int>(data, 0x2C));
    params->PushFloat(kScale, Prop<float>(data, 0x30));
    params->PutVec3(kPos, Prop<Scripts::VectorValue>(data, 4));
    params->PutVec3(kVel, Prop<Scripts::VectorValue>(data, 0x14));
    uint32_t launch = Prop<uint32_t>(data, 0x24) == kFireball ? kClientLaunchFireball : kClientLaunchProjectile;
    Scripts::LaunchScript(launch, params, reinterpret_cast<GameObjects::CEntity*>(skater), false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x0056C6E0
int SkateNet::HandleCheats(NetLink::InboundMsgCtx* context)
{
    ScriptCommands::CFunc_ClearOmnigons(nullptr, nullptr); // (the cheats off first: the function folded there)
    auto* data = reinterpret_cast<uint32_t*>(context->m_body);
    for (int i = 0; i < static_cast<int>(data[0]); ++i) {
        CStruct* params = NewStructure();
        params->PutNameKey(kOn, 0);
        params->PutNameKey(kCheatFlag, data[1 + i]);
        Scripts::LaunchScript(kClientToggleCheat, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    return 1;
}

// 0x0056C7E0
int SkateNet::HandleToggleCheat(NetLink::InboundMsgCtx* context)
{
    CStruct* params = NewStructure();
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    if (data[4])
        params->PutNameKey(kOn, 0);
    params->PutNameKey(kCheatFlag, Prop<uint32_t>(data, 0));
    Scripts::LaunchScript(kClientToggleCheat, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x0056C8C0
int SkateNet::HandleSkaterChannel(NetLink::InboundMsgCtx* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_body);
    GameObjects::CEntity* object = ObjectManager()->LookUpEntity(static_cast<uint32_t>(IdByte(data, 0)));
    if (!object)
        return 1;
    auto* blender = static_cast<GameObjects::CAnimBlenderComponent*>(
        static_cast<GameObjects::CGameEntity*>(object)->FetchModule(kAnimBlender));
    if (!blender)
        return 1;
    Gfx2D::CBlendChannel* channel = blender->FindChannel(0);
    if (channel)
        channel->SetFlipped(static_cast<uint32_t>(IdByte(data, 0)), data[1] != 0, 0, false);
    return 1;
}

// --- the skate module's side ----------------------------------------------------------------------------------------

// 0x0056B890
void Skate::LeaveServer()
{
    OnlinePlay::Hub* net = NetLink();
    Scripts::LaunchScript(kPatchCleanup, nullptr, nullptr, false, nullptr);
    Scripts::ProcessPendingSymbols();
    if (net->OnServer()) {
        // the server: everyone told it's over (0x32), the server and its tasks gone
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        NetLink::AppHost* server = ServerApp();
        if (net->GetHostingRole() == 1)
            net->RemoveHostTask();
        desc.m_ident = 0x32;
        desc.m_rank = 0xFF;
        server->PutOnOutbox(0xFF, &desc);
        server->Transmit(false);
        net->DestroyServer();
        m_serverTasks[0]->DropOut();
        m_serverTasks[1]->DropOut();
        net->RemovePlayersAndApps();
        return;
    }
    // a client: the server told it's leaving (0xB)
    NetLink::AppHost* client = net->FetchPatron(0);
    if (client) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        desc.m_ident = 0xB;
        Sequence(desc, 8);
        static_cast<NetLink::Guest*>(client)->SendToHost(&desc);
        client->Transmit(false);
    }
    net->RemovePlayersAndApps();
}

// 0x0056B990
void Skate::SendCheats(OnlinePlay::PlayerRecord* player)
{
    OnlinePlay::Hub* net = NetLink();
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    struct {
        int count;
        uint32_t cheats[32];
    } message;
    message.count = 0;
    for (uint32_t cheat : kSentCheats) {
        if (m_career->IsCodeOn(cheat))
            message.cheats[message.count++] = cheat;
    }
    desc.m_content = &message;
    desc.m_ident = 0x82;
    desc.m_byteCount = static_cast<uint16_t>(message.count * 4 + 4);
    Sequence(desc, 8);
    int handle = player->GetLinkHandle();
    reinterpret_cast<NetLink::AppHost*>(net->m_host)->PutOnOutbox(handle, &desc);
}

// 0x0056BB70
void Skate::BroadcastPoints(bool final)
{
    OnlinePlay::Hub* net = NetLink();
    PlayerSearch players;
    PlayerSearch others;
    // the message (0x40): the time left in seconds, the cheats, final, the count, then 5 bytes a skater (its id and
    // score)
#pragma pack(push, 1)
    struct ScoreMessage {
        uint32_t timeLeft;
        uint32_t cheats;
        uint8_t final;
        uint8_t count;
        struct {
            uint8_t id;
            int score;
        } entries[52];
    };
#pragma pack(pop)
    static_assert(sizeof(ScoreMessage) == 0x10E, "the message's layout");
    for (PlayerRecord* player = players.First(true); player; player = players.Next(true)) {
        ScoreMessage message;
        message.cheats = 0;
        message.final = final;
        message.timeLeft = final ? 0u : static_cast<uint32_t>(GameMode()->GetGoalTimeLeft()) / 1000u;
        uint8_t count = 0;
        for (PlayerRecord* other = others.First(false); other; other = others.Next(false)) {
            // (its own score too, when it isn't the trick score the player keeps itself)
            GoalSystem::CMatchRules* mode = GameMode();
            bool all = final || mode->IsTrue(kShouldModulateColor) || !mode->KeepsMovePoints();
            if (other == player && !all)
                continue;
            uint8_t id = SkaterId(SkaterOf(other));
            int score;
            if (GameMode()->GetNameKey() == kNetGoalAttack)
                score = GoalSystem::GetObjectiveBoard()->CountClearedByPlayer(Prop<int>(SkaterOf(other), 0x14));
            else
                score = net->GetEntrantPoints(Prop<uint32_t>(SkaterOf(other), 0x14));
            message.entries[count].id = id;
            message.entries[count].score = score;
            ++count;
        }
        if (static_cast<int8_t>(count) <= 0)
            continue;
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        uint32_t cheats = GetCheats();
        desc.m_content = &message;
        message.count = count;
        message.cheats = cheats;
        desc.m_ident = 0x40;
        desc.m_byteCount = static_cast<uint16_t>((static_cast<int8_t>(count) + 2) * 5);
        if (final || cheats)
            Sequence(desc, 8);
        Enqueue(ServerApp(), player, &desc);
        if (player->IsOwnPlayer() || !player->HasFinishedJoining())
            continue;
        // the cheats' check (0x80): 8 bytes, the second word whatever the stack held (zero here)
        uint32_t check[2] = {GetCheats() ^ 0xDEADFACEu, 0};
        desc.m_content = check;
        desc.m_ident = 0x80;
        Sequence(desc, 8);
        desc.m_byteCount = 8;
        Enqueue(ServerApp(), player, &desc);
    }
    net->SetLastScoreUpdate(ServerApp()->m_clock);
}

} // namespace Mdl
