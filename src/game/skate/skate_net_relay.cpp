// The skate module's network side, the server's part and the rest of the client's (0x00569560-0x0056C91F): the two
// main-loop tasks (the other skaters' updates to each client, the scores every two seconds), the handlers that relay
// a client's messages to the others (as they are, to the skaters near it, sequenced), the scavenger hunt's counts,
// the graffiti / face textures passed on, the client's state-history messages, projectiles and cheats; and the skate
// module's LeaveServer / send_cheats / broadcast_points. From the disassembly; see skate_net.h.

#include "game/skate/skate_net.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <array>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_texture.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"
#include "engine/net/player_info.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/rider/state_history.h"
#include "game/rider/tagging.h"
#include "engine/util/bitstream.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model.h"
#include "engine/gfx/anim_blend.h"
#include "engine/objects/compositemgr.h"
#include "game/rider/skater_score.h"
#include "engine/objects/model_component.h"
#include "formats/byte_stream.h"
#include "wire/skate_net_messages.h"
#include "wire/skater_messages.h"
#include "wire/play_messages.h"
#include "engine/gfx/tod_manager.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace modules {

namespace {
uint8_t& net_relay_object_id()
{
    static uint8_t net_relay_object_id{};
    return net_relay_object_id;
}
constexpr float kRelayTimeScale{0.000244140625f}; // (2^-12: the relayed times and positions)

using online::PlayerRecord;

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;               // NetLobby (a game mode)
constexpr uint32_t kFirefight = 0x3D6D444F;              // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;           // netfirefight
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;          // netgoalattack
constexpr uint32_t kShouldModulateColor = 0x11941568;    // should_modulate_color
constexpr uint32_t kSkaterScore = 0xD262802F;            // SkaterScore (the component)
constexpr uint32_t kTagging = 0x5B512EDC;                // Tagging (the component)
constexpr uint32_t kModel = 0x286A8D26;                  // model (the component)
constexpr uint32_t kAnimBlender = 0x7149B753;            // animblender (the component)
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F; // scavenger_collect_period (a goal)
constexpr uint32_t kCreateServerQuitDialog = 0x1D3A6D04; // CreateServerQuitDialog
constexpr uint32_t kPatchCleanup = 0xFF5C39B6;           // PatchCleanup
constexpr uint32_t kHatLogo = 0x5B288E0B;                // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;              // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;               // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;            // deck_graphic
constexpr uint32_t kAll = 0xC4E78E22;                    // ALL
constexpr uint32_t kOn = 0xF649D637;                     // on
constexpr uint32_t kCheatFlag = 0xAE94C183;              // cheat_flag
constexpr uint32_t kClientToggleCheat = 0x3CF9E86D;      // client_toggle_cheat
constexpr uint32_t kOwnerId = 0x81C39E06;                // owner_id
constexpr uint32_t kAmmoType = 0x1D1A8B59;               // ammo_type
constexpr uint32_t kX = 0x7323E97C;                      // x
constexpr uint32_t kY = 0x0424D9EA;                      // y
constexpr uint32_t kZ = 0x9D2D8850;                      // z
constexpr uint32_t kScaledX = 0xBDED7A76;                // scaled_x
constexpr uint32_t kScaledY = 0xCAEA4AE0;                // scaled_y
constexpr uint32_t kScaledZ = 0x53E31B5A;                // scaled_z
constexpr uint32_t kRadius = 0xC48391A5;                 // radius
constexpr uint32_t kScale = 0x13B9DA7B;                  // scale
constexpr uint32_t kPos = 0x7F261953;                    // Pos
constexpr uint32_t kVel = 0x0C4C809E;                    // vel
constexpr uint32_t kFireball = 0xD039432C;               // fireball
constexpr uint32_t kClientLaunchFireball = 0xAA2B5552;   // ClientLaunchFireball
constexpr uint32_t kClientLaunchProjectile = 0x866C26A9; // ClientLaunchProjectile

// The cheats send_cheats tells a player about, in order (CHEAT_ALWAYS_SPECIAL twice, as the original).
constexpr auto kSentCheats = std::to_array<uint32_t>({
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
});

online::Hub* net_link() { return online::online_hub(); }
goals::MatchRules* get_game_mode() { return goals::g_game_mode.get(); }
objects::ObjectHubBase* entity_hub_handle()
{
    return static_cast<objects::ObjectHubBase*>(objects::g_composite_registry.get());
}

PlayerRecord* by_connection(net::Conn* conn) { return net_link()->find_player_by_sender(conn); }
PlayerRecord* by_object_id(int id) { return net_link()->find_player_slot_by_obj(static_cast<uint16_t>(id)); }

// A walk over the network manager's players (begin_player_walk / step_player_walk).
struct PlayerSearch : online::PlayerSearch {
    PlayerRecord* get_first(bool observers)
    {
        return static_cast<PlayerRecord*>(net_link()->begin_player_walk(this, observers));
    }
    PlayerRecord* get_next(bool observers)
    {
        return static_cast<PlayerRecord*>(net_link()->step_player_walk(this, observers));
    }
};

// A message on the server app to `player`, sequenced in `group`.
void get_sequence(net::NetMessageInfo& desc, uint8_t group)
{
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = group;
}
void enqueue(net::AppHost* app, PlayerRecord* player, net::NetMessageInfo* desc)
{
    const int handle = player->get_link_handle();
    app->put_on_outbox(handle, desc);
}
net::AppHost* server_app() { return net_link()->host_; }

objects::Rider* skater_of(PlayerRecord* player) { return player->rider(); }
uint8_t get_skater_id(objects::Rider* skater) { return static_cast<uint8_t>(skater->ident_); }
PointTally* get_score_of(objects::Rider* skater) { return skater->get_points_total(); }

// A skater's state history (+0x27C) and its newest state / message.
objects::RiderFrameLog* get_history(const objects::Entity* object)
{
    return core::down_cast<const objects::Rider>(object)->state_history_;
}
int& message_count(objects::RiderFrameLog* history) { return history->message_count_; }
// The newest message slot made a message of `id`, stamped with the newest state's time.
objects::StateHistoryMessage* new_message(objects::RiderFrameLog* history, uint8_t id)
{
    objects::StateHistoryMessage* message = history->get_latest_message();
    message->ident_ = id;
    auto* state = history->fetch_newest_snapshot();
    message->change_moment(state->get_time());
    return message;
}
float get_scaled(uint32_t value, float constant) { return static_cast<float>(static_cast<double>(value) * constant); }

// The relay message: the received one as it is.
void relay(net::NetMessageInfo& desc, const net::InboundMsgCtx* context)
{
    desc.byte_count_ = static_cast<uint16_t>(context->body_size_);
    desc.content_ = context->body_;
    desc.ident_ = context->type_code_;
}

} // namespace

