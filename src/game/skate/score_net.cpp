// modules::PointTally's network side: trick objects (graffiti) scored on, logged on the server and sent to the players (see
// point_tally.h).

#include "game/skate/point_tally.h"

#include <cstdint>
#include <memory>
#include <algorithm>
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_game_state.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/objects/trick_objects.h"
#include "game/skate/skate_module_calls.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "game/skate/game_mode.h"
#include "game/skate/score_internal.h"
#include "game/rider/trick_component.h"
#include "game/skate/skate_module.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include "wire/message_ids.h"

namespace modules {

using namespace score_internal;

namespace {
constexpr int kVictimSlotCount = 8; // one per skater of a game

const char* const kTrickObjBuffer = "TrickObj Buffer"; // "TrickObj Buffer"

// A walk over the players (online::Hub::begin_player_walk / step_player_walk).
using PlayerSearch = online::PlayerSearch;

online::PlayerRecord* as_player(void* player) { return static_cast<online::PlayerRecord*>(player); }

} // namespace

// 0x00561BA0
void PointTally::note_tagged_prop_ask(int score) const
{
    online::Hub* net = online::online_hub();
    net::NetMessageInfo desc;
    auto* info = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(rider_no_)));
    auto* client = core::down_cast<net::Guest>(net->fetch_patron(info->rider()->view_slot()));
    online::TrickObjectsMessage message;
    message.player = rider_no_;
    message.score = static_cast<uint32_t>(score);
    message.game_id = net->get_game_id();
    auto* trick = core::down_cast<objects::MoveExecutor>(rider_for_slot(rider_no_)->fetch_module(kTrick));
    const uint32_t bytes = trick->end_trick_objects(message.objects,
                                                    sizeof(message.objects));
    message.count = bytes >> 2;
    debug::printf("Client -> server %d tricks\n", bytes >> 2); // "Client -> server %d tricks\n"
    std::array<uint8_t, online::kTrickObjectsHeaderSize + sizeof(message.objects)> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_trick_objects(writer, message);
    // (the descriptor is filled and not used: the message goes by the stream's own arguments)
    desc.set_content(data.data());
    desc.set_byte_count(static_cast<uint16_t>(writer.position()));
    desc.set_ident(online::MessageId::kTrickObjects);
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    client->send_bulk_to_host(online::MessageId::kTrickObjects, desc.byte_count(), data.data(), kTrickObjBuffer, online::MessageGroup::kGame,
                              true, false);
}

