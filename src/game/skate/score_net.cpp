// modules::PointTally's network side: trick objects (graffiti) scored on, logged on the server and sent to the players (see
// point_tally.h).

#include "game/skate/point_tally.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "engine/core/dbg.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/net/skater_messages.h"
#include "engine/util/byte_stream.h"
#include "game/skate/game_mode.h"
#include "game/skate/score_internal.h"
#include "game/rider/trick_component.h"
#include "game/skate/skate_module.h"

namespace modules {

using namespace score_internal;

namespace {

const char* const kTrickObjBuffer = "TrickObj Buffer"; // "TrickObj Buffer"

// A walk over the players (online::Hub::begin_player_walk / step_player_walk).
using PlayerSearch = online::PlayerSearch;

online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

} // namespace

// 0x00561BA0
void PointTally::note_tagged_prop_ask(int score)
{
    online::Hub* net = net_session_slot();
    net::NetMessageInfo desc;
    auto* info = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(rider_no_)));
    auto* client = static_cast<net::Guest*>(net->fetch_patron(info->skater_->view_slot_));
    online::TrickObjectsMessage message;
    message.player = rider_no_;
    message.score = static_cast<uint32_t>(score);
    message.game_id = net->get_game_id();
    auto* trick = static_cast<objects::MoveExecutor*>(rider_for_slot(rider_no_)->fetch_module(kTrick));
    uint32_t bytes = trick->end_trick_objects(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.objects)),
                                              sizeof(message.objects));
    message.count = bytes >> 2;
    debug::printf("Client -> server %d tricks\n", bytes >> 2); // "Client -> server %d tricks\n"
    uint8_t data[online::kTrickObjectsHeaderSize + sizeof(message.objects)];
    util::ByteWriter writer(data, sizeof(data));
    online::write_trick_objects(writer, message);
    // (the descriptor is filled and not used: the message goes by the stream's own arguments)
    desc.content_ = data;
    desc.byte_count_ = static_cast<uint16_t>(writer.position());
    desc.ident_ = online::kMsgTrickObjects;
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = objects::kGroupGame;
    client->send_bulk_to_host(online::kMsgTrickObjects, desc.byte_count_, data, kTrickObjBuffer, objects::kGroupGame,
                              true, false);
}

// 0x00561CB0
void PointTally::note_tagged_prop(int skater_id, int score, uint32_t count, uint32_t* objects, bool send)
{
    online::Hub* net = net_session_slot();
    void* skate = skate_calls::get_skate();
    PlayerSearch search;
    if (send) {
        net::AppHost* server = net->host_; // (0x005480C0)
        net::Conn* conn = nullptr;
        if (objects::Rider* skater = rider_for_slot(rider_no_)) {
            if (auto* info = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_))))
                conn = info->conn_;
        }
        uint8_t stolen[8] = {};
        count = get_trick_objects()->log_trick_objects(count, objects, stolen, skater_id, static_cast<uint32_t>(score));
        if (!count)
            return;
        for (int victim = 0; victim < 8; ++victim) {
            if (!stolen[victim])
                continue;
            online::TagStolenMessage stolen_message;
            stolen_message.thief_id = static_cast<uint32_t>(skater_id);
            stolen_message.victim_id = static_cast<uint32_t>(victim);
            stolen_message.game_id = net->get_game_id();
            uint8_t stolen_data[online::kTagStolenSize];
            util::ByteWriter stolen_writer(stolen_data, sizeof(stolen_data));
            online::write_tag_stolen(stolen_writer, stolen_message);
            auto* thief = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(skater_id)));
            auto* robbed = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(victim)));
            // (not between players of one team)
            if (static_cast<uint8_t>(get_game_mode()->is_squad_match()) && robbed && thief &&
                robbed->team_ == thief->team_)
                continue;
            net::NetMessageInfo desc;
            desc.content_ = stolen_data;
            desc.byte_count_ = sizeof(stolen_data);
            desc.ident_ = online::kMsgTagStolen;
            server->put_on_outbox(thief->get_link_handle(), &desc);
            if (robbed)
                server->put_on_outbox(robbed->get_link_handle(), &desc);
        }

        // (NOT ORIGINAL: no more names than the message holds - the original copied a remote count on past its
        // buffer)
        if (count > online::kMaxTrickObjects)
            count = online::kMaxTrickObjects;
        auto all = std::make_unique<online::TrickObjectsMessage>();
        all->player = skater_id;
        all->score = static_cast<uint32_t>(score);
        all->count = count;
        all->game_id = net->get_game_id();
        std::memcpy(all->objects, objects, count * 4);
        uint8_t data[online::kTrickObjectsHeaderSize + sizeof(all->objects)];
        util::ByteWriter writer(data, sizeof(data));
        online::write_trick_objects(writer, *all);
        net::NetMessageInfo desc;
        desc.byte_count_ = static_cast<uint16_t>(writer.position());
        desc.content_ = data;
        desc.ident_ = online::kMsgTrickObjects;
        desc.queue_ = net::NetMessageInfo::kLaneOrdered;
        desc.group_id_ = objects::kGroupGame;
        for (void* info = net->begin_player_walk(&search, false); info; info = net->step_player_walk(&search, false))
            server->send_bulk_message(as_player(info)->get_link_handle(), desc.ident_, desc.byte_count_, data,
                                      kTrickObjBuffer, objects::kGroupGame, true, false);

        util::ByteWriter observed_writer(data, sizeof(data));
        online::write_observed_trick_objects(observed_writer, *all);
        desc.content_ = data;
        desc.byte_count_ = static_cast<uint16_t>(observed_writer.position());
        desc.ident_ = online::kMsgObservedTrickObjects;
        for (void* info = net->begin_player_walk(&search, true); info; info = net->step_player_walk(&search, true)) {
            if (static_cast<uint8_t>(as_player(info)->is_observer()))
                server->send_bulk_message(as_player(info)->get_link_handle(), desc.ident_, desc.byte_count_,
                                          data, kTrickObjBuffer, objects::kGroupGame, true, false);
        }
        static_cast<modules::Skate*>(skate)->broadcast_points(false); // the skate module's score updates
        if (conn->is_host_local())                                    // (no check: as the original)
            return;
    }
    int owner = static_cast<uint8_t>(get_game_mode()->is_squad_match())
                    ? as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(skater_id)))->team_
                    : skater_id;
    for (uint32_t i = 0; i < count; ++i)
        get_trick_objects()->set_object_graffiti_color(objects[i], owner);
}

// 0x005626F0
int PointTally::on_score_msg(net::InboundMsgCtx* context)
{
    // (NOT ORIGINAL: one whose count is more than it holds is dropped - the original read the names on past its end)
    util::ByteReader reader(context->body_, context->body_size_);
    auto message = std::make_unique<online::TrickObjectsMessage>();
    if (!online::read_trick_objects(reader, message.get()))
        return 4;
    auto* score = static_cast<PointTally*>(static_cast<objects::Rider*>(context->content_)->get_points_total());
    if (message->one == 1 && message->game_id == net_session_slot()->get_game_id())
        score->note_tagged_prop(message->player, static_cast<int>(message->score), message->count, message->objects,
                                false);
    return 1;
}

} // namespace modules