// --- the server's tasks ---------------------------------------------------------------------------------------------

// 0x00569BB0
void SkateNet::server_task(tasks::Task<Skate>* /*task*/)
{
    PlayerSearch search;
    net::AppHost* server = server_app();
    std::array<uint8_t, 0x1000> buffer{};
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        RelayStream out;
        if (!player->conn_->is_off_machine())
            continue;
        out.feed_.bind_output_buffer(buffer.data(), sizeof(buffer));
        out.unknown18_ = static_cast<uint32_t>(player->conn_->hdl_);
        out.sent_ = player;
        uint32_t mask = 0;
        int count = 0;
        // (round the skaters from the one after the last sent, as many as the connection takes in one message)
        const int start = (player->get_last_sent_skater() + 1) % 8;
        for (int i = 0; i < 8; ++i) {
            const int id = (i + start) % 8;
            PlayerRecord* other = by_object_id(id);
            if (!other)
                continue;
            other = by_object_id(static_cast<int>(skater_of(other)->ident_));
            if (!other || other == player)
                continue;
            const objects::Rider* skater = skater_of(other);
            auto* history = get_history(skater);
            const int states = history->state_count_;
            if (!states)
                continue;
            const uint16_t newest = core::at(history->states_, (states - 1) % objects::RiderFrameLog::kEntries).time;
            if (newest == core::at(player->sent_.time_, skater->ident_))
                continue;
            mask |= 1u << id;
            if (++count >= player->get_skaters_per_update())
                break;
        }
        if (!count)
            continue;
        out.feed_.write_bits(mask, 8);
        for (int i = 0; i < 8; ++i) {
            if (!(mask & (1u << i)))
                continue;
            write_object_update(static_cast<objects::Entity*>(skater_of(by_object_id(i))), &out);
            player->set_last_sent_skater(i);
        }
        out.feed_.drain();
        const int size = out.feed_.get_byte_count();
        if (size) {
            net::NetMessageInfo desc;
            desc.content_ = buffer.data();
            desc.byte_count_ = static_cast<uint16_t>(size);
            desc.ident_ = 0x23;
            enqueue(server, player, &desc);
        }
    }
}

// 0x0056BE10
void SkateNet::score_task(tasks::Task<Skate>* task)
{
    Skate* skate = task->content_;
    online::Hub* net = net_link();
    if (!net->keeps_score())
        return;
    const net::AppHost* server = server_app();
    if (server->clock_ - net->fetch_score_sync_stamp() >= 2000)
        skate->broadcast_points(false);
}

// --- the server's handlers ------------------------------------------------------------------------------------------

// 0x00569E20
int SkateNet::handle_trick_objects(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player)
        return 4;
    PointTally* score = get_score_of(skater_of(player));
    // (the message: wire/skater_messages.h; NOT ORIGINAL: one whose count is more than it holds is dropped - the
    // original read the names on past its end and passed the count on to be copied into a fixed buffer)
    util::ByteReader reader(context->body_, context->body_size_);
    auto message = std::make_unique<online::TrickObjectsMessage>();
    if (!online::read_trick_objects(reader, message.get()))
        return 4;
    if (message->one == 1 && message->game_id == net->get_game_id())
        score->note_tagged_prop(message->player, static_cast<int>(message->score), std::span<uint32_t>(message->objects.data(), message->count), true);
    return 1;
}

