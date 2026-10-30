// The skate module's network side, the server's part and the rest of the client's (0x00569560-0x0056C91F): the two
// main-loop tasks (the other skaters' updates to each client, the scores every two seconds), the handlers that relay
// a client's messages to the others (as they are, to the skaters near it, sequenced), the scavenger hunt's counts,
// the graffiti / face textures passed on, the client's state-history messages, projectiles and cheats; and the skate
// module's LeaveServer / SendCheats / SendScoreUpdates. From the disassembly; see skate_net.h.

#include "game/skate/skate_net.h"

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
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Mdl {

GAME_GLOBAL(uint8_t, g_netRelayObjectId, 0x007CE48C);

namespace {

using Script::CStruct;
using GameNet::PlayerInfo;

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

GameNet::Manager* Net() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Game::CGameMode* GameMode() { return Game::g_gameMode; }
Obj::CBaseManager* ObjectManager() { return reinterpret_cast<Obj::CBaseManager*>(Obj::g_compositeObjectManager); }

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}
// thiscall calls into code not rewritten yet, by return type.
template <typename R = void, typename... A>
R Call(uint32_t address, const void* object, A... args)
{
    return GameFunction<R(__thiscall*)(const void*, A...)>(address)(object, args...);
}

PlayerInfo* Info(uint8_t* player) { return reinterpret_cast<PlayerInfo*>(player); }
PlayerInfo* ByConnection(Net::Conn* conn) { return Info(Net()->PlayerByConnection(conn)); }
PlayerInfo* ByObjectId(int id) { return Info(Net()->PlayerByObjectId(static_cast<uint16_t>(id))); }
// An object id from a message byte (signed, as the original reads it).
int IdByte(const uint8_t* data, int offset) { return static_cast<int8_t>(data[offset]); }

// Lst::Search<PlayerInfo> (vtable 0x0064C1A8) for the network manager's FirstPlayerInfo / NextPlayerInfo.
struct PlayerSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064C1A8);
    void* m_node = nullptr;
    PlayerInfo* First(bool observers) { return static_cast<PlayerInfo*>(Net()->FirstPlayerInfo(this, observers)); }
    PlayerInfo* Next(bool observers) { return static_cast<PlayerInfo*>(Net()->NextPlayerInfo(this, observers)); }
};

// A message on the server app to `player`, sequenced in `group`.
void Sequence(Net::MsgDesc& desc, uint8_t group)
{
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = group;
}
void Enqueue(Net::App* app, PlayerInfo* player, Net::MsgDesc* desc)
{
    int handle = player->GetConnHandle();
    app->EnqueueMessage(handle, desc);
}
Net::App* ServerApp() { return reinterpret_cast<Net::App*>(Net()->m_server); }

Obj::CSkater* SkaterOf(PlayerInfo* player) { return player->m_skater; }
uint8_t SkaterId(Obj::CSkater* skater) { return Field<uint8_t>(skater, 0x14); }
Score* ScoreOf(Obj::CSkater* skater) { return static_cast<Score*>(skater->GetScore()); }

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

// A skater's state history (+0x27C) and its newest state / message.
Obj::CSkaterStateHistoryComponent* History(const void* object)
{
    return Field<Obj::CSkaterStateHistoryComponent*>(object, 0x27C);
}
int& MessageCount(Obj::CSkaterStateHistoryComponent* history) { return Field<int>(history, 0xA6C); }
// The newest message slot made a message of `id`, stamped with the newest state's time.
Obj::StateHistoryMessage* NewMessage(Obj::CSkaterStateHistoryComponent* history, uint8_t id)
{
    Obj::StateHistoryMessage* message = history->GetLatestMessage();
    message->m_id = id;
    auto* state = reinterpret_cast<Obj::SkaterStateHistoryFrame*>(history->GetLatestState());
    message->SetTime(state->GetTime());
    return message;
}
template <typename T>
T& At(Obj::StateHistoryMessage* message, uint32_t offset)
{
    return Field<T>(message, offset);
}
float Scaled(uint32_t value, uint32_t constant)
{
    return static_cast<float>(static_cast<double>(value) * GameVariable<float>(constant));
}

// The relay message: the received one as it is.
void Relay(Net::MsgDesc& desc, const Net::MsgHandlerContext* context)
{
    desc.m_length = static_cast<uint16_t>(context->m_MsgLength);
    desc.m_data = context->m_Msg;
    desc.m_id = context->m_MsgId;
}

} // namespace

// --- the server's tasks ---------------------------------------------------------------------------------------------

