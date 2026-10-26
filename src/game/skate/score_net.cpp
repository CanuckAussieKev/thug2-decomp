// Mdl::Score's network side: trick objects (graffiti) scored on, logged on the server and sent to the players (see
// score.h).

#include "game/skate/score.h"

#include <cstring>

#include "game/core/dbg.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/skate/score_internal.h"
#include "game/skater/trick_component.h"
#include "hook/hook.h"

namespace Mdl {

using namespace ScoreInternal;

namespace {

const char* const kTrickObjBuffer = reinterpret_cast<const char*>(0x0064C8BC); // "TrickObj Buffer"

// A walk over the players (GameNet::Manager::FirstPlayerInfo / NextPlayerInfo).
struct PlayerSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064C1A8);
    void* m_node = nullptr;
};

GameNet::PlayerInfo* AsPlayer(void* player) { return static_cast<GameNet::PlayerInfo*>(player); }

} // namespace

// 0x00561BA0
void Score::LogTrickObjectRequest(int score)
{
    GameNet::Manager* net = NetManager();
    Net::MsgDesc desc;
    desc.Construct();
    auto* info = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(m_skaterId)));
    auto* client = static_cast<Net::Client*>(net->GetClient(info->m_skater->m_viewport));
    Obj::TrickObjectsMessage message;
    message.one = 1;
    message.player = m_skaterId;
    message.score = static_cast<uint32_t>(score);
    message.gameId = net->GetGameId();
    auto* trick = static_cast<Obj::CTrickComponent*>(SkaterById(m_skaterId)->GetComponent(kTrick));
    uint32_t bytes = trick->EndTrickObjects(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.objects)),
                                            sizeof(message.objects));
    message.count = bytes >> 2;
    Dbg::Printf(Text(0x0064E2B0), bytes >> 2); // "Client -> server %d tricks\n"
    // (the descriptor is filled and not used: the message goes by the stream's own arguments)
    desc.m_data = &message;
    desc.m_length = static_cast<uint16_t>(bytes + offsetof(Obj::TrickObjectsMessage, objects));
    desc.m_id = Obj::kMsgTrickObjects;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = Obj::kGroupGame;
    client->StreamMessageToServer(Obj::kMsgTrickObjects, desc.m_length, &message, kTrickObjBuffer, Obj::kGroupGame,
                                  true, false);
}

// 0x00561CB0
void Score::LogTrickObject(int skaterId, int score, uint32_t count, uint32_t* objects, bool send)
{
    GameNet::Manager* net = NetManager();
    void* skate = SkateCalls::Skate();
    PlayerSearch search;
    if (send) {
        Net::App* server = net->m_server; // (0x005480C0)
        Net::Conn* conn = nullptr;
        if (Obj::CSkater* skater = SkaterById(m_skaterId)) {
            if (auto* info = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(skater->m_id))))
                conn = info->m_conn;
        }
        uint8_t stolen[8] = {};
        count = TrickObjects()->LogTrickObjects(count, objects, stolen, skaterId, static_cast<uint32_t>(score));
        if (!count)
            return;
        Obj::TagStolenMessage stolenMessage;
        for (int victim = 0; victim < 8; ++victim) {
            if (!stolen[victim])
                continue;
            stolenMessage.thief = skaterId;
            stolenMessage.victim = victim;
            stolenMessage.gameId = net->GetGameId();
            auto* thief = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(skaterId)));
            auto* robbed = AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(victim)));
            // (not between players of one team)
            if (static_cast<uint8_t>(GameMode()->IsTeamGame()) && robbed && thief && robbed->m_team == thief->m_team)
                continue;
            Net::MsgDesc desc;
            desc.Construct();
            desc.m_data = &stolenMessage;
            desc.m_length = sizeof(stolenMessage);
            desc.m_id = Obj::kMsgTagStolen;
            server->EnqueueMessage(thief->GetConnHandle(), &desc);
            stolenMessage.thief = skaterId;
            stolenMessage.victim = victim;
            stolenMessage.gameId = net->GetGameId();
            if (robbed)
                server->EnqueueMessage(robbed->GetConnHandle(), &desc);
        }

        Obj::TrickObjectsMessage all;
        all.one = 1;
        all.player = skaterId;
        all.score = static_cast<uint32_t>(score);
        all.count = count;
        all.gameId = net->GetGameId();
        std::memcpy(all.objects, objects, count * 4);
        Net::MsgDesc desc;
        desc.Construct();
        desc.m_length = static_cast<uint16_t>(count * 4 + offsetof(Obj::TrickObjectsMessage, objects));
        desc.m_data = &all;
        desc.m_id = Obj::kMsgTrickObjects;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = Obj::kGroupGame;
        for (void* info = net->FirstPlayerInfo(&search, false); info; info = net->NextPlayerInfo(&search, false))
            server->StreamMessage(AsPlayer(info)->GetConnHandle(), desc.m_id, desc.m_length, desc.m_data,
                                  kTrickObjBuffer, Obj::kGroupGame, true, false);

        Obj::ObserverTrickObjectsMessage observed;
        observed.count = count;
        observed.player = skaterId;
        observed.gameId = net->GetGameId();
        std::memcpy(observed.objects, objects, count * 4);
        desc.m_data = &observed;
        desc.m_length = static_cast<uint16_t>(count * 4 + offsetof(Obj::ObserverTrickObjectsMessage, objects));
        desc.m_id = Obj::kMsgObserverTrickObjects;
        for (void* info = net->FirstPlayerInfo(&search, true); info; info = net->NextPlayerInfo(&search, true)) {
            if (static_cast<uint8_t>(AsPlayer(info)->IsObserver()))
                server->StreamMessage(AsPlayer(info)->GetConnHandle(), desc.m_id, desc.m_length, desc.m_data,
                                      kTrickObjBuffer, Obj::kGroupGame, true, false);
        }
        GameFunction<void(__thiscall*)(void*, int)>(0x0056BB70)(skate, 0); // the skate module's score updates
        if (conn->IsLocal()) // (no check: as the original)
            return;
    }
    int owner = static_cast<uint8_t>(GameMode()->IsTeamGame())
                    ? AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(skaterId)))->m_team
                    : skaterId;
    for (uint32_t i = 0; i < count; ++i)
        TrickObjects()->SetObjectGraffitiColor(objects[i], owner);
}

// 0x005626F0
int __cdecl Score::HandleScoreMessage(Net::MsgHandlerContext* context)
{
    auto* message = reinterpret_cast<Obj::TrickObjectsMessage*>(context->m_Msg);
    auto* score = static_cast<Score*>(static_cast<Obj::CSkater*>(context->m_data)->GetScore());
    if (static_cast<int8_t>(message->one) == 1 && message->gameId == NetManager()->GetGameId())
        score->LogTrickObject(message->player, static_cast<int>(message->score), message->count, message->objects, false);
    return 1;
}

} // namespace Mdl

REPLACE(0x00561BA0, &Mdl::Score::LogTrickObjectRequest);
REPLACE(0x00561CB0, &Mdl::Score::LogTrickObject);
REPLACE(0x005626F0, Mdl::Score::HandleScoreMessage);