// 0x00569E90
int SkateNet::handle_game_stats(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player)
        return 4;
    util::ByteReader reader(context->body_, context->body_size_);
    online::PointsMessage message;
    if (!online::read_points(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    if (get_game_mode()->get_name_key() == kNetLobby || net->get_game_id() != message.game_id)
        return 4;
    player->best_combo_ = message.points;
    return 1;
}

// 0x00569EF0
int SkateNet::handle_combo_score(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player || !skater_of(player))
        return 4;
    util::ByteReader reader(context->body_, context->body_size_);
    online::PointsMessage message;
    if (!online::read_points(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    if (net->get_game_id() != message.game_id)
        return 4;
    const int combo = message.points;
    PointTally* score = get_score_of(skater_of(player));
    if (combo > score->top_chain_)
        score->top_chain_ = combo;
    goals::MatchRules* mode = get_game_mode();
    if (mode->is_true(kShouldModulateColor) || !mode->keeps_move_points())
        return 1;
    if (mode->keeps_running_total()) {
        score->set_grand_total(score->get_total_score() + combo);
        return 1;
    }
    if (mode->keeps_best_chain() && score->get_total_score() < combo)
        score->set_grand_total(combo);
    return 1;
}

// 0x00569FD0
int SkateNet::handle_player_value48(net::InboundMsgCtx* context)
{
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (!player)
        return 4;
    util::ByteReader reader(context->body_, context->body_size_);
    online::BoardVehicle message;
    if (!online::read_board_vehicle(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    player->drive_control_mode_ = static_cast<int32_t>(message.control_type);
    return 1;
}

// 0x0056A000
int SkateNet::handle_relay(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* sender = by_connection(context->sender_conn_);
    if (!sender)
        return 4;
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        net::NetMessageInfo desc;
        if (player == sender)
            continue;
        relay(desc, context);
        switch (context->type_code_) {
        case 0x3A:
        case 0x4C:
        case 0x89:
            get_sequence(desc, 8);
            break;
        case 0x99:
            get_sequence(desc, 10);
            break;
        case 0x83: {
            // a projectile in firefight: its owner and the sender's latency put in (once in range)
            const uint32_t mode = get_game_mode()->get_name_key();
            if (mode != kFirefight && get_game_mode()->get_name_key() != kNetFirefight)
                break;
            // (the message: wire/play_messages.h; NOT ORIGINAL: one too short is passed on as it is - the
            // original read and wrote past its end)
            util::ByteReader reader(context->body_, context->body_size_);
            online::Projectile projectile;
            if (!online::read_projectile(reader, &projectile))
                break;
            if (!(projectile.scale <= 10.0f) || projectile.radius > 0xF0)
                break;
            util::ByteWriter rewrite(context->body_, context->body_size_);
            rewrite.seek(online::kProjectileOwnerAt);
            rewrite.u8(get_skater_id(skater_of(sender)));
            rewrite.seek(online::kProjectileTimeAt);
            rewrite.i32(context->sender_conn_->get_mean_ping());
            break;
        }
        default:
            break;
        }
        enqueue(context->receiver_app_, player, &desc);
    }
    return 1;
}

// 0x0056A170
int SkateNet::handle_relay_nearby(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* sender = by_connection(context->sender_conn_);
    if (!sender)
        return 4;
    const objects::Rider* from = skater_of(sender);
    if (!from)
        return 4;
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        if (player == sender)
            continue;
        if (!player->is_observer()) {
            const objects::Rider* to = skater_of(player);
            if (!to)
                continue;
            // (x87: the differences and the sum unrounded)
            const double dx = static_cast<double>(to->at_point_.x) - from->at_point_.x;
            const double dy = static_cast<double>(to->at_point_.y) - from->at_point_.y;
            const double dz = static_cast<double>(to->at_point_.z) - from->at_point_.z;
            const double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
            if (!(distance < 1800.0))
                continue;
        }
        net::NetMessageInfo desc;
        relay(desc, context);
        enqueue(context->receiver_app_, player, &desc);
    }
    return 1;
}

// 0x0056A2D0
int SkateNet::handle_relay_sequenced(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerSearch search;
    PlayerRecord* sender = by_connection(context->sender_conn_);
    if (!sender)
        return 4;
    for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
        net::NetMessageInfo desc;
        if (player == sender)
            continue;
        relay(desc, context);
        get_sequence(desc, 8);
        enqueue(context->receiver_app_, player, &desc);
    }
    // a script run on every machine (0x49): the server queues its own (NOT ORIGINAL: not one too short - the original
    // read past its end)
    if (context->type_code_ == online::kMsgSpawnScript) {
        util::ByteReader reader(context->body_, context->body_size_);
        online::SpawnScript script;
        if (online::read_spawn_script(reader, &script) && script.permanent)
            net->queue_script(script.node, script.object_id, script.name);
    }
    return 1;
}

// 0x0056A3E0
int SkateNet::handle_joined(net::InboundMsgCtx* context)
{
    PlayerRecord* player = by_connection(context->sender_conn_);
    if (player)
        player->clear_just_joined();
    return 1;
}

// 0x0056AD10
int SkateNet::handle_scavenger_collected(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    PlayerSearch search;
    auto* msg = context->body_;
    // NOT ORIGINAL: one too short for the item's id (passed on below), or from no player (read through null in the
    // original), is dropped
    if (context->body_size_ < 4)
        return 4;
    PlayerRecord* collector = by_connection(context->sender_conn_);
    if (!collector)
        return 4;
    if (skater_of(collector)) {
        auto* component = skater_of(collector)->fetch_module(kSkaterScore);
        PointTally* score = core::down_cast<objects::RiderPointKeeper>(component)->points_held_;
        if (score)
            score->set_grand_total(score->get_total_score() + 1);
        int total = 0;
        for (PlayerRecord* player = search.get_first(false); player; player = search.get_next(false))
            total += player->num_placed_;
        goals::MatchRules* mode = get_game_mode();
        const int collected =
            mode->is_squad_match() ? net->get_squad_points(collector->team_) : score->get_total_score();
        net::NetMessageInfo desc;
        std::array<uint8_t, 2> left{};
        if (collected == total) {
            // all collected: the collect period over everywhere
            desc = net::NetMessageInfo();
            net::AppHost* server = server_app();
            desc.ident_ = 0x94;
            get_sequence(desc, 8);
            for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
                if (!player->is_own_player())
                    enqueue(server, player, &desc);
            }
            goals::Objective* goal = goals::get_objective_board()->get_goal(kScavengerCollectPeriod, 1);
            if (goal->is_operative())
                goal->expire();
        } else {
            // five or one left: told to everyone (0x95: the player or the team, how many)
            uint8_t count = 0;
            if (collected == total - 5 && !commands::cfunc_in_split_screen_game(nullptr, nullptr))
                count = 5;
            else if (score->get_total_score() == total - 1 && !commands::cfunc_in_split_screen_game(nullptr, nullptr))
                count = 1;
            if (count) {
                desc = net::NetMessageInfo();
                net::AppHost* server = server_app();
                core::at(left, 0) = get_game_mode()->is_squad_match() ? static_cast<uint8_t>(collector->team_)
                                                            : get_skater_id(skater_of(collector));
                core::at(left, 1) = count;
                desc.content_ = left.data();
                desc.ident_ = 0x95;
                get_sequence(desc, 8);
                desc.byte_count_ = 2;
                for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true))
                    enqueue(server, player, &desc);
            }
        }
    }
    // the object picked up: told to the collector's team mates
    if (get_game_mode()->is_squad_match()) {
        net::NetMessageInfo desc;
        net::AppHost* server = server_app();
        for (PlayerRecord* player = search.get_first(false); player; player = search.get_next(false)) {
            if (player == collector || player->team_ != collector->team_)
                continue;
            desc.content_ = msg;
            desc.ident_ = 0x90;
            get_sequence(desc, 8);
            desc.byte_count_ = 4;
            enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B0B0
int SkateNet::handle_scavenger_dropped(net::InboundMsgCtx* context)
{
    PlayerSearch search;
    PlayerRecord* dropper = by_connection(context->sender_conn_);
    if (!dropper)
        return 4;
    if (dropper->num_placed_ > 5 || (dropper->num_placed_ == 5 && !dropper->is_own_player()))
        return 4;
    util::ByteReader reader(context->body_, context->body_size_);
    std::array<float, 4> position{};
    if (!online::read_hunt_position(reader, position)) // NOT ORIGINAL: too short - dropped
        return 4;
    if (!dropper->is_own_player())
        core::at(dropper->placed_, dropper->num_placed_) = {.x = core::at(position, 0), .y = core::at(position, 1), .z = core::at(position, 2), .w = core::at(position, 3)};
    if (!dropper->is_own_player())
        ++dropper->num_placed_;
    int dropped = 0;
    int players = 0;
    for (PlayerRecord* player = search.get_first(false); player; player = search.get_next(false)) {
        dropped += player->num_placed_;
        ++players;
    }
    if (dropped == players * 5) {
        // every player's five dropped: the hunt goes on (0x92 to everyone)
        net::NetMessageInfo desc;
        net::AppHost* server = server_app();
        for (PlayerRecord* player = search.get_first(true); player; player = search.get_next(true)) {
            desc.ident_ = 0x92;
            get_sequence(desc, 8);
            enqueue(server, player, &desc);
        }
    }
    return 1;
}

// 0x0056B2B0
int SkateNet::handle_graffiti_texture(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    auto* msg = context->body_;
    util::ByteReader reader(context->body_, context->body_size_);
    // (a client's server: the sender; a client: the player the last 0x96 named)
    auto from = [&]() -> PlayerRecord* {
        if (net->get_on_server())
            return by_connection(context->sender_conn_);
        return by_object_id(net_relay_object_id());
    };
    // the texture's virtual slot 5 (through 0x00456B30): its texture and palette - on PC, as in the engine's
    // table, the shared "return false" stub: nothing is loaded
    auto load = [](void* /*texture*/, const void* /*texture_data*/, uint32_t /*texture_size*/, const void* /*palette*/,
                   uint32_t /*palette_size*/) {};
    switch (context->type_code_) {
    case 0x96: { // the player the next ones are for
        const uint8_t id = reader.u8();
        if (!reader.ok()) // NOT ORIGINAL: empty - dropped
            return 4;
        net_relay_object_id() = id;
        break;
    }
    case 0x98: { // the palette
        PlayerRecord* player = from();
        if (player)
            load(player->get_graffiti_texture(), nullptr, 0, msg, context->body_size_);
        break;
    }
    case 0x97: { // the texture
        PlayerRecord* player = from();
        if (!player)
            break;
        load(player->get_graffiti_texture(), msg, context->body_size_, nullptr, 0);
        player->set_custom_tag(true);
        break;
    }
    case 0x9A: { // which board parts show it - the last: passed on to the others by a server
        bool on_server = false;
        PlayerRecord* player = nullptr;
        if (net->get_on_server()) {
            player = by_connection(context->sender_conn_);
            on_server = true;
        } else {
            player = by_object_id(net_relay_object_id());
        }
        uint32_t parts = 0;
        if (player) {
            parts = reader.u32();
            if (!reader.ok()) // NOT ORIGINAL: too short - dropped
                return 4;
            if (parts & 1)
                skater_of(player)->show_board_element(kHatLogo);
            if (parts & 2)
                skater_of(player)->show_board_element(kFrontLogo);
            if (parts & 4)
                skater_of(player)->show_board_element(kBackLogo);
            if (parts & 8)
                skater_of(player)->show_board_element(kDeckGraphic);
        }
        if (!on_server || !player)
            break;
        PlayerSearch search;
        auto* texture = static_cast<render::BitmapTexture*>(player->get_graffiti_texture());
        net::AppHost* app = context->receiver_app_;
        for (PlayerRecord* other = search.get_first(true); other; other = search.get_next(true)) {
            net::NetMessageInfo desc;
            if (other->is_own_player() || other == player || other->conn_->get_link_speed_class() == 1)
                continue;
            uint8_t id = get_skater_id(skater_of(player));
            desc.content_ = &id;
            desc.ident_ = 0x96;
            desc.byte_count_ = 1;
            get_sequence(desc, 10);
            enqueue(app, other, &desc);
            // (the texture's palette and pixels: through the geoms' jumps 0x0048DF20 / 0x0048DF30 and 0x0048DF00 /
            // 0x0049F3A0 - the same stub in the engine's and the PC's tables: none)
            void* palette = texture->get_palette();
            const uint32_t palette_size = texture->get_palette_size();
            app->send_bulk_to_conn(other->conn_, 0x98, static_cast<uint16_t>(palette_size), palette, "CAG Palette", 10,
                                   false, true); // "CAG Palette"
            void* pixels = texture->get_pixels();
            const uint32_t pixels_size = texture->get_pixels_size();
            app->send_bulk_to_conn(other->conn_, 0x97, static_cast<uint16_t>(pixels_size), pixels, "CAG Texture", 10,
                                   false, true); // "CAG Texture"
            uint32_t sent_parts = parts;
            desc.content_ = &sent_parts;
            desc.ident_ = 0x9A;
            desc.byte_count_ = 4;
            get_sequence(desc, 10);
            enqueue(app, other, &desc);
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

// 0x0056B5D0
int SkateNet::handle_face_texture(net::InboundMsgCtx* context)
{
    const online::Hub* net = net_link();
    void* memory = memory::alloc_zeroed(0x449C);
    void* texture = memory ? memory::construct_in<gfx::FaceSkin>(memory) : nullptr;
    auto* data = context->body_;
    uint32_t size = context->body_size_;
    PlayerRecord* player = nullptr;
    if (net->get_on_server()) {
        player = by_connection(context->sender_conn_);
    } else {
        // (a client: the player's object id first; NOT ORIGINAL: an empty one dropped - the original's size went
        // round)
        if (!size)
            return 4;
        const uint8_t id = *data;
        data = std::next(data);
        --size;
        player = by_object_id(id);
    }
    // (quirk kept: the texture is leaked here)
    if (!player)
        return 4;
    objects::Rider* skater = skater_of(player);
    if (!skater)
        return 4;
    static_cast<gfx::FaceSkin*>(texture)->read_from(std::span<const uint8_t>(data, size));
    static_cast<gfx::FaceSkin*>(texture)->mark_usable(true);
    auto* model = skater->fetch_module(kModel);
    core::down_cast<objects::MeshInstanceModule>(model)->mesh_instance_->replace_face_texture(texture,
                                                                                           "CS_NSN_facemap.png", kAll);
    if (net->get_on_server()) {
        // the server keeps the face and streams it to the others
        PlayerSearch search;
        net::AppHost* app = context->receiver_app_;
        PlayerRecord* sender = by_connection(context->sender_conn_);
        sender->set_face_texture_data(context->body_, context->body_size_);
        for (PlayerRecord* other = search.get_first(true); other; other = search.get_next(true)) {
            if (other->is_own_player() || other == sender)
                continue;
            // (NOT ORIGINAL: no more than the kept copy holds - the original streamed 0x4499 bytes of it whatever
            // the face's size)
            const uint32_t kept = context->body_size_ + 1 < 0x4499 ? context->body_size_ + 1 : 0x4499;
            app->send_bulk_to_conn(other->conn_, 0x7E, static_cast<uint16_t>(kept), sender->get_face_texture_data(),
                                   "Face Data", 9, false, true); // "Face Data"
        }
    }
    // (freed without its destructor, as the original)
    memory::Hub::sole()->dealloc(texture);
    return 1;
}

// --- the client's handlers ------------------------------------------------------------------------------------------

// 0x00569560
int SkateNet::on_level_switch_msg(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    const uint32_t level = reader.u32();
    if (!reader.ok()) // NOT ORIGINAL: too short - dropped
        return 4;
    static_cast<Skate*>(context->content_)->change_level(level);
    return 1;
}

// 0x0056A410
int SkateNet::handle_history_value(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    online::BoardVehicle message;
    if (!online::read_board_vehicle(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    objects::Rider* skater =
        static_cast<Skate*>(context->content_)->get_rider_by_number(static_cast<uint32_t>(message.skater_id));
    if (!skater)
        return 4;
    auto* history = core::down_cast<objects::RiderFrameLog>(skater->fetch_module(objects::RiderFrameLog::kType));
    history->vehicle_control_type_ = message.control_type;
    return 1;
}

// 0x0056A450
int SkateNet::handle_skater_messages(net::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->content_);
    objects::ObjectHubBase* objects = entity_hub_handle();
    // the object (by the message's object id, read signed) and, for some, the skater too
    auto object = [&](uint8_t id) { return objects->look_up_entity(static_cast<uint32_t>(static_cast<int8_t>(id))); };
    auto skater = [&](uint8_t id) -> objects::Entity* {
        if (!object(id))
            return nullptr;
        return static_cast<objects::Entity*>(skate->get_rider_by_number(static_cast<uint32_t>(static_cast<int8_t>(id))));
    };
    // (the messages: wire/skater_messages.h; NOT ORIGINAL: one too short for its fields is dropped - the
    // original read on past its end)
    util::ByteReader reader(context->body_, context->body_size_);
    const objects::Entity* target = nullptr;
    objects::StateHistoryMessage* message = nullptr;
    int result = 1;
    switch (context->type_code_) {
    case online::kMsgPlayAnim: {
        online::PlayAnimMessage sent;
        if (!online::read_play_anim(reader, &sent))
            return 4;
        target = object(sent.object_id);
        if (!target)
            return 1;
        float scaled_time = get_scaled(sent.blend_period, kRelayTimeScale);
        online::Hub* net = net_link();
        if (net->in_net_game()) {
            // (only the camera's own player's)
            PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(target->ident_));
            PlayerRecord* camera = net->fetch_viewed_player();
            if (!camera || player != camera)
                scaled_time = 0.0f;
        }
        message = new_message(get_history(target), online::kMsgPlayAnim);
        message->name_ = sent.anim;
        message->from_ = get_scaled(sent.from, kRelayTimeScale);
        message->object_id_ = sent.object_id;
        message->blend_period_ = scaled_time;
        message->loop_type_ = sent.loop_type;
        message->to_ = get_scaled(sent.to, kRelayTimeScale);
        message->speed_ = get_scaled(sent.speed, kRelayTimeScale);
        break;
    }
    case online::kMsgWobbleTarget: {
        online::PairMessage sent; // (the object id, the target x 255)
        if (!online::read_pair(reader, &sent))
            return 4;
        target = object(sent.first);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgWobbleTarget);
        message->object_id_ = sent.first;
        message->wobble_target_ = get_scaled(sent.second, gfx::kOneOver255);
        break;
    }
    case online::kMsgBoardRotated: {
        online::PairMessage sent; // (the object id, rotated)
        if (!online::read_pair(reader, &sent))
            return 4;
        target = object(sent.first);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgBoardRotated);
        message->object_id_ = sent.first;
        message->rotated_ = sent.second != 0;
        break;
    }
    case online::kMsgWobbleDetails: {
        online::PairMessage sent; // (the details' bits, the object id)
        if (!online::read_pair(reader, &sent))
            return 4;
        target = object(sent.second);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgWobbleDetails);
        message->object_id_ = sent.second;
        const uint32_t bits = sent.first;
        // five settings picked by the bits: two bits, then one each
        static const std::array<float, 4> kFirst = {{std::bit_cast<float>(0x3D4CCCCDu), std::bit_cast<float>(0x3DCCCCCDu),
                                        std::bit_cast<float>(0x4121999Au),
                                        0.0f}}; // 0.05, 0.1, 10.1, 0 (0x00645D6C, 0x00645D74, 0x006486B8, 0x0066D520)
        core::at(message->wobble_details_, 0) = core::at(kFirst, bits & 3);
        core::at(message->wobble_details_, 1) = (bits >> 2 & 1)
                                          ? std::bit_cast<float>(0x4121999Au)
                                          : std::bit_cast<float>(0x3D23D70Au); // 10.1 / 0.04 (0x006486B8 / 0x006486B4)
        core::at(message->wobble_details_, 2) = (bits >> 3 & 1)
                                          ? std::bit_cast<float>(0x41A00000u)
                                          : std::bit_cast<float>(0x3B102DE0u); // 20 / 0.0022 (0x006486AC / 0x006486B0)
        core::at(message->wobble_details_, 3) = (bits >> 4 & 1)
                                          ? std::bit_cast<float>(0x41200000u)
                                          : std::bit_cast<float>(0x3ADED289u); // 10 / 0.0017 (0x00646710 / 0x006486A8)
        core::at(message->wobble_details_, 4) = (bits >> 5 & 1)
                                          ? std::bit_cast<float>(0x3F800000u)
                                          : std::bit_cast<float>(0x3FC00000u); // 1 / 1.5 (0x006453A8 / 0x00646744)
        break;
    }
    case online::kMsgLoopType: {
        online::PairMessage sent; // (the object id, the loop type)
        if (!online::read_pair(reader, &sent))
            return 4;
        target = object(sent.first);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgLoopType);
        message->object_id_ = sent.first;
        message->loop_type_ = sent.second;
        break;
    }
    case online::kMsgHideGeom: {
        online::HideGeomMessage sent;
        if (!online::read_hide_geom(reader, &sent))
            return 4;
        target = object(sent.object_id);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgHideGeom);
        message->object_id_ = sent.object_id;
        message->name_ = sent.name;
        message->hidden_ = sent.hide;
        break;
    }
    case online::kMsgAnimSpeed: {
        online::AnimSpeedMessage sent;
        if (!online::read_anim_speed(reader, &sent))
            return 4;
        target = object(sent.object_id);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgAnimSpeed);
        message->object_id_ = sent.object_id;
        message->speed_ = std::bit_cast<float>(sent.speed_bits);
        break;
    }
    case online::kMsgFlip: {
        // (also handled by 0x0056C8C0: 4 lets that run)
        online::PairMessage sent; // (the object id, flipped)
        if (!online::read_pair(reader, &sent))
            return 4;
        target = skater(sent.first);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgFlip);
        message->flipped_ = sent.second != 0;
        message->object_id_ = sent.first;
        result = 4;
        break;
    }
    case online::kMsgSpinModel: {
        online::SpinModelMessage sent;
        if (!online::read_spin_model(reader, &sent))
            return 4;
        target = skater(sent.object_id);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgSpinModel);
        message->object_id_ = sent.object_id;
        message->spin_period_ = sent.period;
        message->spin_power_ = sent.power;
        message->spin_start_ = static_cast<float>(sent.start);
        message->spin_range_ = static_cast<float>(sent.range);
        message->spin_hold_ = sent.hold != 0;
        message->spin_axes_ = sent.axes;
        break;
    }
    case online::kMsgCancelSpins: {
        const uint8_t id = reader.u8();
        if (!reader.ok())
            return 4;
        target = skater(id);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgCancelSpins);
        message->object_id_ = id;
        break;
    }
    case online::kMsgCreateSpecialItem: {
        online::SpecialItemMessage sent;
        if (!online::read_special_item(reader, &sent))
            return 4;
        target = skater(sent.object_id);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgCreateSpecialItem);
        message->object_id_ = sent.object_id;
        message->bone_ = sent.bone;
        message->item_slot_ = int{sent.slot};
        message->name_ = sent.params;
        break;
    }
    case online::kMsgDestroySpecialItem: {
        online::SpecialItemMessage sent;
        if (!online::read_special_item(reader, &sent))
            return 4;
        target = skater(sent.object_id);
        if (!target)
            return 1;
        message = new_message(get_history(target), online::kMsgDestroySpecialItem);
        message->object_id_ = sent.object_id;
        message->item_slot_ = int{sent.slot};
        break;
    }
    default:
        return 1;
    }
    ++message_count(get_history(target));
    return result;
}