// 0x00569BB0
void SkateNet::ServerTask(Tsk::BaseTask* /*task*/)
{
    PlayerSearch search;
    Net::App* server = ServerApp();
    uint8_t buffer[0x1000];
    for (PlayerInfo* player = search.First(true); player; player = search.Next(true)) {
        RelayStream out;
        if (!player->m_conn->IsRemote())
            continue;
        out.m_stream.SetOutputData(buffer, sizeof(buffer));
        out.m_unknown18 = static_cast<uint32_t>(player->m_conn->m_handle);
        out.m_sent = player;
        uint32_t mask = 0;
        int count = 0;
        // (round the skaters from the one after the last sent, as many as the connection takes in one message)
        int start = (player->GetLastSentSkater() + 1) % 8;
        for (int i = 0; i < 8; ++i) {
            int id = (i + start) % 8;
            PlayerInfo* other = ByObjectId(id);
            if (!other)
                continue;
            other = ByObjectId(static_cast<int>(Field<uint32_t>(SkaterOf(other), 0x14)));
            if (!other || other == player)
                continue;
            Obj::CSkater* skater = SkaterOf(other);
            auto* history = History(skater);
            int states = Field<int>(history, 0x18);
            if (!states)
                continue;
            uint16_t newest = Field<uint16_t>(history, 0x1C + ((states - 1) % 20) * 0x84 + 0x78);
            if (newest == Field<uint32_t>(player, 0x70 + Field<uint32_t>(skater, 0x14) * 4))
                continue;
            mask |= 1u << id;
            if (++count >= player->GetSkatersPerUpdate())
                break;
        }
        if (!count)
            continue;
        out.m_stream.WriteBits(mask, 8);
        for (int i = 0; i < 8; ++i) {
            if (!(mask & (1u << i)))
                continue;
            WriteObjectUpdate(reinterpret_cast<Obj::CObject*>(SkaterOf(ByObjectId(i))), &out);
            player->SetLastSentSkater(i);
        }
        out.m_stream.Flush();
        int size = out.m_stream.GetByteCount();
        if (size) {
            Net::MsgDesc desc;
            desc.Construct();
            desc.m_data = buffer;
            desc.m_length = static_cast<uint16_t>(size);
            desc.m_id = 0x23;
            Enqueue(server, player, &desc);
        }
    }
}

// 0x0056BE10
void SkateNet::ScoreTask(Tsk::BaseTask* task)
{
    auto* skate = Field<Skate*>(task, 0x1C);
    GameNet::Manager* net = Net();
    if (!net->KeepsScore())
        return;
    Net::App* server = ServerApp();
    if (server->m_time - net->GetLastScoreUpdate() >= 2000)
        skate->SendScoreUpdates(false);
}

// --- the server's handlers ------------------------------------------------------------------------------------------

// 0x00569E20
int SkateNet::HandleTrickObjects(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    PlayerInfo* player = ByConnection(context->m_Conn);
    if (!player)
        return 4;
    Score* score = ScoreOf(SkaterOf(player));
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    if (static_cast<int8_t>(data[0]) == 1 && data[0xA] == net->GetGameId())
        score->LogTrickObject(Field<int>(data, 0xC), Field<int>(data, 0x10), Field<uint32_t>(data, 0x14),
                              reinterpret_cast<uint32_t*>(data + 0x18), true);
    return 1;
}

// 0x00569E90
int SkateNet::HandleGameStats(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    PlayerInfo* player = ByConnection(context->m_Conn);
    if (!player)
        return 4;
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    if (GameMode()->GetNameChecksum() == kNetLobby || net->GetGameId() != data[0])
        return 4;
    player->m_unknown24 = Field<uint32_t>(data, 4);
    return 1;
}

// 0x00569EF0
int SkateNet::HandleComboScore(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    PlayerInfo* player = ByConnection(context->m_Conn);
    if (!player || !SkaterOf(player))
        return 4;
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    if (net->GetGameId() != data[0])
        return 4;
    int combo = Field<int>(data, 4);
    Score* score = ScoreOf(SkaterOf(player));
    if (combo > score->m_bestCombo)
        score->m_bestCombo = combo;
    Game::CGameMode* mode = GameMode();
    if (mode->IsTrue(kShouldModulateColor) || !mode->ShouldTrackTrickScore())
        return 1;
    if (mode->ShouldAccumulateScore()) {
        score->SetTotalScore(score->GetTotalScore() + combo);
        return 1;
    }
    if (mode->ShouldTrackBestCombo() && score->GetTotalScore() < combo)
        score->SetTotalScore(combo);
    return 1;
}

// 0x00569FD0
int SkateNet::HandlePlayerValue48(Net::MsgHandlerContext* context)
{
    PlayerInfo* player = ByConnection(context->m_Conn);
    if (!player)
        return 4;
    player->m_unknown48 = Field<uint32_t>(context->m_Msg, 4);
    return 1;
}

// 0x0056A000
int SkateNet::HandleRelay(Net::MsgHandlerContext* context)
{
    PlayerSearch search;
    PlayerInfo* sender = ByConnection(context->m_Conn);
    if (!sender)
        return 4;
    for (PlayerInfo* player = search.First(true); player; player = search.Next(true)) {
        Net::MsgDesc desc;
        desc.Construct();
        if (player == sender)
            continue;
        Relay(desc, context);
        switch (context->m_MsgId) {
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
            uint32_t mode = GameMode()->GetNameChecksum();
            if (mode != kFirefight && GameMode()->GetNameChecksum() != kNetFirefight)
                break;
            uint8_t* msg = reinterpret_cast<uint8_t*>(context->m_Msg);
            if (!(Field<float>(msg, 0x30) <= 10.0f) || Field<int>(msg, 0x2C) > 0xF0)
                break;
            msg[0x34] = SkaterId(SkaterOf(sender));
            Field<int>(msg, 0) = context->m_Conn->GetAveLatency();
            break;
        }
        default:
            break;
        }
        Enqueue(context->m_App, player, &desc);
    }
    return 1;
}

