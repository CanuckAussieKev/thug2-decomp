// GameModules::Score's network side: trick objects (graffiti) scored on, logged on the server and sent to the players (see
// score.h).

#include "game/skate/score.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/skate/game_mode.h"
#include "game/skate/score_internal.h"
#include "game/skater/trick_component.h"
#include "game/skate/skate_module.h"

namespace GameModules {

using namespace ScoreInternal;

namespace {

const char* const kTrickObjBuffer = "TrickObj Buffer"; // "TrickObj Buffer"

// A walk over the players (OnlinePlay::Manager::BeginPlayerWalk / StepPlayerWalk).
struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};

OnlinePlay::PlayerRecord* AsPlayer(void* player) { return static_cast<OnlinePlay::PlayerRecord*>(player); }

} // namespace

// 0x00561BA0
void Score::NoteTaggedPropAsk(int score)
{
    OnlinePlay::Manager* net = NetManager();
    NetLink::NetMessageInfo desc;
    desc.Construct();
    auto* info = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(m_riderNo)));
    auto* client = static_cast<NetLink::Client*>(net->GetClient(info->m_skater->m_viewSlot));
    GameObjects::TrickObjectsMessage message;
    message.one = 1;
    message.player = m_riderNo;
    message.score = static_cast<uint32_t>(score);
    message.gameId = net->GetGameId();
    auto* trick = static_cast<GameObjects::CMoveExecutor*>(SkaterById(m_riderNo)->GetComponent(kTrick));
    uint32_t bytes = trick->EndTrickObjects(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.objects)),
                                            sizeof(message.objects));
    message.count = bytes >> 2;
    DebugOut::Printf("Client -> server %d tricks\n", bytes >> 2); // "Client -> server %d tricks\n"
    // (the descriptor is filled and not used: the message goes by the stream's own arguments)
    desc.m_data = &message;
    desc.m_byteCount = static_cast<uint16_t>(bytes + offsetof(GameObjects::TrickObjectsMessage, objects));
    desc.m_id = GameObjects::kMsgTrickObjects;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = GameObjects::kGroupGame;
    client->SendBulkToHost(GameObjects::kMsgTrickObjects, desc.m_byteCount, &message, kTrickObjBuffer, GameObjects::kGroupGame,
                                  true, false);
}

// 0x00561CB0
void Score::NoteTaggedProp(int skaterId, int score, uint32_t count, uint32_t* objects, bool send)
{
    OnlinePlay::Manager* net = NetManager();
    void* skate = SkateCalls::Skate();
    PlayerSearch search;
    if (send) {
        NetLink::App* server = net->m_server; // (0x005480C0)
        NetLink::Conn* conn = nullptr;
        if (GameObjects::CRider* skater = SkaterById(m_riderNo)) {
            if (auto* info = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(skater->m_id))))
                conn = info->m_conn;
        }
        uint8_t stolen[8] = {};
        count = TrickObjects()->LogTrickObjects(count, objects, stolen, skaterId, static_cast<uint32_t>(score));
        if (!count)
            return;
        GameObjects::TagStolenMessage stolenMessage;
        for (int victim = 0; victim < 8; ++victim) {
            if (!stolen[victim])
                continue;
            stolenMessage.thief = skaterId;
            stolenMessage.victim = victim;
            stolenMessage.gameId = net->GetGameId();
            auto* thief = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(skaterId)));
            auto* robbed = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(victim)));
            // (not between players of one team)
            if (static_cast<uint8_t>(GameMode()->IsSquadMatch()) && robbed && thief && robbed->m_team == thief->m_team)
                continue;
            NetLink::NetMessageInfo desc;
            desc.Construct();
            desc.m_data = &stolenMessage;
            desc.m_byteCount = sizeof(stolenMessage);
            desc.m_id = GameObjects::kMsgTagStolen;
            server->PutOnOutbox(thief->GetLinkHandle(), &desc);
            stolenMessage.thief = skaterId;
            stolenMessage.victim = victim;
            stolenMessage.gameId = net->GetGameId();
            if (robbed)
                server->PutOnOutbox(robbed->GetLinkHandle(), &desc);
        }

        GameObjects::TrickObjectsMessage all;
        all.one = 1;
        all.player = skaterId;
        all.score = static_cast<uint32_t>(score);
        all.count = count;
        all.gameId = net->GetGameId();
        std::memcpy(all.objects, objects, count * 4);
        NetLink::NetMessageInfo desc;
        desc.Construct();
        desc.m_byteCount = static_cast<uint16_t>(count * 4 + offsetof(GameObjects::TrickObjectsMessage, objects));
        desc.m_data = &all;
        desc.m_id = GameObjects::kMsgTrickObjects;
        desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        desc.m_groupId = GameObjects::kGroupGame;
        for (void* info = net->BeginPlayerWalk(&search, false); info; info = net->StepPlayerWalk(&search, false))
            server->SendBulkMessage(AsPlayer(info)->GetLinkHandle(), desc.m_id, desc.m_byteCount, desc.m_data,
                                  kTrickObjBuffer, GameObjects::kGroupGame, true, false);

        GameObjects::ObserverTrickObjectsMessage observed;
        observed.count = count;
        observed.player = skaterId;
        observed.gameId = net->GetGameId();
        std::memcpy(observed.objects, objects, count * 4);
        desc.m_data = &observed;
        desc.m_byteCount = static_cast<uint16_t>(count * 4 + offsetof(GameObjects::ObserverTrickObjectsMessage, objects));
        desc.m_id = GameObjects::kMsgObserverTrickObjects;
        for (void* info = net->BeginPlayerWalk(&search, true); info; info = net->StepPlayerWalk(&search, true)) {
            if (static_cast<uint8_t>(AsPlayer(info)->IsObserver()))
                server->SendBulkMessage(AsPlayer(info)->GetLinkHandle(), desc.m_id, desc.m_byteCount, desc.m_data,
                                      kTrickObjBuffer, GameObjects::kGroupGame, true, false);
        }
        static_cast<GameModules::Skate*>(skate)->BroadcastPoints(false); // the skate module's score updates
        if (conn->IsLocal()) // (no check: as the original)
            return;
    }
    int owner = static_cast<uint8_t>(GameMode()->IsSquadMatch())
                    ? AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(skaterId)))->m_team
                    : skaterId;
    for (uint32_t i = 0; i < count; ++i)
        TrickObjects()->SetObjectGraffitiColor(objects[i], owner);
}

// 0x005626F0
int __cdecl Score::HandleScoreMessage(NetLink::InboundMsgCtx* context)
{
    auto* message = reinterpret_cast<GameObjects::TrickObjectsMessage*>(context->m_body);
    auto* score = static_cast<Score*>(static_cast<GameObjects::CRider*>(context->m_data)->GetPointsTotal());
    if (static_cast<int8_t>(message->one) == 1 && message->gameId == NetManager()->GetGameId())
        score->NoteTaggedProp(message->player, static_cast<int>(message->score), message->count, message->objects, false);
    return 1;
}

} // namespace Mdl