// 0x0056ABD0
int SkateNet::handle_player_left(net::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->content_);
    online::Hub* net = net_link();
    // (the message: wire/play_messages.h - the object id, the reason)
    util::ByteReader reader(context->body_, context->body_size_);
    const int id = int{reader.i8()};
    const int reason = int{reader.i8()};
    if (!reader.ok()) // NOT ORIGINAL: too short - dropped
        return 4;
    PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(id));
    if (player) {
        objects::Rider* skater = player->rider();
        net->evict_player(player, reason);
        skate->drop_skater_from_roster(skater);
    } else {
        // (one still joining)
        online::JoinerRecord* joining = net->fetch_incoming_player_by_obj(static_cast<uint16_t>(id));
        if (joining)
            net->remove_new_player(joining);
    }
    net->send_ready_response();
    return 1;
}

// 0x0056AC50
int SkateNet::handle_server_quit(net::InboundMsgCtx* /*context*/)
{
    online::Hub* net = net_link();
    script::launch_script(kCreateServerQuitDialog, nullptr, nullptr, false, nullptr);
    net->remove_players();
    net->disconnect_clients();
    return 3;
}

// 0x0056AC90
int SkateNet::handle_graffiti_objects(net::InboundMsgCtx* context)
{
    // (the message: wire/skater_messages.h; NOT ORIGINAL: one whose count is more than it holds is dropped - the
    // original read the names on past its end)
    util::ByteReader reader(context->body_, context->body_size_);
    auto message = std::make_unique<online::TrickObjectsMessage>();
    if (!online::read_observed_trick_objects(reader, message.get()))
        return 4;
    online::Hub* net = net_link();
    if (message->game_id != net->get_game_id())
        return 4;
    int owner = 0;
    if (get_game_mode()->is_squad_match()) {
        PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(message->player));
        if (!player) // NOT ORIGINAL: no such player - dropped (the original read through null)
            return 4;
        owner = player->team_;
    } else {
        owner = message->player;
    }
    for (uint32_t i = 0; i < message->count; ++i)
        objects::g_trick_spot_hub->set_object_graffiti_color(core::at(message->objects, i), owner);
    return 1;
}