// 0x00561CB0
void PointTally::note_tagged_prop(int skater_id, int score, std::span<uint32_t> objects, bool send) const
{
    online::Hub* net = online::online_hub();
    void* skate = skate_calls::get_skate();
    PlayerSearch search;
    if (send) {
        net::AppHost* server = net->host(); // (0x005480C0)
        const net::Conn* conn = nullptr;
        if (const objects::Rider* skater = rider_for_slot(rider_no_)) {
            if (auto* info = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident())))) {
                conn = info->conn();
            }
        }
        std::array<uint8_t, kVictimSlotCount> stolen = {};
        uint32_t count = get_trick_objects()->log_trick_objects(objects, stolen, skater_id, static_cast<uint32_t>(score));
        objects = objects.first(count);
        if (count == 0U) {
            return;
        }
        for (int victim = 0; victim < kVictimSlotCount; ++victim) {
            if (core::at(stolen, victim) == 0U) {
                continue;
            }
            online::TagStolenMessage stolen_message;
            stolen_message.thief_id = static_cast<uint32_t>(skater_id);
            stolen_message.victim_id = static_cast<uint32_t>(victim);
            stolen_message.game_id = net->get_game_id();
            std::array<uint8_t, online::kTagStolenSize> stolen_data{};
            util::ByteWriter stolen_writer(stolen_data.data(), sizeof(stolen_data));
            online::write_tag_stolen(stolen_writer, stolen_message);
            auto* thief = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(skater_id)));
            auto* robbed = as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(victim)));
            // (not between players of one team)
            if ((static_cast<uint8_t>(get_game_mode()->is_squad_match()) != 0U) && robbed && thief &&
                robbed->team() == thief->team()) {
                continue;
            }
            net::NetMessageInfo desc;
            desc.set_content(stolen_data.data());
            desc.set_byte_count(sizeof(stolen_data));
            desc.set_ident(online::MessageId::kTagStolen);
            server->put_on_outbox(thief->get_link_handle(), &desc);
            if (robbed) {
                server->put_on_outbox(robbed->get_link_handle(), &desc);
            }
        }

        // (NOT ORIGINAL: no more names than the message holds - the original copied a remote count on past its
        // buffer)
        count = std::min(count, online::kMaxTrickObjects);
        objects = objects.first(count);
        auto all = std::make_unique<online::TrickObjectsMessage>();
        all->player = skater_id;
        all->score = static_cast<uint32_t>(score);
        all->count = count;
        all->game_id = net->get_game_id();
        std::ranges::copy(objects, all->objects.begin());
        std::array<uint8_t, online::kTrickObjectsHeaderSize + sizeof(all->objects)> data{};
        util::ByteWriter writer(data.data(), sizeof(data));
        online::write_trick_objects(writer, *all);
        net::NetMessageInfo desc;
        desc.set_byte_count(static_cast<uint16_t>(writer.position()));
        desc.set_content(data.data());
        desc.set_ident(online::MessageId::kTrickObjects);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        for (void* info = net->begin_player_walk(&search, false); info; info = net->step_player_walk(&search, false)) {
            server->send_bulk_message(as_player(info)->get_link_handle(), {.msg_id_ = desc.ident(), .size = desc.byte_count()}, data.data(),
                                      kTrickObjBuffer, online::MessageGroup::kGame, true, false);
        }

        util::ByteWriter observed_writer(data.data(), sizeof(data));
        online::write_observed_trick_objects(observed_writer, *all);
        desc.set_content(data.data());
        desc.set_byte_count(static_cast<uint16_t>(observed_writer.position()));
        desc.set_ident(online::MessageId::kObservedTrickObjects);
        for (void* info = net->begin_player_walk(&search, true); info; info = net->step_player_walk(&search, true)) {
            if (static_cast<uint8_t>(as_player(info)->is_observer()) != 0U) {
                server->send_bulk_message(as_player(info)->get_link_handle(), {.msg_id_ = desc.ident(), .size = desc.byte_count()},
                                          data.data(), kTrickObjBuffer, online::MessageGroup::kGame, true, false);
            }
        }
        static_cast<modules::Skate*>(skate)->broadcast_points(false); // the skate module's score updates
        if (conn->is_host_local()) {                                  // (no check: as the original)
            return;
        }
    }
    const int owner = (static_cast<uint8_t>(get_game_mode()->is_squad_match()) != 0U)
                          ? as_player(net->find_player_slot_by_obj(static_cast<uint16_t>(skater_id)))->team()
                          : skater_id;
    for (const uint32_t name : objects) {
        get_trick_objects()->set_object_graffiti_color(checksums::Name{name}, owner);
    }
}

// 0x005626F0
int PointTally::on_score_msg(net::InboundMsgCtx* context)
{
    // (NOT ORIGINAL: one whose count is more than it holds is dropped - the original read the names on past its end)
    util::ByteReader reader(context->body(), context->body_size());
    auto message = std::make_unique<online::TrickObjectsMessage>();
    if (!online::read_trick_objects(reader, message.get())) {
        return 4;
    }
    auto* score = static_cast<objects::Rider*>(context->content())->get_points_total();
    if (message->one == 1 && message->game_id == online::online_hub()->get_game_id()) {
        score->note_tagged_prop(message->player, static_cast<int>(message->score), std::span<uint32_t>(message->objects.data(), message->count), false);
    }
    return 1;
}

} // namespace modules