// 0x0056A170
int SkateNet::HandleRelayNearby(Net::MsgHandlerContext* context)
{
    PlayerSearch search;
    PlayerInfo* sender = ByConnection(context->m_Conn);
    if (!sender)
        return 4;
    Obj::CSkater* from = SkaterOf(sender);
    if (!from)
        return 4;
    for (PlayerInfo* player = search.First(true); player; player = search.Next(true)) {
        if (player == sender)
            continue;
        if (!player->IsObserver()) {
            Obj::CSkater* to = SkaterOf(player);
            if (!to)
                continue;
            // (x87: the differences and the sum unrounded)
            double dx = static_cast<double>(Field<float>(to, 0x4C)) - Field<float>(from, 0x4C);
            double dy = static_cast<double>(Field<float>(to, 0x50)) - Field<float>(from, 0x50);
            double dz = static_cast<double>(Field<float>(to, 0x54)) - Field<float>(from, 0x54);
            double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
            if (!(distance < 1800.0))
                continue;
        }
        Net::MsgDesc desc;
        desc.Construct();
        Relay(desc, context);
        Enqueue(context->m_App, player, &desc);
    }
    return 1;
}

// 0x0056A2D0
int SkateNet::HandleRelaySequenced(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    PlayerSearch search;
    PlayerInfo* sender = ByConnection(context->m_Conn);
    if (!sender)
        return 4;
    for (PlayerInfo* player = search.First(true); player; player = search.Next(true)) {
        Net::MsgDesc desc;
        desc.Construct();
        if (player == sender)
            continue;
        Relay(desc, context);
        Sequence(desc, 8);
        Enqueue(context->m_App, player, &desc);
    }
    // a script run on every machine (0x49): the server queues its own
    auto* msg = reinterpret_cast<uint8_t*>(context->m_Msg);
    if (context->m_MsgId == 0x49 && msg[0xC])
        net->QueueScript(Field<int>(msg, 8), Field<uint32_t>(msg, 4), Field<uint32_t>(msg, 0));
    return 1;
}

// 0x0056A3E0
int SkateNet::HandleJoined(Net::MsgHandlerContext* context)
{
    PlayerInfo* player = ByConnection(context->m_Conn);
    if (player)
        player->ClearJustJoined();
    return 1;
}