// 0x0056B220
int SkateNet::handle_place_tag(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    online::PlaceTag message;
    if (!online::read_place_tag(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    PlayerRecord* player = by_object_id(message.object_id);
    if (!player)
        return 4;
    auto* tagging = core::down_cast<objects::TaggingComponent>(skater_of(player)->fetch_module(kTagging));
    if (!tagging)
        return 4;
    const maths::Vec4 start = {.x = core::at(message.start, 0), .y = core::at(message.start, 1), .z = core::at(message.start, 2), .w = core::at(message.start, 3)};
    const maths::Vec4 end = {.x = core::at(message.end, 0), .y = core::at(message.end, 1), .z = core::at(message.end, 2), .w = core::at(message.end, 3)};
    tagging->place_tag(message.sticker, start, end, message.size, false);
    return 1;
}

// 0x0056B7A0
int SkateNet::on_sparks_msg(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    const int id = int{reader.i8()};
    if (!reader.ok()) // NOT ORIGINAL: empty - dropped
        return 4;
    auto* object = core::down_cast<objects::Rider>(entity_hub_handle()->look_up_entity(static_cast<uint32_t>(id)));
    if (!object)
        return 1;
    if (context->type_code_ == 0x37)
        object->sparks_on();
    else if (context->type_code_ == 0x38)
        object->sparks_off();
    return 1;
}

// 0x0056B7E0
int SkateNet::handle_look_up_object(net::InboundMsgCtx* context)
{
    // (the object looked up, nothing done with it)
    util::ByteReader reader(context->body_, context->body_size_);
    const int id = int{reader.i8()};
    if (!reader.ok()) // NOT ORIGINAL: empty - dropped
        return 4;
    entity_hub_handle()->look_up_entity(static_cast<uint32_t>(id));
    return 1;
}

// 0x0056B800
int SkateNet::handle_terrain_sound(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    online::TerrainSound message;
    if (!online::read_terrain_sound(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    maths::Vec4 position;
    position.x = static_cast<float>(message.x);
    position.y = static_cast<float>(message.y);
    position.z = static_cast<float>(message.z);
    position.w = 0.0f; // (the original leaves w as the stack had it)
    auto volume = static_cast<float>(message.volume);
    auto pitch = static_cast<float>(message.pitch);
    auto value = static_cast<float>(message.value);
    surface_kind::play_terrain_sound(message.table, message.terrain, &position, volume, pitch, value, false);
    return 1;
}

// 0x0056C330
int SkateNet::handle_run_script_now(net::InboundMsgCtx* context)
{
    online::Hub* net = net_link();
    util::ByteReader reader(context->body_, context->body_size_);
    online::SpawnScript message;
    if (!online::read_spawn_script(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    if (!net->cleared_to_start_) {
        net->queue_script(message.node, message.object_id, message.name);
        return 1;
    }
    objects::Entity* object = entity_hub_handle()->look_up_entity(message.object_id);
    script::Script* script =
        script::spawn_script(message.name, nullptr, 0, nullptr, message.node, 0, false, false, false, false);
    script->entity_.assign_value(object);
    script->tick_frame();
    return 1;
}

// 0x0056C3D0
int SkateNet::on_projectile_msg(net::InboundMsgCtx* context)
{
    auto* skate = static_cast<Skate*>(context->content_);
    util::ByteReader reader(context->body_, context->body_size_);
    online::Projectile message;
    if (!online::read_projectile(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    objects::Rider* skater = skate->get_rider_by_number(static_cast<uint32_t>(message.owner_id));
    if (!skater)
        return 4;
    maths::Vec4 velocity = {.x = core::at(message.velocity, 0), .y = core::at(message.velocity, 1), .z = core::at(message.velocity, 2), .w = core::at(message.velocity, 3)};
    maths::Vec4 position = {.x = core::at(message.position, 0), .y = core::at(message.position, 1), .z = core::at(message.position, 2), .w = core::at(message.position, 3)};
    velocity.make_unit();
    uint32_t time = message.time;
    if (!net_link()->get_on_server()) {
        // (a client: the server's latency added)
        net::ConnSearch search;
        net::Conn* conn = context->receiver_app_->begin_conn_walk(&search);
        time += conn->is_host_local() ? 0 : static_cast<uint32_t>(conn->ping_probe_.average_latency_);
    }
    // where it is now: moved along its direction for the time it took (x87: unrounded until stored)
    const double seconds = static_cast<double>(time) * static_cast<double>(goals::kGoalLifeSecondsPerMillisecond);
    position.x = static_cast<float>(velocity.x * seconds + position.x);
    position.y = static_cast<float>(position.y + velocity.y * seconds);
    position.z = static_cast<float>(position.z + velocity.z * seconds);
    script::StructPtr params = script::make_struct();
    params->put_name_key(kOwnerId, static_cast<uint32_t>(message.owner_id));
    params->put_name_key(kAmmoType, message.ammo_type);
    params->put_int(kX, static_cast<int>(position.x));
    params->put_int(kY, static_cast<int>(position.y));
    params->put_int(kZ, static_cast<int>(position.z));
    params->put_int(kScaledX, static_cast<int>(core::at(message.velocity, 0)));
    params->put_int(kScaledY, static_cast<int>(core::at(message.velocity, 1)));
    params->put_int(kScaledZ, static_cast<int>(core::at(message.velocity, 2)));
    params->put_int(kRadius, message.radius);
    params->push_float(kScale, message.scale);
    params->put_vec3(kPos, {.x = core::at(message.position, 0), .y = core::at(message.position, 1), .z = core::at(message.position, 2), .w = core::at(message.position, 3)});
    params->put_vec3(kVel, {.x = core::at(message.velocity, 0), .y = core::at(message.velocity, 1), .z = core::at(message.velocity, 2), .w = core::at(message.velocity, 3)});
    const uint32_t launch = message.type == kFireball ? kClientLaunchFireball : kClientLaunchProjectile;
    script::launch_script(launch, params.get(), static_cast<objects::Entity*>(skater), false, nullptr);
    params.reset();
    return 1;
}

// 0x0056C6E0
int SkateNet::on_cheat_state_msg(net::InboundMsgCtx* context)
{
    commands::cfunc_clear_omnigons(nullptr, nullptr); // (the cheats off first: the function folded there)
    // (NOT ORIGINAL: a count more than the message holds (or than 32) turns none on - the original read on past its
    // end)
    util::ByteReader reader(context->body_, context->body_size_);
    online::Cheats message;
    online::read_cheats(reader, &message);
    for (int i = 0; i < message.count; ++i) {
        const script::StructPtr params = script::make_struct();
        params->put_name_key(kOn, 0);
        params->put_name_key(kCheatFlag, core::at(message.cheats, i));
        script::launch_script(kClientToggleCheat, params.get(), nullptr, false, nullptr);
    }
    return 1;
}

// 0x0056C7E0
int SkateNet::handle_toggle_cheat(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    online::ToggleCheat message;
    if (!online::read_toggle_cheat(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    script::StructPtr params = script::make_struct();
    if (message.on)
        params->put_name_key(kOn, 0);
    params->put_name_key(kCheatFlag, message.cheat);
    script::launch_script(kClientToggleCheat, params.get(), nullptr, false, nullptr);
    params.reset();
    return 1;
}

// 0x0056C8C0
int SkateNet::handle_skater_channel(net::InboundMsgCtx* context)
{
    util::ByteReader reader(context->body_, context->body_size_);
    online::PairMessage message;              // (the object id, flipped)
    if (!online::read_pair(reader, &message)) // NOT ORIGINAL: too short - dropped
        return 4;
    const int id = int{static_cast<int8_t>(message.first)};
    objects::Entity* object = entity_hub_handle()->look_up_entity(static_cast<uint32_t>(id));
    if (!object)
        return 1;
    auto* blender = core::down_cast<objects::AnimBlenderComponent>(
        core::down_cast<objects::GameEntity>(object)->fetch_module(kAnimBlender));
    if (!blender)
        return 1;
    gfx::BlendChannel* channel = blender->find_channel(0);
    if (channel)
        channel->set_flipped(static_cast<uint32_t>(id), message.second != 0, 0, false);
    return 1;
}

// --- the skate module's side ----------------------------------------------------------------------------------------

// 0x0056B890
void Skate::leave_server()
{
    online::Hub* net = net_link();
    script::launch_script(kPatchCleanup, nullptr, nullptr, false, nullptr);
    script::process_pending_symbols();
    if (net->get_on_server()) {
        // the server: everyone told it's over (0x32), the server and its tasks gone
        net::NetMessageInfo desc;
        net::AppHost* server = server_app();
        if (net->get_hosting_role() == 1)
            net->remove_host_task();
        desc.ident_ = 0x32;
        desc.rank_ = 0xFF;
        server->put_on_outbox(0xFF, &desc);
        server->transmit(false);
        net->destroy_server();
        core::at(server_tasks_, 0)->drop_out();
        core::at(server_tasks_, 1)->drop_out();
        net->remove_players_and_apps();
        return;
    }
    // a client: the server told it's leaving (0xB)
    net::AppHost* client = net->fetch_patron(0);
    if (client) {
        net::NetMessageInfo desc;
        desc.ident_ = 0xB;
        get_sequence(desc, 8);
        core::down_cast<net::Guest>(client)->send_to_host(&desc);
        client->transmit(false);
    }
    net->remove_players_and_apps();
}

// 0x0056B990
void Skate::send_cheats(online::PlayerRecord* player)
{
    const online::Hub* net = net_link();
    net::NetMessageInfo desc;
    online::Cheats message;
    for (const uint32_t cheat : kSentCheats) {
        if (career_->is_code_on(cheat))
            core::at(message.cheats, message.count++) = cheat;
    }
    std::array<uint8_t, 4 + 4 * online::kMaxCheats> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_cheats(writer, message);
    desc.content_ = data.data();
    desc.ident_ = online::kMsgCheats;
    desc.byte_count_ = static_cast<uint16_t>(writer.position());
    get_sequence(desc, 8);
    const int handle = player->get_link_handle();
    net->host_->put_on_outbox(handle, &desc);
}

// 0x0056BB70
void Skate::broadcast_points(bool final)
{
    online::Hub* net = net_link();
    PlayerSearch players;
    PlayerSearch others;
    // the message (0x40, skate_net_messages.h): the time left in seconds, the cheats, final, the count, then 5 bytes
    // a skater (its id and score) - at most 52 skaters (the original's buffer)
    constexpr int kMaxSent = 52;
    for (PlayerRecord* player = players.get_first(true); player; player = players.get_next(true)) {
        auto message = std::make_unique<skate_net::ScoresMessage>();
        message->final = final;
        message->time_left = final ? 0u : static_cast<uint32_t>(get_game_mode()->get_goal_time_left()) / 1000u;
        uint8_t count = 0;
        for (PlayerRecord* other = others.get_first(false); other; other = others.get_next(false)) {
            // (its own score too, when it isn't the trick score the player keeps itself)
            goals::MatchRules* mode = get_game_mode();
            const bool all = final || mode->is_true(kShouldModulateColor) || !mode->keeps_move_points();
            if (other == player && !all)
                continue;
            const uint8_t id = get_skater_id(skater_of(other));
            int score = 0;
            if (get_game_mode()->get_name_key() == kNetGoalAttack)
                score =
                    goals::get_objective_board()->count_cleared_by_player(static_cast<int>(skater_of(other)->ident_));
            else
                score = net->get_entrant_points(skater_of(other)->ident_);
            if (count >= kMaxSent) // (NOT ORIGINAL: the original wrote on past its buffer)
                continue;
            core::at(message->entries, count).id = static_cast<int8_t>(id);
            core::at(message->entries, count).score = score;
            ++count;
        }
        if (static_cast<int8_t>(count) <= 0)
            continue;
        net::NetMessageInfo desc;
        const uint32_t cheats = get_cheats();
        message->count = count;
        message->cheats = cheats;
        std::array<uint8_t, skate_net::kScoresHeaderSize + kMaxSent * skate_net::kScoreEntrySize> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        skate_net::write_scores(writer, *message);
        desc.content_ = bytes.data();
        desc.ident_ = skate_net::kMsgScores;
        desc.byte_count_ = static_cast<uint16_t>(writer.position());
        if (final || cheats)
            get_sequence(desc, 8);
        enqueue(server_app(), player, &desc);
        if (player->is_own_player() || !player->has_finished_joining())
            continue;
        // the cheats' check (0x80): 8 bytes, the second word whatever the stack held (zero here)
        std::array<uint32_t, 2> check = {{get_cheats() ^ skate_net::kCheatsCheckMask, 0}};
        desc.content_ = check.data();
        desc.ident_ = skate_net::kMsgCheatsCheck;
        get_sequence(desc, 8);
        desc.byte_count_ = 8;
        enqueue(server_app(), player, &desc);
    }
    net->change_score_sync_stamp(server_app()->clock_);
}

} // namespace modules