// 0x0056AD10
int SkateNet::HandleScavengerCollected(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    PlayerSearch search;
    auto* msg = context->m_Msg;
    PlayerInfo* collector = ByConnection(context->m_Conn);
    if (SkaterOf(collector)) {
        auto* component = SkaterOf(collector)->GetComponent(kSkaterScore);
        Score* score = Field<Score*>(component, 0x18);
        if (score)
            score->SetTotalScore(score->GetTotalScore() + 1);
        int total = 0;
        for (PlayerInfo* player = search.First(false); player; player = search.Next(false))
            total += player->m_unknown34;
        Game::CGameMode* mode = GameMode();
        int collected = mode->IsTeamGame() ? net->GetTeamScore(collector->m_team) : score->GetTotalScore();
        Net::MsgDesc desc;
        uint8_t left[2];
        if (collected == total) {
            // all collected: the collect period over everywhere
            desc.Construct();
            Net::App* server = ServerApp();
            desc.m_id = 0x94;
            Sequence(desc, 8);
            for (PlayerInfo* player = search.First(true); player; player = search.Next(true)) {
                if (!player->IsLocalPlayer())
                    Enqueue(server, player, &desc);
            }
            Game::CGoal* goal = Game::GetGoalManager()->GetGoal(kScavengerCollectPeriod, 1);
            if (Game::Virtual<bool>(goal, Game::CGoal::kVtIsActive))
                Game::Virtual<void>(goal, Game::CGoal::kVtExpire);
        } else {
            // five or one left: told to everyone (0x95: the player or the team, how many)
            uint8_t count = 0;
            if (collected == total - 5 && !CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr))
                count = 5;
            else if (score->GetTotalScore() == total - 1 && !CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr))
                count = 1;
            if (count) {
                desc.Construct();
                Net::App* server = ServerApp();
                left[0] = GameMode()->IsTeamGame() ? static_cast<uint8_t>(collector->m_team)
                                                    : SkaterId(SkaterOf(collector));
                left[1] = count;
                desc.m_data = left;
                desc.m_id = 0x95;
                Sequence(desc, 8);
                desc.m_length = 2;
                for (PlayerInfo* player = search.First(true); player; player = search.Next(true))
                    Enqueue(server, player, &desc);
            }
        }
    }
    // the object picked up: told to the collector's team mates
    if (GameMode()->IsTeamGame()) {
        Net::MsgDesc desc;
        desc.Construct();
        Net::App* server = ServerApp();
        for (PlayerInfo* player = search.First(false); player; player = search.Next(false)) {
            if (player == collector || player->m_team != collector->m_team)
                continue;
            desc.m_data = msg;
            desc.m_id = 0x90;
            Sequence(desc, 8);
            desc.m_length = 4;
            Enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B0B0
int SkateNet::HandleScavengerDropped(Net::MsgHandlerContext* context)
{
    PlayerSearch search;
    PlayerInfo* dropper = ByConnection(context->m_Conn);
    if (!dropper)
        return 4;
    if (dropper->m_unknown34 > 5 || (dropper->m_unknown34 == 5 && !dropper->IsLocalPlayer()))
        return 4;
    auto* position = reinterpret_cast<const uint32_t*>(context->m_Msg);
    if (!dropper->IsLocalPlayer()) {
        auto* to = &Field<uint32_t>(dropper, static_cast<uint32_t>((dropper->m_unknown34 + 0x18) * 0x10));
        for (int i = 0; i < 4; ++i)
            to[i] = position[i];
    }
    if (!dropper->IsLocalPlayer())
        ++dropper->m_unknown34;
    int dropped = 0;
    int players = 0;
    for (PlayerInfo* player = search.First(false); player; player = search.Next(false)) {
        dropped += player->m_unknown34;
        ++players;
    }
    if (dropped == players * 5) {
        // every player's five dropped: the hunt goes on (0x92 to everyone)
        Net::MsgDesc desc;
        desc.Construct();
        Net::App* server = ServerApp();
        for (PlayerInfo* player = search.First(true); player; player = search.Next(true)) {
            desc.m_id = 0x92;
            Sequence(desc, 8);
            Enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B2B0
int SkateNet::HandleGraffitiTexture(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    auto* msg = reinterpret_cast<uint8_t*>(context->m_Msg);
    // (a client's server: the sender; a client: the player the last 0x96 named)
    auto from = [&]() -> PlayerInfo* {
        if (net->OnServer())
            return ByConnection(context->m_Conn);
        return ByObjectId(g_netRelayObjectId);
    };
    // the texture's virtual slot 5 (0x00456B30): its texture and palette
    auto load = [](void* texture, const void* textureData, uint32_t textureSize, const void* palette,
                   uint32_t paletteSize) {
        reinterpret_cast<void(__thiscall*)(void*, const void*, uint32_t, const void*, uint32_t)>(
            Field<void**>(texture, 0)[5])(texture, textureData, textureSize, palette, paletteSize);
    };
    switch (context->m_MsgId) {
    case 0x96: // the player the next ones are for
        g_netRelayObjectId = msg[0];
        break;
    case 0x98: { // the palette
        PlayerInfo* player = from();
        if (player)
            load(player->GetGraffitiTexture(), nullptr, 0, msg, context->m_MsgLength);
        break;
    }
    case 0x97: { // the texture
        PlayerInfo* player = from();
        if (!player)
            break;
        load(player->GetGraffitiTexture(), msg, context->m_MsgLength, nullptr, 0);
        player->SetCustomTag(true);
        break;
    }
    case 0x9A: { // which board parts show it - the last: passed on to the others by a server
        bool onServer = false;
        PlayerInfo* player;
        if (net->OnServer()) {
            player = ByConnection(context->m_Conn);
            onServer = true;
        } else {
            player = ByObjectId(g_netRelayObjectId);
        }
        uint32_t parts = 0;
        if (player) {
            parts = Field<uint32_t>(msg, 0);
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
        Net::App* app = context->m_App;
        for (PlayerInfo* other = search.First(true); other; other = search.Next(true)) {
            Net::MsgDesc desc;
            desc.Construct();
            if (other->IsLocalPlayer() || other == player || other->m_conn->GetBandwidthType() == 1)
                continue;
            uint8_t id = SkaterId(SkaterOf(player));
            desc.m_data = &id;
            desc.m_id = 0x96;
            desc.m_length = 1;
            Sequence(desc, 10);
            Enqueue(app, other, &desc);
            void* palette = Call<void*>(0x0048DF20, texture);
            uint32_t paletteSize = Call<uint32_t>(0x0048DF30, texture);
            app->StreamMessageToConn(other->m_conn, 0x98, static_cast<uint16_t>(paletteSize), palette,
                                     reinterpret_cast<const char*>(0x0064C974), 10, false, true); // "CAG Palette"
            void* pixels = Call<void*>(0x0048DF00, texture);
            uint32_t pixelsSize = Call<uint32_t>(0x0049F3A0, texture);
            app->StreamMessageToConn(other->m_conn, 0x97, static_cast<uint16_t>(pixelsSize), pixels,
                                     reinterpret_cast<const char*>(0x0064C968), 10, false, true); // "CAG Texture"
            uint32_t sentParts = parts;
            desc.m_data = &sentParts;
            desc.m_id = 0x9A;
            desc.m_length = 4;
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
int SkateNet::HandleFaceTexture(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    void* memory = Mem::Calloc(0x449C);
    void* texture = memory ? reinterpret_cast<void*>(static_cast<Gfx::CFaceTexture*>(memory)->Construct()) : nullptr;
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    uint32_t size = context->m_MsgLength;
    PlayerInfo* player;
    if (net->OnServer()) {
        player = ByConnection(context->m_Conn);
    } else {
        // (a client: the player's object id first)
        uint8_t id = *data++;
        --size;
        player = ByObjectId(id);
    }
    // (quirk kept: the texture is leaked here)
    if (!player)
        return 4;
    Obj::CSkater* skater = SkaterOf(player);
    if (!skater)
        return 4;
    static_cast<Gfx::CFaceTexture*>(texture)->ReadFrom(data, static_cast<int>(size));
    static_cast<Gfx::CFaceTexture*>(texture)->SetValid(true);
    auto* model = skater->GetComponent(kModel);
    static_cast<Nx::CModel*>(Field<void*>(model, 0x208))->ReplaceFaceTexture(texture, reinterpret_cast<const char*>(0x0064E820), kAll);
    if (net->OnServer()) {
        // the server keeps the face and streams it to the others
        PlayerSearch search;
        Net::App* app = context->m_App;
        PlayerInfo* sender = ByConnection(context->m_Conn);
        sender->SetFaceTextureData(reinterpret_cast<const uint8_t*>(context->m_Msg), context->m_MsgLength);
        for (PlayerInfo* other = search.First(true); other; other = search.Next(true)) {
            if (other->IsLocalPlayer() || other == sender)
                continue;
            app->StreamMessageToConn(other->m_conn, 0x7E, 0x4499, sender->GetFaceTextureData(),
                                     reinterpret_cast<const char*>(0x0064CA2C), 9, false, true); // "Face Data"
        }
    }
    // (freed without its destructor, as the original)
    Mem::Manager::Instance()->Free(texture);
    return 1;
}

// --- the client's handlers ------------------------------------------------------------------------------------------

// 0x00569560
int SkateNet::HandleChangeLevel(Net::MsgHandlerContext* context)
{
    static_cast<Skate*>(context->m_data)->ChangeLevel(Field<uint32_t>(context->m_Msg, 0));
    return 1;
}

// 0x0056A410
int SkateNet::HandleHistoryValue(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    Obj::CSkater* skater = static_cast<Skate*>(context->m_data)->GetSkaterById(static_cast<uint32_t>(IdByte(data, 0)));
    if (!skater)
        return 4;
    auto* history = skater->GetComponent(Obj::CSkaterStateHistoryComponent::kType);
    Field<uint32_t>(history, 0x1104) = Field<uint32_t>(data, 4);
    return 1;
}

// 0x0056A450
int SkateNet::HandleSkaterMessages(Net::MsgHandlerContext* context)
{
    auto* skate = static_cast<Skate*>(context->m_data);
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    Obj::CBaseManager* objects = ObjectManager();
    // the object (by the message's id byte at `offset`) and, for some, the skater too
    auto object = [&](int offset) { return objects->GetObjectByID(static_cast<uint32_t>(IdByte(data, offset))); };
    auto skater = [&](int offset) -> Obj::CObject* {
        if (!object(offset))
            return nullptr;
        return reinterpret_cast<Obj::CObject*>(skate->GetSkaterById(static_cast<uint32_t>(IdByte(data, offset))));
    };
    Obj::CObject* target = nullptr;
    Obj::StateHistoryMessage* message;
    int result = 1;
    switch (context->m_MsgId) {
    case 0x29: {
        uint8_t first = data[0];
        uint8_t id = data[1];
        uint32_t value = Field<uint32_t>(data, 2);
        uint16_t a = Field<uint16_t>(data, 6), b = Field<uint16_t>(data, 8), time = Field<uint16_t>(data, 10);
        uint16_t c = Field<uint16_t>(data, 12);
        target = objects->GetObjectByID(static_cast<uint32_t>(static_cast<int8_t>(id)));
        if (!target)
            return 1;
        float scaledTime = Scaled(time, 0x0064A568);
        GameNet::Manager* net = Net();
        if (net->InNetGame()) {
            // (only the camera's own player's)
            uint8_t* player = net->PlayerByObjectId(static_cast<uint16_t>(target->m_id));
            void* camera = net->GetCamPlayer();
            if (!camera || player != camera)
                scaledTime = 0.0f;
        }
        message = NewMessage(History(target), 0x29);
        At<uint32_t>(message, 0x20) = value;
        At<float>(message, 0x10) = Scaled(a, 0x0064A568);
        At<uint8_t>(message, 1) = id;
        At<float>(message, 0x18) = scaledTime;
        At<uint8_t>(message, 2) = first;
        At<float>(message, 0x14) = Scaled(b, 0x0064A568);
        At<float>(message, 0x1C) = Scaled(c, 0x0064A568);
        break;
    }
    case 0x2B:
        target = object(0);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x2B);
        At<uint8_t>(message, 1) = data[0];
        At<float>(message, 0xC) = Scaled(data[1], 0x00645D70);
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
        static const uint32_t kFirst[4] = {0x00645D6C, 0x00645D74, 0x006486B8, 0x0066D520}; // 0.05, 0.1, 10.1, (0)
        At<float>(message, 0x28) = GameVariable<float>(kFirst[bits & 3]);
        At<float>(message, 0x2C) = GameVariable<float>((bits >> 2 & 1) ? 0x006486B8 : 0x006486B4); // 10.1 / 0.04
        At<float>(message, 0x30) = GameVariable<float>((bits >> 3 & 1) ? 0x006486AC : 0x006486B0); // 20 / 0.0022
        At<float>(message, 0x34) = GameVariable<float>((bits >> 4 & 1) ? 0x00646710 : 0x006486A8); // 10 / 0.0017
        At<float>(message, 0x38) = GameVariable<float>((bits >> 5 & 1) ? 0x006453A8 : 0x00646744); // 1 / 1.5
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
        At<uint32_t>(message, 0x20) = Field<uint32_t>(data, 0);
        At<bool>(message, 0xA) = data[5] != 0;
        break;
    case 0x41:
        target = object(4);
        if (!target)
            return 1;
        message = NewMessage(History(target), 0x41);
        At<uint8_t>(message, 1) = data[4];
        At<uint32_t>(message, 0x1C) = Field<uint32_t>(data, 0);
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
        At<uint32_t>(message, 0x3C) = Field<uint32_t>(data, 0);
        At<uint32_t>(message, 0x40) = Field<uint32_t>(data, 4);
        At<float>(message, 0x48) = static_cast<float>(Field<int16_t>(data, 8));
        At<float>(message, 0x4C) = static_cast<float>(Field<int16_t>(data, 0xA));
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
        At<uint32_t>(message, 0x24) = Field<uint32_t>(data, 4);
        At<int>(message, 0x44) = static_cast<int8_t>(data[9]);
        At<uint32_t>(message, 0x20) = Field<uint32_t>(data, 0);
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
int SkateNet::HandlePlayerLeft(Net::MsgHandlerContext* context)
{
    auto* skate = static_cast<Skate*>(context->m_data);
    GameNet::Manager* net = Net();
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    uint8_t* player = net->PlayerByObjectId(static_cast<uint16_t>(IdByte(data, 0)));
    if (player) {
        Obj::CSkater* skater = Info(player)->m_skater;
        net->DropPlayer(player, IdByte(data, 1));
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
int SkateNet::HandleServerQuit(Net::MsgHandlerContext* /*context*/)
{
    GameNet::Manager* net = Net();
    Script::RunScript(kCreateServerQuitDialog, nullptr, nullptr, false, nullptr);
    net->RemovePlayers();
    net->DisconnectClients();
    return 3;
}

// 0x0056AC90
int SkateNet::HandleGraffitiObjects(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    GameNet::Manager* net = Net();
    if (data[0] != net->GetGameId())
        return 4;
    int owner;
    if (GameMode()->IsTeamGame())
        owner = Info(net->PlayerByObjectId(Field<uint16_t>(data, 4)))->m_team;
    else
        owner = Field<int>(data, 4);
    auto* names = reinterpret_cast<const uint32_t*>(data + 0xC);
    for (uint32_t i = 0; i < Field<uint32_t>(data, 8); ++i)
        Obj::g_trickObjectManager->SetObjectGraffitiColor(names[i], owner);
    return 1;
}

// 0x0056B220
int SkateNet::HandlePlaceTag(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    PlayerInfo* player = ByObjectId(IdByte(data, 0));
    if (!player)
        return 4;
    auto* tagging = static_cast<Obj::CTaggingComponent*>(SkaterOf(player)->GetComponent(kTagging));
    if (!tagging)
        return 4;
    Mth::Vector start = Field<Mth::Vector>(data, 4);
    Mth::Vector end = Field<Mth::Vector>(data, 0x14);
    tagging->PlaceTag(data[1] != 0, start, end, Field<int>(data, 0x24), false);
    return 1;
}

// 0x0056B7A0
int SkateNet::HandleSparks(Net::MsgHandlerContext* context)
{
    auto* object = reinterpret_cast<Obj::CSkater*>(
        ObjectManager()->GetObjectByID(static_cast<uint32_t>(IdByte(reinterpret_cast<uint8_t*>(context->m_Msg), 0))));
    if (!object)
        return 1;
    if (context->m_MsgId == 0x37)
        object->SparksOn();
    else if (context->m_MsgId == 0x38)
        object->SparksOff();
    return 1;
}

// 0x0056B7E0
int SkateNet::HandleLookUpObject(Net::MsgHandlerContext* context)
{
    // (the object looked up, nothing done with it)
    ObjectManager()->GetObjectByID(static_cast<uint32_t>(IdByte(reinterpret_cast<uint8_t*>(context->m_Msg), 0)));
    return 1;
}

// 0x0056B800
int SkateNet::HandleTerrainSound(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    Mth::Vector position;
    position.x = static_cast<float>(Field<int16_t>(data, 0));
    position.y = static_cast<float>(Field<int16_t>(data, 2));
    position.z = static_cast<float>(Field<int16_t>(data, 4));
    position.w = 0.0f; // (the original leaves w as the stack had it)
    float volume = static_cast<float>(static_cast<int8_t>(data[8]));
    float pitch = static_cast<float>(static_cast<int8_t>(data[9]));
    float value = static_cast<float>(static_cast<int8_t>(data[0xA]));
    Terrain::PlayTerrainSound(data[6], data[7], &position, volume, pitch, value, false);
    return 1;
}

// 0x0056C330
int SkateNet::HandleRunScriptNow(Net::MsgHandlerContext* context)
{
    GameNet::Manager* net = Net();
    auto* data = reinterpret_cast<uint32_t*>(context->m_Msg);
    if (!Field<bool>(net, 0x18C)) {
        net->QueueScript(static_cast<int>(data[2]), data[1], data[0]);
        return 1;
    }
    Obj::CObject* object = ObjectManager()->GetObjectByID(data[1]);
    Script::CScript* script =
        Script::SpawnScript(data[0], nullptr, 0, nullptr, static_cast<int>(data[2]), 0, false, false, false, false);
    script->m_object.Set(object);
    script->Update();
    return 1;
}

// 0x0056C3D0
int SkateNet::HandleProjectile(Net::MsgHandlerContext* context)
{
    auto* skate = static_cast<Skate*>(context->m_data);
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    Obj::CSkater* skater = skate->GetSkaterById(static_cast<uint32_t>(IdByte(data, 0x34)));
    if (!skater)
        return 4;
    Mth::Vector velocity = Field<Mth::Vector>(data, 0x14);
    Mth::Vector position = Field<Mth::Vector>(data, 4);
    velocity.Normalize();
    uint32_t time = Field<uint32_t>(data, 0);
    if (!Net()->OnServer()) {
        // (a client: the server's latency added)
        Net::ConnSearch search{reinterpret_cast<void**>(0x0064759C), nullptr};
        Net::Conn* conn = context->m_App->FirstConnection(&search);
        time += conn->IsLocal() ? 0 : static_cast<uint32_t>(conn->m_latencyTest.m_averageLatency);
    }
    // where it is now: moved along its direction for the time it took (x87: unrounded until stored)
    double seconds = static_cast<double>(time) * static_cast<double>(Game::g_goalLifeSecondsPerMillisecond);
    position.x = static_cast<float>(velocity.x * seconds + position.x);
    position.y = static_cast<float>(position.y + velocity.y * seconds);
    position.z = static_cast<float>(position.z + velocity.z * seconds);
    CStruct* params = NewStructure();
    params->AddChecksum(kOwnerId, static_cast<uint32_t>(IdByte(data, 0x34)));
    params->AddChecksum(kAmmoType, Field<uint32_t>(data, 0x28));
    params->AddInteger(kX, static_cast<int>(position.x));
    params->AddInteger(kY, static_cast<int>(position.y));
    params->AddInteger(kZ, static_cast<int>(position.z));
    params->AddInteger(kScaledX, static_cast<int>(Field<float>(data, 0x14)));
    params->AddInteger(kScaledY, static_cast<int>(Field<float>(data, 0x18)));
    params->AddInteger(kScaledZ, static_cast<int>(Field<float>(data, 0x1C)));
    params->AddInteger(kRadius, Field<int>(data, 0x2C));
    params->AddFloat(kScale, Field<float>(data, 0x30));
    params->AddVector(kPos, Field<Script::VectorValue>(data, 4));
    params->AddVector(kVel, Field<Script::VectorValue>(data, 0x14));
    uint32_t launch = Field<uint32_t>(data, 0x24) == kFireball ? kClientLaunchFireball : kClientLaunchProjectile;
    Script::RunScript(launch, params, reinterpret_cast<Obj::CObject*>(skater), false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x0056C6E0
int SkateNet::HandleCheats(Net::MsgHandlerContext* context)
{
    CFuncs::CFunc_ClearOmnigons(nullptr, nullptr); // (the cheats off first: the function folded there)
    auto* data = reinterpret_cast<uint32_t*>(context->m_Msg);
    for (int i = 0; i < static_cast<int>(data[0]); ++i) {
        CStruct* params = NewStructure();
        params->AddChecksum(kOn, 0);
        params->AddChecksum(kCheatFlag, data[1 + i]);
        Script::RunScript(kClientToggleCheat, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    return 1;
}

// 0x0056C7E0
int SkateNet::HandleToggleCheat(Net::MsgHandlerContext* context)
{
    CStruct* params = NewStructure();
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    if (data[4])
        params->AddChecksum(kOn, 0);
    params->AddChecksum(kCheatFlag, Field<uint32_t>(data, 0));
    Script::RunScript(kClientToggleCheat, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x0056C8C0
int SkateNet::HandleSkaterChannel(Net::MsgHandlerContext* context)
{
    auto* data = reinterpret_cast<uint8_t*>(context->m_Msg);
    Obj::CObject* object = ObjectManager()->GetObjectByID(static_cast<uint32_t>(IdByte(data, 0)));
    if (!object)
        return 1;
    auto* blender = static_cast<Obj::CAnimBlenderComponent*>(
        static_cast<Obj::CCompositeObject*>(object)->GetComponent(kAnimBlender));
    if (!blender)
        return 1;
    Gfx::CBlendChannel* channel = blender->FindChannel(0);
    if (channel)
        channel->SetFlipped(static_cast<uint32_t>(IdByte(data, 0)), data[1] != 0, 0, false);
    return 1;
}

// --- the skate module's side ----------------------------------------------------------------------------------------

// 0x0056B890
void Skate::LeaveServer()
{
    GameNet::Manager* net = Net();
    Script::RunScript(kPatchCleanup, nullptr, nullptr, false, nullptr);
    Script::ProcessPendingSymbols();
    if (net->OnServer()) {
        // the server: everyone told it's over (0x32), the server and its tasks gone
        Net::MsgDesc desc;
        desc.Construct();
        Net::App* server = ServerApp();
        if (net->GetHostMode() == 1)
            net->RemoveHostTask();
        desc.m_id = 0x32;
        desc.m_priority = 0xFF;
        server->EnqueueMessage(0xFF, &desc);
        server->SendData(false);
        net->DestroyServer();
        m_serverTasks[0]->Remove();
        m_serverTasks[1]->Remove();
        net->RemovePlayersAndApps();
        return;
    }
    // a client: the server told it's leaving (0xB)
    Net::App* client = net->GetClient(0);
    if (client) {
        Net::MsgDesc desc;
        desc.Construct();
        desc.m_id = 0xB;
        Sequence(desc, 8);
        static_cast<Net::Client*>(client)->EnqueueMessageToServer(&desc);
        client->SendData(false);
    }
    net->RemovePlayersAndApps();
}

// 0x0056B990
void Skate::SendCheats(GameNet::PlayerInfo* player)
{
    GameNet::Manager* net = Net();
    Net::MsgDesc desc;
    desc.Construct();
    struct {
        int count;
        uint32_t cheats[32];
    } message;
    message.count = 0;
    for (uint32_t cheat : kSentCheats) {
        if (m_career->GetCheat(cheat))
            message.cheats[message.count++] = cheat;
    }
    desc.m_data = &message;
    desc.m_id = 0x82;
    desc.m_length = static_cast<uint16_t>(message.count * 4 + 4);
    Sequence(desc, 8);
    int handle = player->GetConnHandle();
    reinterpret_cast<Net::App*>(net->m_server)->EnqueueMessage(handle, &desc);
}

// 0x0056BB70
void Skate::SendScoreUpdates(bool final)
{
    GameNet::Manager* net = Net();
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
    for (PlayerInfo* player = players.First(true); player; player = players.Next(true)) {
        ScoreMessage message;
        message.cheats = 0;
        message.final = final;
        message.timeLeft = final ? 0u : static_cast<uint32_t>(GameMode()->GetGoalTimeLeft()) / 1000u;
        uint8_t count = 0;
        for (PlayerInfo* other = others.First(false); other; other = others.Next(false)) {
            // (its own score too, when it isn't the trick score the player keeps itself)
            Game::CGameMode* mode = GameMode();
            bool all = final || mode->IsTrue(kShouldModulateColor) || !mode->ShouldTrackTrickScore();
            if (other == player && !all)
                continue;
            uint8_t id = SkaterId(SkaterOf(other));
            int score;
            if (GameMode()->GetNameChecksum() == kNetGoalAttack)
                score = Game::GetGoalManager()->NumGoalsBeatenBy(Field<int>(SkaterOf(other), 0x14));
            else
                score = net->GetPlayerScore(Field<uint32_t>(SkaterOf(other), 0x14));
            message.entries[count].id = id;
            message.entries[count].score = score;
            ++count;
        }
        if (static_cast<int8_t>(count) <= 0)
            continue;
        Net::MsgDesc desc;
        desc.Construct();
        uint32_t cheats = GetCheats();
        desc.m_data = &message;
        message.count = count;
        message.cheats = cheats;
        desc.m_id = 0x40;
        desc.m_length = static_cast<uint16_t>((static_cast<int8_t>(count) + 2) * 5);
        if (final || cheats)
            Sequence(desc, 8);
        Enqueue(ServerApp(), player, &desc);
        if (player->IsLocalPlayer() || !player->IsFullyIn())
            continue;
        // the cheats' check (0x80): 8 bytes, the second word whatever the stack held (zero here)
        uint32_t check[2] = {GetCheats() ^ 0xDEADFACEu, 0};
        desc.m_data = check;
        desc.m_id = 0x80;
        Sequence(desc, 8);
        desc.m_length = 8;
        Enqueue(ServerApp(), player, &desc);
    }
    net->SetLastScoreUpdate(ServerApp()->m_time);
}

} // namespace Mdl

REPLACE(0x00569BB0, Mdl::SkateNet::ServerTask);
REPLACE(0x0056BE10, Mdl::SkateNet::ScoreTask);
REPLACE(0x00569E20, Mdl::SkateNet::HandleTrickObjects);
REPLACE(0x00569E90, Mdl::SkateNet::HandleGameStats);
REPLACE(0x00569EF0, Mdl::SkateNet::HandleComboScore);
REPLACE(0x00569FD0, Mdl::SkateNet::HandlePlayerValue48);
REPLACE(0x0056A000, Mdl::SkateNet::HandleRelay);
REPLACE(0x0056A170, Mdl::SkateNet::HandleRelayNearby);
REPLACE(0x0056A2D0, Mdl::SkateNet::HandleRelaySequenced);
REPLACE(0x0056A3E0, Mdl::SkateNet::HandleJoined);
REPLACE(0x0056AD10, Mdl::SkateNet::HandleScavengerCollected);
REPLACE(0x0056B0B0, Mdl::SkateNet::HandleScavengerDropped);
REPLACE(0x0056B2B0, Mdl::SkateNet::HandleGraffitiTexture);
REPLACE(0x0056B5D0, Mdl::SkateNet::HandleFaceTexture);
REPLACE(0x00569560, Mdl::SkateNet::HandleChangeLevel);
REPLACE(0x0056A410, Mdl::SkateNet::HandleHistoryValue);
REPLACE(0x0056A450, Mdl::SkateNet::HandleSkaterMessages);
REPLACE(0x0056ABD0, Mdl::SkateNet::HandlePlayerLeft);
REPLACE(0x0056AC50, Mdl::SkateNet::HandleServerQuit);
REPLACE(0x0056AC90, Mdl::SkateNet::HandleGraffitiObjects);
REPLACE(0x0056B220, Mdl::SkateNet::HandlePlaceTag);
REPLACE(0x0056B7A0, Mdl::SkateNet::HandleSparks);
REPLACE(0x0056B7E0, Mdl::SkateNet::HandleLookUpObject);
REPLACE(0x0056B800, Mdl::SkateNet::HandleTerrainSound);
REPLACE(0x0056C330, Mdl::SkateNet::HandleRunScriptNow);
REPLACE(0x0056C3D0, Mdl::SkateNet::HandleProjectile);
REPLACE(0x0056C6E0, Mdl::SkateNet::HandleCheats);
REPLACE(0x0056C7E0, Mdl::SkateNet::HandleToggleCheat);
REPLACE(0x0056C8C0, Mdl::SkateNet::HandleSkaterChannel);
REPLACE(0x0056B890, &Mdl::Skate::LeaveServer);
REPLACE(0x0056B990, &Mdl::Skate::SendCheats);
REPLACE(0x0056BB70, &Mdl::Skate::SendScoreUpdates);
